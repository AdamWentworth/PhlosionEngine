// src/engine/assets/AsyncAssetLoader.cpp
#include "engine/assets/AsyncAssetLoader.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace engine::assets {
namespace {

void setError(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
}

bool isTerminal(AssetLoadState state) {
    return state == AssetLoadState::Ready ||
        state == AssetLoadState::Failed ||
        state == AssetLoadState::Cancelled;
}

} // namespace

struct AsyncAssetLoader::Impl {
    struct Entry {
        AssetLoadId id = 0u;
        std::string virtualPath;
        AssetLoadState state = AssetLoadState::Queued;
        std::vector<std::uint8_t> bytes;
        std::string error;
    };

    Impl(
        std::shared_ptr<const engine::IAssetStore> store,
        const AssetLoadOptions& options)
        : store_(std::move(store)),
          maxRequests_(options.maxRequests),
          maxResidentBytes_(options.maxResidentBytes) {
        if (!store_) {
            throw std::invalid_argument(
                "AsyncAssetLoader requires a non-null asset store.");
        }
        if (maxRequests_ == 0u) {
            throw std::invalid_argument(
                "AsyncAssetLoader maxRequests must be at least 1.");
        }
        worker_ = std::thread([this] { workerMain(); });
    }

    ~Impl() {
        requestStop();
        if (worker_.joinable()) {
            worker_.join();
        }
        // store_ and entries_ are destroyed here, after the worker has exited.
    }

    void requestStop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        // Queued work is dropped so the provider is never called for it.
        queued_.clear();
        for (auto& item : entries_) {
            Entry& entry = *item.second;
            if (entry.state == AssetLoadState::Queued ||
                entry.state == AssetLoadState::Loading) {
                entry.state = AssetLoadState::Cancelled;
                entry.bytes.clear();
                entry.error = "Asset load was cancelled.";
            }
        }
        workReady_.notify_all();
    }

    void workerMain() {
        for (;;) {
            std::shared_ptr<Entry> entry;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                workReady_.wait(lock, [this] {
                    return stopping_ || !queued_.empty();
                });
                if (stopping_) {
                    queued_.clear();
                    return;
                }
                entry = std::move(queued_.front());
                queued_.pop_front();
                if (!entry || entry->state != AssetLoadState::Queued) {
                    continue;
                }
                entry->state = AssetLoadState::Loading;
            }

            // Provider I/O runs without the state lock.
            std::vector<std::uint8_t> bytes;
            std::string error;
            bool readSucceeded = false;
            try {
                readSucceeded =
                    store_->readBytes(entry->virtualPath, bytes, &error);
            } catch (const std::exception& exception) {
                error = std::string("Asset store threw during read: ") +
                    exception.what();
            } catch (...) {
                error = "Asset store threw during read.";
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto found = entries_.find(entry->id);
                // A released or taken request is erased from entries_, so a late
                // result is discarded instead of resurrecting the request.
                if (!stopping_ &&
                    found != entries_.end() &&
                    found->second.get() == entry.get() &&
                    entry->state == AssetLoadState::Loading) {
                    publish(
                        *entry,
                        readSucceeded,
                        std::move(bytes),
                        std::move(error));
                }
            }
        }
    }

    // Requires mutex_.
    void publish(
        Entry& entry,
        bool readSucceeded,
        std::vector<std::uint8_t> bytes,
        std::string error) {
        if (!readSucceeded) {
            entry.state = AssetLoadState::Failed;
            // Partial provider output never leaks into a failed result.
            entry.bytes.clear();
            entry.error = error.empty()
                ? "Asset store could not read '" + entry.virtualPath + "'."
                : std::move(error);
            return;
        }
        // Overflow-safe check: the retained budget is never exceeded.
        if (bytes.size() > maxResidentBytes_ - residentBytes_) {
            entry.state = AssetLoadState::Failed;
            entry.bytes.clear();
            entry.error = "Asset '" + entry.virtualPath +
                "' exceeds the retained resident byte budget.";
            return;
        }
        residentBytes_ += bytes.size();
        entry.state = AssetLoadState::Ready;
        entry.bytes = std::move(bytes);
        entry.error.clear();
    }

    AssetLoadId request(std::string virtualPath, std::string* outError) {
        if (virtualPath.empty()) {
            setError(outError, "Asset virtual path must not be empty.");
            return 0u;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            setError(
                outError,
                "Async asset loader has stopped and no longer accepts requests.");
            return 0u;
        }
        if (entries_.size() >= maxRequests_) {
            setError(
                outError,
                "Async asset loader is at its request capacity of " +
                    std::to_string(maxRequests_) +
                    " admitted, unreleased requests.");
            return 0u;
        }
        if (nextId_ == std::numeric_limits<AssetLoadId>::max()) {
            setError(
                outError,
                "Async asset loader has exhausted its load IDs.");
            return 0u;
        }
        auto entry = std::make_shared<Entry>();
        entry->id = nextId_++;
        entry->virtualPath = std::move(virtualPath);
        const AssetLoadId id = entry->id;
        entries_.emplace(id, std::move(entry));
        queued_.push_back(entries_.at(id));
        workReady_.notify_one();
        return id;
    }

    AssetLoadState state(AssetLoadId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(id);
        return found == entries_.end()
            ? AssetLoadState::Unknown
            : found->second->state;
    }

    bool cancel(AssetLoadId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(id);
        if (found == entries_.end()) {
            return false;
        }
        Entry& entry = *found->second;
        if (entry.state != AssetLoadState::Queued &&
            entry.state != AssetLoadState::Loading) {
            return false;
        }
        if (entry.state == AssetLoadState::Queued) {
            removeFromQueue(found->second);
        }
        entry.state = AssetLoadState::Cancelled;
        entry.bytes.clear();
        entry.error = "Asset load was cancelled.";
        return true;
    }

    bool take(AssetLoadId id, AssetLoadResult& outResult) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(id);
        if (found == entries_.end()) {
            return false;
        }
        Entry& entry = *found->second;
        if (!isTerminal(entry.state)) {
            return false;
        }
        const std::size_t released =
            entry.state == AssetLoadState::Ready ? entry.bytes.size() : 0u;
        outResult.state = entry.state;
        outResult.bytes = std::move(entry.bytes);
        outResult.error = std::move(entry.error);
        residentBytes_ -= released;
        entries_.erase(found);
        return true;
    }

    bool release(AssetLoadId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(id);
        if (found == entries_.end()) {
            return false;
        }
        Entry& entry = *found->second;
        if (entry.state == AssetLoadState::Queued) {
            removeFromQueue(found->second);
        } else if (entry.state == AssetLoadState::Ready) {
            residentBytes_ -= entry.bytes.size();
        } else if (entry.state == AssetLoadState::Loading) {
            // A started read may still finish; erasing the entry discards it.
            entry.state = AssetLoadState::Cancelled;
        }
        entries_.erase(found);
        return true;
    }

    std::size_t residentBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return residentBytes_;
    }

    // Requires mutex_.
    void removeFromQueue(const std::shared_ptr<Entry>& entry) {
        queued_.erase(
            std::remove(queued_.begin(), queued_.end(), entry),
            queued_.end());
    }

    std::shared_ptr<const engine::IAssetStore> store_;
    const std::size_t maxRequests_;
    const std::size_t maxResidentBytes_;

    mutable std::mutex mutex_;
    std::condition_variable workReady_;
    bool stopping_ = false;
    AssetLoadId nextId_ = 1u;
    std::size_t residentBytes_ = 0u;
    std::deque<std::shared_ptr<Entry>> queued_;
    std::unordered_map<AssetLoadId, std::shared_ptr<Entry>> entries_;
    std::thread worker_;
};

AsyncAssetLoader::AsyncAssetLoader(
    std::shared_ptr<const engine::IAssetStore> store,
    AssetLoadOptions options)
    : impl_(std::make_unique<Impl>(std::move(store), options)) {}

AsyncAssetLoader::~AsyncAssetLoader() = default;

AssetLoadId AsyncAssetLoader::request(
    std::string virtualPath,
    std::string* outError) {
    return impl_->request(std::move(virtualPath), outError);
}

AssetLoadState AsyncAssetLoader::state(AssetLoadId id) const {
    return impl_->state(id);
}

bool AsyncAssetLoader::cancel(AssetLoadId id) {
    return impl_->cancel(id);
}

bool AsyncAssetLoader::take(AssetLoadId id, AssetLoadResult& outResult) {
    return impl_->take(id, outResult);
}

bool AsyncAssetLoader::release(AssetLoadId id) {
    return impl_->release(id);
}

void AsyncAssetLoader::requestStop() {
    impl_->requestStop();
}

std::size_t AsyncAssetLoader::residentBytes() const {
    return impl_->residentBytes();
}

} // namespace engine::assets
