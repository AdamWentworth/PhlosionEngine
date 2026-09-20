// src/engine/assets/phlosion/PhlosionArchiveSession.cpp
#include "engine/assets/phlosion/PhlosionArchiveSession.h"

#include <exception>
#include <utility>
#include <vector>

namespace engine::assets::phlosion {
namespace {

void setError(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
}

bool isKnownKind(ArchiveKind kind) {
    return kind == ArchiveKind::Scene || kind == ArchiveKind::Prefab;
}

const char* kindName(ArchiveKind kind) {
    return kind == ArchiveKind::Prefab ? "Prefab" : "Scene";
}

} // namespace

struct PhlosionArchiveSession::Impl {
    Impl(std::shared_ptr<const engine::IAssetStore> store,
         AssetResolver resolver,
         const AssetGraphOptions& options)
        : graph(std::move(store), std::move(resolver), options) {}

    bool begin(
        const std::string& rootAssetId,
        ArchiveKind kind,
        std::string* outError) {
        if (state != ArchiveSessionState::Idle) {
            setError(
                outError,
                "Archive session is not idle; discard() the previous "
                "candidate first.");
            return false;
        }
        if (rootAssetId.empty()) {
            setError(outError, "Archive root asset ID must not be empty.");
            return false;
        }
        if (!isKnownKind(kind)) {
            setError(outError, "Archive session was given an unknown kind.");
            return false;
        }
        // The candidate is admitted by the graph loader without resolving,
        // reading or decoding anything.
        std::string graphError;
        if (!graph.start(rootAssetId, &graphError)) {
            setError(
                outError,
                "Archive session could not admit root '" + rootAssetId +
                    "': " + graphError);
            return false;
        }
        rootAssetId_ = rootAssetId;
        kind_ = kind;
        error_.clear();
        state = ArchiveSessionState::Loading;
        return true;
    }

    void pump() {
        if (state != ArchiveSessionState::Loading) {
            return;
        }
        graph.pump();
        switch (graph.state()) {
        case AssetGraphState::Idle:
        case AssetGraphState::Loading:
            return;
        case AssetGraphState::Ready: {
            AssetGraphResult result;
            if (!graph.take(result)) {
                failCandidate(
                    "Archive root '" + rootAssetId_ +
                    "' lost its ready graph result.");
                return;
            }
            completeCandidate(std::move(result));
            return;
        }
        case AssetGraphState::Failed: {
            AssetGraphResult result;
            if (!graph.take(result)) {
                failCandidate(
                    "Archive root '" + rootAssetId_ +
                    "' lost its failed graph result.");
                return;
            }
            failCandidate(
                "Archive " + std::string(kindName(kind_)) + " root '" +
                rootAssetId_ + "' graph failed: " + result.error);
            return;
        }
        case AssetGraphState::Cancelled: {
            AssetGraphResult result;
            if (!graph.take(result)) {
                failCandidate(
                    "Archive root '" + rootAssetId_ +
                    "' lost its cancelled graph result.");
                return;
            }
            staged.reset();
            error_ = "Archive root '" + rootAssetId_ +
                "' was cancelled: " + result.error;
            state = ArchiveSessionState::Cancelled;
            return;
        }
        }
    }

    void completeCandidate(AssetGraphResult result) {
        const auto rootIt = result.assets.find(rootAssetId_);
        if (rootIt == result.assets.end()) {
            failCandidate(
                "Archive root '" + rootAssetId_ +
                "' is missing from the loaded graph.");
            return;
        }
        if (rootIt->second.kind != AssetKind::Phrc) {
            failCandidate(
                "Archive root '" + rootAssetId_ +
                "' must be a Phrc container, but the resolver mapped it as "
                "an opaque byte asset.");
            return;
        }
        auto snapshot = std::make_shared<ArchiveSnapshot>();
        snapshot->kind = kind_;
        snapshot->graph = std::move(result);
        const std::vector<std::uint8_t>& rootBytes =
            snapshot->graph.assets.at(rootAssetId_).bytes;
        std::string decodeError;
        try {
            if (kind_ == ArchiveKind::Prefab) {
                auto prefab = std::make_shared<PrefabArchiveStore>();
                if (!prefab->loadBytes(rootBytes, &decodeError)) {
                    failCandidate(
                        "Prefab archive root '" + rootAssetId_ +
                        "' could not be decoded: " + decodeError);
                    return;
                }
                snapshot->prefab = std::move(prefab);
            } else {
                auto scene = std::make_shared<SceneArchiveStore>();
                if (!scene->loadBytes(rootBytes, &decodeError)) {
                    failCandidate(
                        "Scene archive root '" + rootAssetId_ +
                        "' could not be decoded: " + decodeError);
                    return;
                }
                snapshot->scene = std::move(scene);
            }
        } catch (const std::exception& exception) {
            failCandidate(
                "Archive " + std::string(kindName(kind_)) + " root '" +
                rootAssetId_ + "' could not be decoded: " + exception.what());
            return;
        } catch (...) {
            failCandidate(
                "Archive " + std::string(kindName(kind_)) + " root '" +
                rootAssetId_ + "' could not be decoded.");
            return;
        }
        staged = std::move(snapshot);
        error_.clear();
        state = ArchiveSessionState::Staged;
    }

    // The candidate is dropped, but the active snapshot and any snapshot a
    // caller already holds are untouched.
    void failCandidate(std::string message) {
        error_ = std::move(message);
        staged.reset();
        graph.reset();
        state = ArchiveSessionState::Failed;
    }

    bool activate() {
        if (state != ArchiveSessionState::Staged || !staged) {
            return false;
        }
        // Pointer transfer only: the snapshot keeps its identity and content,
        // and nothing is re-read or re-decoded.
        active = std::move(staged);
        staged.reset();
        error_.clear();
        state = ArchiveSessionState::Idle;
        return true;
    }

    bool cancel() {
        if (state != ArchiveSessionState::Loading) {
            return false;
        }
        // Releases the in-flight request and drops the staged payloads without
        // waiting for a read that already started. The terminal graph state is
        // cleared as well so a later begin starts from a clean candidate.
        graph.cancel();
        graph.reset();
        staged.reset();
        error_ = "Archive root '" + rootAssetId_ + "' load was cancelled.";
        state = ArchiveSessionState::Cancelled;
        return true;
    }

    void discard() {
        // reset, never the terminal requestStop: a released read stays
        // harmless and a replacement begin is admitted immediately.
        graph.reset();
        staged.reset();
        error_.clear();
        state = ArchiveSessionState::Idle;
    }

    void unload() {
        discard();
        active.reset();
    }

    PhlosionAssetGraphLoader graph;
    ArchiveSessionState state = ArchiveSessionState::Idle;
    ArchiveKind kind_ = ArchiveKind::Scene;
    std::string rootAssetId_;
    std::string error_;
    std::shared_ptr<const ArchiveSnapshot> staged;
    std::shared_ptr<const ArchiveSnapshot> active;
};

PhlosionArchiveSession::PhlosionArchiveSession(
    std::shared_ptr<const engine::IAssetStore> store,
    AssetResolver resolver,
    AssetGraphOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(store),
          std::move(resolver),
          options)) {}

PhlosionArchiveSession::~PhlosionArchiveSession() = default;

bool PhlosionArchiveSession::begin(
    std::string rootAssetId,
    ArchiveKind kind,
    std::string* outError) {
    return impl_->begin(rootAssetId, kind, outError);
}

void PhlosionArchiveSession::pump() {
    impl_->pump();
}

ArchiveSessionState PhlosionArchiveSession::state() const {
    return impl_->state;
}

const std::string& PhlosionArchiveSession::error() const {
    return impl_->error_;
}

std::shared_ptr<const ArchiveSnapshot> PhlosionArchiveSession::staged() const {
    return impl_->staged;
}

std::shared_ptr<const ArchiveSnapshot> PhlosionArchiveSession::active() const {
    return impl_->active;
}

bool PhlosionArchiveSession::activate() {
    return impl_->activate();
}

bool PhlosionArchiveSession::cancel() {
    return impl_->cancel();
}

void PhlosionArchiveSession::discard() {
    impl_->discard();
}

void PhlosionArchiveSession::unload() {
    impl_->unload();
}

} // namespace engine::assets::phlosion
