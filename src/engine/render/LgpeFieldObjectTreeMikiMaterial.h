#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_object_tree_miki {

inline constexpr std::uint8_t kMaterialMode = 7u;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    float highlightAlpha = 0.0f;
    float toon = 1.0f;
    float projectedShadow = 1.0f;
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    float onGameColorValue = 1.0f;
    float transparent = 1.0f;
    float onGameAlpha = 1.0f;
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float normalDotView = 1.0f;
};

inline std::array<float, 4> evaluateSurface(const SurfaceInputs& input) {
    std::array<float, 4> result{};
    const float rimSpan =
        std::max(input.rimLightMax, input.rimLightMin) -
        input.rimLightMin;
    const float rimCoordinate =
        std::clamp(1.0f - input.normalDotView, 0.0f, 1.0f);
    const float rim =
        rimSpan > 0.0f
        ? std::clamp(
              (rimCoordinate - input.rimLightMin) / rimSpan,
              0.0f,
              1.0f) *
              input.rimLightStrength
        : 0.0f;
    const float sourceLighting =
        std::clamp(input.toon, 0.0f, 1.0f) *
        std::clamp(input.projectedShadow, 0.0f, 1.0f);

    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float lighting =
            input.shadowColor[channel] +
            sourceLighting * (1.0f - input.shadowColor[channel]);
        const float highlighted =
            input.texture01[channel] +
            input.rimColor[channel] * rim * input.highlightAlpha;
        const float onGameMultiplier =
            1.0f +
            (input.onGameColor[channel] - 1.0f) *
                input.onGameColorValue;
        result[channel] =
            lighting * highlighted * input.vertexColor[channel] *
            onGameMultiplier;
    }
    result[3] =
        input.texture01[3] * input.vertexColor[3] *
        input.transparent * input.onGameAlpha;
    return result;
}

} // namespace engine::render::lgpe_field_object_tree_miki
