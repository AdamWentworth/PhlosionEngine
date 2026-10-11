#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include <array>
#include <vector>

namespace engine::render {

// Reusable ground-level atmosphere and layered clouds. Data is built before activation;
// GPU data belongs to the caller's world scope, using existing portable draws.
class AtmosphereSky {
public:
    AtmosphereSky();
    static std::array<float, 3> referenceRadiance(float sunElevationRadians,
        float viewElevationRadians, float relativeAzimuthRadians);
    static std::array<float, 2> referenceCloudDensity(float u, float v);
    void prewarm(IRenderBackend& renderer, WorldResourceScopeId scope) const;
    void draw(IRenderBackend& renderer, WorldResourceScopeId scope,
        const WorldEnvironmentLighting& environment,
        const std::array<float, 3>& cameraPosition, const float* viewProjection,
        int width, int height) const;
private:
    IRenderBackend::WorldTextureData texture() const;
    std::vector<unsigned char> atlas_;
    std::array<IRenderBackend::WorldMeshVertex, 8> vertices_{};
    std::array<std::uint32_t, 36> indices_{};
};

} // namespace engine::render
