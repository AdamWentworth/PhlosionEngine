#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"

namespace engine::render::vulkan_backend {

struct alignas(16) WorldSpecializedMaterialState {
    std::array<float, 4> timingFlagsAtlas{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> rect0{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<float, 4> rect1{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<float, 4> flipbook0{1.0f, 1.0f, 1.0f, 0.0f};
    std::array<float, 4> flipbook1{1.0f, 1.0f, 1.0f, 0.0f};
};

static_assert(std::is_standard_layout_v<WorldSpecializedMaterialState>);
static_assert(sizeof(WorldSpecializedMaterialState) == 80u);
static_assert(offsetof(WorldSpecializedMaterialState, timingFlagsAtlas) == 0u);
static_assert(offsetof(WorldSpecializedMaterialState, rect0) == 16u);
static_assert(offsetof(WorldSpecializedMaterialState, rect1) == 32u);
static_assert(offsetof(WorldSpecializedMaterialState, flipbook0) == 48u);
static_assert(offsetof(WorldSpecializedMaterialState, flipbook1) == 64u);

inline WorldSpecializedMaterialState makeWorldSpecializedMaterialState(
    const backend::WorldTextureData* texture) {
    WorldSpecializedMaterialState out;
    if (!texture) return out;

    out.timingFlagsAtlas = {
        texture->materialTimeSec,
        texture->materialFlags,
        texture->materialAtlasWidth,
        texture->materialAtlasHeight};
    out.rect0 = {
        texture->materialRect0U,
        texture->materialRect0V,
        texture->materialRect0W,
        texture->materialRect0H};
    out.rect1 = {
        texture->materialRect1U,
        texture->materialRect1V,
        texture->materialRect1W,
        texture->materialRect1H};
    out.flipbook0 = {
        texture->materialFlipbook0Cols,
        texture->materialFlipbook0Rows,
        texture->materialFlipbook0Frames,
        texture->materialFlipbook0Fps};
    out.flipbook1 = {
        texture->materialFlipbook1Cols,
        texture->materialFlipbook1Rows,
        texture->materialFlipbook1Frames,
        texture->materialFlipbook1Fps};
    return out;
}

} // namespace engine::render::vulkan_backend
