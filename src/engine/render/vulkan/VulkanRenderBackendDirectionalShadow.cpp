#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <stdexcept>

namespace {
void shadowVk(VkResult result, const char* action) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
void resetBindings(VulkanRenderBackendImpl& b) {
    b.boundGraphicsPipeline = VK_NULL_HANDLE; b.boundTexturedDescriptorSet = VK_NULL_HANDLE;
    b.boundVertexBuffer = b.boundIndexBuffer = VK_NULL_HANDLE;
    b.boundViewportValid = false;
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
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
        }
    }
}

void VulkanRenderBackendImpl::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
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
    if (worldShadowActive) return false;
    worldShadowReady = false;
    if (!frameActive || !worldSceneColorPassActive || !engine::render::validWorldDirectionalShadowView(view) ||
        view.resolution != worldShadowResolution || !worldShadowPipeline) return false;
    worldShadowView = view; shadowSavedWidth = boundViewportWidth; shadowSavedHeight = boundViewportHeight;
    auto cmd = frames[currentFrame].commandBuffer;
    vkCmdEndRenderPass(cmd);
    VkClearValue clear{}; clear.depthStencil = {1, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldShadowRenderPass; pass.framebuffer = worldShadowFramebuffers[currentFrame];
    pass.renderArea.extent = {worldShadowResolution, worldShadowResolution};
    pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    worldShadowActive = true; resetBindings(*this);
    setViewportAndScissor(cmd, worldShadowResolution, worldShadowResolution);
    return true;
}

bool VulkanRenderBackendImpl::endWorldDirectionalShadow() {
    if (!worldShadowActive) return false;
    auto cmd = frames[currentFrame].commandBuffer; vkCmdEndRenderPass(cmd);
    worldShadowActive = false; worldShadowReady = true;
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
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    resetBindings(*this); setViewportAndScissor(cmd, shadowSavedWidth, shadowSavedHeight);
    return true;
}

void VulkanRenderBackendImpl::clearWorldDirectionalShadow() {
    if (worldShadowActive) endWorldDirectionalShadow();
    worldShadowReady = false; worldShadowView = {};
}

void VulkanRenderBackendImpl::destroyWorldDirectionalShadow() {
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

void VulkanRenderBackend::prewarmWorldDirectionalShadow(std::uint32_t resolution) { if (impl_) impl_->prewarmWorldDirectionalShadow(resolution); }
bool VulkanRenderBackend::beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) { return impl_ && impl_->beginWorldDirectionalShadow(view); }
bool VulkanRenderBackend::endWorldDirectionalShadow() { return impl_ && impl_->endWorldDirectionalShadow(); }
void VulkanRenderBackend::clearWorldDirectionalShadow() { if (impl_) impl_->clearWorldDirectionalShadow(); }
