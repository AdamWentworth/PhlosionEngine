#pragma once

#include <cstdint>

namespace engine::render::vulkan_backend {

enum class WorldMaterialBinding : std::uint32_t {
    BaseColor = 0u,
    Normal = 1u,
    MetallicRoughness = 2u,
    Occlusion = 3u,
    Emissive = 4u,
    Environment = 5u,
    Count = 6u,
};

inline constexpr std::uint32_t kWorldMaterialTextureCount =
    static_cast<std::uint32_t>(WorldMaterialBinding::Count);

} // namespace engine::render::vulkan_backend
