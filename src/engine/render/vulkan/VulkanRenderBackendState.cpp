#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldPlanarCapture.h"
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
    bool built=false;
    const auto result=frameMaterialConstants.resolve(std::as_bytes(std::span(&state,1)),[&]() -> std::optional<std::uint64_t> {
        if(!writeTransient(&state,sizeof(state),alignment,outBuffer,outOffset))return {};
        built=true;return outOffset;
    });
    if(!result)return false;
    outBuffer=frames[currentFrame].transient.buffer;outOffset=*result;
    if(built)frameWorldStateUploadBytes+=sizeof(state);else frameWorldStateReuseBytes+=sizeof(state);
    return true;
}

void VulkanRenderBackendImpl::bindGraphicsPipeline(VkCommandBuffer commandBuffer, VkPipeline pipeline) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindGraphicsPipeline(pipeline);
}

void VulkanRenderBackendImpl::bindVertexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindVertexBuffer(buffer, offset);
}

void VulkanRenderBackendImpl::bindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindIndexBuffer(buffer, offset, indexType);
}

void VulkanRenderBackendImpl::bindTextureDescriptorSet(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindTextureDescriptorSet(descriptorSet);
}

engine::render::vulkan_backend::WorldDescriptorBindings VulkanRenderBackendImpl::prepareWorldStateBindings(
    VkDescriptorSet materialDescriptorSet, const std::array<std::uint32_t,4>& dynamicOffsets, bool haze) {
    const auto planarSet=haze ? worldHazeDescriptor : worldPlanarDescriptor ? worldPlanarDescriptor : worldPlanarFallback;
    const auto contactSet=worldContactDescriptor ? worldContactDescriptor : worldPlanarFallback;
    const auto localSet=worldLocalReflectionDescriptor ? worldLocalReflectionDescriptor : worldPlanarFallback;
    const auto screenSet=worldScreenReflectionDescriptor ? worldScreenReflectionDescriptor : worldPlanarFallback;
    const VkDescriptorSet worldStateDescriptorSet =
        worldShadowActive ? frames[currentFrame].shadowWorldStateDescriptorSet : frames[currentFrame].worldStateDescriptorSet;
    return {{materialDescriptorSet,worldStateDescriptorSet,planarSet,contactSet,screenSet,localSet,
        worldShadowActive?worldContactFilterFallback:worldContactFilterSet?worldContactFilterSet:worldContactFilterFallback,worldWindDescriptorSet()},dynamicOffsets};
}
void VulkanRenderBackendImpl::bindWorldStateDescriptorSets(VkCommandBuffer commandBuffer,
    VkDescriptorSet materialDescriptorSet, const std::array<std::uint32_t,4>& dynamicOffsets, bool haze) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindWorldDescriptors(prepareWorldStateBindings(materialDescriptorSet,dynamicOffsets,haze));
}

engine::render::vulkan_backend::WorldViewport VulkanRenderBackendImpl::prepareWorldViewport(int surfaceWidth, int surfaceHeight) const {
    const std::uint32_t width = worldShadowActive ? worldShadowView.resolution : std::min(
        swapchainExtent.width, static_cast<std::uint32_t>(std::max(1, surfaceWidth)));
    const std::uint32_t height = worldShadowActive ? worldShadowView.resolution : std::min(
        swapchainExtent.height, static_cast<std::uint32_t>(std::max(1, surfaceHeight)));
    const auto tile = worldShadowActive ? worldShadowOffset() : std::array<std::uint32_t,2>{};
    VkViewport viewport{};
    viewport.x = float(tile[0]); viewport.y = float(tile[1]);
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{int32_t(tile[0]), int32_t(tile[1])}, {width, height}};
    if(!worldShadowActive && worldPlanarReflection[4][3]<0) {
        const auto bounds=worldHazeActive
            ? engine::render::worldPlanarHazeScissor(worldPlanarScissor,worldPlanarReflectionView,width,height)
            : worldPlanarScissor;
        scissor.offset={bounds[0],bounds[1]};
        scissor.extent={std::uint32_t(bounds[2]-bounds[0]),std::uint32_t(bounds[3]-bounds[1])};
    }
    return {viewport,scissor};
}
void VulkanRenderBackendImpl::setViewportAndScissor(VkCommandBuffer commandBuffer, int width, int height) {
    flushWorldDrawPackets();
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.setViewport(prepareWorldViewport(width,height));
}

void VulkanRenderBackendImpl::resetWorldFrameStateCache() {
    preparedWorldViews.invalidate();
    frameWorldViewStates.clear();
    frameMaterialConstants.beginFrame();
    worldCommandState.boundGraphicsPipeline = VK_NULL_HANDLE;
    worldCommandState.boundTexturedDescriptorSet = VK_NULL_HANDLE;
    worldCommandState.boundVertexBuffer = VK_NULL_HANDLE;
    worldCommandState.boundVertexOffset = 0u;
    worldCommandState.boundIndexBuffer = VK_NULL_HANDLE;
    worldCommandState.boundIndexOffset = 0u;
    worldCommandState.boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    worldCommandState.boundViewportWidth = 0u;
    worldCommandState.boundViewportHeight = 0u;
    worldCommandState.boundViewportValid = false;
    frameWorldStateUploadBytes = 0u;
    frameWorldStateReuseBytes = 0u;
    worldCommandCounters.framePipelineBindCalls = 0u;
    worldCommandCounters.framePipelineBindSkips = 0u;
    worldCommandCounters.frameDescriptorBindCalls = 0u;
    worldCommandCounters.frameDescriptorBindSkips = 0u;
    worldCommandCounters.frameVertexBufferBindCalls = 0u;
    worldCommandCounters.frameVertexBufferBindSkips = 0u;
    worldCommandCounters.frameIndexBufferBindCalls = 0u;
    worldCommandCounters.frameIndexBufferBindSkips = 0u;
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
    worldCommandCounters.frameViewportUpdates = 0u;
    worldCommandCounters.frameViewportSkips = 0u;
}

void VulkanRenderBackendImpl::maybeLogWorldFrameCache() const {
    static const bool enabled = []() {
        const auto value = engine::env::get("PHLOSION_VULKAN_STATE_CACHE_LOG");
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
              << " pipeline_binds=" << worldCommandCounters.framePipelineBindCalls
              << " pipeline_skips=" << worldCommandCounters.framePipelineBindSkips
              << " descriptor_binds=" << worldCommandCounters.frameDescriptorBindCalls
              << " descriptor_set0_reuses=" << worldCommandCounters.frameDescriptorBindSkips
              << " vertex_buffer_binds=" << worldCommandCounters.frameVertexBufferBindCalls
              << " vertex_buffer_reuses=" << worldCommandCounters.frameVertexBufferBindSkips
              << " index_buffer_binds=" << worldCommandCounters.frameIndexBufferBindCalls
              << " index_buffer_reuses=" << worldCommandCounters.frameIndexBufferBindSkips
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
              << " viewport_updates=" << worldCommandCounters.frameViewportUpdates
              << " viewport_skips=" << worldCommandCounters.frameViewportSkips
              << '\n';
}

engine::render::backend::WorldPreparedViewToken VulkanRenderBackend::prepareWorldViewData(const WorldTextureData& texture) {
    return impl_->prepareWorldViewData(&texture);
}
void VulkanRenderBackend::beginWorldIndexedBatchSubmission() { impl_->preparedWorldViews.beginBatch(); }
void VulkanRenderBackend::endWorldIndexedBatchSubmission() {
    impl_->preparedWorldViews.endBatch();
    if(!impl_->preparedWorldViews.inBatch())impl_->flushWorldDrawPackets();
}

engine::render::backend::WorldPreparedViewStats VulkanRenderBackend::worldPreparedViewStats() const {
    return impl_->preparedWorldViews.stats;
}
