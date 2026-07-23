#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "engine/render/RenderBackendTypes.h"

namespace engine::render::vulkan_backend {

struct alignas(16) SpriteInstanceState {
    std::array<float, 4> rectPx{};
    std::array<float, 4> uvRect{};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

static_assert(std::is_standard_layout_v<SpriteInstanceState>);
static_assert(std::is_trivially_copyable_v<SpriteInstanceState>);
static_assert(sizeof(SpriteInstanceState) == 48u);
static_assert(offsetof(SpriteInstanceState, rectPx) == 0u);
static_assert(offsetof(SpriteInstanceState, uvRect) == 16u);
static_assert(offsetof(SpriteInstanceState, color) == 32u);

inline SpriteInstanceState makeSpriteInstanceState(
    const backend::DebugSprite& sprite) {
    SpriteInstanceState out;
    out.rectPx = {sprite.x, sprite.y, sprite.w, sprite.h};
    out.uvRect = {sprite.u0, sprite.v0, sprite.u1, sprite.v1};
    out.color = {sprite.r, sprite.g, sprite.b, sprite.a};
    return out;
}

} // namespace engine::render::vulkan_backend
