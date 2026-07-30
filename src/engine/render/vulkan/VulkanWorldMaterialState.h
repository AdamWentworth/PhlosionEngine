#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"

namespace engine::render::vulkan_backend {

inline constexpr float kWorldCharacterOutlineExtrude = 0.001f;

struct WorldPushConstants {
    std::array<float, 16> viewProjection{};

    float alphaMode = 0.0f;
    float alphaCutoff = 0.5f;
    float clipSpaceDepthBias = 0.0f;
    float materialMode = 0.0f;

    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float outlineExtrude = 0.0f;
    float reserved0 = 0.0f;

    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;

    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
    float reserved1 = 0.0f;
};

static_assert(sizeof(WorldPushConstants) == 128u,
              "Vulkan world push constants must fit the Vulkan 1.1 minimum guarantee.");
static_assert(std::is_standard_layout_v<WorldPushConstants>);
static_assert(offsetof(WorldPushConstants, alphaMode) == 64u);
static_assert(offsetof(WorldPushConstants, alphaWindowMin) == 80u);
static_assert(offsetof(WorldPushConstants, normalScale) == 96u);
static_assert(offsetof(WorldPushConstants, emissiveFactorR) == 112u);

inline WorldPushConstants makeWorldPushConstants(
    const backend::WorldTextureData* texture) {
    WorldPushConstants out;
    if (!texture) return out;

    out.alphaMode = static_cast<float>(std::min<std::uint8_t>(texture->alphaMode, 2u));
    out.alphaCutoff = std::clamp(texture->alphaCutoff, 0.0f, 1.0f);
    out.clipSpaceDepthBias = std::max(texture->clipSpaceDepthBias, 0.0f);
    out.materialMode = static_cast<float>(texture->materialMode);
    out.alphaWindowMin = std::clamp(texture->alphaWindowMin, 0.0f, 1.0f);
    out.alphaWindowMax = std::clamp(texture->alphaWindowMax, 0.0f, 1.0f);
    out.normalScale = std::max(texture->normalScale, 0.0f);
    out.metallicFactor = std::clamp(texture->metallicFactor, 0.0f, 1.0f);
    out.roughnessFactor = std::clamp(texture->roughnessFactor, 0.0f, 1.0f);
    out.occlusionStrength = std::clamp(texture->occlusionStrength, 0.0f, 1.0f);
    out.emissiveFactorR = std::max(texture->emissiveFactorR, 0.0f);
    out.emissiveFactorG = std::max(texture->emissiveFactorG, 0.0f);
    out.emissiveFactorB = std::max(texture->emissiveFactorB, 0.0f);
    return out;
}

} // namespace engine::render::vulkan_backend
