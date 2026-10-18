#pragma once

#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/WorldEnvironmentLighting.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace engine::render {

inline constexpr std::uint32_t localShadowResolution = 1024;
inline constexpr std::uint32_t localShadowAtlasResolution = 4096;
inline constexpr std::size_t localShadowCapacity = 8;
using WorldLocalShadowUniforms = std::array<std::array<float, 4>, worldRectLightCapacity * 6>;

// A representative perspective projection for a one-sided area source. The
// luminous plane is the near clip plane; housing behind it cannot occlude it.
inline WorldDirectionalShadowView rectLightShadowView(const WorldRectLight& light) {
    WorldDirectionalShadowView result;
    if (!validWorldRectLight(light) || light.range <= 0 ||
        std::max({light.radiance[0], light.radiance[1], light.radiance[2]}) <= 0) return result;
    const glm::vec3 centre(light.position[0], light.position[1], light.position[2]);
    const glm::vec3 u(light.halfU[0], light.halfU[1], light.halfU[2]);
    const glm::vec3 v(light.halfV[0], light.halfV[1], light.halfV[2]);
    const auto normal = glm::normalize(glm::cross(u, v));
    const float nearPlane = std::max(.05f, std::max(glm::length(u), glm::length(v)) / 3.7320508f);
    const auto eye = centre - normal * nearPlane;
    const float farPlane = nearPlane + light.range + glm::length(u) + glm::length(v);
    const auto matrix = glm::perspective(glm::radians(150.f), 1.f, nearPlane,
        farPlane) *
        glm::lookAt(eye, centre, glm::normalize(v));
    std::copy_n(glm::value_ptr(matrix), 16, result.viewProjection.begin());
    result.enabled = true;
    result.resolution = localShadowResolution;
    result.strength = 1;
    result.depthBias = .00015f;
    result.normalOffset = .012f;
    if (light.shadowFilter == WorldLocalShadowFilter::DistanceMoments)
        result.distanceMoments = {farPlane / nearPlane, 1 - farPlane / nearPlane, 3.7320508f, 1 / farPlane};
    return result;
}

// Shared allocation/layout policy. GPU targets remain backend-owned. The sun
// occupies the lower/upper-left 2048 square (API UV origin); locals use the right
// half. Receiver UVs and raster viewport offsets use the same API convention.
struct WorldLocalShadowAtlas {
    bool enabled = false;
    int activeLight = -1;
    std::uint32_t tileCount = 0;
    WorldDirectionalShadowView directionalView{};
    WorldLocalShadowUniforms uniforms{};

    void clear() { activeLight = -1; tileCount = 0; directionalView = {}; uniforms = {}; }
    std::array<std::uint32_t, 2> offset() const {
        if (activeLight < 0) return {};
        return {(2 + tileCount % 2) * localShadowResolution,
                (tileCount / 2) * localShadowResolution};
    }
    std::array<float, 4> directionalUv() const {
        return {0, 0, enabled ? .5f : 1.f, enabled ? .5f : 1.f};
    }
    bool begin(std::uint32_t index, const WorldDirectionalShadowView& view) {
        if (!enabled || activeLight >= 0 || tileCount >= localShadowCapacity ||
            index >= worldRectLightCapacity || uniforms[index * 6 + 4][0] != 0 ||
            !validWorldDirectionalShadowView(view) || view.resolution != localShadowResolution) return false;
        activeLight = static_cast<int>(index);
        return true;
    }
    void finish(const WorldDirectionalShadowView& view) {
        if (activeLight < 0) return;
        const auto first = static_cast<std::size_t>(activeLight) * 6;
        const auto state = packWorldDirectionalShadow(view);
        for (std::size_t c = 0; c < 4; ++c)
            std::copy_n(state.viewProjection.begin() + c * 4, 4, uniforms[first + c].begin());
        uniforms[first + 4] = state.parameters;
        if (view.distanceMoments[3] > 0)
            uniforms[first + 4] = {view.strength, view.distanceMoments[3],
                view.distanceMoments[2], -1.f / static_cast<float>(view.resolution)};
        const auto xy = offset();
        uniforms[first + 5] = {xy[0] / 4096.f, xy[1] / 4096.f, .25f, .25f};
        ++tileCount;
        activeLight = -1;
    }
};

} // namespace engine::render

class IWorldLocalShadow {
public:
    virtual ~IWorldLocalShadow() = default;
    virtual void prewarmWorldLocalShadows() = 0;
    virtual void clearWorldLocalShadows() = 0;
    virtual bool beginWorldLocalShadow(std::uint32_t rectangleIndex,
        const engine::render::WorldDirectionalShadowView& view) = 0;
    virtual bool endWorldLocalShadow() = 0;
};

namespace engine::render {
template<class Draw>
void drawWorldRectShadows(IWorldLocalShadow& renderer,
                         const WorldEnvironmentLighting& environment, Draw&& draw) {
    if (packWorldEnvironment(environment)[0][3] == 0) return;
    std::size_t count = 0;
    for (std::uint32_t i = 0; i < worldRectLightCapacity && count < localShadowCapacity; ++i) {
        const auto view = rectLightShadowView(environment.rectLights[i]);
        if (!view.enabled) continue;
        if (!renderer.beginWorldLocalShadow(i, view))
            throw std::runtime_error("Local shadow pass admission failed; prewarm the renderer first.");
        try { draw(view); }
        catch (...) { renderer.endWorldLocalShadow(); throw; }
        if (!renderer.endWorldLocalShadow()) throw std::runtime_error("Local shadow pass close failed.");
        ++count;
    }
}
}
