#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_flower {

inline constexpr std::uint8_t kMaterialMode = 15u;
inline constexpr float kDiscardValue = 0.85f;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    float toon = 1.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    float transparent = 1.0f;
    float onGameColorValue = 1.0f;
    float onGameAlpha = 1.0f;
};

struct SurfaceResult {
    std::array<float, 4> color{};
    bool discarded = false;
};

inline float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline SurfaceResult evaluateSurface(const SurfaceInputs& input) {
    SurfaceResult result;
    const float alpha =
        saturate(input.texture01[3]) *
        saturate(input.vertexColor[3]) *
        saturate(input.transparent) *
        saturate(input.onGameAlpha);
    result.discarded = alpha <= kDiscardValue;

    const float light = std::min(
        saturate(input.toon) * saturate(input.projectedShadow),
        saturate(input.projectedCloud));
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float lighting =
            input.shadowColor[channel] * (1.0f - light) + light;
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] =
            lighting * input.texture01[channel] *
            input.vertexColor[channel] * onGame;
    }
    result.color[3] = alpha;
    return result;
}

} // namespace engine::render::lgpe_field_flower
