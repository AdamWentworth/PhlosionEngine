#pragma once

#include "engine/assets/phlosion/PhlosionArchiveSession.h"
#include "engine/render/gltf/GltfSceneAsset.h"

#include <functional>
#include <memory>
#include <string>

namespace engine::render::gltf::scene {

enum class GltfSceneSessionState { Idle, Loading, Decoding, Staged, Failed, Cancelled };

struct GltfSceneSessionOptions {
    assets::phlosion::AssetGraphOptions graph;
    GltfSceneLoadOptions import;
    // Logical geometry/pixel bytes in active + accepted candidate outputs.
    // Decode temporaries, archive storage and consumer-held snapshots are separate.
    std::uint64_t maxDecodedResidentBytes = 256ull << 20;
};

struct GltfSceneSnapshot {
    std::shared_ptr<const assets::phlosion::ArchiveSnapshot> archive;
    std::shared_ptr<const GltfSceneAsset> scene;
    std::uint64_t encodedGraphBytes = 0u;
    double decodeSeconds = 0.0;
};

namespace detail { struct GltfSceneSessionTestAccess; }

// Owner-thread API. Archive byte reads and glTF decode run on owned workers;
// pump retains the existing graph/PHRC decode work on its caller. No graphics
// context is used by either worker. Cancellation suppresses publication, but
// destruction can wait for an already running read/decode to finish.
class GltfSceneSession {
public:
    GltfSceneSession(std::shared_ptr<const engine::IAssetStore> store,
                     assets::phlosion::AssetResolver resolver,
                     GltfSceneSessionOptions options = {});
    ~GltfSceneSession();
    GltfSceneSession(const GltfSceneSession&) = delete;
    GltfSceneSession& operator=(const GltfSceneSession&) = delete;

    bool begin(std::string archiveId, assets::phlosion::ArchiveKind kind,
               std::string documentPath, std::string* error = nullptr);
    void pump();
    GltfSceneSessionState state() const;
    const std::string& error() const;
    std::shared_ptr<const GltfSceneSnapshot> staged() const;
    std::shared_ptr<const GltfSceneSnapshot> active() const;
    bool activate();
    bool cancel();
    void discard();
    void unload();
    std::uint64_t decodedResidentBytes() const;

private:
    friend struct detail::GltfSceneSessionTestAccess;
    using Decoder = std::function<bool(const engine::IAssetStore&, const std::string&,
        std::shared_ptr<const GltfSceneAsset>&, std::string*, const GltfSceneLoadOptions&)>;
    GltfSceneSession(std::shared_ptr<const engine::IAssetStore> store,
                     assets::phlosion::AssetResolver resolver,
                     GltfSceneSessionOptions options, Decoder decoder);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::render::gltf::scene
