// src/engine/assets/phlosion/PhlosionArchiveSession.h
#pragma once

#include "engine/assets/phlosion/PhlosionAssetGraphLoader.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <memory>
#include <string>

namespace engine::assets::phlosion {

// Which typed archive decodes the staged root.
enum class ArchiveKind {
    Scene,
    Prefab,
};

// The candidate's lifecycle. It describes the staged replacement only: an
// active snapshot can be published while the candidate is Idle, Loading or
// Failed.
enum class ArchiveSessionState {
    Idle,
    Loading,
    Staged,
    Failed,
    Cancelled,
};

// One complete, immutable candidate: the Ready asset graph plus the root's
// decoded typed archive. `scene` is non-null exactly for ArchiveKind::Scene and
// `prefab` exactly for ArchiveKind::Prefab. Handles returned by the session are
// shared_ptr<const>, so a published snapshot never changes afterwards and
// stays readable after the candidate is discarded, activated or unloaded.
struct ArchiveSnapshot {
    ArchiveKind kind = ArchiveKind::Scene;
    AssetGraphResult graph;
    std::shared_ptr<const SceneArchiveStore> scene;
    std::shared_ptr<const PrefabArchiveStore> prefab;
};

// Staged scene/prefab activation over the dependency-aware asset graph.
//
// The session composes one unchanged PhlosionAssetGraphLoader and the existing
// typed archive stores. `begin` admits a root candidate, `pump` advances it
// without waiting for I/O, and only a Ready graph whose root decodes with the
// requested typed archive store becomes Staged. `activate` transfers the staged
// snapshot to `active` without re-reading or copying payloads; a failed,
// cancelled or discarded candidate never changes the active snapshot.
//
// The session is noncopyable and nonmovable. Every method, the resolver and the
// decode step run on one owner thread; no method may be called concurrently or
// reentered from the resolver. Provider reads run on the graph loader's single
// background worker, and the loader's provider ownership, concurrency,
// constructor rejection and blocked-I/O destructor rules apply unchanged here.
// The destructor joins that reader, which can wait for a read the provider has
// already started and cannot interrupt a blocked provider.
//
// AssetGraphOptions bounds every candidate graph separately. It is not a total
// residency or peak-memory cap: the active and staged snapshots may coexist,
// a typed archive store duplicates the embedded payloads it decoded, and
// consumer-held snapshots can outlive the session.
class PhlosionArchiveSession {
public:
    explicit PhlosionArchiveSession(
        std::shared_ptr<const engine::IAssetStore> store,
        AssetResolver resolver,
        AssetGraphOptions options = {});

    ~PhlosionArchiveSession();

    PhlosionArchiveSession(const PhlosionArchiveSession&) = delete;
    PhlosionArchiveSession& operator=(const PhlosionArchiveSession&) = delete;
    PhlosionArchiveSession(PhlosionArchiveSession&&) = delete;
    PhlosionArchiveSession& operator=(PhlosionArchiveSession&&) = delete;

    // Admits a candidate from Idle only: the root ID must be nonempty and
    // `kind` must be Scene or Prefab. Rejects any other call with a diagnostic
    // and leaves the candidate, the active snapshot and error() unchanged.
    // Success clears the candidate error and enters Loading; no resolver, I/O
    // or decode work runs here.
    bool begin(
        std::string rootAssetId,
        ArchiveKind kind,
        std::string* outError = nullptr);

    // Advances a Loading candidate without waiting for I/O: one graph pump,
    // plus the root decode when that pump makes the graph Ready. A no-op in
    // every other state.
    void pump();

    ArchiveSessionState state() const;

    // Candidate diagnostic, empty unless the candidate failed or was
    // cancelled.
    const std::string& error() const;

    // The complete candidate, non-null only in Staged.
    std::shared_ptr<const ArchiveSnapshot> staged() const;

    // The published snapshot. Never changed by begin, pump or a candidate
    // failure.
    std::shared_ptr<const ArchiveSnapshot> active() const;

    // Succeeds only in Staged: moves the staged snapshot to active and returns
    // the candidate to Idle without re-reading, re-decoding or copying
    // payloads. Any other state returns false and changes nothing.
    bool activate();

    // Succeeds only in Loading: releases the graph candidate, drops its data
    // and enters Cancelled with a root-specific diagnostic. The active
    // snapshot is preserved and a read that already started is never waited
    // for. Any other state returns false and changes nothing.
    bool cancel();

    // Drops only the candidate graph, data and error and returns to Idle,
    // preserving the active snapshot. Valid from any state, and it lets a new
    // begin start even while a released read is still running.
    void discard();

    // discard() plus dropping this session's active reference. Snapshots
    // already returned to callers stay usable.
    void unload();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::assets::phlosion
