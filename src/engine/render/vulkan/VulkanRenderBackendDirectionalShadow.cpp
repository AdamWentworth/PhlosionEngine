#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <stdexcept>
#include "engine/render/ShadowMomentShader.h"

namespace {
void shadowVk(VkResult result, const char* action) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
void resetBindings(VulkanRenderBackendImpl& b) {
    b.worldCommandState.boundGraphicsPipeline = VK_NULL_HANDLE; b.worldCommandState.boundTexturedDescriptorSet = VK_NULL_HANDLE;
    b.worldCommandState.boundVertexBuffer = b.worldCommandState.boundIndexBuffer = VK_NULL_HANDLE;
    b.worldCommandState.boundViewportValid = false;
    b.frames[b.currentFrame].indexedWorldMaterialSetBound = false;
}
}

void VulkanRenderBackendImpl::createWorldShadowRenderPass() {
    if (worldShadowRenderPass) return;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("D32 sampled directional shadows are unavailable.");
    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT; depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &reference;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1; info.pAttachments = &depth;
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = 2; info.pDependencies = deps.data();
    shadowVk(vkCreateRenderPass(device, &info, nullptr, &worldShadowRenderPass), "directional-shadow render pass");
}

void VulkanRenderBackendImpl::updateWorldShadowDescriptors() {
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorImageInfo image{fallbackWorldLinearTexture.sampler, fallbackWorldLinearTexture.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        for (bool depthPass : {true, false}) {
            if (!depthPass && worldShadowViews[i]) { image.sampler = worldShadowSampler; image.imageView = worldShadowViews[i]; }
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = depthPass ? frames[i].shadowWorldStateDescriptorSet : frames[i].worldStateDescriptorSet;
            write.dstBinding = 6; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            image={fallbackWorldLinearTexture.sampler,fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if(!depthPass && worldMomentTargets[i][1].view)
                image={worldShadowSampler,worldMomentTargets[i][1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            write.dstBinding=8;
            vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        }
    }
}

void VulkanRenderBackendImpl::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    if (resolution < 128 || resolution > 4096 || (resolution & (resolution - 1)))
        throw std::invalid_argument("Invalid shadow resolution.");
    if (worldLocalShadows_.enabled) resolution = engine::render::localShadowAtlasResolution;
    if (resolution < 128 || resolution > 4096 || (resolution & (resolution - 1)))
        throw std::invalid_argument("Invalid directional-shadow resolution.");
    if (worldShadowResolution == resolution && worldShadowPipeline && !worldShadowActive) return;
    if (!device || frameActive || !worldShadowPipeline)
        throw std::runtime_error("Directional-shadow prewarm needs an idle renderer and matched material profile.");
    shadowVk(vkDeviceWaitIdle(device), "directional-shadow idle");
    // Keep the independent render pass and pipeline while replacing size-specific resources.
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (worldShadowFramebuffers[i]) vkDestroyFramebuffer(device, worldShadowFramebuffers[i], nullptr);
        if (worldShadowViews[i]) vkDestroyImageView(device, worldShadowViews[i], nullptr);
        if (worldShadowImages[i]) vkDestroyImage(device, worldShadowImages[i], nullptr);
        if (worldShadowMemories[i]) vkFreeMemory(device, worldShadowMemories[i], nullptr);
        worldShadowFramebuffers[i] = VK_NULL_HANDLE; worldShadowViews[i] = VK_NULL_HANDLE;
        worldShadowImages[i] = VK_NULL_HANDLE; worldShadowMemories[i] = VK_NULL_HANDLE;
    }
    if (!worldShadowSampler) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0;
        shadowVk(vkCreateSampler(device, &sampler, nullptr, &worldShadowSampler), "directional-shadow sampler");
    }
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_D32_SFLOAT;
        image.extent = {resolution, resolution, 1}; image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        shadowVk(vkCreateImage(device, &image, nullptr, &worldShadowImages[i]), "directional-shadow image");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, worldShadowImages[i], &requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        shadowVk(vkAllocateMemory(device, &memory, nullptr, &worldShadowMemories[i]), "directional-shadow memory");
        shadowVk(vkBindImageMemory(device, worldShadowImages[i], worldShadowMemories[i], 0), "directional-shadow bind");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = worldShadowImages[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = VK_FORMAT_D32_SFLOAT;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        shadowVk(vkCreateImageView(device, &view, nullptr, &worldShadowViews[i]), "directional-shadow view");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = worldShadowRenderPass; fb.attachmentCount = 1; fb.pAttachments = &worldShadowViews[i];
        fb.width = fb.height = resolution; fb.layers = 1;
        shadowVk(vkCreateFramebuffer(device, &fb, nullptr, &worldShadowFramebuffers[i]), "directional-shadow framebuffer");
        const auto cmd = beginOneTimeCommands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = worldShadowImages[i]; barrier.subresourceRange = view.subresourceRange;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        const VkClearDepthStencilValue clear{1, 0};
        vkCmdClearDepthStencilImage(cmd, barrier.image, barrier.newLayout, &clear, 1, &barrier.subresourceRange);
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneTimeCommands(cmd);
    }
    worldShadowResolution = resolution; worldShadowReady = false;
    updateWorldShadowDescriptors();
}

bool VulkanRenderBackendImpl::beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (worldShadowActive) return false;
    if (view.sunTile==~0u && worldLocalShadows_.activeLight < 0) { worldShadowReady = false; worldLocalShadows_.clear(); worldShadowCaches[currentFrame].resetPass(); }
    if (!frameActive || !worldSceneColorPassActive || !engine::render::validWorldDirectionalShadowView(view) ||
        (view.sunTile!=~0u ? (!worldSunImage || view.sunTile>=9 || !(worldSunPending&(1u<<view.sunTile)) ||
            view.resolution!=(view.sunTile<4?worldSunPlan.budget.resolution:worldSunPlan.budget.distantResolution)) :
            view.resolution != (worldLocalShadows_.enabled ? (worldLocalShadows_.activeLight >= 0 ? view.resolution : 2048u) : worldShadowResolution)) || !worldShadowPipeline) return false;
    if(view.sunTile!=~0u){worldSunSavedView=worldShadowView;worldSunSavedReady=worldShadowReady;}
    worldShadowView = view;
    if (view.sunTile==~0u && worldLocalShadows_.activeLight < 0) worldLocalShadows_.directionalView = view;
    shadowSavedWidth = worldCommandState.boundViewportWidth; shadowSavedHeight = worldCommandState.boundViewportHeight;
    auto cmd = frames[currentFrame].commandBuffer;
    endWorldRecordingRenderPass(cmd);
    VkClearValue clear{}; clear.depthStencil = {1, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldShadowRenderPass; pass.framebuffer = view.sunTile!=~0u?worldSunFramebuffer:worldShadowFramebuffers[currentFrame];
    const auto tile = worldShadowOffset();
    pass.renderArea.offset = {int32_t(tile[0]), int32_t(tile[1])};
    pass.renderArea.extent = {view.resolution, view.resolution};
    pass.clearValueCount = 1; pass.pClearValues = &clear;
    beginWorldRecordingRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    worldShadowActive = true; resetBindings(*this);
    setViewportAndScissor(cmd, view.resolution, view.resolution);
    return true;
}

bool VulkanRenderBackendImpl::endWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (!worldShadowActive) return false;
    auto cmd = frames[currentFrame].commandBuffer; endWorldRecordingRenderPass(cmd);
    if(worldLocalShadows_.activeLight>=0 && worldShadowView.distanceMoments[3]>0)
        filterWorldShadowMoments();
    worldShadowActive = false;
    if(worldShadowView.sunTile!=~0u){worldSunPending&=~(1u<<worldShadowView.sunTile);worldShadowView=worldSunSavedView;worldShadowReady=worldSunSavedReady;}
    else worldShadowReady = true;
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldSceneColorLoadRenderPass;
    if (activeEditorSurface) {
        const auto& editorTarget = editorSurfaces.at(activeEditorSurface);
        pass.framebuffer = editorTarget.frames[currentFrame].linearFramebuffer;
        pass.renderArea.extent = {std::uint32_t(editorTarget.width), std::uint32_t(editorTarget.height)};
    } else {
        pass.framebuffer = worldSceneColorFramebuffers[std::size_t(currentFrame) * swapchainImages.size() + acquiredImage];
        pass.renderArea.extent = swapchainExtent;
    }
    beginWorldRecordingRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    resetBindings(*this); setViewportAndScissor(cmd, shadowSavedWidth, shadowSavedHeight);
    return true;
}

void VulkanRenderBackendImpl::clearWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    if (worldShadowActive) endWorldDirectionalShadow();
    worldShadowReady = false; worldShadowView = {}; worldLocalShadows_.clear(); worldShadowCaches[currentFrame].resetPass();
}

void VulkanRenderBackendImpl::destroyWorldDirectionalShadow() {
    destroyWorldSunShadows();
    if(worldSunFallbackView)vkDestroyImageView(device,worldSunFallbackView,nullptr);
    if(worldSunFallbackImage)vkDestroyImage(device,worldSunFallbackImage,nullptr);
    if(worldSunFallbackMemory)vkFreeMemory(device,worldSunFallbackMemory,nullptr);
    worldSunFallbackView=VK_NULL_HANDLE;worldSunFallbackImage=VK_NULL_HANDLE;worldSunFallbackMemory=VK_NULL_HANDLE;
    if(worldSunSampler){vkDestroySampler(device,worldSunSampler,nullptr);worldSunSampler=VK_NULL_HANDLE;}
    destroyWorldShadowMoments();
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (worldShadowFramebuffers[i]) vkDestroyFramebuffer(device, worldShadowFramebuffers[i], nullptr);
        if (worldShadowViews[i]) vkDestroyImageView(device, worldShadowViews[i], nullptr);
        if (worldShadowImages[i]) vkDestroyImage(device, worldShadowImages[i], nullptr);
        if (worldShadowMemories[i]) vkFreeMemory(device, worldShadowMemories[i], nullptr);
    }
    if (worldShadowSampler) vkDestroySampler(device, worldShadowSampler, nullptr);
    if (worldShadowRenderPass) vkDestroyRenderPass(device, worldShadowRenderPass, nullptr);
    worldShadowImages = {}; worldShadowMemories = {}; worldShadowViews = {}; worldShadowFramebuffers = {};
    worldShadowSampler = VK_NULL_HANDLE; worldShadowRenderPass = VK_NULL_HANDLE;
    worldShadowResolution = 0; worldShadowReady = worldShadowActive = false;
}

void VulkanRenderBackend::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 if (impl_) impl_->prewarmWorldDirectionalShadow(resolution); }
bool VulkanRenderBackend::beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 return impl_ && impl_->beginWorldDirectionalShadow(view); }
bool VulkanRenderBackend::endWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 return impl_ && impl_->endWorldDirectionalShadow(); }
void VulkanRenderBackend::clearWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 if (impl_) impl_->clearWorldDirectionalShadow(); }

void VulkanRenderBackendImpl::prewarmWorldLocalShadows() {
    if (worldShadowActive) throw std::runtime_error("Cannot prepare local shadows during a shadow pass.");
    worldLocalShadows_.enabled = true;
    prewarmWorldDirectionalShadow(engine::render::localShadowAtlasResolution);
    prewarmWorldShadowMoments();
}
void VulkanRenderBackendImpl::clearWorldLocalShadows() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    if (worldShadowActive && worldLocalShadows_.activeLight >= 0) endWorldLocalShadow();
    else if (worldShadowActive) endWorldDirectionalShadow();
    worldLocalShadows_.clear();
    worldShadowCaches[currentFrame].resetPass();
    worldLocalShadows_.directionalView = worldShadowView;
}
bool VulkanRenderBackendImpl::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (worldShadowActive || !worldLocalShadows_.begin(index, view)) return false;
    worldShadowUpdates[currentFrame].erase(worldLocalShadows_.slot(index));
    worldShadowCaches[currentFrame].activate(worldLocalShadows_.slot(index),worldLocalShadows_.owner(index),view);
    if (beginWorldDirectionalShadow(view)) return true;
    worldLocalShadows_.activeLight = -1;
    return false;
}
bool VulkanRenderBackendImpl::endWorldLocalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (worldLocalShadows_.activeLight < 0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.finish(worldShadowView);
    worldShadowView = worldLocalShadows_.directionalView;
    worldShadowCaches[currentFrame].resetPass();
    return true;
}

engine::render::WorldShadowCacheResult VulkanRenderBackendImpl::beginWorldStaticShadow(std::uint32_t index,
        const engine::render::WorldDirectionalShadowView& view, engine::render::WorldShadowCacheKey key) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    using engine::render::WorldShadowCacheResult;
    auto& cache=worldShadowCaches[currentFrame];
    if(!key.scene || !key.revision || view.distanceMoments[3]<=0) { cache.resetPass(); return WorldShadowCacheResult::Unavailable; }
    if(worldShadowActive || !frameActive || !worldSceneColorPassActive || !worldMomentPipeline ||
       !engine::render::validWorldDirectionalShadowView(view) || (view.resolution!=512 && view.resolution!=1024) ||
       index>=engine::render::worldLocalShadowSourceCapacity || worldLocalShadows_.tileCount>=engine::render::localShadowCapacity)
        throw std::runtime_error("Invalid static shadow pass");
    if(!cache.prepare(worldLocalShadows_.slot(index),worldLocalShadows_.owner(index),view,key)) return WorldShadowCacheResult::Reuse;
    if(!worldLocalShadows_.begin(index,view) || !beginWorldDirectionalShadow(view)) {
        worldLocalShadows_.activeLight=-1; cache.invalidate(); throw std::runtime_error("Static shadow pass admission failed");
    }
    return WorldShadowCacheResult::Draw;
}
bool VulkanRenderBackendImpl::endWorldStaticShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    auto& cache=worldShadowCaches[currentFrame];
    if(!cache.writing || worldLocalShadows_.activeLight<0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.activeLight=-1; worldShadowView=worldLocalShadows_.directionalView;
    cache.commit(); return true;
}
engine::render::WorldShadowCacheResult VulkanRenderBackend::beginWorldStaticShadow(std::uint32_t index,
        const engine::render::WorldDirectionalShadowView& view, engine::render::WorldShadowCacheKey key) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
    return impl_ ? impl_->beginWorldStaticShadow(index,view,key) : engine::render::WorldShadowCacheResult::Unavailable;
}
bool VulkanRenderBackend::endWorldStaticShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 return impl_ && impl_->endWorldStaticShadow(); }
engine::render::WorldShadowCacheResult VulkanRenderBackend::queryWorldStaticShadow(
    std::span<const std::uint32_t> sources,std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowCacheKey key) const {
    using engine::render::WorldShadowCacheResult;
    if(!impl_ || !key.scene || !key.revision || view.distanceMoments[3]<=0)return WorldShadowCacheResult::Unavailable;
    if(!impl_->frameActive || !impl_->worldMomentPipeline || impl_->worldShadowActive)
        throw std::runtime_error("Static shadow planning requires an idle pass in an active frame");
    auto allocation=impl_->worldLocalShadows_;allocation.clear();allocation.plan(sources);
    return impl_->worldShadowCaches[impl_->currentFrame].matches(allocation.slot(light),allocation.owner(light),view,key)?
        WorldShadowCacheResult::Reuse:WorldShadowCacheResult::Draw;
}
void VulkanRenderBackend::setWorldLocalShadowIdentities(std::span<const std::uint64_t> ids) { if(impl_)impl_->worldLocalShadows_.identities(ids); }
void VulkanRenderBackend::planWorldLocalShadowSources(std::span<const std::uint32_t> sources) {
    if(impl_)impl_->worldLocalShadows_.plan(sources);
}
void VulkanRenderBackend::invalidateWorldStaticShadows() { if(impl_) {for(auto& cache:impl_->worldShadowCaches) cache.invalidate();for(auto& c:impl_->worldShadowUpdates)c.clear();} }

void VulkanRenderBackend::prewarmWorldLocalShadows() { if (impl_) impl_->prewarmWorldLocalShadows(); }
void VulkanRenderBackend::clearWorldLocalShadows() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 if (impl_) impl_->clearWorldLocalShadows(); }
bool VulkanRenderBackend::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 return impl_ && impl_->beginWorldLocalShadow(index, view); }
bool VulkanRenderBackend::endWorldLocalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) impl_->preparedWorldViews.invalidate(); });
 return impl_ && impl_->endWorldLocalShadow(); }

void VulkanRenderBackendImpl::prewarmWorldShadowMoments() {
    if(worldMomentPipeline) return;
    if(frameActive) throw std::runtime_error("Cannot prepare moment shadows during a frame");
    shadowVk(vkDeviceWaitIdle(device),"moment prewarm idle");
    VkFormatProperties format{};vkGetPhysicalDeviceFormatProperties(physicalDevice,VK_FORMAT_R16G16_UNORM,&format);
    const auto features=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((format.optimalTilingFeatures&features)!=features) throw std::runtime_error("RG16_UNORM moment targets unavailable");
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R16G16_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    std::array<VkSubpassDependency,2> dependencies{};
    dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};
    dependencies[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,0};
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=2;pass.pDependencies=dependencies.data();
    shadowVk(vkCreateRenderPass(device,&pass,nullptr,&worldMomentRenderPass),"moment render pass");
    const std::array<VkDescriptorSetLayoutBinding,2> bindings{{
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=2;set.pBindings=bindings.data();
    shadowVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&worldMomentSetLayout),"moment descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,48};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&worldMomentSetLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    shadowVk(vkCreatePipelineLayout(device,&layout,nullptr,&worldMomentLayout),"moment pipeline layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,kFramesInFlight*6};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=kFramesInFlight*3;pool.poolSizeCount=1;pool.pPoolSizes=&size;
    shadowVk(vkCreateDescriptorPool(device,&pool,nullptr,&worldMomentPool),"moment descriptor pool");
    for(auto& cache:worldShadowCaches) cache.invalidate();
    for(auto& cache:worldShadowUpdates)cache.clear();
    for(std::uint32_t frame=0;frame<kFramesInFlight;++frame) for(std::uint32_t stage=0;stage<3;++stage) {
        auto& target=worldMomentTargets[frame][stage];
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R16G16_UNORM;
        image.extent={stage?2048u:1024u,stage?4096u:1024u,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        shadowVk(vkCreateImage(device,&image,nullptr,&target.image),"moment image");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,target.image,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=findMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        shadowVk(vkAllocateMemory(device,&memory,nullptr,&target.memory),"moment memory");shadowVk(vkBindImageMemory(device,target.image,target.memory,0),"moment image bind");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=target.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        shadowVk(vkCreateImageView(device,&view,nullptr,&target.view),"moment image view");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=worldMomentRenderPass;fb.attachmentCount=1;fb.pAttachments=&target.view;fb.width=image.extent.width;fb.height=image.extent.height;fb.layers=1;
        shadowVk(vkCreateFramebuffer(device,&fb,nullptr,&target.framebuffer),"moment framebuffer");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=worldMomentPool;alloc.descriptorSetCount=1;alloc.pSetLayouts=&worldMomentSetLayout;
        shadowVk(vkAllocateDescriptorSets(device,&alloc,&target.input),"moment input descriptor");
        VkDescriptorImageInfo input{worldShadowSampler,stage==1?worldMomentTargets[frame][0].view:worldShadowViews[frame],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=target.input;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&input;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        const auto cmd=beginOneTimeCommands();VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=target.image;barrier.subresourceRange=view.subresourceRange;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        const VkClearColorValue clear{{1,1,1,1}};vkCmdClearColorImage(cmd,target.image,barrier.newLayout,&clear,1,&barrier.subresourceRange);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);endOneTimeCommands(cmd);
    }
    for(std::uint32_t frame=0;frame<kFramesInFlight;++frame) for(std::uint32_t stage=0;stage<3;++stage) {
        const VkDescriptorImageInfo input{worldShadowSampler,stage==0?worldMomentTargets[frame][2].view:worldShadowViews[frame],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=worldMomentTargets[frame][stage].input;
        write.dstBinding=1;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&input;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    }
    createWorldShadowMomentPipeline();updateWorldShadowDescriptors();
}

void VulkanRenderBackendImpl::filterWorldShadowMoments() {
    const auto cmd=frames[currentFrame].commandBuffer;
    const auto& cache=worldShadowCaches[currentFrame];
    for(std::uint32_t stage=cache.writing?2u:0u;stage<(cache.writing?3u:2u);++stage) {
        const auto data=engine::render::shadowMomentCacheConstants(worldShadowView,worldLocalShadows_,stage,cache.reading);
        const auto& target=worldMomentTargets[currentFrame][stage];
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldMomentRenderPass;pass.framebuffer=target.framebuffer;
        pass.renderArea={{int32_t(data[6]),int32_t(data[7])},{uint32_t(data[9]),uint32_t(data[9])}};
        beginWorldRecordingRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{data[6],data[7],data[9],data[9],0,1};
        vkCmdSetViewport(worldRecordingCommand(cmd),0,1,&viewport);vkCmdSetScissor(worldRecordingCommand(cmd),0,1,&pass.renderArea);
        vkCmdBindPipeline(worldRecordingCommand(cmd),VK_PIPELINE_BIND_POINT_GRAPHICS,worldMomentPipeline);
        vkCmdBindDescriptorSets(worldRecordingCommand(cmd),VK_PIPELINE_BIND_POINT_GRAPHICS,worldMomentLayout,0,1,&target.input,0,nullptr);
        vkCmdPushConstants(worldRecordingCommand(cmd),worldMomentLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,48,data.data());
        vkCmdDraw(worldRecordingCommand(cmd),3,1,0,0);endWorldRecordingRenderPass(cmd);
    }
}

void VulkanRenderBackendImpl::destroyWorldShadowMoments() {
    for(auto& cache:worldShadowCaches) cache.invalidate();
    for(auto& cache:worldShadowUpdates)cache.clear();
    if(worldMomentPipeline)vkDestroyPipeline(device,worldMomentPipeline,nullptr);
    if(worldMomentPool)vkDestroyDescriptorPool(device,worldMomentPool,nullptr);
    for(auto& frame:worldMomentTargets)for(auto& target:frame) {
        if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        if(target.view)vkDestroyImageView(device,target.view,nullptr);
        if(target.image)vkDestroyImage(device,target.image,nullptr);
        if(target.memory)vkFreeMemory(device,target.memory,nullptr);
        target={};
    }
    if(worldMomentLayout)vkDestroyPipelineLayout(device,worldMomentLayout,nullptr);
    if(worldMomentSetLayout)vkDestroyDescriptorSetLayout(device,worldMomentSetLayout,nullptr);
    if(worldMomentRenderPass)vkDestroyRenderPass(device,worldMomentRenderPass,nullptr);
    worldMomentPipeline=VK_NULL_HANDLE;worldMomentPool=VK_NULL_HANDLE;worldMomentLayout=VK_NULL_HANDLE;worldMomentSetLayout=VK_NULL_HANDLE;worldMomentRenderPass=VK_NULL_HANDLE;
}


engine::render::WorldShadowUpdateQuery VulkanRenderBackend::queryWorldShadowUpdate(
    std::span<const std::uint32_t> sources,std::uint32_t light,const engine::render::WorldDirectionalShadowView& view,
    engine::render::WorldShadowUpdateKey key) const {
    if(!impl_)throw std::runtime_error("No renderer");
    auto allocation=impl_->worldLocalShadows_;allocation.clear();allocation.plan(sources);
    return impl_->worldShadowUpdates[impl_->currentFrame].query(allocation.slot(light),view,key);
}
bool VulkanRenderBackend::reuseWorldShadowUpdate(std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowUpdateKey key) {
    if(!impl_)throw std::runtime_error("No renderer");
    if(impl_->worldShadowActive || !impl_->worldShadowUpdates[impl_->currentFrame].query(impl_->worldLocalShadows_.slot(light),view,key).valid ||
        !impl_->worldLocalShadows_.begin(light,view))return false;
    impl_->worldLocalShadows_.finish(view);return true;
}
void VulkanRenderBackend::commitWorldShadowUpdate(std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowUpdateKey key) {
    if(!impl_)throw std::runtime_error("No renderer");
    if(impl_->worldShadowActive)throw std::logic_error("Shadow update committed before completion");
    impl_->worldShadowUpdates[impl_->currentFrame].commit(impl_->worldLocalShadows_.slot(light),view,key);

}

unsigned VulkanRenderBackend::worldShadowScheduleBank() const {
    static_assert(VulkanRenderBackendImpl::kFramesInFlight<=engine::render::IWorldShadowScheduling::maxBanks);
    if(!impl_ || !impl_->frameActive)throw std::logic_error("Shadow scheduling requires an active frame");
    return impl_->currentFrame;
}

void VulkanRenderBackendImpl::prewarmWorldSunShadows(unsigned quality) {
    const auto budget=engine::render::worldSunShadowBudget(quality);
    if(worldSunImage&&worldSunQuality==quality)return;
    if(frameActive||!device||!worldShadowPipeline)throw std::runtime_error("Sun shadows need idle prepared renderer");
    shadowVk(vkDeviceWaitIdle(device),"Sun shadow idle");
    destroyWorldSunShadows();
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_D32_SFLOAT;
        image.extent = {budget.width(), budget.height(), 1}; image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        shadowVk(vkCreateImage(device, &image, nullptr, &worldSunImage), "directional-shadow image");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, worldSunImage, &requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        shadowVk(vkAllocateMemory(device, &memory, nullptr, &worldSunMemory), "directional-shadow memory");
        shadowVk(vkBindImageMemory(device, worldSunImage, worldSunMemory, 0), "directional-shadow bind");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = worldSunImage; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = VK_FORMAT_D32_SFLOAT;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        shadowVk(vkCreateImageView(device, &view, nullptr, &worldSunView), "directional-shadow view");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = worldShadowRenderPass; fb.attachmentCount = 1; fb.pAttachments = &worldSunView;
        fb.width = budget.width(); fb.height = budget.height(); fb.layers = 1;
        shadowVk(vkCreateFramebuffer(device, &fb, nullptr, &worldSunFramebuffer), "directional-shadow framebuffer");
        const auto cmd = beginOneTimeCommands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = worldSunImage; barrier.subresourceRange = view.subresourceRange;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        const VkClearDepthStencilValue clear{1, 0};
        vkCmdClearDepthStencilImage(cmd, barrier.image, barrier.newLayout, &clear, 1, &barrier.subresourceRange);
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneTimeCommands(cmd);
    worldSunQuality=quality;updateWorldShadowDescriptors();
}
void VulkanRenderBackendImpl::destroyWorldSunShadows() {
    if(worldSunFramebuffer)vkDestroyFramebuffer(device,worldSunFramebuffer,nullptr);
    if(worldSunView)vkDestroyImageView(device,worldSunView,nullptr);
    if(worldSunImage)vkDestroyImage(device,worldSunImage,nullptr);
    if(worldSunMemory)vkFreeMemory(device,worldSunMemory,nullptr);
    worldSunFramebuffer=VK_NULL_HANDLE;worldSunView=VK_NULL_HANDLE;worldSunImage=VK_NULL_HANDLE;worldSunMemory=VK_NULL_HANDLE;
    worldSunQuality=~0u;worldSunPlan={};
}
bool VulkanRenderBackendImpl::setWorldSunShadowPlan(const engine::render::WorldSunShadowPlan& plan) {
    engine::render::validateWorldSunShadowPlan(plan);
    if(!frameActive||worldShadowActive||!worldSunImage||engine::render::worldSunShadowBudget(worldSunQuality).width()!=plan.budget.width())return false;
    flushWorldDrawPackets();worldSunPlan=plan;worldSunPending=plan.drawMask;worldContactFilterSet=createContactFilterSet(worldContactFilterSources);preparedWorldViews.invalidate();return true;
}
void VulkanRenderBackendImpl::clearWorldSunShadows() {flushWorldDrawPackets();worldSunPlan={};preparedWorldViews.invalidate();}
std::array<std::uint32_t,2> VulkanRenderBackendImpl::worldShadowOffset() const {
    if(worldShadowView.sunTile==~0u)return worldLocalShadows_.offset();
    const unsigned slot=worldShadowView.sunTile;const auto& b=worldSunPlan.budget;
    if(slot<4)return {(slot%2)*b.resolution,(slot/2)*b.resolution};
    return {((slot-4)%(b.width()/b.distantResolution))*b.distantResolution,
        2*b.resolution+((slot-4)/(b.width()/b.distantResolution))*b.distantResolution};
}
void VulkanRenderBackend::prewarmWorldSunShadows(unsigned q){if(impl_)impl_->prewarmWorldSunShadows(q);}
bool VulkanRenderBackend::setWorldSunShadowPlan(const engine::render::WorldSunShadowPlan& p){return impl_&&impl_->setWorldSunShadowPlan(p);}
void VulkanRenderBackend::clearWorldSunShadows(){if(impl_)impl_->clearWorldSunShadows();}

void VulkanRenderBackendImpl::ensureWorldSunFallback() {
    if(worldSunFallbackView)return;
    if(!worldSunSampler) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.compareEnable=VK_TRUE;sampler.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        shadowVk(vkCreateSampler(device,&sampler,nullptr,&worldSunSampler),"Sun comparison sampler");
    }
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_D32_SFLOAT;
        image.extent = {1, 1, 1}; image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        shadowVk(vkCreateImage(device, &image, nullptr, &worldSunFallbackImage), "directional-shadow image");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, worldSunFallbackImage, &requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        shadowVk(vkAllocateMemory(device, &memory, nullptr, &worldSunFallbackMemory), "directional-shadow memory");
        shadowVk(vkBindImageMemory(device, worldSunFallbackImage, worldSunFallbackMemory, 0), "directional-shadow bind");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = worldSunFallbackImage; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = VK_FORMAT_D32_SFLOAT;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        shadowVk(vkCreateImageView(device, &view, nullptr, &worldSunFallbackView), "directional-shadow view");
        const auto cmd = beginOneTimeCommands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = worldSunFallbackImage; barrier.subresourceRange = view.subresourceRange;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        const VkClearDepthStencilValue clear{1, 0};
        vkCmdClearDepthStencilImage(cmd, barrier.image, barrier.newLayout, &clear, 1, &barrier.subresourceRange);
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneTimeCommands(cmd);
}

std::uint64_t VulkanRenderBackend::worldSunShadowIdentity() const {return impl_&&!impl_->worldSunPending?impl_->worldSunPlan.identity:0;}
