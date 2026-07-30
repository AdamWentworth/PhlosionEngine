#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"

namespace engine::render::vulkan_backend {

inline constexpr std::uint32_t kMaxIndexedWorldMaterials = 256u;

struct alignas(16) WorldIndirectDrawState {
    WorldSpecializedMaterialState specializedMaterial{};
    std::array<float, 4> materialParams{0.0f, 0.5f, 0.0f, 0.0f};
    std::array<float, 4> shadingParams{0.0f, 1.0f, 0.0f, 0.0f};
    std::array<float, 4> pbrFactors{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> emissiveAndCamera{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<std::uint32_t, 4> drawParams{0u, 0u, 0u, 0u};
};

struct alignas(16) WorldIndirectPushConstants {
    std::array<float, 16> viewProjection{};
    std::array<std::uint32_t, 4> batchParams{0u, 0u, 0u, 0u};
};

static_assert(std::is_standard_layout_v<WorldIndirectDrawState>);
static_assert(sizeof(WorldIndirectDrawState) == 272u);
static_assert(offsetof(WorldIndirectDrawState, specializedMaterial) == 0u);
static_assert(offsetof(WorldIndirectDrawState, materialParams) == 192u);
static_assert(offsetof(WorldIndirectDrawState, shadingParams) == 208u);
static_assert(offsetof(WorldIndirectDrawState, pbrFactors) == 224u);
static_assert(offsetof(WorldIndirectDrawState, emissiveAndCamera) == 240u);
static_assert(offsetof(WorldIndirectDrawState, drawParams) == 256u);
static_assert(std::is_standard_layout_v<WorldIndirectPushConstants>);
static_assert(sizeof(WorldIndirectPushConstants) == 80u);

inline WorldIndirectDrawState makeWorldIndirectDrawState(
    const backend::WorldTextureData* texture,
    std::uint32_t materialIndex,
    std::uint32_t instanceBaseWordIndex) {
    const WorldPushConstants material = makeWorldPushConstants(texture);
    WorldIndirectDrawState out;
    out.specializedMaterial = makeWorldSpecializedMaterialState(texture);
    out.materialParams = {
        material.alphaMode,
        material.alphaCutoff,
        material.clipSpaceDepthBias,
        material.materialMode};
    out.shadingParams = {
        material.alphaWindowMin,
        material.alphaWindowMax,
        material.outlineExtrude,
        material.reserved0};
    out.pbrFactors = {
        material.normalScale,
        material.metallicFactor,
        material.roughnessFactor,
        material.occlusionStrength};
    out.emissiveAndCamera = {
        material.emissiveFactorR,
        material.emissiveFactorG,
        material.emissiveFactorB,
        material.reserved1};
    out.drawParams = {materialIndex, instanceBaseWordIndex, 0u, 0u};
    return out;
}

inline WorldIndirectDrawState makeWorldIndirectOutlineDrawState(
    const WorldIndirectDrawState& surfaceState) {
    WorldIndirectDrawState out = surfaceState;
    out.materialParams[3] = 3.0f;
    out.shadingParams[2] = kWorldCharacterOutlineExtrude;
    return out;
}

} // namespace engine::render::vulkan_backend
