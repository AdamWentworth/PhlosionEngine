#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace engine::render::lgpe_field_encounter_grass {

inline constexpr std::uint8_t kMaterialMode = 18u;
inline constexpr float kDiscardValue = 0.632317066f;
inline constexpr float kRimMin = 0.5f;
inline constexpr float kRimMax = 1.0f;
inline constexpr float kRimStrength = 1.0f;
inline constexpr float kWindCycleSeconds = 4.0f;
inline constexpr float kMaximumBendRadians = 0.133f;
inline constexpr float kMaximumCrossRadians = 0.04f;

enum class SourceVariant : std::uint8_t {
    Grass01,
    Grass02,
};

struct WindJointRotation {
    float bendRadians = 0.0f;
    float crossRadians = 0.0f;
};

inline std::array<float, 3> sourceJointPivot(
    SourceVariant variant,
    std::uint32_t jointIndex) {
    // Exact DAE controller-joint order, in original centimetres/Y-up.
    constexpr std::array<std::array<float, 3>, 5> grass01{{
        {0.0f, 0.0f, 0.0f},
        {-36.41992f, 44.032f, 39.627182f},
        {31.750124f, 44.032f, 38.619396f},
        {-36.4830971f, 39.21621f, -31.41788f},
        {31.31517f, 44.032f, -31.06537f},
    }};
    constexpr std::array<std::array<float, 3>, 6> grass02{{
        {0.0f, 0.0f, 0.0f},
        {-36.41992f, 44.032f, 39.627182f},
        {-2.194621f, 39.21621f, 4.910496f},
        {31.750124f, 44.032f, 38.619396f},
        {-36.4830971f, 39.21621f, -31.41788f},
        {31.31517f, 44.032f, -31.06537f},
    }};
    if (variant == SourceVariant::Grass01) {
        return jointIndex < grass01.size()
            ? grass01[jointIndex]
            : std::array<float, 3>{};
    }
    return jointIndex < grass02.size()
        ? grass02[jointIndex]
        : std::array<float, 3>{};
}

inline WindJointRotation evaluateWindJointRotation(
    std::uint32_t jointIndex,
    float placementPhaseCycles,
    float windPhaseCycles) {
    if (jointIndex == 0u) return {};
    constexpr float kTau = 6.2831853071795864769f;
    const float componentPhase =
        static_cast<float>(jointIndex - 1u) * 0.071f;
    const float angle =
        kTau * (windPhaseCycles + placementPhaseCycles +
                componentPhase) +
        static_cast<float>(jointIndex) * 0.19f;
    return {
        0.115f * std::sin(angle) +
            0.018f * std::sin(2.0f * angle + 0.55f),
        0.040f * std::sin(angle + 0.92f)};
}

struct SurfaceInputs {
    std::array<float, 4> texture01{};
    float texture02Red = 0.0f;
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    float shadowToon = 1.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    float normalDotView = 1.0f;
    float rimMin = kRimMin;
    float rimMax = kRimMax;
    float rimStrength = kRimStrength;
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
        saturate(input.vertexColor[3]);
    result.discarded = input.texture01[3] <= kDiscardValue;

    const float rimSpan =
        std::max(input.rimMax, input.rimMin) - input.rimMin;
    const float rimCoordinate =
        rimSpan > 0.0f
        ? saturate(
              (saturate(1.0f - input.normalDotView) - input.rimMin) /
              rimSpan)
        : 0.0f;
    const float smoothRim =
        rimCoordinate * rimCoordinate * (3.0f - 2.0f * rimCoordinate);
    const float rim =
        smoothRim * input.rimStrength * saturate(input.texture02Red);
    const float light = std::min(
        saturate(input.shadowToon) *
            saturate(input.projectedShadow),
        saturate(input.projectedCloud));

    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float base =
            input.texture01[channel] * input.vertexColor[channel];
        const float lighting =
            input.shadowColor[channel] * (1.0f - light) + light;
        result.color[channel] =
            (base + input.rimColor[channel] * rim) * lighting;
    }
    result.color[3] = alpha;
    return result;
}

} // namespace engine::render::lgpe_field_encounter_grass
