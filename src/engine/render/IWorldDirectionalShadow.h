#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace engine::render {

// Column-major OpenGL clip coordinates (-1..1 depth), matching world draws.
// Bias is normalized depth; normal offset is in world metres.
struct WorldDirectionalShadowView {
    bool enabled = false;
    std::array<float, 16> viewProjection{};
    std::uint32_t resolution = 2048;
    float strength = 1;
    float depthBias = .0004f;
    float normalOffset = .025f;
};

struct WorldDirectionalShadowState {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> parameters{};
};
static_assert(sizeof(WorldDirectionalShadowState) == 80);

inline bool validWorldDirectionalShadowView(const WorldDirectionalShadowView& view) {
    if (!view.enabled || view.resolution < 128 || view.resolution > 4096 ||
        (view.resolution & (view.resolution - 1)) != 0 ||
        !std::isfinite(view.strength) || view.strength <= 0 || view.strength > 1 ||
        !std::isfinite(view.depthBias) || view.depthBias < 0 || view.depthBias > .01f ||
        !std::isfinite(view.normalOffset) || view.normalOffset < 0 || view.normalOffset > 1)
        return false;
    float magnitude = 0;
    for (float value : view.viewProjection) {
        if (!std::isfinite(value) || std::abs(value) > 1000000) return false;
        magnitude = std::max(magnitude, std::abs(value));
    }
    return magnitude > .000001f;
}

inline WorldDirectionalShadowState packWorldDirectionalShadow(
    const WorldDirectionalShadowView& view) {
    if (!validWorldDirectionalShadowView(view)) return {};
    return {view.viewProjection, {view.strength, view.depthBias, view.normalOffset,
        1.f / static_cast<float>(view.resolution)}};
}

} // namespace engine::render

// Optional renderer pass. Submit the same current geometry/poses between begin
// and end; the backend supplies the light projection and rejects non-casters.
// Resources are prepared before play. clear disables receivers for a new view.
class IWorldDirectionalShadow {
public:
    virtual ~IWorldDirectionalShadow() = default;
    virtual void prewarmWorldDirectionalShadow(std::uint32_t resolution) = 0;
    virtual bool beginWorldDirectionalShadow(
        const engine::render::WorldDirectionalShadowView& view) = 0;
    virtual bool endWorldDirectionalShadow() = 0;
    virtual void clearWorldDirectionalShadow() = 0;
};
