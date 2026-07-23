#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t kMaxWorldVertices = 540000u;
constexpr std::size_t kMaxWorldIndices = 900000u;

void flushStagingBuffer(VkDevice device, const VulkanRenderBackendImpl::Buffer& buffer) {
    if (buffer.coherent) return;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = buffer.memory;
    range.size = VK_WHOLE_SIZE;
    const VkResult result = vkFlushMappedMemoryRanges(device, 1u, &range);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "vkFlushMappedMemoryRanges(geometry) failed with VkResult " +
            std::to_string(result) + ".");
    }
}

} // namespace

VulkanRenderBackendImpl::CachedWorldMesh* VulkanRenderBackendImpl::ensureCachedWorldMesh(
    const char* geometryKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
    if (!geometryKey || geometryKey[0] == '\0' || !vertices || !indices ||
        vertexCount == 0u || indexCount < 3u || device == VK_NULL_HANDLE) {
        return nullptr;
    }

    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount != vertexCount || safeIndexCount != indexCount) return nullptr;
    for (std::size_t i = 0u; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return nullptr;
    }

    const std::string key(geometryKey);
    auto existing = cachedWorldMeshes.find(key);
    if (existing != cachedWorldMeshes.end()) {
        CachedWorldMesh& mesh = existing->second;
        if (mesh.geometryBuffer != VK_NULL_HANDLE &&
            mesh.vertexCount == safeVertexCount &&
            mesh.indexCount == safeIndexCount) {
            return &mesh;
        }
        // Geometry keys are immutable contracts. Keep any in-flight resource alive
        // and let a mismatched caller use the transient fallback.
        return nullptr;
    }

    const VkDeviceSize vertexBytes =
        static_cast<VkDeviceSize>(safeVertexCount) * sizeof(IRenderBackend::WorldMeshVertex);
    const VkDeviceSize indexBytes =
        static_cast<VkDeviceSize>(safeIndexCount) * sizeof(std::uint32_t);
    Buffer vertexStaging;
    Buffer indexStaging;
    CachedWorldMesh mesh;
    try {
        vertexStaging = createBuffer(
            vertexBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        indexStaging = createBuffer(
            indexBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(vertexStaging.mapped, vertices, static_cast<std::size_t>(vertexBytes));
        std::memcpy(indexStaging.mapped, indices, static_cast<std::size_t>(indexBytes));
        flushStagingBuffer(device, vertexStaging);
        flushStagingBuffer(device, indexStaging);

        if (!allocateWorldGeometry(vertexBytes, indexBytes, mesh)) {
            destroyBuffer(vertexStaging);
            destroyBuffer(indexStaging);
            return nullptr;
        }

        VkCommandBuffer commandBuffer = beginOneTimeCommands();
        VkBufferCopy vertexCopy{};
        vertexCopy.dstOffset = mesh.vertexByteOffset;
        vertexCopy.size = vertexBytes;
        vkCmdCopyBuffer(
            commandBuffer,
            vertexStaging.buffer,
            mesh.geometryBuffer,
            1u,
            &vertexCopy);
        VkBufferCopy indexCopy{};
        indexCopy.dstOffset = mesh.indexByteOffset;
        indexCopy.size = indexBytes;
        vkCmdCopyBuffer(
            commandBuffer,
            indexStaging.buffer,
            mesh.geometryBuffer,
            1u,
            &indexCopy);

        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mesh.geometryBuffer;
        barrier.offset = mesh.vertexByteOffset;
        barrier.size =
            mesh.indexByteOffset + indexBytes - mesh.vertexByteOffset;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0u,
            0u,
            nullptr,
            1u,
            &barrier,
            0u,
            nullptr);
        endOneTimeCommands(commandBuffer);

        mesh.vertexCount = safeVertexCount;
        mesh.indexCount = safeIndexCount;
    } catch (...) {
        destroyBuffer(vertexStaging);
        destroyBuffer(indexStaging);
        throw;
    }
    destroyBuffer(vertexStaging);
    destroyBuffer(indexStaging);
    return &cachedWorldMeshes.emplace(key, std::move(mesh)).first->second;
}

void VulkanRenderBackendImpl::prewarmWorldIndexedMesh(
    const char* geometryKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
    (void)ensureCachedWorldMesh(
        geometryKey, vertices, vertexCount, indices, indexCount);
}

void VulkanRenderBackendImpl::drawWorldIndexedMeshCached(
    const char* geometryKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const IRenderBackend::WorldTextureData* texture,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    CachedWorldMesh* mesh = ensureCachedWorldMesh(
        geometryKey, vertices, vertexCount, indices, indexCount);
    if (!mesh) {
        drawWorldIndexedMesh(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    drawWorldIndexedMeshBuffers(
        mesh->geometryBuffer,
        0u,
        mesh->vertexCount,
        mesh->geometryBuffer,
        0u,
        mesh->indexCount,
        mesh->firstIndex,
        mesh->baseVertex,
        VK_NULL_HANDLE,
        texture,
        nullptr,
        0u,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void VulkanRenderBackendImpl::drawWorldIndexedMeshCachedInstanced(
    const char* geometryKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const IRenderBackend::WorldTextureData* texture,
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (!instances || instanceCount == 0u) {
        drawWorldIndexedMeshCached(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    CachedWorldMesh* mesh = ensureCachedWorldMesh(
        geometryKey, vertices, vertexCount, indices, indexCount);
    if (!mesh) {
        drawWorldIndexedMeshInstanced(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    drawWorldIndexedMeshBuffers(
        mesh->geometryBuffer,
        0u,
        mesh->vertexCount,
        mesh->geometryBuffer,
        0u,
        mesh->indexCount,
        mesh->firstIndex,
        mesh->baseVertex,
        VK_NULL_HANDLE,
        texture,
        instances,
        instanceCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void VulkanRenderBackendImpl::drawWorldIndexedMeshCachedPreparedInstanced(
    const char* geometryKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    VkDescriptorSet materialDescriptorSet,
    const IRenderBackend::WorldTextureData* texture,
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    CachedWorldMesh* mesh = ensureCachedWorldMesh(
        geometryKey, vertices, vertexCount, indices, indexCount);
    if (!mesh) {
        drawWorldIndexedMeshInstanced(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    drawWorldIndexedMeshBuffers(
        mesh->geometryBuffer,
        0u,
        mesh->vertexCount,
        mesh->geometryBuffer,
        0u,
        mesh->indexCount,
        mesh->firstIndex,
        mesh->baseVertex,
        materialDescriptorSet,
        texture,
        instances,
        instanceCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void VulkanRenderBackendImpl::destroyCachedWorldMeshes() {
    cachedWorldMeshes.clear();
    destroyWorldGeometryArena();
}
