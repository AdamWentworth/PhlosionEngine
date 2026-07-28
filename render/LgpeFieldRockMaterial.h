#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_rock {

inline constexpr std::uint8_t kMaterialMode = 16u;
inline constexpr float kBlendTextureUvScale = 0.3f;
inline constexpr std::array<float, 3> kRoute1SunRay{
    0.5533391237f,
    0.2078260481f,
    -0.8066127300f};
inline constexpr std::uint32_t kLightTableWidth = 512u;
inline constexpr std::uint32_t kLightTableFirstNonzeroTexel = 458u;
inline constexpr std::array<std::uint8_t, 54> kLightTableRed{
    1u, 3u, 5u, 7u, 10u, 13u, 16u, 19u, 22u, 26u, 30u,
    34u, 38u, 43u, 47u, 53u, 58u, 63u, 68u, 73u, 80u, 85u,
    91u, 97u, 103u, 110u, 116u, 122u, 129u, 135u, 142u, 148u,
    155u, 161u, 168u, 174u, 181u, 188u, 193u, 200u, 207u,
    211u, 218u, 223u, 227u, 232u, 236u, 239u, 243u, 247u,
    249u, 253u, 255u, 255u};

struct SurfaceInputs {
    std::array<float, 4> rockTexture{};
    std::array<float, 3> groundTexture02{};
    std::array<float, 3> groundTexture01{};
    float blendTextureRed = 0.0f;
    std::array<float, 4> borderTexture{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> lightColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    float lightToon = 0.0f;
    float shadowToon = 1.0f;
    float projectedShadow = 1.0f;
    float projectedCloud = 1.0f;
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float normalDotView = 1.0f;
    float onGameColorValue = 1.0f;
    float onGameAlpha = 1.0f;
};

inline float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float evaluateLightTable(float toonCoordinate) {
    const float sourceTexel =
        saturate(toonCoordinate) *
            static_cast<float>(kLightTableWidth) -
        0.5f;
    const auto texelValue = [](std::int32_t texel) {
        if (texel <
            static_cast<std::int32_t>(kLightTableFirstNonzeroTexel)) {
            return 0.0f;
        }
        if (texel >= static_cast<std::int32_t>(kLightTableWidth)) {
            return 255.0f;
        }
        return static_cast<float>(
            kLightTableRed[static_cast<std::size_t>(
                texel -
                static_cast<std::int32_t>(
                    kLightTableFirstNonzeroTexel))]);
    };
    const std::int32_t lower =
        static_cast<std::int32_t>(std::floor(sourceTexel));
    const float fraction =
        sourceTexel - static_cast<float>(lower);
    return (
        texelValue(lower) * (1.0f - fraction) +
        texelValue(lower + 1) * fraction) /
        255.0f;
}

inline std::array<float, 4> evaluateSurface(const SurfaceInputs& input) {
    const float blend = saturate(input.blendTextureRed);
    const float border = saturate(input.borderTexture[3]);
    const float rimSpan =
        std::max(input.rimLightMax, input.rimLightMin) -
        input.rimLightMin;
    const float rimCoordinate = 1.0f - input.normalDotView;
    const float rim =
        rimSpan > 0.0f
        ? saturate(
              (rimCoordinate - input.rimLightMin) / rimSpan) *
              input.rimLightStrength
        : 0.0f;
    const float light = std::min(
        saturate(input.shadowToon) *
            saturate(input.projectedShadow),
        saturate(input.projectedCloud));
    const float onGameValue = saturate(input.onGameColorValue);

    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float rock =
            input.rockTexture[channel] +
            input.lightColor[channel] * saturate(input.lightToon) +
            input.rimColor[channel] * rim * input.rockTexture[3];
        const float ground =
            input.groundTexture02[channel] * (1.0f - blend) +
            input.groundTexture01[channel] * blend;
        const float surface = rock * (1.0f - border) + ground * border;
        const float lighting =
            input.shadowColor[channel] * (1.0f - light) + light;
        const float onGame =
            1.0f +
            (input.onGameColor[channel] - 1.0f) * onGameValue;
        output[channel] =
            lighting * input.borderTexture[channel] *
            input.vertexColor[channel] * surface * onGame;
    }
    output[3] = saturate(input.onGameAlpha);
    return output;
}

} // namespace engine::render::lgpe_field_rock
