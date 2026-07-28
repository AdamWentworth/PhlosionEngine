#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_sign {

inline constexpr std::uint8_t kMaterialMode = 17u;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};
inline constexpr float kShadowMin = 0.0f;
inline constexpr float kShadowMax = 1.0f;
inline constexpr float kShadowStrength = 1.0f;
inline constexpr float kRimMin = 0.432098567f;
inline constexpr float kRimMax = 1.0f;
inline constexpr float kRimStrength = 1.0f;

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> normal{};
    std::array<float, 3> lightColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> autoShadowColor{1.0f, 1.0f, 1.0f};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    std::array<float, 3> rimColor{1.0f, 1.0f, 1.0f};
    float shadowToon = 1.0f;
    float lightToon = 0.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    float shadowMin = kShadowMin;
    float shadowMax = kShadowMax;
    float shadowStrength = kShadowStrength;
    float rimMin = kRimMin;
    float rimMax = kRimMax;
    float rimStrength = kRimStrength;
    float normalDotView = 1.0f;
    float transparent = 1.0f;
    float onGameColorValue = 1.0f;
    float onGameAlpha = 1.0f;
};

inline float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float linearWindow(
    float value,
    float minimum,
    float maximum,
    float strength) {
    const float span = std::max(maximum, minimum) - minimum;
    if (span <= 0.0f) return 0.0f;
    return saturate((value - minimum) / span) * strength;
}

inline std::array<float, 4> evaluateSurface(const SurfaceInputs& input) {
    const float autoShadowEdge = saturate(
        1.0f -
        (-input.normal[0] + input.normal[1] + input.normal[2]));
    const float autoShadow = linearWindow(
        autoShadowEdge,
        input.shadowMin,
        input.shadowMax,
        input.shadowStrength);
    const float rim = linearWindow(
        saturate(1.0f - input.normalDotView),
        input.rimMin,
        input.rimMax,
        input.rimStrength);
    const float light = std::min(
        saturate(input.shadowToon) *
            saturate(input.projectedShadow),
        saturate(input.projectedCloud));
    const float onGameValue = saturate(input.onGameColorValue);

    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float localShadow =
            1.0f +
            (input.autoShadowColor[channel] - 1.0f) * autoShadow;
        const float surface =
            input.texture01[channel] * localShadow +
            input.lightColor[channel] * saturate(input.lightToon) +
            input.rimColor[channel] * rim;
        const float lighting =
            input.shadowColor[channel] * (1.0f - light) + light;
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        output[channel] =
            lighting * surface * input.vertexColor[channel] * onGame;
    }
    output[3] =
        saturate(input.texture01[3]) *
        saturate(input.vertexColor[3]) *
        saturate(input.transparent) *
        saturate(input.onGameAlpha);
    return output;
}

} // namespace engine::render::lgpe_field_sign
