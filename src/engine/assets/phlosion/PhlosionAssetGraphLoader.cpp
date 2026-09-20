// src/engine/assets/phlosion/PhlosionAssetGraphLoader.cpp
#include "engine/assets/phlosion/PhlosionAssetGraphLoader.h"

#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::assets::phlosion {
namespace {

void setError(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
}

std::string hex64(std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "0x";
    out.resize(18u, '0');
    for (std::size_t index = 0u; index < 16u; ++index) {
        out[out.size() - 1u - index] =
            kDigits[(value >> (index * 4u)) & 0xfu];
    }
    return out;
}

std::shared_ptr<const engine::IAssetStore> requireStore(
    std::shared_ptr<const engine::IAssetStore> store) {
    if (!store) {
        throw std::invalid_argument(
            "PhlosionAssetGraphLoader requires a non-null asset store.");
    }
    return store;
}

AssetResolver requireResolver(AssetResolver resolver) {
    if (!resolver) {
        throw std::invalid_argument(
            "PhlosionAssetGraphLoader requires an asset resolver.");
    }
    return resolver;
}

AssetGraphOptions requireOptions(AssetGraphOptions options) {
    if (options.maxAssets == 0u) {
        throw std::invalid_argument(
            "PhlosionAssetGraphLoader maxAssets must be at least 1.");
    }
    return options;
}

engine::assets::AssetLoadOptions readerOptions(
    const AssetGraphOptions& options) {
    // The graph admits exactly one read at a time and never keeps more than one
    // completed payload inside the reader; the cumulative encoded-byte budget
    // belongs to the graph.
    engine::assets::AssetLoadOptions loadOptions;
    loadOptions.maxRequests = 1u;
    loadOptions.maxResidentBytes = options.maxEncodedBytes;
    return loadOptions;
}

bool isKnownKind(AssetKind kind) {
    return kind == AssetKind::Phrc || kind == AssetKind::Bytes;
}

} // namespace

struct PhlosionAssetGraphLoader::Impl {
    struct Node {
        AssetLocation location;
        bool resolved = false;
        bool loaded = false;
        // Nonzero expected hashes recorded by required edges that named this
        // asset before it was loaded, with the parent ID for diagnostics.
        std::vector<std::pair<std::string, std::uint64_t>> hashConstraints;
    };

    Impl(std::shared_ptr<const engine::IAssetStore> store,
         AssetResolver resolver,
         const AssetGraphOptions& options)
        : resolver_(requireResolver(std::move(resolver))),
          options_(requireOptions(options)),
          reader_(
              requireStore(std::move(store)),
              readerOptions(options_)) {}

    bool start(const std::string& rootAssetId, std::string* outError) {
        if (state_ != AssetGraphState::Idle) {
            setError(
                outError,
                "Asset graph loader is not idle; take() or reset() the "
                "previous session first.");
            return false;
        }
        if (rootAssetId.empty()) {
            setError(outError, "Asset graph root asset ID must not be empty.");
            return false;
        }
        rootId_ = rootAssetId;
        error_.clear();
        state_ = AssetGraphState::Loading;
        nodes_.emplace(rootId_, Node{});
        queue_.push_back(rootId_);
        return true;
    }

    void pump() {
        if (state_ != AssetGraphState::Loading) {
            return;
        }
        if (inFlight_) {
            const engine::assets::AssetLoadState loadState =
                reader_.state(inFlightRequest_);
            if (loadState == engine::assets::AssetLoadState::Queued ||
                loadState == engine::assets::AssetLoadState::Loading) {
                return;
            }
            engine::assets::AssetLoadResult loadResult;
            if (!reader_.take(inFlightRequest_, loadResult)) {
                fail(
                    "Asset graph lost the read state for '" +
                    inFlightAssetId_ + "'.");
                return;
            }
            const std::string assetId = inFlightAssetId_;
            inFlight_ = false;
            inFlightRequest_ = 0u;
            inFlightAssetId_.clear();
            if (loadResult.state != engine::assets::AssetLoadState::Ready) {
                const std::string detail = loadResult.error.empty()
                    ? std::string("the read did not complete.")
                    : loadResult.error;
                fail(
                    "Asset '" + assetId + "' could not be loaded: " + detail);
                return;
            }
            if (!consume(assetId, std::move(loadResult.bytes))) {
                return;
            }
        }
        scheduleNext();
    }

    bool cancel() {
        if (state_ != AssetGraphState::Loading) {
            return false;
        }
        error_ = "Asset graph load was cancelled.";
        state_ = AssetGraphState::Cancelled;
        releaseInFlight();
        clearPayloads();
        return true;
    }

    bool take(AssetGraphResult& outResult) {
        if (state_ != AssetGraphState::Ready &&
            state_ != AssetGraphState::Failed &&
            state_ != AssetGraphState::Cancelled) {
            return false;
        }
        AssetGraphResult result;
        result.state = state_;
        result.rootAssetId = rootId_;
        result.error = error_;
        if (state_ == AssetGraphState::Ready) {
            result.assets = std::move(staged_);
            result.skippedOptionalDependencies = std::move(skipped_);
        }
        releaseInFlight();
        clearPayloads();
        error_.clear();
        rootId_.clear();
        state_ = AssetGraphState::Idle;
        outResult = std::move(result);
        return true;
    }

    void reset() {
        releaseInFlight();
        clearPayloads();
        error_.clear();
        rootId_.clear();
        state_ = AssetGraphState::Idle;
    }

    AssetGraphState state() const {
        return state_;
    }

    std::size_t loadedBytes() const {
        return stagedBytes_;
    }

private:
    // Fails the whole session and releases every staged payload and any
    // in-flight read so no late publication can escape.
    void fail(std::string message) {
        error_ = std::move(message);
        state_ = AssetGraphState::Failed;
        releaseInFlight();
        clearPayloads();
    }

    void releaseInFlight() {
        if (!inFlight_) {
            return;
        }
        // release() discards queued or in-flight publication and frees the
        // reader's single request slot without stopping the reader, so a
        // released read can never publish into a later session.
        reader_.release(inFlightRequest_);
        inFlight_ = false;
        inFlightRequest_ = 0u;
        inFlightAssetId_.clear();
    }

    void clearPayloads() {
        nodes_.clear();
        staged_.clear();
        skipped_.clear();
        queue_.clear();
        requiredEdges_.clear();
        edgeCount_ = 0u;
        stagedBytes_ = 0u;
    }

    bool ensureResolved(const std::string& assetId, Node& node) {
        if (node.resolved) {
            return true;
        }
        AssetLocation location;
        std::string resolverError;
        bool resolved = false;
        try {
            resolved = resolver_(assetId, location, &resolverError);
        } catch (const std::exception& exception) {
            fail(
                "Asset resolver threw for '" + assetId + "': " +
                exception.what());
            return false;
        } catch (...) {
            fail("Asset resolver threw for '" + assetId + "'.");
            return false;
        }
        if (!resolved) {
            fail(
                "Asset resolver could not resolve '" + assetId + "'" +
                (resolverError.empty()
                    ? std::string(".")
                    : ": " + resolverError));
            return false;
        }
        if (location.virtualPath.empty()) {
            fail(
                "Asset resolver returned an empty virtual path for '" +
                assetId + "'.");
            return false;
        }
        if (!isKnownKind(location.kind)) {
            fail(
                "Asset resolver returned an invalid asset kind for '" +
                assetId + "'.");
            return false;
        }
        node.location = std::move(location);
        node.resolved = true;
        return true;
    }

    void scheduleNext() {
        if (inFlight_ || state_ != AssetGraphState::Loading) {
            return;
        }
        if (queue_.empty()) {
            error_.clear();
            state_ = AssetGraphState::Ready;
            return;
        }
        const std::string assetId = queue_.front();
        queue_.pop_front();
        const auto nodeIt = nodes_.find(assetId);
        if (nodeIt == nodes_.end() || nodeIt->second.loaded) {
            // Defensive: unknown or already loaded IDs are never queued.
            scheduleNext();
            return;
        }
        if (!ensureResolved(assetId, nodeIt->second)) {
            return;
        }
        std::string requestError;
        const engine::assets::AssetLoadId requestId =
            reader_.request(nodeIt->second.location.virtualPath, &requestError);
        if (requestId == 0u) {
            fail(
                "Asset graph could not schedule a read for '" + assetId +
                "': " + requestError);
            return;
        }
        inFlight_ = true;
        inFlightRequest_ = requestId;
        inFlightAssetId_ = assetId;
    }

    bool consume(const std::string& assetId, std::vector<std::uint8_t> bytes) {
        const auto nodeIt = nodes_.find(assetId);
        if (nodeIt == nodes_.end()) {
            fail("Asset graph lost the node state for '" + assetId + "'.");
            return false;
        }
        Node& node = nodeIt->second;
        const AssetKind kind = node.location.kind;
        std::uint64_t contentHash = 0u;
        std::vector<phrc::Dependency> dependencies;
        if (kind == AssetKind::Bytes) {
            contentHash = phrc::contentHash64(bytes);
        } else {
            phrc::Document document;
            std::string decodeError;
            if (!phrc::decode(bytes, document, &decodeError)) {
                fail(
                    "Asset '" + assetId +
                    "' is not a valid PHRC container: " + decodeError);
                return false;
            }
            contentHash = document.contentHash;
            dependencies = std::move(document.dependencies);
        }
        for (const auto& constraint : node.hashConstraints) {
            if (constraint.second != contentHash) {
                fail(
                    "Dependency hash mismatch: '" + constraint.first +
                    "' requires '" + assetId + "' at hash " +
                    hex64(constraint.second) + ", but the asset hash is " +
                    hex64(contentHash) + ".");
                return false;
            }
        }
        if (bytes.size() > options_.maxEncodedBytes - stagedBytes_) {
            fail(
                "Asset '" + assetId +
                "' exceeds the encoded byte limit of " +
                std::to_string(options_.maxEncodedBytes) +
                " bytes for this graph.");
            return false;
        }
        stagedBytes_ += bytes.size();
        LoadedAsset loaded;
        loaded.virtualPath = node.location.virtualPath;
        loaded.kind = kind;
        loaded.contentHash = contentHash;
        loaded.bytes = std::move(bytes);
        staged_.insert_or_assign(assetId, std::move(loaded));
        node.loaded = true;
        for (const phrc::Dependency& dependency : dependencies) {
            if (!registerEdge(assetId, dependency)) {
                return false;
            }
        }
        return true;
    }

    bool registerEdge(
        const std::string& parentAssetId,
        const phrc::Dependency& dependency) {
        if (edgeCount_ >= options_.maxDependencyEdges ||
            edgeCount_ == std::numeric_limits<std::size_t>::max()) {
            fail(
                "Asset '" + parentAssetId + "' dependency '" +
                dependency.assetId +
                "' exceeds the dependency edge limit of " +
                std::to_string(options_.maxDependencyEdges) + ".");
            return false;
        }
        ++edgeCount_;
        if ((dependency.flags & ~phrc::kDependencyRequired) != 0u) {
            fail(
                "Asset '" + parentAssetId + "' dependency '" +
                dependency.assetId + "' has unsupported flag bits " +
                hex64(static_cast<std::uint64_t>(dependency.flags)) + ".");
            return false;
        }
        if ((dependency.flags & phrc::kDependencyRequired) == 0u) {
            skipped_.push_back(
                SkippedOptionalDependency{parentAssetId, dependency});
            return true;
        }

        const std::string& targetAssetId = dependency.assetId;
        if (targetAssetId == parentAssetId) {
            fail(
                "Required dependency cycle: asset '" + parentAssetId +
                "' depends on itself.");
            return false;
        }
        // Required cycles are rejected as soon as the closing edge appears: a
        // cycle's remaining edges were all discovered before this one.
        if (reaches(targetAssetId, parentAssetId)) {
            fail(
                "Required dependency cycle: '" + parentAssetId + "' -> '" +
                targetAssetId + "' closes a required dependency cycle.");
            return false;
        }
        requiredEdges_[parentAssetId].insert(targetAssetId);

        const auto stagedIt = staged_.find(targetAssetId);
        if (stagedIt != staged_.end()) {
            if (dependency.expectedContentHash != 0u &&
                dependency.expectedContentHash !=
                    stagedIt->second.contentHash) {
                fail(
                    "Dependency hash mismatch: '" + parentAssetId +
                    "' requires '" + targetAssetId + "' at hash " +
                    hex64(dependency.expectedContentHash) +
                    ", but the loaded asset hash is " +
                    hex64(stagedIt->second.contentHash) + ".");
                return false;
            }
            return true;
        }

        auto nodeIt = nodes_.find(targetAssetId);
        if (nodeIt == nodes_.end()) {
            if (nodes_.size() >= options_.maxAssets) {
                fail(
                    "Asset graph exceeded the unique asset limit of " +
                    std::to_string(options_.maxAssets) +
                    " while adding '" + targetAssetId + "' required by '" +
                    parentAssetId + "'.");
                return false;
            }
            nodeIt = nodes_.emplace(targetAssetId, Node{}).first;
            queue_.push_back(targetAssetId);
        }
        if (dependency.expectedContentHash != 0u) {
            nodeIt->second.hashConstraints.emplace_back(
                parentAssetId,
                dependency.expectedContentHash);
        }
        return true;
    }

    // Bounded reachability over the required edges discovered so far.
    bool reaches(
        const std::string& fromAssetId,
        const std::string& toAssetId) const {
        std::vector<std::string> pending;
        std::unordered_set<std::string> visited;
        pending.push_back(fromAssetId);
        while (!pending.empty()) {
            std::string current = std::move(pending.back());
            pending.pop_back();
            if (current == toAssetId) {
                return true;
            }
            if (!visited.insert(current).second) {
                continue;
            }
            const auto found = requiredEdges_.find(current);
            if (found == requiredEdges_.end()) {
                continue;
            }
            for (const std::string& next : found->second) {
                if (next == toAssetId) {
                    return true;
                }
                pending.push_back(next);
            }
        }
        return false;
    }

    AssetResolver resolver_;
    const AssetGraphOptions options_;
    // Declared after the resolver and options it was built from, so it is
    // destroyed before them: the reader's join runs while the resolver and
    // options that configured it are still alive. The graph state members
    // declared below it are destroyed first.
    engine::assets::AsyncAssetLoader reader_;

    AssetGraphState state_ = AssetGraphState::Idle;
    std::string rootId_;
    std::string error_;
    std::map<std::string, Node> nodes_;
    std::map<std::string, LoadedAsset> staged_;
    std::vector<SkippedOptionalDependency> skipped_;
    std::deque<std::string> queue_;
    std::map<std::string, std::unordered_set<std::string>> requiredEdges_;
    std::size_t edgeCount_ = 0u;
    std::size_t stagedBytes_ = 0u;
    bool inFlight_ = false;
    engine::assets::AssetLoadId inFlightRequest_ = 0u;
    std::string inFlightAssetId_;
};

PhlosionAssetGraphLoader::PhlosionAssetGraphLoader(
    std::shared_ptr<const engine::IAssetStore> store,
    AssetResolver resolver,
    AssetGraphOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(store),
          std::move(resolver),
          options)) {}

PhlosionAssetGraphLoader::~PhlosionAssetGraphLoader() = default;

bool PhlosionAssetGraphLoader::start(
    std::string rootAssetId,
    std::string* outError) {
    return impl_->start(rootAssetId, outError);
}

void PhlosionAssetGraphLoader::pump() {
    impl_->pump();
}

AssetGraphState PhlosionAssetGraphLoader::state() const {
    return impl_->state();
}

bool PhlosionAssetGraphLoader::cancel() {
    return impl_->cancel();
}

bool PhlosionAssetGraphLoader::take(AssetGraphResult& outResult) {
    return impl_->take(outResult);
}

void PhlosionAssetGraphLoader::reset() {
    impl_->reset();
}

std::size_t PhlosionAssetGraphLoader::loadedBytes() const {
    return impl_->loadedBytes();
}

} // namespace engine::assets::phlosion
