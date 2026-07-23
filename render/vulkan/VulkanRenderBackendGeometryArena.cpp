#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include "engine/render/vulkan/VulkanGeometryArenaLayout.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

constexpr VkDeviceSize kDefaultWorldGeometryPageBytes =
    64ull * 1024ull * 1024ull;

void applyAllocation(
    VulkanRenderBackendImpl::CachedWorldMesh& mesh,
    VkBuffer buffer,
    const engine::render::vulkan_backend::GeometryArenaAllocation& allocation) {
    mesh.geometryBuffer = buffer;
    mesh.vertexByteOffset = allocation.vertexByteOffset;
    mesh.indexByteOffset = allocation.indexByteOffset;
    mesh.firstIndex = allocation.firstIndex;
    mesh.baseVertex = allocation.baseVertex;
}

} // namespace

bool VulkanRenderBackendImpl::allocateWorldGeometry(
    VkDeviceSize vertexBytes,
    VkDeviceSize indexBytes,
    CachedWorldMesh& outMesh) {
    namespace vulkan = engine::render::vulkan_backend;
    const auto tryPage = [&](WorldGeometryPage& page) {
        vulkan::GeometryArenaAllocation allocation;
        if (!vulkan::planGeometryArenaAllocation(
                page.buffer.size,
                page.usedBytes,
                sizeof(IRenderBackend::WorldMeshVertex),
                vertexBytes,
                indexBytes,
                allocation)) {
            return false;
        }
        applyAllocation(outMesh, page.buffer.buffer, allocation);
        page.usedBytes = allocation.endByteOffset;
        return true;
    };

    for (WorldGeometryPage& page : worldGeometryPages) {
        if (tryPage(page)) return true;
    }

    vulkan::GeometryArenaAllocation requiredAllocation;
    const VkDeviceSize maximumPageSize =
        (std::numeric_limits<VkDeviceSize>::max)();
    if (!vulkan::planGeometryArenaAllocation(
            maximumPageSize,
            0u,
            sizeof(IRenderBackend::WorldMeshVertex),
            vertexBytes,
            indexBytes,
            requiredAllocation)) {
        return false;
    }

    WorldGeometryPage page;
    page.buffer = createBuffer(
        std::max(kDefaultWorldGeometryPageBytes, requiredAllocation.endByteOffset),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    try {
        worldGeometryPages.push_back(std::move(page));
    } catch (...) {
        destroyBuffer(page.buffer);
        throw;
    }
    return tryPage(worldGeometryPages.back());
}

void VulkanRenderBackendImpl::destroyWorldGeometryArena() {
    for (WorldGeometryPage& page : worldGeometryPages) {
        destroyBuffer(page.buffer);
    }
    worldGeometryPages.clear();
}
