#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_flower {

inline constexpr std::uint8_t kMaterialMode = 15u;
// The accepted Blender checkpoint keeps the exact source program available
// for the road flower, but compensates BuildModel flower alpha coverage lost
// during texture minification. Keep that reviewed presentation in a separate
// mode so it cannot silently change the source-exact road material.
inline constexpr std::uint8_t kBuildmodelMaterialMode = 20u;
inline constexpr float kDiscardValue = 0.85f;
inline constexpr float kBuildmodelCoverageMin = 0.55f;
inline constexpr float kBuildmodelCoverageMax = 0.85f;
inline constexpr float kBuildmodelSaturation = 1.14f;
inline constexpr float kBuildmodelValue = 1.12f;
inline constexpr float kBuildmodelSurfaceRestraint = 0.35f;
inline constexpr float kBuildmodelProjectionRestraint = 0.25f;
inline constexpr float kBuildmodelEmissionStrength = 0.12f;
inline constexpr float kRoadShadowBias = 0.003f;
inline constexpr float kBuildmodelShadowBias = 0.001f;
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

inline float buildmodelCoverage(float alpha) {
    const float t = saturate(
        (alpha - kBuildmodelCoverageMin) /
        (kBuildmodelCoverageMax - kBuildmodelCoverageMin));
    return t * t * (3.0f - 2.0f * t);
}

inline std::array<float, 3> buildmodelFieldHighlight(
    const std::array<float, 4>& texture01) {
    const float maximum = std::max({
        texture01[0], texture01[1], texture01[2]});
    const float minimum = std::min({
        texture01[0], texture01[1], texture01[2]});
    const float chroma = maximum - minimum;
    const float sourceSaturation =
        maximum > 0.000001f ? chroma / maximum : 0.0f;
    const float saturation =
        saturate(sourceSaturation * kBuildmodelSaturation);
    const float value = maximum * kBuildmodelValue;
    const float adjustedMinimum = value * (1.0f - saturation);
    const float adjustedChroma = value * saturation;
    std::array<float, 3> color{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float hueComponent =
            chroma > 0.000001f
                ? (texture01[channel] - minimum) / chroma
                : 0.0f;
        color[channel] =
            adjustedMinimum + hueComponent * adjustedChroma;
    }
    return color;
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

inline SurfaceResult evaluateBuildmodelSurface(
    const SurfaceInputs& input,
    float ditherThreshold = 0.0f) {
    SurfaceResult result;
    const float sourceAlpha =
        saturate(input.texture01[3]) *
        saturate(input.vertexColor[3]) *
        saturate(input.transparent) *
        saturate(input.onGameAlpha);
    const float coverage = buildmodelCoverage(sourceAlpha);
    result.discarded = coverage <= saturate(ditherThreshold);

    const auto accepted = buildmodelFieldHighlight(input.texture01);
    const float projectedCloud = saturate(input.projectedCloud);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float projectedLighting =
            input.shadowColor[channel] * (1.0f - projectedCloud) +
            projectedCloud;
        const float projectionCompensation =
            1.0f +
            (projectedLighting - 1.0f) *
                kBuildmodelProjectionRestraint;
        const float exact =
            input.texture01[channel] *
            input.vertexColor[channel] *
            projectionCompensation;
        const float restrained =
            accepted[channel] * (1.0f - kBuildmodelSurfaceRestraint) +
            exact * kBuildmodelSurfaceRestraint;
        const float sourceLight = std::min(
            saturate(input.toon) * saturate(input.projectedShadow),
            projectedCloud);
        const float fieldLighting =
            input.shadowColor[channel] * (1.0f - sourceLight) +
            sourceLight;
        result.color[channel] =
            restrained * (fieldLighting + kBuildmodelEmissionStrength);
    }
    result.color[3] = coverage;
    return result;
}

} // namespace engine::render::lgpe_field_flower
