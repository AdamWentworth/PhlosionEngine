#pragma once

#include <cstdint>

namespace engine::render::vulkan_backend {

struct GeometryArenaAllocation {
    std::uint64_t vertexByteOffset = 0u;
    std::uint64_t indexByteOffset = 0u;
    std::uint64_t endByteOffset = 0u;
    std::uint32_t firstIndex = 0u;
    std::int32_t baseVertex = 0;
};

bool planGeometryArenaAllocation(
    std::uint64_t pageSize,
    std::uint64_t usedBytes,
    std::uint64_t vertexStride,
    std::uint64_t vertexBytes,
    std::uint64_t indexBytes,
    GeometryArenaAllocation& outAllocation);

} // namespace engine::render::vulkan_backend
