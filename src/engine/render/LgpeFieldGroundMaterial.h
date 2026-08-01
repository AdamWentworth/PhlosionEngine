#pragma once

#include "engine/render/LgpeFieldSharedLighting.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::render::lgpe_field_ground {

// World material modes 0-3 predate direct LGPE material interpretation.
inline constexpr std::uint8_t kMaterialMode = 4u;
inline constexpr float kBlendTextureUvScale = 0.3f;

struct SurfaceInputs {
    std::array<float, 4> groundTex01{};
    std::array<float, 4> groundTex02{};
    std::array<float, 4> grassTex02{};
    std::array<float, 4> grassTex01{};
    float grassBlendTexRed = 0.0f;
    std::array<float, 4> blendTex{};
    std::array<float, 4> vertexColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> alphaLight{};
};

// GrassBlendTex is the low-frequency soil/grass texture-variation noise.
// BlendTex is the UV2-authored route paint whose alpha selects dirt or lawn
// and whose RGB supplies the authored edge decoration.
inline std::array<float, 4> evaluateSurface(const SurfaceInputs& input) {
    const float blend =
        std::clamp(input.grassBlendTexRed, 0.0f, 1.0f);
    const float grassBlend =
        std::clamp(input.blendTex[3], 0.0f, 1.0f);
    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float ground =
            input.groundTex01[channel] * (1.0f - blend) +
            input.groundTex02[channel] * blend;
        const float grass =
            input.grassTex02[channel] * (1.0f - blend) +
            input.grassTex01[channel] * blend;
        const float surface =
            ground * (1.0f - grassBlend) + grass * grassBlend;
        output[channel] =
            input.blendTex[channel] *
                input.vertexColor[channel] * surface +
            input.alphaLight[channel] *
                (1.0f - std::clamp(input.vertexColor[3], 0.0f, 1.0f));
    }
    output[3] = 1.0f;
    return output;
}

inline std::array<float, 4> applySharedLighting(
    const std::array<float, 4>& surface,
    float projectedCloud) {
    return lgpe_field_shared::applyUniformWhiteToonCloudLighting(
        surface, projectedCloud);
}

} // namespace engine::render::lgpe_field_ground
