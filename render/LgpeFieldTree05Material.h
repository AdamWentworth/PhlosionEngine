#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_tree05 {

inline constexpr std::uint8_t kMaterialMode = 6u;
inline constexpr float kLightGateScale = 12.740800857543945f;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};
// FieldTreeShader05 omits vector3Constant slot 1 from every audited source
// material. Route 1's FieldTreeShader04 sibling uses the byte-identical
// fragment program and the same tree20 Texture01/Texture03 atlases while
// explicitly supplying this lightColor. Keep the inherited value named and
// isolated until a correlated guest draw exposes the runtime upload.
inline constexpr std::array<float, 3> kInheritedSourceLightColor{
    0.110647157f,
    0.3070065f,
    0.0411512256f};

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    std::array<float, 4> texture02{};
    std::array<float, 4> texture03{};
    float toon = 1.0f;
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> rimColor02{};
    std::array<float, 3> sourceLightColor = kInheritedSourceLightColor;
    float discardThreshold = 0.0f;
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float secondaryMin = 0.0f;
    float secondaryMax = 1.0f;
    float normalDotView = 1.0f;
    float normalDotLight = 1.0f;
    float normalDotSecondary = 1.0f;
};

struct SurfaceResult {
    std::array<float, 4> color{};
    bool discarded = false;
};

inline SurfaceResult evaluateSurface(const SurfaceInputs& input) {
    SurfaceResult result{};
    if (input.texture01[3] <= input.discardThreshold) {
        result.discarded = true;
        return result;
    }

    const float rimSpan =
        std::max(input.rimLightMax, input.rimLightMin) - input.rimLightMin;
    const float rimCoordinate = 1.0f - input.normalDotView;
    const float rim =
        rimSpan > 0.0f
        ? std::clamp(
              (rimCoordinate - input.rimLightMin) / rimSpan,
              0.0f,
              1.0f) *
              input.rimLightStrength
        : 0.0f;
    const float lightGate =
        1.0f -
        std::clamp(
            (1.0f - input.normalDotLight) * kLightGateScale,
            0.0f,
            1.0f);
    const float secondarySpan =
        std::max(input.secondaryMax, input.secondaryMin) -
        input.secondaryMin;
    const float secondaryCoordinate =
        std::clamp(1.0f - input.normalDotSecondary, 0.0f, 1.0f);
    const float secondary =
        secondarySpan > 0.0f
        ? std::clamp(
              (secondaryCoordinate - input.secondaryMin) / secondarySpan,
              0.0f,
              1.0f)
        : 0.0f;
    const float secondaryContribution = 1.0f - secondary;
    const float toon = std::clamp(input.toon, 0.0f, 1.0f);

    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float lighting =
            input.shadowColor[channel] * (1.0f - toon) + toon;
        const float surface =
            input.texture01[channel] +
            input.texture02[channel] * rim * input.rimColor[channel] +
            input.sourceLightColor[channel] * secondaryContribution +
            input.texture03[0] * lightGate *
                input.rimColor02[channel];
        result.color[channel] = lighting * surface;
    }
    result.color[3] = input.texture01[3];
    return result;
}

} // namespace engine::render::lgpe_field_tree05
