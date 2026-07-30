#include "engine/render/vulkan/VulkanGeometryArenaLayout.h"

#include <limits>

namespace {

bool alignUp(
    std::uint64_t value,
    std::uint64_t alignment,
    std::uint64_t& outAligned) {
    if (alignment == 0u) return false;
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0u) {
        outAligned = value;
        return true;
    }
    const std::uint64_t adjustment = alignment - remainder;
    if (value > (std::numeric_limits<std::uint64_t>::max)() - adjustment) {
        return false;
    }
    outAligned = value + adjustment;
    return true;
}

bool checkedAdd(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& outSum) {
    if (left > (std::numeric_limits<std::uint64_t>::max)() - right) {
        return false;
    }
    outSum = left + right;
    return true;
}

} // namespace

namespace engine::render::vulkan_backend {

bool planGeometryArenaAllocation(
    std::uint64_t pageSize,
    std::uint64_t usedBytes,
    std::uint64_t vertexStride,
    std::uint64_t vertexBytes,
    std::uint64_t indexBytes,
    GeometryArenaAllocation& outAllocation) {
    outAllocation = {};
    if (pageSize == 0u || usedBytes > pageSize || vertexStride == 0u ||
        vertexBytes == 0u || vertexBytes % vertexStride != 0u ||
        indexBytes == 0u || indexBytes % sizeof(std::uint32_t) != 0u) {
        return false;
    }

    std::uint64_t vertexOffset = 0u;
    std::uint64_t vertexEnd = 0u;
    std::uint64_t indexOffset = 0u;
    std::uint64_t allocationEnd = 0u;
    if (!alignUp(usedBytes, vertexStride, vertexOffset) ||
        !checkedAdd(vertexOffset, vertexBytes, vertexEnd) ||
        !alignUp(vertexEnd, sizeof(std::uint32_t), indexOffset) ||
        !checkedAdd(indexOffset, indexBytes, allocationEnd) ||
        allocationEnd > pageSize) {
        return false;
    }

    const std::uint64_t firstIndex = indexOffset / sizeof(std::uint32_t);
    const std::uint64_t baseVertex = vertexOffset / vertexStride;
    if (firstIndex > (std::numeric_limits<std::uint32_t>::max)() ||
        baseVertex > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int32_t>::max)())) {
        return false;
    }

    outAllocation.vertexByteOffset = vertexOffset;
    outAllocation.indexByteOffset = indexOffset;
    outAllocation.endByteOffset = allocationEnd;
    outAllocation.firstIndex = static_cast<std::uint32_t>(firstIndex);
    outAllocation.baseVertex = static_cast<std::int32_t>(baseVertex);
    return true;
}

} // namespace engine::render::vulkan_backend
