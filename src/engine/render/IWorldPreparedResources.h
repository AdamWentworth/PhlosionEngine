#pragma once
#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include <ostream>
#include <sstream>

namespace engine::render {
struct WorldPreparedResourceToken {
    const void* owner = nullptr;
    std::uint64_t generation = 0, scope = 0;
    std::uint32_t index = 0;
    explicit operator bool() const noexcept { return owner != nullptr; }
};
struct WorldPreparedResourceStats {
    std::uint64_t geometryPreparations = 0, materialPreparations = 0, draws = 0, staleDraws = 0;
    void print(std::ostream& out,const char* api,std::uint64_t frame) const {
        std::ostringstream line;
        line<<"[WorldPreparedResources] api="<<api<<" frame="<<frame
            <<" geometry="<<geometryPreparations<<" materials="<<materialPreparations
            <<" draws="<<draws<<" stale="<<staleDraws<<'\n';
        // Keep redirected diagnostic pipes to one write, as recording stats do.
        out<<line.str();
    }
};
// Owner-thread extension for immutable resources in a live managed scope.
// Handles borrow native resources; release closes admission immediately, while
// existing backend fences govern physical destruction. No camera, pose, light,
// material scalar or frame-upload state belongs to a resource handle.
// Texture identities and sampling must stay unchanged until a new handle is
// prepared. Anonymous/frame-owned snapshot textures use the ordinary draw path.
class IWorldPreparedResources {
public:
    virtual ~IWorldPreparedResources() = default;
    virtual WorldPreparedResourceToken prepareWorldGeometry(const char* key,
        const IRenderBackend::WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t) = 0;
    virtual WorldPreparedResourceToken prepareWorldMaterial(const IRenderBackend::WorldTextureData&) = 0;
    virtual bool worldPreparedGeometryValid(WorldPreparedResourceToken) const = 0;
    virtual bool worldPreparedMaterialValid(WorldPreparedResourceToken) const = 0;
    // False means invalid/stale handles: only rejection statistics change.
    // Valid handles use the ordinary view-specific shading/culling/recording path.
    virtual bool drawWorldPreparedIndexed(WorldPreparedResourceToken geometry,
        WorldPreparedResourceToken material,const IRenderBackend::WorldTextureData&,
        const float* viewProjection,int width,int height) = 0;
    virtual WorldPreparedResourceStats worldPreparedResourceStats() const = 0;
};
}
