#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"

namespace engine::render::vulkan_backend {

struct alignas(16) WorldViewState {
    std::array<float, 4> cameraPosition{0.0f, 7.0f, 9.0f, 0.0f};
    std::array<float, 4> cameraForward{0.0f, -0.6139406f, -0.7893522f, 0.0f};
    std::array<float, 4> cameraTarget{0.0f, -1.0f, 0.0f, 0.0f};
};

static_assert(std::is_standard_layout_v<WorldViewState>);
static_assert(sizeof(WorldViewState) == 48u);
static_assert(offsetof(WorldViewState, cameraPosition) == 0u);
static_assert(offsetof(WorldViewState, cameraForward) == 16u);
static_assert(offsetof(WorldViewState, cameraTarget) == 32u);

inline WorldViewState makeWorldViewState(
    const backend::WorldTextureData* texture) {
    WorldViewState out;
    if (!texture) return out;

    out.cameraPosition = {
        texture->cameraPosX,
        texture->cameraPosY,
        texture->cameraPosZ,
        0.0f};
    out.cameraForward = {
        texture->cameraForwardX,
        texture->cameraForwardY,
        texture->cameraForwardZ,
        0.0f};
    out.cameraTarget = {
        texture->cameraTargetX,
        texture->cameraTargetY,
        texture->cameraTargetZ,
        0.0f};
    return out;
}

} // namespace engine::render::vulkan_backend
