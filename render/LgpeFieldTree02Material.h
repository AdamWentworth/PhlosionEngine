#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_tree02 {

inline constexpr std::uint8_t kMaterialMode = 8u;
inline constexpr float kDirectionalEdgeScale = 5.0f / 3.0f;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    std::array<float, 4> texture02{};
    float toon = 1.0f;
    float lightToon = 0.0f;
    float projectedShadow = 1.0f;
    std::array<float, 3> greenColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> directionalLightColor{};
    std::array<float, 3> rimColor02{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    float discardThreshold = 0.0f;
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float normalDotView = 1.0f;
};

struct SurfaceResult {
    std::array<float, 4> color{};
    bool discarded = false;
};

inline float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float rangeMap(float value, float minimum, float maximum) {
    const float span = std::max(maximum, minimum) - minimum;
    return span > 0.0f ? saturate((value - minimum) / span) : 0.0f;
}

// Deterministic scalar oracle for the exact local FieldTreeShader02 fragment
// program recovered from Route 1's named tree004/tree005 BNSH programs.
// projectedShadow is an explicit input because the source ten-tap PCF requires
// the game's shared shadow matrix/depth state, which is not part of a material.
inline SurfaceResult evaluateSurface(const SurfaceInputs& input) {
    SurfaceResult result;
    if (input.texture01[3] <= saturate(input.discardThreshold)) {
        result.discarded = true;
        return result;
    }

    const float edge = saturate(1.0f - input.normalDotView);
    const float rim =
        rangeMap(edge, input.rimLightMin, input.rimLightMax) *
        input.rimLightStrength;
    const float directional = saturate(edge * kDirectionalEdgeScale);
    const float shadow =
        saturate(input.toon) * saturate(input.projectedShadow);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float secondary =
            rim * input.rimColor[channel] +
            (1.0f - directional) * input.rimColor02[channel] +
            saturate(input.lightToon) *
                input.directionalLightColor[channel];
        const float surface =
            input.texture01[channel] +
            input.texture02[channel] * secondary;
        const float authored = surface * input.vertexColor[channel];
        const float tinted =
            input.greenColor[channel] * (1.0f - input.vertexColor[3]) +
            authored * input.vertexColor[3];
        const float lighting =
            input.shadowColor[channel] * (1.0f - shadow) + shadow;
        result.color[channel] = lighting * tinted;
    }
    result.color[3] = input.texture01[3];
    return result;
}

} // namespace engine::render::lgpe_field_tree02
