#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_cliff {

// World material modes 0-4 predate direct FieldCliffShader01 interpretation.
inline constexpr std::uint8_t kMaterialMode = 5u;
inline constexpr float kBlendTextureUvScale = 0.3f;

struct SurfaceInputs {
    std::array<float, 4> cliffTex01{};
    std::array<float, 4> groundTex02{};
    std::array<float, 4> groundTex01{};
    float blendTexRed = 0.0f;
    std::array<float, 4> borderTex{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> rimColor{};
    float rimLightMin = 0.0f;
    float rimLightMax = 1.0f;
    float rimLightStrength = 0.0f;
    float normalDotView = 1.0f;
};

inline std::array<float, 4> evaluateSurface(const SurfaceInputs& input) {
    const float blend = std::clamp(input.blendTexRed, 0.0f, 1.0f);
    const float border = std::clamp(input.borderTex[3], 0.0f, 1.0f);
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

    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float cliff =
            input.cliffTex01[channel] +
            input.rimColor[channel] * rim * input.cliffTex01[3];
        const float grass =
            input.groundTex02[channel] * (1.0f - blend) +
            input.groundTex01[channel] * blend;
        const float surface = cliff * (1.0f - border) + grass * border;
        output[channel] =
            input.borderTex[channel] * input.vertexColor[channel] * surface;
    }
    output[3] = 1.0f;
    return output;
}

} // namespace engine::render::lgpe_field_cliff
