#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_overlay {

inline constexpr std::uint8_t kRoadstoneMaterialMode = 13u;
inline constexpr std::uint8_t kRockMaskMaterialMode = 14u;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};

struct SharedInputs {
    float toon = 1.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    float onGameColorValue = 1.0f;
    float onGameAlpha = 1.0f;
};

struct RoadstoneInputs : SharedInputs {
    std::array<float, 4> texture01{};
    float transparent = 1.0f;
};

struct RockMaskInputs : SharedInputs {
    std::array<float, 3> textureMap01{};
    std::array<float, 3> textureMap02{};
    std::array<float, 4> greenHikari{};
    std::array<float, 3> color{};
    float greenBlend = 0.0f;
    float highlight = 0.0f;
};

struct SurfaceResult {
    // Both recovered shaders write premultiplied RGB.
    std::array<float, 4> color{};
};

inline float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline std::array<float, 3> evaluateLighting(
    const SharedInputs& input) {
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

inline float evaluateAlpha(
    float sourceAlpha,
    float vertexAlpha,
    float transparent,
    float onGameAlpha) {
    return saturate(sourceAlpha) * saturate(vertexAlpha) *
        saturate(transparent) * saturate(onGameAlpha);
}

inline SurfaceResult evaluateRoadstoneSurface(
    const RoadstoneInputs& input) {
    SurfaceResult result;
    const auto lighting = evaluateLighting(input);
    const float alpha = evaluateAlpha(
        input.texture01[3],
        input.vertexColor[3],
        input.transparent,
        input.onGameAlpha);
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] =
            lighting[channel] * input.texture01[channel] *
            input.vertexColor[channel] * onGame * alpha;
    }
    result.color[3] = alpha;
    return result;
}

inline SurfaceResult evaluateRockMaskSurface(
    const RockMaskInputs& input) {
    SurfaceResult result;
    const auto lighting = evaluateLighting(input);
    // The recovered fragment program intentionally does not multiply the
    // rock-mask alpha by Color0.a.
    const float alpha =
        saturate(input.greenHikari[3]) * saturate(input.onGameAlpha);
    const float textureBlend = saturate(input.greenBlend);
    const float highlight = saturate(input.highlight);
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float soil =
            input.textureMap02[channel] * (1.0f - textureBlend) +
            input.textureMap01[channel] * textureBlend;
        const float decoration =
            soil + input.color[channel] * (1.0f - highlight);
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] =
            lighting[channel] * decoration *
            input.greenHikari[channel] * input.vertexColor[channel] *
            onGame * alpha;
    }
    result.color[3] = alpha;
    return result;
}

} // namespace engine::render::lgpe_field_overlay
