#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_small_grass {

inline constexpr std::uint8_t kShader04MaterialMode = 11u;
inline constexpr std::uint8_t kShader05MaterialMode = 12u;
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
    float discardThreshold = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 1.0f;
};

struct Shader04Inputs : SharedInputs {
    std::array<float, 4> texture01{};
    std::array<float, 4> texture02{};
    float texture03 = 0.0f;
    float transparent = 1.0f;
};

struct Shader05Inputs : SharedInputs {
    std::array<float, 3> textureMap01{};
    std::array<float, 3> textureMap02{};
    float greenBlend = 0.0f;
    float lightLine = 0.0f;
    std::array<float, 4> alpha01Primary{};
    std::array<float, 4> alpha01Secondary{};
};

struct SurfaceResult {
    std::array<float, 4> color{};
    bool discarded = false;
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

inline SurfaceResult evaluateShader04Surface(const Shader04Inputs& input) {
    SurfaceResult result;
    std::array<float, 4> base{};
    const float textureBlend = saturate(input.texture03);
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        base[channel] =
            input.texture02[channel] * (1.0f - textureBlend) +
            input.texture01[channel] * textureBlend;
    }
    const float alpha =
        base[3] * input.vertexColor[3] * saturate(input.transparent) *
        saturate(input.onGameAlpha);
    if (alpha <= saturate(input.discardThreshold)) {
        result.discarded = true;
        return result;
    }
    const auto lighting = evaluateLighting(input);
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] =
            lighting[channel] * base[channel] *
            input.vertexColor[channel] * onGame;
    }
    result.color[3] = alpha;
    return result;
}

inline SurfaceResult evaluateShader05Surface(const Shader05Inputs& input) {
    SurfaceResult result;
    std::array<float, 4> base{};
    const float baseBlend = saturate(input.lightLine);
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        base[channel] =
            input.alpha01Primary[channel] * (1.0f - baseBlend) +
            input.alpha01Secondary[channel] * baseBlend;
    }
    const float alpha =
        base[3] * input.vertexColor[3] * saturate(input.onGameAlpha);
    if (alpha <= saturate(input.discardThreshold)) {
        result.discarded = true;
        return result;
    }
    const auto lighting = evaluateLighting(input);
    const float decorationBlend = saturate(input.greenBlend);
    const float onGameValue = saturate(input.onGameColorValue);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float decoration =
            input.textureMap02[channel] * (1.0f - decorationBlend) +
            input.textureMap01[channel] * decorationBlend;
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        result.color[channel] =
            lighting[channel] * (base[channel] + decoration) *
            input.vertexColor[channel] * onGame;
    }
    result.color[3] = alpha;
    return result;
}

} // namespace engine::render::lgpe_field_small_grass
