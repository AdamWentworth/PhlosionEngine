#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/vulkan/VulkanWorldTransformState.h"

namespace engine::render::vulkan_backend {

inline constexpr std::uint32_t kWorldInstanceWordCount = 6u;

struct alignas(16) WorldInstanceState {
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> vertexColorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> skinningParams{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(std::is_standard_layout_v<WorldInstanceState>);
static_assert(sizeof(WorldInstanceState) == sizeof(float) * 4u * kWorldInstanceWordCount);
static_assert(offsetof(WorldInstanceState, modelMatrix) == 0u);
static_assert(offsetof(WorldInstanceState, vertexColorMultiplier) == 64u);
static_assert(offsetof(WorldInstanceState, skinningParams) == 80u);

inline bool worldInstanceHasSkinMatrices(const backend::WorldMeshInstance& instance) {
    return instance.gpuSkinning != 0u &&
           instance.skinMatrices != nullptr &&
           instance.skinMatrixCount > 0u;
}

inline std::size_t worldInstanceSkinMatrixFloatCount(
    const backend::WorldMeshInstance& instance) {
    if (!worldInstanceHasSkinMatrices(instance)) return 0u;
    const std::size_t matrixCount = std::min<std::uint32_t>(
        instance.skinMatrixCount,
        kMaxWorldSkinMatrices);
    return matrixCount * (instance.gpuSkinningMode != 0u ? 32u : 16u);
}

inline WorldInstanceState makeWorldInstanceState(
    const backend::WorldMeshInstance& instance,
    std::uint32_t skinMatrixBaseIndex) {
    WorldInstanceState out;
    out.modelMatrix = instance.modelMatrix;
    out.vertexColorMultiplier = {
        instance.vertexColorMulR,
        instance.vertexColorMulG,
        instance.vertexColorMulB,
        instance.vertexColorMulA};
    if (worldInstanceHasSkinMatrices(instance)) {
        out.skinningParams = {
            1.0f,
            instance.gpuSkinningMode != 0u ? 1.0f : 0.0f,
            static_cast<float>(std::min(
                instance.skinMatrixCount,
                kMaxWorldSkinMatrices)),
            static_cast<float>(skinMatrixBaseIndex)};
    }
    return out;
}

} // namespace engine::render::vulkan_backend
