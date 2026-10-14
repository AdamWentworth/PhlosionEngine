#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace {

void requireSceneColorVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " +
            std::to_string(result) + ".");
    }
}

void invalidateSceneColorBindings(VulkanRenderBackendImpl& backend) {
    backend.boundGraphicsPipeline = VK_NULL_HANDLE;
    backend.boundTexturedDescriptorSet = VK_NULL_HANDLE;
    backend.boundVertexBuffer = VK_NULL_HANDLE;
    backend.boundVertexOffset = 0u;
    backend.boundIndexBuffer = VK_NULL_HANDLE;
    backend.boundIndexOffset = 0u;
    backend.boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    backend.boundViewportWidth = 0u;
    backend.boundViewportHeight = 0u;
    backend.boundViewportValid = false;
    backend.frames[backend.currentFrame].indexedWorldMaterialSetBound = false;
}

} // namespace

void VulkanRenderBackendImpl::createWorldSceneColorResources() {
    destroyWorldSceneColorResources();
    if (device == VK_NULL_HANDLE ||
        worldSceneColorRenderPass == VK_NULL_HANDLE ||
        swapchainExtent.width == 0u ||
        swapchainExtent.height == 0u) {
        return;
    }

    for (std::uint32_t frameIndex = 0u;
         frameIndex < kFramesInFlight;
         ++frameIndex) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = swapchainFormat;
        imageInfo.extent = {
            swapchainExtent.width,
            swapchainExtent.height,
            1u};
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        requireSceneColorVk(
            vkCreateImage(
                device,
                &imageInfo,
                nullptr,
                &worldSceneColorImages[frameIndex]),
            "vkCreateImage(world scene color)");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(
            device,
            worldSceneColorImages[frameIndex],
            &requirements);
        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        requireSceneColorVk(
            vkAllocateMemory(
                device,
                &allocation,
                nullptr,
                &worldSceneColorMemories[frameIndex]),
            "vkAllocateMemory(world scene color)");
        requireSceneColorVk(
            vkBindImageMemory(
                device,
                worldSceneColorImages[frameIndex],
                worldSceneColorMemories[frameIndex],
                0u),
            "vkBindImageMemory(world scene color)");

        VkImageViewCreateInfo viewInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = worldSceneColorImages[frameIndex];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.layerCount = 1u;
        requireSceneColorVk(
            vkCreateImageView(
                device,
                &viewInfo,
                nullptr,
                &worldSceneColorViews[frameIndex]),
            "vkCreateImageView(world scene color)");

        VkDescriptorImageInfo descriptorImageInfo{};
        descriptorImageInfo.sampler = worldSceneColorSampler;
        descriptorImageInfo.imageView =
            worldSceneColorViews[frameIndex];
        descriptorImageInfo.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet descriptorWrite{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        descriptorWrite.dstSet =
            worldSceneColorDescriptorSets[frameIndex];
        descriptorWrite.dstBinding = 0u;
        descriptorWrite.descriptorCount = 1u;
        descriptorWrite.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.pImageInfo = &descriptorImageInfo;
        vkUpdateDescriptorSets(
            device, 1u, &descriptorWrite, 0u, nullptr);
    }

    const std::size_t swapchainImageCount = swapchainImages.size();
    worldSceneColorFramebuffers.assign(
        kFramesInFlight * swapchainImageCount,
        VK_NULL_HANDLE);
    for (std::uint32_t frameIndex = 0u;
         frameIndex < kFramesInFlight;
         ++frameIndex) {
        for (std::size_t imageIndex = 0u;
             imageIndex < swapchainImageCount;
             ++imageIndex) {
            const std::array<VkImageView, 2> attachments{
                worldSceneColorViews[frameIndex],
                depthViews[imageIndex],
            };
            VkFramebufferCreateInfo framebufferInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = worldSceneColorRenderPass;
            framebufferInfo.attachmentCount =
                static_cast<std::uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapchainExtent.width;
            framebufferInfo.height = swapchainExtent.height;
            framebufferInfo.layers = 1u;
            const std::size_t framebufferIndex =
                static_cast<std::size_t>(frameIndex) *
                    swapchainImageCount +
                imageIndex;
            requireSceneColorVk(
                vkCreateFramebuffer(
                    device,
                    &framebufferInfo,
                    nullptr,
                    &worldSceneColorFramebuffers[framebufferIndex]),
                "vkCreateFramebuffer(world scene color)");
        }
    }
}

void VulkanRenderBackendImpl::destroyWorldSceneColorResources() {
    if (device == VK_NULL_HANDLE) return;
    for (VkFramebuffer framebuffer : worldSceneColorFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    worldSceneColorFramebuffers.clear();
    for (std::uint32_t frameIndex = 0u;
         frameIndex < kFramesInFlight;
         ++frameIndex) {
        if (worldSceneColorViews[frameIndex] != VK_NULL_HANDLE) {
            vkDestroyImageView(
                device, worldSceneColorViews[frameIndex], nullptr);
        }
        if (worldSceneColorImages[frameIndex] != VK_NULL_HANDLE) {
            vkDestroyImage(
                device, worldSceneColorImages[frameIndex], nullptr);
        }
        if (worldSceneColorMemories[frameIndex] != VK_NULL_HANDLE) {
            vkFreeMemory(
                device, worldSceneColorMemories[frameIndex], nullptr);
        }
        worldSceneColorViews[frameIndex] = VK_NULL_HANDLE;
        worldSceneColorImages[frameIndex] = VK_NULL_HANDLE;
        worldSceneColorMemories[frameIndex] = VK_NULL_HANDLE;
    }
}

void VulkanRenderBackendImpl::beginWorldSceneColorPass(
    int surfaceWidth,
    int surfaceHeight) {
    if (activeEditorSurface != 0u) {
        const auto iterator =
            editorSurfaces.find(activeEditorSurface);
        if (!frameActive ||
            iterator == editorSurfaces.end() ||
            !editorSurfacePassActive ||
            worldSceneColorPassActive ||
            worldSceneColorRenderPass == VK_NULL_HANDLE ||
            worldSceneColorPipeline == VK_NULL_HANDLE) {
            return;
        }
        EditorSurface& editorSurface =
            iterator->second;
        FrameResources& frame = frames[currentFrame];
        vkCmdEndRenderPass(frame.commandBuffer);
        editorSurfacePassActive = false;

        const std::array<VkClearValue, 2> clearValues{
            VkClearValue{{{
                frameClearColor[0],
                frameClearColor[1],
                frameClearColor[2],
                frameClearColor[3]}}},
            VkClearValue{{{1.0f, 0u}}},
        };
        VkRenderPassBeginInfo passInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        passInfo.renderPass = worldSceneColorRenderPass;
        passInfo.framebuffer =
            editorSurface.frames[currentFrame]
                .linearFramebuffer;
        passInfo.renderArea.extent = {
            static_cast<std::uint32_t>(
                editorSurface.width),
            static_cast<std::uint32_t>(
                editorSurface.height)};
        passInfo.clearValueCount =
            static_cast<std::uint32_t>(
                clearValues.size());
        passInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(
            frame.commandBuffer,
            &passInfo,
            VK_SUBPASS_CONTENTS_INLINE);
        invalidateSceneColorBindings(*this);
        setViewportAndScissor(
            frame.commandBuffer,
            std::max(1, surfaceWidth),
            std::max(1, surfaceHeight));
        worldSceneColorPassActive = true;
        return;
    }
    (void)surfaceWidth;
    (void)surfaceHeight;
    if (!frameActive ||
        !mainRenderPassActive ||
        worldSceneColorPassActive ||
        worldSceneColorRenderPass == VK_NULL_HANDLE ||
        worldSceneColorPipeline == VK_NULL_HANDLE ||
        acquiredImage >= swapchainImages.size()) {
        return;
    }

    FrameResources& frame = frames[currentFrame];
    vkCmdEndRenderPass(frame.commandBuffer);
    mainRenderPassActive = false;

    const std::array<VkClearValue, 2> clearValues{
        VkClearValue{{{
            frameClearColor[0],
            frameClearColor[1],
            frameClearColor[2],
            frameClearColor[3]}}},
        VkClearValue{{{1.0f, 0u}}},
    };
    const std::size_t framebufferIndex =
        static_cast<std::size_t>(currentFrame) *
            swapchainImages.size() +
        acquiredImage;
    VkRenderPassBeginInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = worldSceneColorRenderPass;
    renderPassInfo.framebuffer =
        worldSceneColorFramebuffers[framebufferIndex];
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount =
        static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(
        frame.commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);

    invalidateSceneColorBindings(*this);
    setViewportAndScissor(
        frame.commandBuffer,
        static_cast<int>(swapchainExtent.width),
        static_cast<int>(swapchainExtent.height));
    worldSceneColorPassActive = true;
}

bool VulkanRenderBackendImpl::captureWorldDepthSnapshot() {
    if (!frameActive || !worldSceneColorPassActive || worldShadowActive) return false;
    VkExtent2D extent = swapchainExtent;
    VkImage source = depthImages.at(acquiredImage);
    VkFramebuffer framebuffer = worldSceneColorFramebuffers.at(currentFrame * swapchainImages.size() + acquiredImage);
    if (activeEditorSurface) {
        auto& editorSurface = editorSurfaces.at(activeEditorSurface);
        extent = {static_cast<std::uint32_t>(editorSurface.width), static_cast<std::uint32_t>(editorSurface.height)};
        source = editorSurface.frames[currentFrame].depth;
        framebuffer = editorSurface.frames[currentFrame].linearFramebuffer;
    }
    if (extent.width > 8192 || extent.height > 8192) return false;
    const auto bucket = [](std::uint32_t value) { std::uint32_t size = 256; while (size < value) size *= 2; return size; };
    const auto width = bucket(std::max(extent.width, extent.height)), height = width;
    const std::string key = "scene-depth/" + std::to_string(width) + "x" + std::to_string(height) + "/" + std::to_string(depthFormat);
    auto found = worldTextures.find(key);
    bool fresh = found == worldTextures.end();
    if (fresh) {
        Texture target;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D; info.format = depthFormat;
            info.extent = {width, height, 1}; info.mipLevels = info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            requireSceneColorVk(vkCreateImage(device, &info, nullptr, &target.image), "create depth snapshot");
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, target.image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireSceneColorVk(vkAllocateMemory(device, &allocation, nullptr, &target.memory), "allocate depth snapshot");
            requireSceneColorVk(vkBindImageMemory(device, target.image, target.memory, 0), "bind depth snapshot");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = target.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = depthFormat;
            view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            requireSceneColorVk(vkCreateImageView(device, &view, nullptr, &target.view), "view depth snapshot");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            requireSceneColorVk(vkCreateSampler(device, &sampler, nullptr, &target.sampler), "sample depth snapshot");
            target.width = static_cast<int>(width); target.height = static_cast<int>(height);
            target.format = depthFormat; target.allocationBytes = requirements.size;
        } catch (...) { destroyTexture(target); throw; }
        found = worldTextures.emplace(key, target).first;
    }
    auto& target = found->second;
    const VkCommandBuffer command = frames[currentFrame].commandBuffer;
    vkCmdEndRenderPass(command);
    const auto aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (depthFormat == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
    const auto barrier = [&](VkImage image, VkImageLayout before, VkImageLayout after, VkAccessFlags from, VkAccessFlags to,
                             VkPipelineStageFlags fromStage, VkPipelineStageFlags toStage) {
        VkImageMemoryBarrier value{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        value.oldLayout = before; value.newLayout = after; value.image = image;
        value.srcAccessMask = from; value.dstAccessMask = to;
        value.srcQueueFamilyIndex = value.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        value.subresourceRange = {static_cast<VkImageAspectFlags>(aspect), 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, fromStage, toStage, 0, 0, nullptr, 0, nullptr, 1, &value);
    };
    barrier(source, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(target.image, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, fresh ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    barrier(target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldSceneColorLoadRenderPass; pass.framebuffer = framebuffer; pass.renderArea.extent = extent;
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    invalidateSceneColorBindings(*this);
    setViewportAndScissor(command, static_cast<int>(extent.width), static_cast<int>(extent.height));
    worldDepthSnapshot = &target;
    return true;
}

bool VulkanRenderBackendImpl::clearWorldDepth() {
    if (worldShadowActive || !frameActive || !worldSceneColorPassActive) return false;
    VkExtent2D extent = swapchainExtent;
    if (activeEditorSurface != 0u) {
        const auto found = editorSurfaces.find(activeEditorSurface);
        if (found == editorSurfaces.end()) return false;
        extent = {static_cast<std::uint32_t>(found->second.width),
                  static_cast<std::uint32_t>(found->second.height)};
    }
    VkClearAttachment clear{};
    clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clear.clearValue.depthStencil = {1.0f, 0u};
    VkClearRect rect{};
    rect.rect.extent = extent;
    rect.layerCount = 1;
    vkCmdClearAttachments(frames[currentFrame].commandBuffer, 1, &clear, 1, &rect);
    return true;
}

void VulkanRenderBackendImpl::endWorldSceneColorPass() {
    if (!frameActive || !worldSceneColorPassActive) return;

    FrameResources& frame = frames[currentFrame];
    vkCmdEndRenderPass(frame.commandBuffer);
    worldSceneColorPassActive = false;

    if (activeEditorSurface != 0u) {
        const auto iterator =
            editorSurfaces.find(activeEditorSurface);
        if (iterator == editorSurfaces.end()) {
            return;
        }
        EditorSurface& editorSurface =
            iterator->second;
        beginEditorSurfaceDisplayPass(editorSurface);
        vkCmdBindPipeline(
            frame.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            worldSceneColorPipeline);
        const VkDescriptorSet descriptorSet =
            editorSurface.frames[currentFrame]
                .linearDescriptorSet;
        vkCmdBindDescriptorSets(
            frame.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            worldSceneColorPipelineLayout,
            0u,
            1u,
            &descriptorSet,
            0u,
            nullptr);
        VkViewport viewport{};
        viewport.width =
            static_cast<float>(
                editorSurface.width);
        viewport.height =
            static_cast<float>(
                editorSurface.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = {
            static_cast<std::uint32_t>(
                editorSurface.width),
            static_cast<std::uint32_t>(
                editorSurface.height)};
        vkCmdSetViewport(
            frame.commandBuffer,
            0u,
            1u,
            &viewport);
        vkCmdSetScissor(
            frame.commandBuffer,
            0u,
            1u,
            &scissor);
        vkCmdDraw(
            frame.commandBuffer,
            3u,
            1u,
            0u,
            0u);
        invalidateSceneColorBindings(*this);
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = loadRenderPass;
    renderPassInfo.framebuffer = framebuffers[acquiredImage];
    renderPassInfo.renderArea.extent = swapchainExtent;
    vkCmdBeginRenderPass(
        frame.commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    mainRenderPassActive = true;

    vkCmdBindPipeline(
        frame.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        worldSceneColorPipeline);
    const VkDescriptorSet descriptorSet =
        worldSceneColorDescriptorSets[currentFrame];
    vkCmdBindDescriptorSets(
        frame.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        worldSceneColorPipelineLayout,
        0u,
        1u,
        &descriptorSet,
        0u,
        nullptr);
    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = swapchainExtent;
    vkCmdSetViewport(frame.commandBuffer, 0u, 1u, &viewport);
    vkCmdSetScissor(frame.commandBuffer, 0u, 1u, &scissor);
    vkCmdDraw(frame.commandBuffer, 3u, 1u, 0u, 0u);

    invalidateSceneColorBindings(*this);
}

void VulkanRenderBackendImpl::ensureScreenFeedbackResources(int width,int height) {
    if(feedbackRenderPass) {
        if(feedbackWidth!=width || feedbackHeight!=height)throw std::runtime_error("Screen feedback dimensions must remain fixed for a renderer session");
        if(!feedbackPipeline)createScreenFeedbackPipeline();
        return;
    }
    std::array<VkDescriptorSetLayoutBinding,4> bindings{};
    for(std::uint32_t i=0;i<4;++i)bindings[i]={i,i==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=4;setInfo.pBindings=bindings.data();
    requireSceneColorVk(vkCreateDescriptorSetLayout(device,&setInfo,nullptr,&feedbackSetLayout),"screen feedback set layout");
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&feedbackSetLayout;
    requireSceneColorVk(vkCreatePipelineLayout(device,&layout,nullptr,&feedbackLayout),"screen feedback pipeline layout");
    const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3*2*kFramesInFlight},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2*kFramesInFlight}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=2*kFramesInFlight;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    requireSceneColorVk(vkCreateDescriptorPool(device,&pool,nullptr,&feedbackPool),"screen feedback descriptor pool");
    for(auto& sets : feedbackSets) {
        const VkDescriptorSetLayout layouts[]={feedbackSetLayout,feedbackSetLayout};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=feedbackPool;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
        requireSceneColorVk(vkAllocateDescriptorSets(device,&allocate,sets.data()),"screen feedback descriptor sets");
    }
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    const VkSubpassDependency dependencies[]={
        {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
        {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,0}};
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=2;pass.pDependencies=dependencies;
    requireSceneColorVk(vkCreateRenderPass(device,&pass,nullptr,&feedbackRenderPass),"screen feedback render pass");
    for(std::size_t i=0;i<2;++i) {
        auto& image=feedbackImages[i];image.width=width;image.height=height;image.format=attachment.format;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=attachment.format;
        info.extent={static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),1};info.mipLevels=info.arrayLayers=1;
        info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        requireSceneColorVk(vkCreateImage(device,&info,nullptr,&image.image),"screen feedback image");
        VkMemoryRequirements need{};vkGetImageMemoryRequirements(device,image.image,&need);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=need.size;allocate.memoryTypeIndex=findMemoryType(need.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        requireSceneColorVk(vkAllocateMemory(device,&allocate,nullptr,&image.memory),"screen feedback memory");
        requireSceneColorVk(vkBindImageMemory(device,image.image,image.memory,0),"screen feedback image memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=attachment.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        requireSceneColorVk(vkCreateImageView(device,&view,nullptr,&image.view),"screen feedback view");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=feedbackRenderPass;fb.attachmentCount=1;fb.pAttachments=&image.view;fb.width=width;fb.height=height;fb.layers=1;
        requireSceneColorVk(vkCreateFramebuffer(device,&fb,nullptr,&feedbackFramebuffers[i]),"screen feedback framebuffer");
        const auto command=beginOneTimeCommands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.image=image.image;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange=view.subresourceRange;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        endOneTimeCommands(command);
    }
    feedbackWidth=width;feedbackHeight=height;createScreenFeedbackPipeline();
}

void VulkanRenderBackendImpl::destroyScreenFeedback() {
    if(!device)return;
    for(auto& fb:feedbackFramebuffers){if(fb)vkDestroyFramebuffer(device,fb,nullptr);fb=VK_NULL_HANDLE;}
    for(auto& image:feedbackImages){
        if(image.view)vkDestroyImageView(device,image.view,nullptr);if(image.image)vkDestroyImage(device,image.image,nullptr);if(image.memory)vkFreeMemory(device,image.memory,nullptr);image={};
    }
    if(feedbackPipeline)vkDestroyPipeline(device,feedbackPipeline,nullptr);
    if(feedbackDisplayPipeline)vkDestroyPipeline(device,feedbackDisplayPipeline,nullptr);
    if(feedbackRenderPass)vkDestroyRenderPass(device,feedbackRenderPass,nullptr);
    if(feedbackPool)vkDestroyDescriptorPool(device,feedbackPool,nullptr);
    if(feedbackLayout)vkDestroyPipelineLayout(device,feedbackLayout,nullptr);
    if(feedbackSetLayout)vkDestroyDescriptorSetLayout(device,feedbackSetLayout,nullptr);
    feedbackPipeline=feedbackDisplayPipeline=VK_NULL_HANDLE;feedbackRenderPass=VK_NULL_HANDLE;
    feedbackPool=VK_NULL_HANDLE;feedbackLayout=VK_NULL_HANDLE;feedbackSetLayout=VK_NULL_HANDLE;
    feedbackWidth=feedbackHeight=0;feedbackSequence=0;feedbackLast={};
}

void VulkanRenderBackendImpl::endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame& input) {
    using namespace engine::render;
    validateScreenFeedback(input);
    if(!worldSceneColorPassActive)throw std::runtime_error("Screen feedback requires an active world pass");
    const bool editor=activeEditorSurface!=0;
    auto* editorTarget=editor?&editorSurfaces.at(activeEditorSurface):nullptr;
    const VkImageView sceneView=editor?editorTarget->frames[currentFrame].linearColorView:worldSceneColorViews[currentFrame];
    endWorldSceneColorPass();
    ensureScreenFeedbackResources(input.width,input.height);
    auto* mask=ensureWorldTextureRaw(input.maskKey.c_str(),nullptr,input.maskRgba,input.maskWidth,input.maskHeight,33071,33071,false);
    if(!mask)throw std::runtime_error("Could not upload screen feedback ingredient");
    if(input.seed){feedbackSequence=input.sequence;feedbackHistory=0;feedbackDisplayInput=0;feedbackLast={};feedbackLast.count=1;feedbackLast.passes[0].rectangle={0,0,float(input.width),float(input.height)};}
    else if(feedbackSequence!=input.sequence)throw std::runtime_error("Screen feedback must be seeded before playback");
    auto& frame=frames[currentFrame];auto command=frame.commandBuffer;
    vkCmdEndRenderPass(command);mainRenderPassActive=false;editorSurfacePassActive=false;
    for(int history=0;history<2;++history) {
        const VkDescriptorImageInfo images[]={{worldSceneColorSampler,sceneView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {worldSceneColorSampler,feedbackImages[history].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mask->sampler,mask->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorBufferInfo buffer{frame.transient.buffer,0,sizeof(ScreenFeedbackUniform)};
        std::array<VkWriteDescriptorSet,4> writes{};
        for(std::uint32_t i=0;i<4;++i){auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=feedbackSets[currentFrame][history];write.dstBinding=i;write.descriptorCount=1;write.descriptorType=i==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;if(i==3)write.pBufferInfo=&buffer;else write.pImageInfo=&images[i];}
        vkUpdateDescriptorSets(device,4,writes.data(),0,nullptr);
    }
    auto draw=[&](int mode,int history,int width,int height,const ScreenFeedbackUpdate& update,VkPipeline pipeline) {
        const ScreenFeedbackUniform uniform{{float(input.width),float(input.height),float(mode),float(update.count)},update.passes};
        VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;
        if(!writeTransient(&uniform,sizeof(uniform),physicalDeviceProperties.limits.minUniformBufferOffsetAlignment,buffer,offset))throw std::runtime_error("Screen feedback uniform arena exhausted");
        const auto dynamicOffset=static_cast<std::uint32_t>(offset);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,feedbackLayout,0,1,&feedbackSets[currentFrame][history],1,&dynamicOffset);
        VkViewport viewport{0,0,float(width),float(height),0,1};
        VkRect2D scissor{{0,0},{static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height)}};
        vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
        vkCmdDraw(command,3,1,0,0);
    };
    auto historyDraw=[&](int mode,int history,int target,const ScreenFeedbackUpdate& update){
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=feedbackRenderPass;pass.framebuffer=feedbackFramebuffers[target];pass.renderArea.extent={static_cast<std::uint32_t>(input.width),static_cast<std::uint32_t>(input.height)};
        vkCmdBeginRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);draw(mode,history,input.width,input.height,update,feedbackPipeline);vkCmdEndRenderPass(command);
    };
    if(input.seed)historyDraw(0,1,0,{});
    for(const auto& update:input.updates){feedbackDisplayInput=feedbackHistory;feedbackHistory=1-feedbackHistory;feedbackLast=update;historyDraw(1,feedbackDisplayInput,feedbackHistory,update);}
    if(editor)beginEditorSurfaceDisplayPass(*editorTarget);
    else {
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=loadRenderPass;pass.framebuffer=framebuffers[acquiredImage];pass.renderArea.extent=swapchainExtent;
        vkCmdBeginRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);mainRenderPassActive=true;
    }
    // Seed also redraws the ordinary scene, because an editor display pass clears.
    draw(input.seed && input.updates.empty()?0:2,feedbackDisplayInput,
        editor?editorTarget->width:int(swapchainExtent.width),editor?editorTarget->height:int(swapchainExtent.height),feedbackLast,feedbackDisplayPipeline);
    invalidateSceneColorBindings(*this);
}
