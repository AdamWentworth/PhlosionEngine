#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace engine::render::lgpe_field_shared {

inline constexpr std::array<float, 3> kShadowColor{
    0.235f,
    0.361f,
    0.391f};

inline std::array<float, 4> applyUniformWhiteToonCloudLighting(
    const std::array<float, 4>& surface,
    float projectedCloud) {
    const float light = std::clamp(projectedCloud, 0.0f, 1.0f);
    std::array<float, 4> output = surface;
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        const float lighting =
            kShadowColor[channel] * (1.0f - light) + light;
        output[channel] *= lighting;
    }
    return output;
}

} // namespace engine::render::lgpe_field_shared
