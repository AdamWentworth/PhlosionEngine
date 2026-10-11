#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include <functional>
#include <optional>
#include <span>
#include <glm/glm.hpp>

namespace engine::render {
struct RainHit { glm::vec3 point{}, normal{0,1,0}; float fraction = 0; };
using RainCoverQuery = std::function<std::optional<RainHit>(glm::vec3, glm::vec3)>;
struct RainVolumeStats {
    std::size_t drops = 0, impacts = 0, covered = 0, queries = 0;
};

// Analytic, world-anchored rain in a bounded camera-local volume. The owner
// supplies collision cover and accepted play time, including presentation remainder.
class RainVolume {
public:
    static constexpr std::size_t maxDrops = 1215, maxImpacts = 81;
    RainVolume();
    RainVolumeStats build(const WorldEnvironmentLighting&, glm::vec3 camera,
        const RainCoverQuery&);
    void prewarm(IRenderBackend&, WorldResourceScopeId) const;
    void draw(IRenderBackend&, WorldResourceScopeId, const float* viewProjection,
        int width, int height) const;
    std::span<const IRenderBackend::WorldMeshVertex> dropVertices() const { return drops_; }
    std::span<const IRenderBackend::WorldMeshVertex> impactVertices() const { return impacts_; }
private:
    IRenderBackend::WorldTextureData texture(bool impact) const;
    void quad(std::vector<IRenderBackend::WorldMeshVertex>&, glm::vec3 start,
        glm::vec3 end, glm::vec3 side, glm::vec3 color, float alpha);
    std::array<unsigned char,32*64*4> streakPixels_{};
    std::array<unsigned char,32*32*4> impactPixels_{};
    std::vector<IRenderBackend::WorldMeshVertex> drops_, impacts_;
    std::vector<std::uint32_t> indices_;
};
} // namespace engine::render
