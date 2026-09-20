// src/engine/assets/phlosion/PhlosionAssetGraphLoader.h
#pragma once

#include "engine/assets/AsyncAssetLoader.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/core/IAssetStore.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

// How a resolved asset is interpreted. `Phrc` documents are decoded and their
// required dependencies are traversed; `Bytes` is an opaque leaf.
enum class AssetKind {
    Phrc,
    Bytes,
};

struct AssetLocation {
    std::string virtualPath;
    AssetKind kind = AssetKind::Phrc;
};

// Maps an opaque asset ID to the place it is read from. The session never
// infers a path or a kind from an ID; the resolver owns that mapping.
using AssetResolver = std::function<bool(const std::string& assetId,
    AssetLocation& outLocation, std::string* outError)>;

enum class AssetGraphState {
    Idle,
    Loading,
    Ready,
    Failed,
    Cancelled,
};

struct AssetGraphOptions {
    // Unique required assets, including the root.
    std::size_t maxAssets = 256u;
    // Every encountered dependency record, including duplicate and optional
    // records. A zero limit admits documents without dependencies.
    std::size_t maxDependencyEdges = 4096u;
    // Cumulative retained encoded payload bytes for this session. Zero admits
    // only empty payloads.
    std::size_t maxEncodedBytes = 64u * 1024u * 1024u;
};

struct LoadedAsset {
    std::string virtualPath;
    AssetKind kind = AssetKind::Phrc;
    std::vector<std::uint8_t> bytes;
    // Validated PHRC document hash for `Phrc`, or the whole-payload hash for
    // `Bytes`. This is the value dependency constraints are compared against.
    std::uint64_t contentHash = 0u;
};

struct SkippedOptionalDependency {
    std::string parentAssetId;
    phrc::Dependency dependency;
};

struct AssetGraphResult {
    AssetGraphState state = AssetGraphState::Idle;
    std::string rootAssetId;
    // Keyed by opaque asset ID. Only a Ready result carries assets.
    std::map<std::string, LoadedAsset> assets;
    std::vector<SkippedOptionalDependency> skippedOptionalDependencies;
    std::string error;
};

// Dependency-aware asset session over an existing IAssetStore.
//
// Loads one root asset and its required transitive closure, verifies PHRC
// integrity and dependency hashes, and publishes the complete closure or
// nothing. Provider reads run on the AsyncAssetLoader worker; every public
// method of this session, including the resolver, runs on one owner thread and
// must not be called concurrently. The resolver must not reenter this session.
//
// `pump` never waits for I/O. It resolves or schedules work, or consumes at
// most one completed read and decodes it on the calling thread, and it may
// schedule the next read in the same call. Decoding and traversal are not
// frame-time bounded.
//
// The encoded-byte cap is a retained accounting budget, not a peak-memory
// bound: provider read temporaries, the reader's completed-read staging,
// decoder allocations, provider caches, graph metadata, vector capacity and
// the bytes after a take are outside it.
//
// Destruction stops and joins the background reader, which can wait for a read
// the provider has already started; it cannot interrupt a blocked provider.
class PhlosionAssetGraphLoader {
public:
    explicit PhlosionAssetGraphLoader(
        std::shared_ptr<const engine::IAssetStore> store,
        AssetResolver resolver,
        AssetGraphOptions options = {});

    ~PhlosionAssetGraphLoader();

    PhlosionAssetGraphLoader(const PhlosionAssetGraphLoader&) = delete;
    PhlosionAssetGraphLoader& operator=(const PhlosionAssetGraphLoader&) = delete;
    PhlosionAssetGraphLoader(PhlosionAssetGraphLoader&&) = delete;
    PhlosionAssetGraphLoader& operator=(PhlosionAssetGraphLoader&&) = delete;

    // Admits a root from Idle only. Resolving and reading start in pump().
    // Rejects an empty ID or any other state with a diagnostic and leaves an
    // existing session intact.
    bool start(std::string rootAssetId, std::string* outError = nullptr);

    // Advances the session without waiting for I/O. A no-op outside Loading.
    void pump();

    AssetGraphState state() const;

    // Succeeds only in Loading. The graph becomes Cancelled immediately and
    // releases the underlying request and staged data; a read that already
    // started may finish but can never publish afterwards.
    bool cancel();

    // Moves a terminal result out and returns the session to Idle. Succeeds
    // only for Ready, Failed or Cancelled; otherwise the output is unchanged.
    bool take(AssetGraphResult& outResult);

    // Releases active or staged data from any state and returns to Idle. A
    // released read may still be finishing and can never reach a later session.
    void reset();

    // Accepted encoded payload bytes this session still owns.
    std::size_t loadedBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::assets::phlosion
