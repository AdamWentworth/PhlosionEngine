#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

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

void VulkanRenderBackendImpl::endWorldSceneColorPass() {
    if (!frameActive || !worldSceneColorPassActive) return;

    FrameResources& frame = frames[currentFrame];
    vkCmdEndRenderPass(frame.commandBuffer);
    worldSceneColorPassActive = false;

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
