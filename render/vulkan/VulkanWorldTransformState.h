#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"

namespace engine::render::vulkan_backend {

inline constexpr std::uint32_t kMaxWorldSkinMatrices = 128u;

struct alignas(16) WorldTransformState {
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> vertexColorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> skinningParams{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> instanceParams{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(std::is_standard_layout_v<WorldTransformState>);
static_assert(sizeof(WorldTransformState) == 112u);
static_assert(offsetof(WorldTransformState, modelMatrix) == 0u);
static_assert(offsetof(WorldTransformState, vertexColorMultiplier) == 64u);
static_assert(offsetof(WorldTransformState, skinningParams) == 80u);
static_assert(offsetof(WorldTransformState, instanceParams) == 96u);

inline WorldTransformState makeWorldTransformState(
    const backend::WorldTextureData* texture,
    std::uint32_t skinMatrixBaseIndex,
    bool instancingEnabled = false,
    std::uint32_t instanceBaseWordIndex = 0u) {
    WorldTransformState out;
    out.instanceParams = {
        instancingEnabled ? 1.0f : 0.0f,
        static_cast<float>(instanceBaseWordIndex),
        0.0f,
        0.0f};
    if (!texture) return out;

    out.modelMatrix = texture->modelMatrix;
    out.vertexColorMultiplier = {
        texture->vertexColorMulR,
        texture->vertexColorMulG,
        texture->vertexColorMulB,
        texture->vertexColorMulA};
    const bool skinningEnabled =
        texture->gpuSkinning != 0u &&
        texture->skinMatrices != nullptr &&
        texture->skinMatrixCount > 0u;
    if (skinningEnabled) {
        out.skinningParams = {
            1.0f,
            texture->gpuSkinningMode != 0u ? 1.0f : 0.0f,
            static_cast<float>(std::min(
                texture->skinMatrixCount,
                kMaxWorldSkinMatrices)),
            static_cast<float>(skinMatrixBaseIndex)};
    }
    return out;
}

inline std::size_t worldSkinMatrixFloatCount(
    const backend::WorldTextureData* texture) {
    if (!texture ||
        texture->gpuSkinning == 0u ||
        texture->skinMatrices == nullptr ||
        texture->skinMatrixCount == 0u) {
        return 0u;
    }
    const std::size_t matrixCount = std::min<std::uint32_t>(
        texture->skinMatrixCount,
        kMaxWorldSkinMatrices);
    return matrixCount * (texture->gpuSkinningMode != 0u ? 32u : 16u);
}

} // namespace engine::render::vulkan_backend
