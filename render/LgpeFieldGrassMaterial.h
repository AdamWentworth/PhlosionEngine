#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_grass {

inline constexpr std::uint8_t kShader02MaterialMode = 9u;
inline constexpr std::uint8_t kShader01MaterialMode = 10u;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};

struct SurfaceInputs {
    std::array<float, 4> textureMap01{};
    std::array<float, 4> textureMap02{};
    std::array<float, 4> greenHikari{};
    float greenBlend = 0.0f;
    float highlight = 0.0f;
    float toon = 1.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    std::array<float, 3> color{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    float discardThreshold = 0.0f;
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float normalDotView = 1.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 1.0f;
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

inline std::array<float, 3> evaluateLocalSurface(
    const SurfaceInputs& input) {
    std::array<float, 3> surface{};
    const float blend = saturate(input.greenBlend);
    const float highlight = saturate(input.highlight);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float base =
            input.textureMap02[channel] * (1.0f - blend) +
            input.textureMap01[channel] * blend;
        const float decoration =
            base + input.color[channel] * (1.0f - highlight);
        surface[channel] =
            decoration * input.greenHikari[channel] *
            input.vertexColor[channel];
    }
    return surface;
}

inline std::array<float, 3> evaluateLighting(
    const SurfaceInputs& input) {
    const float light = std::min(
        saturate(input.toon) * saturate(input.projectedShadow),
        saturate(input.projectedCloud));
    std::array<float, 3> lighting{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        lighting[channel] =
            input.shadowColor[channel] * (1.0f - light) + light;
    }
    return lighting;
}

// Source-backed FieldGrassShader02 order after reconciling the recovered
// program with its sampler dictionary and the authored alpha-bearing
// green_hikari atlas. The shared projected shadow/cloud terms remain explicit
// because their matrices and frame state are not material-local.
inline SurfaceResult evaluateShader02Surface(const SurfaceInputs& input) {
    SurfaceResult result;
    if (input.greenHikari[3] <= saturate(input.discardThreshold)) {
        result.discarded = true;
        return result;
    }
    const auto surface = evaluateLocalSurface(input);
    const auto lighting = evaluateLighting(input);
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] = lighting[channel] * surface[channel] * onGame;
    }
    result.color[3] =
        input.greenHikari[3] * saturate(input.onGameAlpha);
    return result;
}

// FieldGrassShader01 uses the same base/decal contract, then adds RimColor
// before toon/cloud/shadow lighting.
inline SurfaceResult evaluateShader01Surface(const SurfaceInputs& input) {
    SurfaceResult result;
    if (input.greenHikari[3] <= saturate(input.discardThreshold)) {
        result.discarded = true;
        return result;
    }
    auto surface = evaluateLocalSurface(input);
    const auto lighting = evaluateLighting(input);
    const float rim =
        rangeMap(
            saturate(1.0f - input.normalDotView),
            input.rimLightMin,
            input.rimLightMax) *
        input.rimLightStrength;
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        surface[channel] += input.rimColor[channel] * rim;
        result.color[channel] = lighting[channel] * surface[channel];
    }
    result.color[3] = input.greenHikari[3];
    return result;
}

} // namespace engine::render::lgpe_field_grass
