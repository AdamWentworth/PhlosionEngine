#pragma once

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/WorldBlendPolicy.h"

#include <cstddef>
#include <cstdint>

namespace engine::render::vulkan_backend {

inline constexpr std::size_t kWorldPipelineCount = 12u;
inline constexpr std::size_t kFirstStandardBlendPipeline = 2u;
inline constexpr std::size_t kFirstDualSourceBlendPipeline = 8u;

inline std::size_t worldPipelineIndex(
    const backend::WorldTextureData* texture,
    bool dualSourceAvailable) noexcept {
    const auto blendState = world_blend::resolve(
        texture ? texture->alphaMode : 0u,
        texture ? texture->blendMode : 0u,
        !texture || texture->depthTestEnabled != 0u,
        texture && texture->dualSourceBlendEnabled != 0u,
        dualSourceAvailable);
    const std::size_t depthOffset = blendState.depthTestEnabled ? 0u : 1u;
    if (!blendState.enabled) return depthOffset;
    if (blendState.dualSourceEnabled) {
        const std::size_t modeOffset = blendState.mode == world_blend::Mode::Additive
            ? 2u
            : 0u;
        return kFirstDualSourceBlendPipeline + modeOffset + depthOffset;
    }
    return kFirstStandardBlendPipeline +
           static_cast<std::size_t>(blendState.mode) * 2u + depthOffset;
}

} // namespace engine::render::vulkan_backend
