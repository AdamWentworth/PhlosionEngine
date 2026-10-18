#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <stdexcept>
#include "engine/render/ShadowMomentShader.h"

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
            image={fallbackWorldLinearTexture.sampler,fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if(!depthPass && worldMomentTargets[i][1].view)
                image={worldShadowSampler,worldMomentTargets[i][1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            write.dstBinding=8;
            vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        }
    }
}

void VulkanRenderBackendImpl::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
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
    if (worldShadowActive) return false;
    if (worldLocalShadows_.activeLight < 0) { worldShadowReady = false; worldLocalShadows_.clear(); }
    if (!frameActive || !worldSceneColorPassActive || !engine::render::validWorldDirectionalShadowView(view) ||
        view.resolution != (worldLocalShadows_.enabled ? (worldLocalShadows_.activeLight >= 0 ? 1024u : 2048u) : worldShadowResolution) || !worldShadowPipeline) return false;
    worldShadowView = view;
    if (worldLocalShadows_.activeLight < 0) worldLocalShadows_.directionalView = view;
    shadowSavedWidth = boundViewportWidth; shadowSavedHeight = boundViewportHeight;
    auto cmd = frames[currentFrame].commandBuffer;
    vkCmdEndRenderPass(cmd);
    VkClearValue clear{}; clear.depthStencil = {1, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldShadowRenderPass; pass.framebuffer = worldShadowFramebuffers[currentFrame];
    const auto tile = worldLocalShadows_.offset();
    pass.renderArea.offset = {int32_t(tile[0]), int32_t(tile[1])};
    pass.renderArea.extent = {view.resolution, view.resolution};
    pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    worldShadowActive = true; resetBindings(*this);
    setViewportAndScissor(cmd, view.resolution, view.resolution);
    return true;
}

bool VulkanRenderBackendImpl::endWorldDirectionalShadow() {
    if (!worldShadowActive) return false;
    auto cmd = frames[currentFrame].commandBuffer; vkCmdEndRenderPass(cmd);
    if(worldLocalShadows_.activeLight>=0 && worldShadowView.distanceMoments[3]>0)
        filterWorldShadowMoments();
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
    worldShadowReady = false; worldShadowView = {}; worldLocalShadows_.clear();
}

void VulkanRenderBackendImpl::destroyWorldDirectionalShadow() {
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

void VulkanRenderBackend::prewarmWorldDirectionalShadow(std::uint32_t resolution) { if (impl_) impl_->prewarmWorldDirectionalShadow(resolution); }
bool VulkanRenderBackend::beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) { return impl_ && impl_->beginWorldDirectionalShadow(view); }
bool VulkanRenderBackend::endWorldDirectionalShadow() { return impl_ && impl_->endWorldDirectionalShadow(); }
void VulkanRenderBackend::clearWorldDirectionalShadow() { if (impl_) impl_->clearWorldDirectionalShadow(); }

void VulkanRenderBackendImpl::prewarmWorldLocalShadows() {
    if (worldShadowActive) throw std::runtime_error("Cannot prepare local shadows during a shadow pass.");
    worldLocalShadows_.enabled = true;
    prewarmWorldDirectionalShadow(engine::render::localShadowAtlasResolution);
    prewarmWorldShadowMoments();
}
void VulkanRenderBackendImpl::clearWorldLocalShadows() {
    if (worldShadowActive && worldLocalShadows_.activeLight >= 0) endWorldLocalShadow();
    else if (worldShadowActive) endWorldDirectionalShadow();
    worldLocalShadows_.clear();
    worldLocalShadows_.directionalView = worldShadowView;
}
bool VulkanRenderBackendImpl::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    if (worldShadowActive || !worldLocalShadows_.begin(index, view)) return false;
    if (beginWorldDirectionalShadow(view)) return true;
    worldLocalShadows_.activeLight = -1;
    return false;
}
bool VulkanRenderBackendImpl::endWorldLocalShadow() {
    if (worldLocalShadows_.activeLight < 0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.finish(worldShadowView);
    worldShadowView = worldLocalShadows_.directionalView;
    return true;
}

void VulkanRenderBackend::prewarmWorldLocalShadows() { if (impl_) impl_->prewarmWorldLocalShadows(); }
void VulkanRenderBackend::clearWorldLocalShadows() { if (impl_) impl_->clearWorldLocalShadows(); }
bool VulkanRenderBackend::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) { return impl_ && impl_->beginWorldLocalShadow(index, view); }
bool VulkanRenderBackend::endWorldLocalShadow() { return impl_ && impl_->endWorldLocalShadow(); }

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
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=1;set.pBindings=&binding;
    shadowVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&worldMomentSetLayout),"moment descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,48};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&worldMomentSetLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    shadowVk(vkCreatePipelineLayout(device,&layout,nullptr,&worldMomentLayout),"moment pipeline layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,kFramesInFlight*2};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=kFramesInFlight*2;pool.poolSizeCount=1;pool.pPoolSizes=&size;
    shadowVk(vkCreateDescriptorPool(device,&pool,nullptr,&worldMomentPool),"moment descriptor pool");
    for(std::uint32_t frame=0;frame<kFramesInFlight;++frame) for(std::uint32_t stage=0;stage<2;++stage) {
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
        VkDescriptorImageInfo input{worldShadowSampler,stage?worldMomentTargets[frame][0].view:worldShadowViews[frame],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
    createWorldShadowMomentPipeline();updateWorldShadowDescriptors();
}

void VulkanRenderBackendImpl::filterWorldShadowMoments() {
    const auto cmd=frames[currentFrame].commandBuffer;
    for(std::uint32_t stage=0;stage<2;++stage) {
        const auto data=engine::render::shadowMomentConstants(worldShadowView,worldLocalShadows_,stage!=0);
        const auto& target=worldMomentTargets[currentFrame][stage];
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldMomentRenderPass;pass.framebuffer=target.framebuffer;
        pass.renderArea={{int32_t(data[6]),int32_t(data[7])},{1024,1024}};
        vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{data[6],data[7],1024,1024,0,1};
        vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&pass.renderArea);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,worldMomentPipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,worldMomentLayout,0,1,&target.input,0,nullptr);
        vkCmdPushConstants(cmd,worldMomentLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,48,data.data());
        vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
    }
}

void VulkanRenderBackendImpl::destroyWorldShadowMoments() {
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
