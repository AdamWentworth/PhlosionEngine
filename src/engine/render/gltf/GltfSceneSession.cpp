#include "GltfSceneSession.h"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace engine::render::gltf::scene {
namespace ph = engine::assets::phlosion;

struct GltfSceneSession::Impl {
    struct Job {
        std::uint64_t id;
        std::shared_ptr<const ph::ArchiveSnapshot> archive;
        std::string path;
        std::uint64_t budget;
    };
    struct Result {
        std::shared_ptr<const GltfSceneSnapshot> snapshot;
        std::string error;
    };

    ph::PhlosionArchiveSession archive;
    GltfSceneSessionOptions options;
    Decoder decode;
    GltfSceneSessionState state = GltfSceneSessionState::Idle;
    std::string error, path;
    std::shared_ptr<const GltfSceneSnapshot> active, staged;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    bool acceptingResult = false;
    std::uint64_t epoch = 0u;
    std::optional<Job> pending;
    std::optional<Result> result;
    std::thread worker;

    Impl(std::shared_ptr<const engine::IAssetStore> store, ph::AssetResolver resolver,
         GltfSceneSessionOptions configured, Decoder decoder)
        : archive(std::move(store), std::move(resolver), configured.graph),
          options(configured), decode(std::move(decoder)), worker([this] { run(); }) {}

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            pending.reset();
            result.reset();
        }
        wake.notify_one();
        worker.join();
    }

    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || pending.has_value(); });
                if (stopping) return;
                job = std::move(*pending);
                pending.reset();
            }
            Result completed;
            try {
                auto snapshot = std::make_shared<GltfSceneSnapshot>();
                snapshot->archive = job.archive;
                const engine::IAssetStore* store = job.archive->kind == ph::ArchiveKind::Scene
                    ? static_cast<const engine::IAssetStore*>(job.archive->scene.get())
                    : static_cast<const engine::IAssetStore*>(job.archive->prefab.get());
                const auto start = std::chrono::steady_clock::now();
                const bool ok = store && decode(*store, job.path, snapshot->scene,
                                                &completed.error, options.import);
                snapshot->decodeSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count();
                if (!ok || !snapshot->scene) {
                    if (completed.error.empty()) completed.error = "glTF scene decode failed";
                } else if (snapshot->scene->stats.retainedAssetBytes > job.budget) {
                    completed.error = "active plus candidate decoded payload exceeds the scene budget";
                } else {
                    for (const auto& [id, asset] : job.archive->graph.assets) {
                        (void)id;
                        if (asset.bytes.size() > std::numeric_limits<std::uint64_t>::max() -
                                                   snapshot->encodedGraphBytes)
                            throw std::overflow_error("encoded graph byte count overflow");
                        snapshot->encodedGraphBytes += asset.bytes.size();
                    }
                    completed.snapshot = std::move(snapshot);
                    completed.error.clear();
                }
            } catch (const std::exception& exception) {
                completed.error = std::string("glTF scene worker: ") + exception.what();
            } catch (...) {
                completed.error = "glTF scene worker threw an unknown exception";
            }
            {
                std::lock_guard lock(mutex);
                if (!stopping && acceptingResult && job.id == epoch) result = std::move(completed);
            }
        }
    }

    void invalidate() {
        std::lock_guard lock(mutex);
        // Epochs never wrap or alias an old job; begin rejects exhausted IDs.
        if (epoch != std::numeric_limits<std::uint64_t>::max()) ++epoch;
        acceptingResult = false;
        pending.reset();
        result.reset();
    }

    void fail(std::string diagnostic) {
        error = std::move(diagnostic);
        staged.reset();
        archive.discard();
        state = GltfSceneSessionState::Failed;
    }
};

GltfSceneSession::GltfSceneSession(std::shared_ptr<const engine::IAssetStore> store,
    ph::AssetResolver resolver, GltfSceneSessionOptions options)
    : GltfSceneSession(std::move(store), std::move(resolver), options,
        [](const engine::IAssetStore& source, const std::string& path,
           std::shared_ptr<const GltfSceneAsset>& out, std::string* error,
           const GltfSceneLoadOptions& loadOptions) {
            return loadGltfSceneAsset(source, path, out, error, loadOptions);
        }) {}

GltfSceneSession::GltfSceneSession(std::shared_ptr<const engine::IAssetStore> store,
    ph::AssetResolver resolver, GltfSceneSessionOptions options, Decoder decoder)
    : impl_(std::make_unique<Impl>(std::move(store), std::move(resolver),
                                   options, std::move(decoder))) {}

GltfSceneSession::~GltfSceneSession() = default;

bool GltfSceneSession::begin(std::string id, ph::ArchiveKind kind,
                            std::string path, std::string* error) {
    auto& i = *impl_;
    if (i.state != GltfSceneSessionState::Idle || path.empty()) {
        if (error) *error = "scene session requires Idle and a nonempty document path";
        return false;
    }
    {
        std::lock_guard lock(i.mutex);
        if (i.epoch == std::numeric_limits<std::uint64_t>::max()) {
            if (error) *error = "scene candidate IDs exhausted";
            return false;
        }
    }
    if (!i.archive.begin(std::move(id), kind, error)) return false;
    i.invalidate();
    {
        std::lock_guard lock(i.mutex);
        i.acceptingResult = true;
    }
    i.path = std::move(path);
    i.error.clear();
    i.state = GltfSceneSessionState::Loading;
    if (error) error->clear();
    return true;
}

void GltfSceneSession::pump() {
    auto& i = *impl_;
    if (i.state == GltfSceneSessionState::Loading) {
        i.archive.pump();
        if (i.archive.state() == ph::ArchiveSessionState::Failed) {
            i.fail(i.archive.error());
        } else if (i.archive.state() == ph::ArchiveSessionState::Staged) {
            const auto used = i.active ? i.active->scene->stats.retainedAssetBytes : 0u;
            if (used > i.options.maxDecodedResidentBytes) {
                i.fail("active scene exceeds the decoded budget");
                return;
            }
            {
                std::lock_guard lock(i.mutex);
                i.pending = Impl::Job{i.epoch, i.archive.staged(), i.path,
                                     i.options.maxDecodedResidentBytes - used};
            }
            i.state = GltfSceneSessionState::Decoding;
            i.wake.notify_one();
        }
    }
    if (i.state == GltfSceneSessionState::Decoding) {
        std::optional<Impl::Result> completed;
        {
            std::lock_guard lock(i.mutex);
            completed.swap(i.result);
        }
        if (!completed) return;
        if (!completed->snapshot) {
            i.fail(std::move(completed->error));
        } else {
            i.staged = std::move(completed->snapshot);
            i.state = GltfSceneSessionState::Staged;
        }
    }
}

GltfSceneSessionState GltfSceneSession::state() const { return impl_->state; }
const std::string& GltfSceneSession::error() const { return impl_->error; }
std::shared_ptr<const GltfSceneSnapshot> GltfSceneSession::staged() const { return impl_->staged; }
std::shared_ptr<const GltfSceneSnapshot> GltfSceneSession::active() const { return impl_->active; }

bool GltfSceneSession::activate() {
    auto& i = *impl_;
    if (i.state != GltfSceneSessionState::Staged || !i.staged) return false;
    i.active = std::move(i.staged);
    // The composed immutable snapshot now owns the matching archive; keeping a
    // second active reference in the inner session would only obscure ownership.
    i.archive.discard();
    i.state = GltfSceneSessionState::Idle;
    return true;
}

bool GltfSceneSession::cancel() {
    auto& i = *impl_;
    if (i.state != GltfSceneSessionState::Loading &&
        i.state != GltfSceneSessionState::Decoding) return false;
    i.invalidate();
    i.archive.discard();
    i.state = GltfSceneSessionState::Cancelled;
    i.error = "scene candidate cancelled";
    return true;
}

void GltfSceneSession::discard() {
    auto& i = *impl_;
    i.invalidate();
    i.archive.discard();
    i.staged.reset();
    i.error.clear();
    i.path.clear();
    i.state = GltfSceneSessionState::Idle;
}

void GltfSceneSession::unload() { discard(); impl_->active.reset(); }

std::uint64_t GltfSceneSession::decodedResidentBytes() const {
    const auto& i = *impl_;
    auto bytes = i.active ? i.active->scene->stats.retainedAssetBytes : 0u;
    if (i.staged) bytes += i.staged->scene->stats.retainedAssetBytes;
    std::lock_guard lock(impl_->mutex);
    if (i.result && i.result->snapshot) bytes += i.result->snapshot->scene->stats.retainedAssetBytes;
    return bytes;
}

} // namespace engine::render::gltf::scene
