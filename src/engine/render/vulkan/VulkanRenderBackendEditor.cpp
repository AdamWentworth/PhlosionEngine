#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace {

void requireEditorVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) +
            " failed with VkResult " +
            std::to_string(result) + ".");
    }
}

VkImageAspectFlags depthAspect(VkFormat format) {
    VkImageAspectFlags aspect =
        VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspect;
}

void invalidateEditorBindings(
    VulkanRenderBackendImpl& backend) {
    backend.resetWorldFrameStateCache();
}

} // namespace

std::uint64_t VulkanRenderBackendImpl::createEditorSurface(
    int width,
    int height) {
    if (!initialized || device == VK_NULL_HANDLE) {
        return 0u;
    }
    EditorSurface editorSurface;
    editorSurface.id = nextEditorSurface++;
    editorSurface.width = std::max(1, width);
    editorSurface.height = std::max(1, height);
    const std::uint64_t id = editorSurface.id;
    auto [iterator, inserted] =
        editorSurfaces.emplace(
            id,
            std::move(editorSurface));
    if (!inserted ||
        !createEditorSurfaceResources(iterator->second)) {
        editorSurfaces.erase(id);
        return 0u;
    }
    return id;
}

bool VulkanRenderBackendImpl::resizeEditorSurface(
    std::uint64_t surfaceId,
    int width,
    int height) {
    const auto iterator = editorSurfaces.find(surfaceId);
    if (iterator == editorSurfaces.end() ||
        device == VK_NULL_HANDLE) {
        return false;
    }
    EditorSurface& editorSurface = iterator->second;
    width = std::max(1, width);
    height = std::max(1, height);
    if (editorSurface.width == width &&
        editorSurface.height == height &&
        editorSurface.frames[0].displayColorView !=
            VK_NULL_HANDLE) {
        return true;
    }
    requireEditorVk(
        vkDeviceWaitIdle(device),
        "vkDeviceWaitIdle(resize editor surface)");
    destroyEditorSurfaceResources(editorSurface);
    editorSurface.width = width;
    editorSurface.height = height;
    return createEditorSurfaceResources(editorSurface);
}

void VulkanRenderBackendImpl::destroyEditorSurface(
    std::uint64_t surfaceId) {
    const auto iterator = editorSurfaces.find(surfaceId);
    if (iterator == editorSurfaces.end()) {
        return;
    }
    if (activeEditorSurface == surfaceId) {
        endEditorSurface();
    }
    if (device != VK_NULL_HANDLE) {
        requireEditorVk(
            vkDeviceWaitIdle(device),
            "vkDeviceWaitIdle(destroy editor surface)");
        destroyEditorSurfaceResources(iterator->second);
    }
    editorSurfaces.erase(iterator);
}

bool VulkanRenderBackendImpl::createEditorSurfaceResources(
    EditorSurface& editorSurface) {
    if (device == VK_NULL_HANDLE ||
        worldSceneColorRenderPass == VK_NULL_HANDLE ||
        worldSceneColorSetLayout == VK_NULL_HANDLE ||
        descriptorPool == VK_NULL_HANDLE ||
        swapchainFormat == VK_FORMAT_UNDEFINED ||
        depthFormat == VK_FORMAT_UNDEFINED) {
        return false;
    }

    if (editorSurfaceSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode =
            VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0f;
        requireEditorVk(
            vkCreateSampler(
                device,
                &samplerInfo,
                nullptr,
                &editorSurfaceSampler),
            "vkCreateSampler(editor surface)");
    }

    if (editorSurface.frames[0].linearDescriptorSet ==
        VK_NULL_HANDLE) {
        std::array<
            VkDescriptorSetLayout,
            kFramesInFlight> layouts{};
        layouts.fill(worldSceneColorSetLayout);
        std::array<
            VkDescriptorSet,
            kFramesInFlight> descriptors{};
        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount =
            kFramesInFlight;
        allocateInfo.pSetLayouts = layouts.data();
        requireEditorVk(
            vkAllocateDescriptorSets(
                device,
                &allocateInfo,
                descriptors.data()),
            "vkAllocateDescriptorSets(editor surface)");
        for (std::uint32_t index = 0u;
             index < kFramesInFlight;
             ++index) {
            editorSurface.frames[index].linearDescriptorSet =
                descriptors[index];
        }
    }

    const auto createImage =
        [&](VkFormat format,
            VkImageUsageFlags usage,
            VkImageAspectFlags aspect,
            VkImage& outImage,
            VkDeviceMemory& outMemory,
            VkImageView& outView) {
            VkImageCreateInfo imageInfo{
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format;
            imageInfo.extent = {
                static_cast<std::uint32_t>(
                    editorSurface.width),
                static_cast<std::uint32_t>(
                    editorSurface.height),
                1u};
            imageInfo.mipLevels = 1u;
            imageInfo.arrayLayers = 1u;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = usage;
            imageInfo.sharingMode =
                VK_SHARING_MODE_EXCLUSIVE;
            requireEditorVk(
                vkCreateImage(
                    device,
                    &imageInfo,
                    nullptr,
                    &outImage),
                "vkCreateImage(editor surface)");

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(
                device,
                outImage,
                &requirements);
            VkMemoryAllocateInfo allocation{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize =
                requirements.size;
            allocation.memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireEditorVk(
                vkAllocateMemory(
                    device,
                    &allocation,
                    nullptr,
                    &outMemory),
                "vkAllocateMemory(editor surface)");
            requireEditorVk(
                vkBindImageMemory(
                    device,
                    outImage,
                    outMemory,
                    0u),
                "vkBindImageMemory(editor surface)");

            VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = outImage;
            viewInfo.viewType =
                VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask =
                aspect;
            viewInfo.subresourceRange.levelCount = 1u;
            viewInfo.subresourceRange.layerCount = 1u;
            requireEditorVk(
                vkCreateImageView(
                    device,
                    &viewInfo,
                    nullptr,
                    &outView),
                "vkCreateImageView(editor surface)");
        };

    for (std::uint32_t frameIndex = 0u;
         frameIndex < kFramesInFlight;
         ++frameIndex) {
        EditorSurfaceFrame& frame =
            editorSurface.frames[frameIndex];
        createImage(
            swapchainFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            frame.linearColor,
            frame.linearColorMemory,
            frame.linearColorView);
        createImage(
            swapchainFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            frame.displayColor,
            frame.displayColorMemory,
            frame.displayColorView);
        createImage(
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            depthAspect(depthFormat),
            frame.depth,
            frame.depthMemory,
            frame.depthView);

        const auto createFramebuffer =
            [&](VkImageView color,
                VkFramebuffer& outFramebuffer) {
                const std::array<VkImageView, 2>
                    attachments{
                        color,
                        frame.depthView};
                VkFramebufferCreateInfo createInfo{
                    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                createInfo.renderPass =
                    worldSceneColorRenderPass;
                createInfo.attachmentCount =
                    static_cast<std::uint32_t>(
                        attachments.size());
                createInfo.pAttachments =
                    attachments.data();
                createInfo.width =
                    static_cast<std::uint32_t>(
                        editorSurface.width);
                createInfo.height =
                    static_cast<std::uint32_t>(
                        editorSurface.height);
                createInfo.layers = 1u;
                requireEditorVk(
                    vkCreateFramebuffer(
                        device,
                        &createInfo,
                        nullptr,
                        &outFramebuffer),
                    "vkCreateFramebuffer(editor surface)");
            };
        createFramebuffer(
            frame.linearColorView,
            frame.linearFramebuffer);
        createFramebuffer(
            frame.displayColorView,
            frame.displayFramebuffer);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = worldSceneColorSampler;
        imageInfo.imageView =
            frame.linearColorView;
        imageInfo.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame.linearDescriptorSet;
        write.dstBinding = 0u;
        write.descriptorCount = 1u;
        write.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(
            device,
            1u,
            &write,
            0u,
            nullptr);
    }
    return true;
}

void VulkanRenderBackendImpl::destroyEditorSurfaceResources(
    EditorSurface& editorSurface) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (EditorSurfaceFrame& frame :
         editorSurface.frames) {
        if (frame.linearFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(
                device,
                frame.linearFramebuffer,
                nullptr);
        }
        if (frame.displayFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(
                device,
                frame.displayFramebuffer,
                nullptr);
        }
        if (frame.linearColorView != VK_NULL_HANDLE) {
            vkDestroyImageView(
                device,
                frame.linearColorView,
                nullptr);
        }
        if (frame.displayColorView != VK_NULL_HANDLE) {
            vkDestroyImageView(
                device,
                frame.displayColorView,
                nullptr);
        }
        if (frame.depthView != VK_NULL_HANDLE) {
            vkDestroyImageView(
                device,
                frame.depthView,
                nullptr);
        }
        if (frame.linearColor != VK_NULL_HANDLE) {
            vkDestroyImage(
                device,
                frame.linearColor,
                nullptr);
        }
        if (frame.displayColor != VK_NULL_HANDLE) {
            vkDestroyImage(
                device,
                frame.displayColor,
                nullptr);
        }
        if (frame.depth != VK_NULL_HANDLE) {
            vkDestroyImage(
                device,
                frame.depth,
                nullptr);
        }
        if (frame.linearColorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(
                device,
                frame.linearColorMemory,
                nullptr);
        }
        if (frame.displayColorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(
                device,
                frame.displayColorMemory,
                nullptr);
        }
        if (frame.depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(
                device,
                frame.depthMemory,
                nullptr);
        }
        const VkDescriptorSet descriptor =
            frame.linearDescriptorSet;
        frame = {};
        frame.linearDescriptorSet = descriptor;
    }
}

void VulkanRenderBackendImpl::recreateEditorSurfaceResources() {
    for (auto& [_, editorSurface] : editorSurfaces) {
        (void)createEditorSurfaceResources(
            editorSurface);
    }
}

void VulkanRenderBackendImpl::destroyAllEditorSurfaceResources() {
    for (auto& [_, editorSurface] : editorSurfaces) {
        destroyEditorSurfaceResources(editorSurface);
    }
}

void VulkanRenderBackendImpl::beginEditorSurfaceDisplayPass(
    EditorSurface& editorSurface) {
    EditorSurfaceFrame& frame =
        editorSurface.frames[currentFrame];
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
    passInfo.framebuffer = frame.displayFramebuffer;
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
        frames[currentFrame].commandBuffer,
        &passInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    editorSurfacePassActive = true;
    invalidateEditorBindings(*this);
    setViewportAndScissor(
        frames[currentFrame].commandBuffer,
        editorSurface.width,
        editorSurface.height);
}

bool VulkanRenderBackendImpl::beginEditorSurface(
    std::uint64_t surfaceId) {
    const auto iterator = editorSurfaces.find(surfaceId);
    if (!frameActive ||
        iterator == editorSurfaces.end() ||
        activeEditorSurface != 0u ||
        worldSceneColorPassActive ||
        !mainRenderPassActive) {
        return false;
    }
    FrameResources& frame = frames[currentFrame];
    vkCmdEndRenderPass(frame.commandBuffer);
    mainRenderPassActive = false;
    activeEditorSurface = surfaceId;
    beginEditorSurfaceDisplayPass(iterator->second);
    return true;
}

void VulkanRenderBackendImpl::resumeMainRenderPass() {
    if (!frameActive ||
        mainRenderPassActive ||
        acquiredImage >= framebuffers.size()) {
        return;
    }
    VkRenderPassBeginInfo passInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    passInfo.renderPass = loadRenderPass;
    passInfo.framebuffer = framebuffers[acquiredImage];
    passInfo.renderArea.extent = swapchainExtent;
    vkCmdBeginRenderPass(
        frames[currentFrame].commandBuffer,
        &passInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    mainRenderPassActive = true;
    invalidateEditorBindings(*this);
    setViewportAndScissor(
        frames[currentFrame].commandBuffer,
        static_cast<int>(swapchainExtent.width),
        static_cast<int>(swapchainExtent.height));
}

void VulkanRenderBackendImpl::endEditorSurface() {
    if (activeEditorSurface == 0u) {
        return;
    }
    if (worldSceneColorPassActive) {
        endWorldSceneColorPass();
    }
    if (editorSurfacePassActive) {
        vkCmdEndRenderPass(
            frames[currentFrame].commandBuffer);
        editorSurfacePassActive = false;
    }
    activeEditorSurface = 0u;
    resumeMainRenderPass();
}

bool VulkanRenderBackendImpl::getEditorSurfaceTexture(
    std::uint64_t surfaceId,
    std::uint32_t frameIndex,
    VkSampler& outSampler,
    VkImageView& outImageView) const {
    outSampler = VK_NULL_HANDLE;
    outImageView = VK_NULL_HANDLE;
    const auto iterator = editorSurfaces.find(surfaceId);
    if (iterator == editorSurfaces.end() ||
        frameIndex >= kFramesInFlight ||
        editorSurfaceSampler == VK_NULL_HANDLE) {
        return false;
    }
    outSampler = editorSurfaceSampler;
    outImageView =
        iterator->second.frames[frameIndex]
            .displayColorView;
    return outImageView != VK_NULL_HANDLE;
}
