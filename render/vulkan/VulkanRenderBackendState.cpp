#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <string>

bool VulkanRenderBackendImpl::writeCachedWorldViewState(
    const engine::render::vulkan_backend::WorldViewState& state,
    VkDeviceSize alignment,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset) {
    for (const CachedWorldViewState& cached : frameWorldViewStates) {
        if (std::memcmp(&cached.value, &state, sizeof(state)) != 0) continue;
        outBuffer = frames[currentFrame].transient.buffer;
        outOffset = cached.offset;
        frameWorldStateReuseBytes += sizeof(state);
        return true;
    }
    if (!writeTransient(
            &state,
            sizeof(state),
            alignment,
            outBuffer,
            outOffset)) {
        return false;
    }
    frameWorldViewStates.push_back({state, outOffset});
    frameWorldStateUploadBytes += sizeof(state);
    return true;
}

bool VulkanRenderBackendImpl::writeCachedWorldSpecializedMaterialState(
    const engine::render::vulkan_backend::WorldSpecializedMaterialState& state,
    VkDeviceSize alignment,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset) {
    for (const CachedWorldSpecializedMaterialState& cached :
         frameWorldSpecializedMaterialStates) {
        if (std::memcmp(&cached.value, &state, sizeof(state)) != 0) continue;
        outBuffer = frames[currentFrame].transient.buffer;
        outOffset = cached.offset;
        frameWorldStateReuseBytes += sizeof(state);
        return true;
    }
    if (!writeTransient(
            &state,
            sizeof(state),
            alignment,
            outBuffer,
            outOffset)) {
        return false;
    }
    frameWorldSpecializedMaterialStates.push_back({state, outOffset});
    frameWorldStateUploadBytes += sizeof(state);
    return true;
}

void VulkanRenderBackendImpl::bindGraphicsPipeline(
    VkCommandBuffer commandBuffer,
    VkPipeline pipeline) {
    if (boundGraphicsPipeline == pipeline) {
        ++framePipelineBindSkips;
        return;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    boundGraphicsPipeline = pipeline;
    ++framePipelineBindCalls;
}

void VulkanRenderBackendImpl::bindVertexBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize offset) {
    if (boundVertexBuffer == buffer && boundVertexOffset == offset) {
        ++frameVertexBufferBindSkips;
        return;
    }
    vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &buffer, &offset);
    boundVertexBuffer = buffer;
    boundVertexOffset = offset;
    ++frameVertexBufferBindCalls;
}

void VulkanRenderBackendImpl::bindIndexBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkIndexType indexType) {
    if (boundIndexBuffer == buffer && boundIndexOffset == offset &&
        boundIndexType == indexType) {
        ++frameIndexBufferBindSkips;
        return;
    }
    vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
    boundIndexBuffer = buffer;
    boundIndexOffset = offset;
    boundIndexType = indexType;
    ++frameIndexBufferBindCalls;
}

void VulkanRenderBackendImpl::bindTextureDescriptorSet(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet descriptorSet) {
    if (boundTexturedDescriptorSet == descriptorSet) {
        ++frameDescriptorBindSkips;
        return;
    }
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        texturedPipelineLayout,
        0u,
        1u,
        &descriptorSet,
        0u,
        nullptr);
    boundTexturedDescriptorSet = descriptorSet;
    ++frameDescriptorBindCalls;
}

void VulkanRenderBackendImpl::bindWorldStateDescriptorSets(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet materialDescriptorSet,
    const std::array<std::uint32_t, 4>& dynamicOffsets) {
    const VkDescriptorSet worldStateDescriptorSet =
        frames[currentFrame].worldStateDescriptorSet;
    if (boundTexturedDescriptorSet == materialDescriptorSet) {
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            texturedPipelineLayout,
            1u,
            1u,
            &worldStateDescriptorSet,
            static_cast<std::uint32_t>(dynamicOffsets.size()),
            dynamicOffsets.data());
        ++frameDescriptorBindCalls;
        ++frameDescriptorBindSkips;
        return;
    }

    const std::array<VkDescriptorSet, 2> descriptorSets{
        materialDescriptorSet,
        worldStateDescriptorSet,
    };
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        texturedPipelineLayout,
        0u,
        static_cast<std::uint32_t>(descriptorSets.size()),
        descriptorSets.data(),
        static_cast<std::uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
    boundTexturedDescriptorSet = materialDescriptorSet;
    ++frameDescriptorBindCalls;
}

void VulkanRenderBackendImpl::setViewportAndScissor(
    VkCommandBuffer commandBuffer,
    int surfaceWidth,
    int surfaceHeight) {
    const std::uint32_t width = std::min(
        swapchainExtent.width,
        static_cast<std::uint32_t>(std::max(1, surfaceWidth)));
    const std::uint32_t height = std::min(
        swapchainExtent.height,
        static_cast<std::uint32_t>(std::max(1, surfaceHeight)));
    if (boundViewportValid &&
        boundViewportWidth == width &&
        boundViewportHeight == height) {
        ++frameViewportSkips;
        return;
    }

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);
    boundViewportWidth = width;
    boundViewportHeight = height;
    boundViewportValid = true;
    ++frameViewportUpdates;
}

void VulkanRenderBackendImpl::resetWorldFrameStateCache() {
    frameWorldViewStates.clear();
    frameWorldSpecializedMaterialStates.clear();
    boundGraphicsPipeline = VK_NULL_HANDLE;
    boundTexturedDescriptorSet = VK_NULL_HANDLE;
    boundVertexBuffer = VK_NULL_HANDLE;
    boundVertexOffset = 0u;
    boundIndexBuffer = VK_NULL_HANDLE;
    boundIndexOffset = 0u;
    boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    boundViewportWidth = 0u;
    boundViewportHeight = 0u;
    boundViewportValid = false;
    frameWorldStateUploadBytes = 0u;
    frameWorldStateReuseBytes = 0u;
    framePipelineBindCalls = 0u;
    framePipelineBindSkips = 0u;
    frameDescriptorBindCalls = 0u;
    frameDescriptorBindSkips = 0u;
    frameVertexBufferBindCalls = 0u;
    frameVertexBufferBindSkips = 0u;
    frameIndexBufferBindCalls = 0u;
    frameIndexBufferBindSkips = 0u;
    framePreparedMaterialCacheHits = 0u;
    framePreparedMaterialCacheMisses = 0u;
    frameIndirectWorldApiCalls = 0u;
    frameIndirectWorldCommands = 0u;
    frameIndirectWorldFallbacks = 0u;
    frameSpriteInstances = 0u;
    frameSpriteDrawRuns = 0u;
    frameSpriteDrawsSaved = 0u;
    frameSpriteUploadBatches = 0u;
    frameSpriteUploadsSaved = 0u;
    frameViewportUpdates = 0u;
    frameViewportSkips = 0u;
}

void VulkanRenderBackendImpl::maybeLogWorldFrameCache() const {
    static const bool enabled = []() {
        const auto value = engine::env::get("PAC_VULKAN_STATE_CACHE_LOG");
        if (!value.has_value()) return false;
        const std::string& raw = *value;
        return raw != "0" && raw != "false" && raw != "FALSE" &&
               raw != "off" && raw != "OFF";
    }();
    if (!enabled || frameCounter % 120u != 0u) return;
    VkDeviceSize geometryUsedBytes = 0u;
    VkDeviceSize geometryReservedBytes = 0u;
    for (const WorldGeometryPage& page : worldGeometryPages) {
        geometryUsedBytes += page.usedBytes;
        geometryReservedBytes += page.buffer.size;
    }
    std::cout << "[Vulkan][WorldStateCache] frame=" << frameCounter
              << " palette_uploads=" << frameSkinPaletteUploads
              << " palette_upload_bytes=" << frameSkinPaletteUploadBytes
              << " palette_reuses=" << frameSkinPaletteReuses
              << " palette_reuse_bytes=" << frameSkinPaletteReuseBytes
              << " state_upload_bytes=" << frameWorldStateUploadBytes
              << " state_reuse_bytes=" << frameWorldStateReuseBytes
              << " pipeline_binds=" << framePipelineBindCalls
              << " pipeline_skips=" << framePipelineBindSkips
              << " descriptor_binds=" << frameDescriptorBindCalls
              << " descriptor_set0_reuses=" << frameDescriptorBindSkips
              << " vertex_buffer_binds=" << frameVertexBufferBindCalls
              << " vertex_buffer_reuses=" << frameVertexBufferBindSkips
              << " index_buffer_binds=" << frameIndexBufferBindCalls
              << " index_buffer_reuses=" << frameIndexBufferBindSkips
              << " geometry_pages=" << worldGeometryPages.size()
              << " geometry_used_bytes=" << geometryUsedBytes
              << " geometry_reserved_bytes=" << geometryReservedBytes
              << " prepared_material_hits=" << framePreparedMaterialCacheHits
              << " prepared_material_misses=" << framePreparedMaterialCacheMisses
              << " indirect_world_api_calls=" << frameIndirectWorldApiCalls
              << " indirect_world_commands=" << frameIndirectWorldCommands
              << " indirect_world_fallbacks=" << frameIndirectWorldFallbacks
              << " sprite_instances=" << frameSpriteInstances
              << " sprite_runs=" << frameSpriteDrawRuns
              << " sprite_draws_saved=" << frameSpriteDrawsSaved
              << " sprite_upload_batches=" << frameSpriteUploadBatches
              << " sprite_uploads_saved=" << frameSpriteUploadsSaved
              << " viewport_updates=" << frameViewportUpdates
              << " viewport_skips=" << frameViewportSkips
              << '\n';
}
