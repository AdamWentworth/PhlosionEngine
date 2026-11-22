#include "engine/render/WorldPlanarCapture.h"
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
    backend.worldCommandState.boundGraphicsPipeline = VK_NULL_HANDLE;
    backend.worldCommandState.boundTexturedDescriptorSet = VK_NULL_HANDLE;
    backend.worldCommandState.boundVertexBuffer = VK_NULL_HANDLE;
    backend.worldCommandState.boundVertexOffset = 0u;
    backend.worldCommandState.boundIndexBuffer = VK_NULL_HANDLE;
    backend.worldCommandState.boundIndexOffset = 0u;
    backend.worldCommandState.boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    backend.worldCommandState.boundViewportWidth = 0u;
    backend.worldCommandState.boundViewportHeight = 0u;
    backend.worldCommandState.boundViewportValid = false;
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
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = {
            swapchainExtent.width,
            swapchainExtent.height,
            1u};
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
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
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
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
    for(auto& entry:worldSurfaceTargets) {
        auto& target=entry.second;
        if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        for(unsigned i=target.probeScratch?3u:0u;i<6;++i) {
            auto& plane=target.planes[i];
            if(plane.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&plane.descriptorSet);
            destroyTexture(plane);
        }
        if(!target.probeScratch) {
            if(target.depth.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&target.depth.descriptorSet);
            destroyTexture(target.depth);
        }
    }
    worldSurfaceTargets.clear();worldSurfaceTarget=nullptr;worldSurfaceDepth=nullptr;
    for(auto& entry:worldProbeScratch) {
        if(entry.second.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&entry.second.descriptorSet);
        destroyTexture(entry.second);
    }
    worldProbeScratch.clear();destroyBuffer(worldProbeRadianceReadback);worldProbeDiagnostic={};
    worldProbeSurfaces.clear();placedProbes.clear();placedProbeDescriptor=VK_NULL_HANDLE;worldProbeSurfaceKey.clear();
    worldSurfaceActive=worldSurfaceReady=worldSurfaceContact=false;
    for(auto& entry:worldHazeTargets) {
        auto& target=entry.second;
        if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        destroyTexture(target.color);destroyTexture(target.depth);
    }
    worldHazeTargets.clear();worldHazeDescriptor=VK_NULL_HANDLE;worldHazeActive=false;
    clearWorldHazeStencilTargets();worldHazeStencilActive=false;
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
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!worldSceneColorPassActive) {
        clearWorldTemporalScene();clearWorldClouds();worldWet={};worldWetTextures={};worldWetOwner=0;worldFog={};worldFogTextures={};
        if(frameActive)worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
    }
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
        endWorldRecordingRenderPass(frame.commandBuffer);
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
        beginWorldRecordingRenderPass(
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
    endWorldRecordingRenderPass(frame.commandBuffer);
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
    beginWorldRecordingRenderPass(
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

bool VulkanRenderBackendImpl::captureWorldDepthSnapshot(const char* purpose) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
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
    const std::string key = std::string(purpose)+"/"+std::to_string(currentFrame)+"/"+std::to_string(width) + "x" + std::to_string(height) + "/" + std::to_string(depthFormat);
    auto found = worldTextures.find(key);
    bool fresh = found == worldTextures.end();
    if (fresh) {
        Texture target;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D; info.format = depthFormat;
            info.extent = {width, height, 1}; info.mipLevels = info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    endWorldRecordingRenderPass(command);
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
    beginWorldRecordingRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    invalidateSceneColorBindings(*this);
    setViewportAndScissor(command, static_cast<int>(extent.width), static_cast<int>(extent.height));
    worldDepthSnapshot = &target;
    return true;
}

bool VulkanRenderBackendImpl::restoreWorldDepthSnapshot() {
    if(!frameActive || !worldSceneColorPassActive || !worldDepthSnapshot)return false;
    VkExtent2D extent = swapchainExtent;
    VkImage source = depthImages.at(acquiredImage);
    VkFramebuffer framebuffer = worldSceneColorFramebuffers.at(currentFrame * swapchainImages.size() + acquiredImage);
    if (activeEditorSurface) {
        auto& editorSurface = editorSurfaces.at(activeEditorSurface);
        extent = {static_cast<std::uint32_t>(editorSurface.width), static_cast<std::uint32_t>(editorSurface.height)};
        source = editorSurface.frames[currentFrame].depth;
        framebuffer = editorSurface.frames[currentFrame].linearFramebuffer;
    }
    auto& target=*worldDepthSnapshot;
    const VkCommandBuffer command = frames[currentFrame].commandBuffer;
    endWorldRecordingRenderPass(command);
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
    barrier(source, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(target.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {worldPlanarReflection[4][3]<0?std::uint32_t(worldPlanarReflectionView.width):extent.width,
                   worldPlanarReflection[4][3]<0?std::uint32_t(worldPlanarReflectionView.heightPixels):extent.height,1};
    vkCmdCopyImage(command, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    barrier(target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = worldSceneColorLoadRenderPass; pass.framebuffer = framebuffer; pass.renderArea.extent = extent;
    beginWorldRecordingRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    invalidateSceneColorBindings(*this);
    setViewportAndScissor(command, static_cast<int>(extent.width), static_cast<int>(extent.height));
    return true;
}

bool VulkanRenderBackendImpl::clearWorldDepth() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

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
    worldSurfaceComposed=false;
    vkCmdClearAttachments(worldRecordingCommand(frames[currentFrame].commandBuffer), 1, &clear, 1, &rect);
    return true;
}

VkDescriptorSet VulkanRenderBackendImpl::createPlanarDescriptor(const Texture& texture) {
    VkDescriptorSet set=VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool=descriptorPool; allocation.descriptorSetCount=1;
    allocation.pSetLayouts=&worldSceneColorSetLayout;
    requireSceneColorVk(vkAllocateDescriptorSets(device,&allocation,&set),"allocate reflection sampler");
    VkDescriptorImageInfo image{texture.sampler,texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=set; write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&image;
    vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    return set;
}

VkDescriptorSet VulkanRenderBackendImpl::createContactFilterSet(const std::array<VkDescriptorSet,3>& sources) {
    ensureWorldSunFallback();
    if(worldContactFilterSetCount>=worldContactFilterSets[currentFrame].size())throw std::runtime_error("Contact view descriptor budget exceeded");
    auto& set=worldContactFilterSets[currentFrame][worldContactFilterSetCount++];
    if(!set) {
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool=descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&worldContactFilterLayout;
        requireSceneColorVk(vkAllocateDescriptorSets(device,&allocation,&set),"allocate contact filter samplers");
    }
    worldContactFilterSources=sources;
    std::array<VkDescriptorSet,11> inputs{sources[0],sources[1],sources[2],
        worldClusteredDescriptors[0],worldClusteredDescriptors[1],worldClusteredDescriptors[2],
        worldStoredSurfaceDescriptors[0],worldStoredSurfaceDescriptors[1],worldStoredSurfaceDescriptors[2],worldStoredSurfaceDescriptors[3],worldIndirectReceiver};
    std::array<VkCopyDescriptorSet,11> copies{};
    for(unsigned i=0;i<copies.size();++i){auto& copy=copies[i];copy.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        copy.srcSet=inputs[i]?inputs[i]:worldPlanarFallback;copy.dstSet=set;copy.dstBinding=i==10?12:i;copy.descriptorCount=1;}
    if(!worldIndirectFallbackBuffer.buffer) {
        worldIndirectFallbackBuffer=createBuffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memset(worldIndirectFallbackBuffer.mapped,0,16);
    }
    VkDescriptorBufferInfo info[2]{};VkWriteDescriptorSet writes[2]{};
    for(unsigned i=0;i<2;++i) {
        info[i]={worldIndirectBuffers[i]?worldIndirectBuffers[i]:worldIndirectFallbackBuffer.buffer,0,VK_WHOLE_SIZE};
        auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=10+i;
        w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.descriptorCount=1;w.pBufferInfo=&info[i];
    }
    vkUpdateDescriptorSets(device,2,writes,unsigned(copies.size()),copies.data());
    VkDescriptorImageInfo fogInfo[2]{};VkWriteDescriptorSet fogWrites[2]{};
    for(unsigned i=0;i<2;++i) {
        const auto* t=worldFogTextures[i]?worldFogTextures[i]:&fallbackWorldLinearTexture;
        fogInfo[i]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto& w=fogWrites[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=13+i;
        w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.descriptorCount=1;w.pImageInfo=&fogInfo[i];
    }
    vkUpdateDescriptorSets(device,2,fogWrites,0,nullptr);
    VkDescriptorImageInfo wetInfo[5]{};VkWriteDescriptorSet wetWrites[5]{};
    for(unsigned i=0;i<5;++i) {
        const auto* t=worldWetTextures[i]?worldWetTextures[i]:&fallbackWorldLinearTexture;
        wetInfo[i]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto& w=wetWrites[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=15+i;
        w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.descriptorCount=1;w.pImageInfo=&wetInfo[i];
    }
    vkUpdateDescriptorSets(device,5,wetWrites,0,nullptr);
    VkDescriptorImageInfo cloudInfo[2]{};VkWriteDescriptorSet cloudWrites[2]{};
    for(unsigned i=0;i<2;++i) {
        const auto* t=worldCloudTextures[i]?worldCloudTextures[i]:&fallbackWorldLinearTexture;
        cloudInfo[i]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto& w=cloudWrites[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=20+i;
        w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.descriptorCount=1;w.pImageInfo=&cloudInfo[i];
    }
    vkUpdateDescriptorSets(device,2,cloudWrites,0,nullptr);
    VkDescriptorImageInfo sunInfo{worldSunSampler,worldSunView?worldSunView:worldSunFallbackView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet sunWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};sunWrite.dstSet=set;sunWrite.dstBinding=22;
    sunWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;sunWrite.descriptorCount=1;sunWrite.pImageInfo=&sunInfo;
    vkUpdateDescriptorSets(device,1,&sunWrite,0,nullptr);
    auto* noiseTexture = [&] {
        engine::render::WorldResourceScopeState::SharedResources shared(worldScopes);
        const auto& noise=engine::render::worldSunShadowNoise();
        return ensureWorldTextureRaw("__temporal_reflection_noise_u16_v2",nullptr,noise.data(),64,4096,1,1,false);
    }();
    if(!noiseTexture)throw std::runtime_error("Sun blue noise allocation");
    VkDescriptorImageInfo noiseInfo{noiseTexture->sampler,noiseTexture->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    sunWrite.dstBinding=23;sunWrite.pImageInfo=&noiseInfo;vkUpdateDescriptorSets(device,1,&sunWrite,0,nullptr);
    const auto* sky=worldSkyModelTexture?worldSkyModelTexture:&fallbackWorldLinearTexture;
    VkDescriptorImageInfo skyInfo{sky->sampler,sky->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    sunWrite.dstBinding=24;sunWrite.pImageInfo=&skyInfo;vkUpdateDescriptorSets(device,1,&sunWrite,0,nullptr);
    return set;
}

bool VulkanRenderBackendImpl::beginWorldLocalHaze(int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!frameActive || !worldSceneColorPassActive || worldHazeActive || width<=0 || height<=0 || width>2048 || height>2048)return false;
    std::uint32_t size=64;while(size<static_cast<std::uint32_t>(std::max(width,height)))size*=2;
    const std::string key=std::to_string(currentFrame)+"/"+std::to_string(size);
    auto found=worldHazeTargets.find(key);
    if(found==worldHazeTargets.end()) {
        HazeTarget target;
        const auto allocate=[&](Texture& t,bool depth) {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
            info.format=depth?depthFormat:VK_FORMAT_R16G16B16A16_SFLOAT;info.extent={size,size,1};
            info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            requireSceneColorVk(vkCreateImage(device,&info,nullptr,&t.image),"create haze image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,t.image,&needs);
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=needs.size;
            memory.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireSceneColorVk(vkAllocateMemory(device,&memory,nullptr,&t.memory),"allocate haze image");
            requireSceneColorVk(vkBindImageMemory(device,t.image,t.memory,0),"bind haze image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=t.image;
            view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
            view.subresourceRange={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
            requireSceneColorVk(vkCreateImageView(device,&view,nullptr,&t.view),"view haze image");
            t.width=t.height=static_cast<int>(size);t.format=info.format;t.allocationBytes=needs.size;
            if(!depth) {
                VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
                sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                requireSceneColorVk(vkCreateSampler(device,&sampler,nullptr,&t.sampler),"sample haze image");
                t.descriptorSet=createPlanarDescriptor(t);
            }
        };
        try {
            allocate(target.color,false);allocate(target.depth,true);
            const std::array<VkImageView,2> views{target.color.view,target.depth.view};
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=worldSceneColorRenderPass;
            fb.attachmentCount=2;fb.pAttachments=views.data();fb.width=fb.height=size;fb.layers=1;
            requireSceneColorVk(vkCreateFramebuffer(device,&fb,nullptr,&target.framebuffer),"create haze framebuffer");
        } catch(...) {if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);destroyTexture(target.color);destroyTexture(target.depth);throw;}
        found=worldHazeTargets.emplace(key,target).first;
    }
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    // Small reflected/main views and multiple editor views may reuse one bucket
    // in a frame. Finish earlier sampling before clearing that image again.
    VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reuse.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;reuse.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,1,&reuse,0,nullptr,0,nullptr);
    const std::array<VkClearValue,2> clear{VkClearValue{{{0,0,0,0}}},VkClearValue{{{1,0}}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldSceneColorRenderPass;
    pass.framebuffer=found->second.framebuffer;pass.renderArea.extent={size,size};pass.clearValueCount=2;pass.pClearValues=clear.data();
    beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);invalidateSceneColorBindings(*this);
    worldHazeDescriptor=found->second.color.descriptorSet;worldHazeActive=true;return true;
}
bool VulkanRenderBackendImpl::endWorldLocalHaze() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!worldHazeActive)return false;
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    VkExtent2D extent=swapchainExtent;
    auto framebuffer=worldSceneColorFramebuffers.at(currentFrame*swapchainImages.size()+acquiredImage);
    if(activeEditorSurface) {
        auto& editorTarget=editorSurfaces.at(activeEditorSurface);
        extent={static_cast<std::uint32_t>(editorTarget.width),static_cast<std::uint32_t>(editorTarget.height)};
        framebuffer=editorTarget.frames[currentFrame].linearFramebuffer;
    }
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldSceneColorLoadRenderPass;
    pass.framebuffer=framebuffer;pass.renderArea.extent=extent;
    beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);invalidateSceneColorBindings(*this);
    worldHazeActive=false;return true;
}

bool VulkanRenderBackendImpl::beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (!frameActive || !worldSceneColorPassActive || worldShadowActive || worldPlanarReflection[4][3]<0 ||
        view.width<=0 || view.heightPixels<=0) return false;
    worldPlanarReflectionView=view;
    worldPlanarReflection=engine::render::packWorldPlanarReflection(view);
    worldPlanarScissor=engine::render::worldPlanarCaptureScissor(view);
    worldCommandState.boundViewportValid=false;
    VkClearAttachment color{}; color.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    color.clearValue.color={{0,0,0,1}};
    VkClearRect rect{}; rect.layerCount=1;
    rect.rect.extent={static_cast<std::uint32_t>(view.width),static_cast<std::uint32_t>(view.heightPixels)};
    vkCmdClearAttachments(worldRecordingCommand(frames[currentFrame].commandBuffer),1,&color,1,&rect);
    clearWorldDepth();
    return true;
}

bool VulkanRenderBackendImpl::endWorldPlanarReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(worldPlanarReflection[4][3]>=0)return false;
    if(!captureWorldRadianceSnapshot("planar",worldPlanarReflectionView.width,worldPlanarReflectionView.heightPixels,worldPlanarDescriptor,worldPlanarReflection[5]))return false;
    if(worldSurfacePlanar)clearWorldSurfaceBuffer();
    worldPlanarReflection[4][3]=1;
    worldCommandState.boundViewportValid=false;
    return true;
}

bool VulkanRenderBackendImpl::captureWorldRadianceSnapshot(const char* purpose,int width,int height,
    VkDescriptorSet& descriptor,std::array<float,4>& sampling,int atlasFace,int atlasLevel,int atlasResolution,int offsetY,int offsetX,int residentExtent,bool clearScene,bool batchFaces) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if (!worldSceneColorPassActive) return false;
    VkExtent2D extent=swapchainExtent;
    auto source=worldSceneColorImages[currentFrame];
    auto framebuffer=worldSceneColorFramebuffers.at(currentFrame*swapchainImages.size()+acquiredImage);
    if (activeEditorSurface) {
        auto& editorTarget=editorSurfaces.at(activeEditorSurface);
        extent={static_cast<std::uint32_t>(editorTarget.width),static_cast<std::uint32_t>(editorTarget.height)};
        source=editorTarget.frames[currentFrame].linearColor;
        framebuffer=editorTarget.frames[currentFrame].linearFramebuffer;
    }
    if ((batchFaces?3*width:width)>static_cast<int>(extent.width) || (batchFaces?2*height:height)>static_cast<int>(extent.height))
        throw std::runtime_error("Reflection capture exceeds scene target");
    std::uint32_t size=256; while (size<static_cast<std::uint32_t>(std::max({width,height,atlasResolution})*(atlasFace>=0?3:1))) size*=2;
    while(size<static_cast<unsigned>(residentExtent))size*=2;
    if (size>8192) throw std::runtime_error("Reflection target exceeds size limit");
    if(offsetY<0 || offsetY+height>static_cast<int>(size))throw std::runtime_error("Reflection copy offset exceeds target");
    const bool compact=std::string_view(purpose)=="placed-reflection";
    const std::string key=std::string(purpose)+"/"+(residentExtent?std::string("resident"):std::to_string(currentFrame))+"/"+std::to_string(size);
    auto found=worldTextures.find(key);
    const bool fresh=found==worldTextures.end();
    if (fresh) {
        Texture target;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType=VK_IMAGE_TYPE_2D; info.format=compact?VK_FORMAT_B10G11R11_UFLOAT_PACK32:VK_FORMAT_R16G16B16A16_SFLOAT;
            info.extent={size,size,1}; info.mipLevels=info.arrayLayers=1;
            info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|(compact?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
            requireSceneColorVk(vkCreateImage(device,&info,nullptr,&target.image),"create reflection image");
            VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device,target.image,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize=requirements.size;
            allocation.memoryTypeIndex=findMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireSceneColorVk(vkAllocateMemory(device,&allocation,nullptr,&target.memory),"allocate reflection image");
            requireSceneColorVk(vkBindImageMemory(device,target.image,target.memory,0),"bind reflection image");
            VkImageViewCreateInfo imageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            imageView.image=target.image; imageView.viewType=VK_IMAGE_VIEW_TYPE_2D; imageView.format=info.format;
            imageView.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            requireSceneColorVk(vkCreateImageView(device,&imageView,nullptr,&target.view),"view reflection image");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            requireSceneColorVk(vkCreateSampler(device,&sampler,nullptr,&target.sampler),"create reflection sampler");
            target.format=info.format; target.width=target.height=static_cast<int>(size);
            target.allocationBytes=requirements.size;
            target.descriptorSet=createPlanarDescriptor(target);
        } catch (...) { destroyTexture(target); throw; }
        found=worldTextures.emplace(key,target).first;
    }
    auto& target=found->second;
    auto command=frames[currentFrame].commandBuffer;
    endWorldRecordingRenderPass(command);
    const auto barrier=[&](VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags from,VkAccessFlags to,
                           VkPipelineStageFlags fromStage,VkPipelineStageFlags toStage) {
        VkImageMemoryBarrier value{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        value.oldLayout=before; value.newLayout=after; value.image=image;
        value.srcAccessMask=from; value.dstAccessMask=to;
        value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        value.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,fromStage,toStage,0,0,nullptr,0,nullptr,1,&value);
    };
    // The scene pass ends in shader-read layout for its ordinary display pass.
    barrier(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(target.image,fresh?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        fresh?0:VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    std::array<VkImageCopy,6> copies{};
    const unsigned count=batchFaces?6u:1u;
    for(unsigned face=0;face<count;++face) {
        auto& copy=copies[face];copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.extent={static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),1};
        copy.srcOffset={batchFaces?int(face%3)*width:0,batchFaces?int(face/3)*height:0,0};
        const auto atlasOffset=compact?engine::render::worldPlacedReflectionAtlasOffset(atlasFace+int(face),atlasLevel,atlasResolution):engine::render::worldLocalReflectionAtlasOffset(atlasFace+int(face),width,atlasLevel,atlasResolution);
        copy.dstOffset={atlasOffset[0]+offsetX,atlasOffset[1]+offsetY,0};
    }
    if(compact) {
        std::array<VkImageBlit,6> blits{};
        for(unsigned face=0;face<count;++face) {
            auto& b=blits[face];const auto& c=copies[face];
            b.srcSubresource=c.srcSubresource;b.dstSubresource=c.dstSubresource;
            b.srcOffsets[0]=c.srcOffset;b.dstOffsets[0]=c.dstOffset;
            b.srcOffsets[1]={c.srcOffset.x+width,c.srcOffset.y+height,1};
            b.dstOffsets[1]={c.dstOffset.x+width,c.dstOffset.y+height,1};
        }
        vkCmdBlitImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,blits.data(),VK_FILTER_NEAREST);
    } else vkCmdCopyImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,copies.data());
    barrier(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    barrier(target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    const std::array<VkClearValue,2> clear{VkClearValue{{{0,0,0,1}}},VkClearValue{{{1,0}}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass=clearScene?worldSceneColorRenderPass:worldSceneColorLoadRenderPass;
    pass.framebuffer=framebuffer;pass.renderArea.extent=extent;
    pass.clearValueCount=clearScene?2u:0u;pass.pClearValues=clearScene?clear.data():nullptr;
    beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);
    invalidateSceneColorBindings(*this);
    descriptor=target.descriptorSet;
    sampling={static_cast<float>(width)/size,static_cast<float>(height)/size,1,.5f/static_cast<float>(std::min(width,height))};
    return true;
}

void VulkanRenderBackendImpl::endWorldSceneColorPass() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    resolveWorldTemporalScene();
    clearWorldSurfaceBuffer();
    if (!frameActive || !worldSceneColorPassActive) return;

    FrameResources& frame = frames[currentFrame];
    const float post[]{worldDisplayReady?0.f:worldBloomStrength,worldDisplayReady?-1.f:worldSceneRadianceExposure};
    endWorldRecordingRenderPass(frame.commandBuffer);
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
    vkCmdPushConstants(worldRecordingCommand(frame.commandBuffer),worldSceneColorPipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,0u,sizeof(post),post);

        vkCmdBindPipeline(
            worldRecordingCommand(frame.commandBuffer),
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            worldSceneColorPipeline);
        const VkDescriptorSet descriptorSet = worldTemporalOutput ? worldTemporalOutput :
            editorSurface.frames[currentFrame].linearDescriptorSet;
        vkCmdBindDescriptorSets(
            worldRecordingCommand(frame.commandBuffer),
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
            worldRecordingCommand(frame.commandBuffer),
            0u,
            1u,
            &viewport);
        vkCmdSetScissor(
            worldRecordingCommand(frame.commandBuffer),
            0u,
            1u,
            &scissor);
        vkCmdDraw(
            worldRecordingCommand(frame.commandBuffer),
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
    beginWorldRecordingRenderPass(
        frame.commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    mainRenderPassActive = true;
    vkCmdPushConstants(worldRecordingCommand(frame.commandBuffer),worldSceneColorPipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,0u,sizeof(post),post);


    vkCmdBindPipeline(
        worldRecordingCommand(frame.commandBuffer),
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        worldSceneColorPipeline);
    const VkDescriptorSet descriptorSet = worldTemporalOutput ? worldTemporalOutput :
        worldSceneColorDescriptorSets[currentFrame];
    vkCmdBindDescriptorSets(
        worldRecordingCommand(frame.commandBuffer),
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
    vkCmdSetViewport(worldRecordingCommand(frame.commandBuffer), 0u, 1u, &viewport);
    vkCmdSetScissor(worldRecordingCommand(frame.commandBuffer), 0u, 1u, &scissor);
    vkCmdDraw(worldRecordingCommand(frame.commandBuffer), 3u, 1u, 0u, 0u);

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
    const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3*2*kFramesInFlight*kMaxScreenFeedbackViews},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2*kFramesInFlight*kMaxScreenFeedbackViews}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=2*kFramesInFlight*kMaxScreenFeedbackViews;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    requireSceneColorVk(vkCreateDescriptorPool(device,&pool,nullptr,&feedbackPool),"screen feedback descriptor pool");
    for(auto& frameSets : feedbackSets) for(auto& sets : frameSets) {
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
    feedbackWidth=feedbackHeight=0;feedbackSequence=0;feedbackLast={};feedbackSets={};feedbackViewCount=0;
}

void VulkanRenderBackendImpl::endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame& input) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    using namespace engine::render;
    validateScreenFeedback(input);
    if(!worldSceneColorPassActive)throw std::runtime_error("Screen feedback requires an active world pass");
    const bool editor=activeEditorSurface!=0;
    auto* editorTarget=editor?&editorSurfaces.at(activeEditorSurface):nullptr;
    resolveWorldTemporalScene();
    const VkImageView sceneView=worldTemporalOutputView?worldTemporalOutputView:
        editor?editorTarget->frames[currentFrame].linearColorView:worldSceneColorViews[currentFrame];
    endWorldSceneColorPass();
    ensureScreenFeedbackResources(input.width,input.height);
    auto* mask=ensureWorldTextureRaw(input.maskKey.c_str(),nullptr,input.maskRgba,input.maskWidth,input.maskHeight,33071,33071,false);
    if(!mask)throw std::runtime_error("Could not upload screen feedback ingredient");
    if(input.seed){feedbackSequence=input.sequence;feedbackHistory=0;feedbackDisplayInput=0;feedbackLast={};feedbackLast.count=1;feedbackLast.passes[0].rectangle={0,0,float(input.width),float(input.height)};}
    else if(feedbackSequence!=input.sequence)throw std::runtime_error("Screen feedback must be seeded before playback");
    auto& frame=frames[currentFrame];auto command=frame.commandBuffer;
    endWorldRecordingRenderPass(command);mainRenderPassActive=false;editorSurfacePassActive=false;
    // A transition can seed and present feedback in the same host frame.
    // Recorded commands retain each view's descriptor contents until the frame fence.
    if(feedbackViewCount>=kMaxScreenFeedbackViews)throw std::runtime_error("Screen feedback view descriptor budget exceeded");
    auto& sets=feedbackSets[currentFrame][feedbackViewCount++];
    for(int history=0;history<2;++history) {
        const VkDescriptorImageInfo images[]={{worldSceneColorSampler,sceneView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {worldSceneColorSampler,feedbackImages[history].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mask->sampler,mask->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorBufferInfo buffer{frame.transient.buffer,0,sizeof(ScreenFeedbackUniform)};
        std::array<VkWriteDescriptorSet,4> writes{};
        for(std::uint32_t i=0;i<4;++i){auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[history];write.dstBinding=i;write.descriptorCount=1;write.descriptorType=i==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;if(i==3)write.pBufferInfo=&buffer;else write.pImageInfo=&images[i];}
        vkUpdateDescriptorSets(device,4,writes.data(),0,nullptr);
    }
    auto draw=[&](int mode,int history,int width,int height,const ScreenFeedbackUpdate& update,VkPipeline pipeline) {
        const ScreenFeedbackUniform uniform{{float(input.width),float(input.height),float(mode),float(update.count)},update.passes,{worldDisplayReady?0.f:worldBloomStrength,worldDisplayReady?-1.f:worldSceneRadianceExposure,0,0}};
        VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;
        if(!writeTransient(&uniform,sizeof(uniform),physicalDeviceProperties.limits.minUniformBufferOffsetAlignment,buffer,offset))throw std::runtime_error("Screen feedback uniform arena exhausted");
        const auto dynamicOffset=static_cast<std::uint32_t>(offset);
        vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_GRAPHICS,feedbackLayout,0,1,&sets[history],1,&dynamicOffset);
        VkViewport viewport{0,0,float(width),float(height),0,1};
        VkRect2D scissor{{0,0},{static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height)}};
        vkCmdSetViewport(worldRecordingCommand(command),0,1,&viewport);vkCmdSetScissor(worldRecordingCommand(command),0,1,&scissor);
        vkCmdDraw(worldRecordingCommand(command),3,1,0,0);
    };
    auto historyDraw=[&](int mode,int history,int target,const ScreenFeedbackUpdate& update){
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=feedbackRenderPass;pass.framebuffer=feedbackFramebuffers[target];pass.renderArea.extent={static_cast<std::uint32_t>(input.width),static_cast<std::uint32_t>(input.height)};
        beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);draw(mode,history,input.width,input.height,update,feedbackPipeline);endWorldRecordingRenderPass(command);
    };
    if(input.seed)historyDraw(0,1,0,{});
    for(const auto& update:input.updates){feedbackDisplayInput=feedbackHistory;feedbackHistory=1-feedbackHistory;feedbackLast=update;historyDraw(1,feedbackDisplayInput,feedbackHistory,update);}
    if(editor)beginEditorSurfaceDisplayPass(*editorTarget);
    else {
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=loadRenderPass;pass.framebuffer=framebuffers[acquiredImage];pass.renderArea.extent=swapchainExtent;
        beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);mainRenderPassActive=true;
    }
    // Seed also redraws the ordinary scene, because an editor display pass clears.
    draw(input.seed && input.updates.empty()?0:2,feedbackDisplayInput,
        editor?editorTarget->width:int(swapchainExtent.width),editor?editorTarget->height:int(swapchainExtent.height),feedbackLast,feedbackDisplayPipeline);
    invalidateSceneColorBindings(*this);
}

void VulkanRenderBackendImpl::resumeWorldSceneColor() {
    VkExtent2D extent=swapchainExtent;
    auto framebuffer=worldSceneColorFramebuffers.at(currentFrame*swapchainImages.size()+acquiredImage);
    if(activeEditorSurface) {
        auto& target=editorSurfaces.at(activeEditorSurface);
        extent={std::uint32_t(target.width),std::uint32_t(target.height)};
        framebuffer=target.frames[currentFrame].linearFramebuffer;
    }
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass=worldSceneColorLoadRenderPass;pass.framebuffer=framebuffer;pass.renderArea.extent=extent;
    beginWorldRecordingRenderPass(frames[currentFrame].commandBuffer,&pass,VK_SUBPASS_CONTENTS_INLINE);
    invalidateSceneColorBindings(*this);
}
bool VulkanRenderBackendImpl::beginWorldSurfaceBuffer(const float* matrix,int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(width<=0 || height<=0)return false;
    const auto surfaceWidth=static_cast<std::uint32_t>(width),surfaceHeight=static_cast<std::uint32_t>(height);
    const auto view=engine::render::packWorldContactVisibility(matrix,width,height,.8f,.015f);
    const auto extent=activeEditorSurface?VkExtent2D{std::uint32_t(editorSurfaces.at(activeEditorSurface).width),
        std::uint32_t(editorSurfaces.at(activeEditorSurface).height)}:swapchainExtent;
    if(!frameActive || !worldSceneColorPassActive || worldSurfaceActive || worldHazeActive || worldShadowActive ||
       worldContact[8][3]!=0 || worldScreenReflection[8][3]!=0 ||
       worldLocalReflection[0][3]<0 || surfaceWidth>extent.width || surfaceHeight>extent.height ||
       (worldProbeSurfaceKey.empty() && !(worldPlanarReflection[4][3]<0) && (surfaceWidth!=extent.width || surfaceHeight!=extent.height)))return false;
    const bool planar=worldPlanarReflection[4][3]<0;
    if(planar && (width!=worldPlanarReflectionView.width || height!=worldPlanarReflectionView.heightPixels ||
       !std::equal(worldPlanarReflectionView.viewProjection.begin(),worldPlanarReflectionView.viewProjection.end(),matrix)))return false;
    worldSurfacePlanar=planar;
    if(!ensureWorldSurfacePipeline())return false;
    worldClusteredView={};
    worldSurfaceComposed=worldSurfaceCompositionFinished=worldSurfaceCompositionCapture=false;
    worldSurfaceReady=false;worldMotionWords={{}};worldMotionUploaded=false;
    if(worldSurfaceFrame!=frameCounter){worldSurfaceFrame=frameCounter;worldSurfaceSlot=0;worldSurfaceAllocationSlots={};}
    // Admit two scene views plus their cold cube faces during a handoff/prewarm.
    if(worldSurfaceSlot>=32)throw std::runtime_error("Surface captures per frame exceeded");
    const std::string key=worldProbeSurfaceKey.empty()
        ?(planar?std::string("planar/"):std::string())+std::to_string(currentFrame)+"/"+std::to_string(worldSurfaceAllocationSlots[planar?1:0]++):worldProbeSurfaceKey;
    ++worldSurfaceSlot;
    auto found=worldSurfaceTargets.find(key);
    if(found!=worldSurfaceTargets.end() &&
       (found->second.planes[0].width!=width || found->second.planes[0].height!=height)) {
        requireSceneColorVk(vkDeviceWaitIdle(device),"wait surface replacement");
        auto& old=found->second;
        vkDestroyFramebuffer(device,old.framebuffer,nullptr);
        for(unsigned i=old.probeScratch?3u:0u;i<6;++i) {
            auto& plane=old.planes[i];
            vkFreeDescriptorSets(device,descriptorPool,1,&plane.descriptorSet);destroyTexture(plane);
        }
        if(!old.probeScratch){vkFreeDescriptorSets(device,descriptorPool,1,&old.depth.descriptorSet);destroyTexture(old.depth);}
        worldSurfaceTargets.erase(found);found=worldSurfaceTargets.end();
    }
    if(found==worldSurfaceTargets.end()) {
        SurfaceTarget target;target.probeScratch=!worldProbeSurfaceKey.empty();
        const auto allocate=[&](Texture& t,bool depth,unsigned plane=0) {
            const bool shared=target.probeScratch && (depth || plane<3);
            const auto scratchKey=std::to_string(width)+"/"+(depth?"depth":std::to_string(plane));
            const auto scratch=worldProbeScratch.find(scratchKey);
            if(shared && scratch!=worldProbeScratch.end()){t=scratch->second;return;}
            try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
            info.format=depth?depthFormat:kWorldSurfaceFormats[plane];
            info.extent={std::uint32_t(width),std::uint32_t(height),1};
            info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|
                VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            requireSceneColorVk(vkCreateImage(device,&info,nullptr,&t.image),"create surface image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,t.image,&needs);
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=needs.size;
            memory.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireSceneColorVk(vkAllocateMemory(device,&memory,nullptr,&t.memory),"allocate surface image");
            requireSceneColorVk(vkBindImageMemory(device,t.image,t.memory,0),"bind surface image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=t.image;
            view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
            view.subresourceRange={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
            requireSceneColorVk(vkCreateImageView(device,&view,nullptr,&t.view),"view surface image");
            t.width=width;t.height=height;t.format=info.format;t.allocationBytes=needs.size;
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            requireSceneColorVk(vkCreateSampler(device,&sampler,nullptr,&t.sampler),"sample surface image");
            t.descriptorSet=createPlanarDescriptor(t);
            if(shared)worldProbeScratch.emplace(scratchKey,t);
            } catch(...) {
                if(t.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&t.descriptorSet);
                destroyTexture(t);t={};throw;
            }
        };
        try {
            for(unsigned i=0;i<6;++i)allocate(target.planes[i],false,i);
            allocate(target.depth,true);
            std::array<VkImageView,7> views{};
            for(unsigned i=0;i<6;++i)views[i]=target.planes[i].view;views[6]=target.depth.view;
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=worldSurfaceRenderPass;
            fb.attachmentCount=7;fb.pAttachments=views.data();fb.width=width;fb.height=height;fb.layers=1;
            requireSceneColorVk(vkCreateFramebuffer(device,&fb,nullptr,&target.framebuffer),"create surface framebuffer");
        } catch(...) {
            if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
            for(unsigned i=target.probeScratch?3u:0u;i<6;++i) {
                auto& plane=target.planes[i];
                if(plane.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&plane.descriptorSet);
                destroyTexture(plane);
            }
            if(!target.probeScratch) {
                if(target.depth.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&target.depth.descriptorSet);
                destroyTexture(target.depth);
            }
            throw;
        }
        found=worldSurfaceTargets.emplace(key,target).first;
    }
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reuse.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    reuse.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0,1,&reuse,0,nullptr,0,nullptr);
    std::array<VkClearValue,7> clear{};clear[6].depthStencil={1,0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldSurfaceRenderPass;
    pass.framebuffer=found->second.framebuffer;pass.renderArea.extent={std::uint32_t(width),std::uint32_t(height)};pass.clearValueCount=7;pass.pClearValues=clear.data();
    beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);invalidateSceneColorBindings(*this);
    worldSurfaceTarget=&found->second;worldSurfaceView=view;worldSurfaceTarget->view=view;worldSurfaceActive=true;return true;
}
bool VulkanRenderBackendImpl::endWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!worldSurfaceActive)return false;
    endWorldRecordingRenderPass(frames[currentFrame].commandBuffer);
    resumeWorldSceneColor();
    worldSurfaceActive=false;worldSurfaceReady=true;worldSurfaceDepth=&worldSurfaceTarget->depth;
    return true;
}
void VulkanRenderBackendImpl::clearWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    worldSurfaceComposing=worldSurfaceComposed=worldSurfaceCompositionFinished=worldSurfaceCompositionCapture=false;
    worldClusteredView={};
    worldSurfaceComposed=worldSurfaceCompositionFinished=worldSurfaceCompositionCapture=false;worldClusteredDescriptors={};worldStoredSurfaceDescriptors={};
    if(worldSurfaceActive)endWorldSurfaceBuffer();
    worldProbeSurfaces.cancel();worldProbeSurfaceKey.clear();
    if(worldScreenReflection[10][3]==-8)worldScreenReflection[10][3]=0;
    worldSurfacePlanar=false;
    worldSurfaceReady=worldSurfaceContact=false;worldSurfaceTarget=nullptr;worldSurfaceDepth=nullptr;worldSurfaceView={};
}
bool VulkanRenderBackendImpl::setWorldSurfaceMotion(const engine::render::WorldMotionMatrix& view,
    std::span<const engine::render::WorldMotionSample> samples) {
    if(!worldSurfaceActive)return false;
    engine::render::validateWorldMotionSamples(samples);
    std::vector<std::uint32_t> offsets;offsets.reserve(samples.size());
    for(const auto& sample:samples) {
        std::uint32_t index=0;
        const auto& palette=sample.previous->deformation;
        if(!palette.empty() && !uploadWorldSkinPalette(palette.data(),palette.size(),index,sample.previous->deformationVersion))
            throw std::runtime_error("Previous deformation upload budget exhausted");
        offsets.push_back(index*4u);
    }
    engine::render::packWorldMotionDraw(view,samples,worldMotionWords,offsets);worldMotionUploaded=false;return true;
}
bool VulkanRenderBackendImpl::captureWorldSurfaceMotion() {
    if(!frameActive || !worldSceneColorPassActive || !worldSurfaceReady || worldSurfaceActive || worldMotionReadback.buffer)return false;
    worldMotionWidth=worldSurfaceTarget->planes[4].width;worldMotionHeight=worldSurfaceTarget->planes[4].height;
    const VkDeviceSize pixels=VkDeviceSize(worldMotionWidth)*worldMotionHeight;
    worldMotionSurfaceBytes=0;
    for(const auto& t:worldSurfaceTarget->planes)
        worldMotionSurfaceBytes+=VkDeviceSize(t.width)*t.height*((t.format==VK_FORMAT_R16G16B16A16_SFLOAT||t.format==VK_FORMAT_R32G32_UINT)?8:4);
    constexpr unsigned planes[]{4,5,1,2,0};
    constexpr unsigned offsets[]{0,8,16,20,24};
    worldMotionReadback=createBuffer(pixels*28,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    for(unsigned i=0;i<5;++i) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image=worldSurfaceTarget->planes[planes[i]].image;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.bufferOffset=pixels*offsets[i];
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={std::uint32_t(worldMotionWidth),std::uint32_t(worldMotionHeight),1};
        vkCmdCopyImageToBuffer(command,b.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,worldMotionReadback.buffer,1,&copy);
        std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&b);
    }
    resumeWorldSceneColor();return true;
}
engine::render::WorldMotionImage VulkanRenderBackendImpl::readWorldSurfaceMotion() {
    engine::render::WorldMotionImage result;
    if(frameActive || !worldMotionReadback.buffer)return result;
    presentPendingFrame();
    requireSceneColorVk(vkDeviceWaitIdle(device),"wait motion readback");
    result.width=worldMotionWidth;result.height=worldMotionHeight;
    const auto pixels=std::size_t(result.width)*result.height;
    result.velocity.resize(pixels);result.identity.resize(pixels);result.identityMetadata.resize(pixels);result.temporalMaterial.resize(pixels);
    result.albedo.resize(pixels);result.normal.resize(pixels);result.materialClass.resize(pixels);result.material.resize(pixels);result.surfaceTexelBytes=worldMotionSurfaceBytes;
    const auto* bytes=static_cast<const unsigned char*>(worldMotionReadback.mapped);
    const auto* half=reinterpret_cast<const std::uint16_t*>(bytes);
    for(std::size_t i=0;i<pixels;++i)for(unsigned c=0;c<4;++c)
        result.velocity[i][c]=engine::render::decodeMotionHalf(half[4*i+c]);
    for(std::size_t i=0;i<pixels;++i)std::memcpy(&result.identity[i],bytes+pixels*8+i*8,4);
    for(std::size_t i=0;i<pixels;++i) {
        std::uint32_t metadata=0;std::memcpy(&metadata,bytes+pixels*8+i*8+4,4);
        result.identityMetadata[i]=metadata;result.temporalMaterial[i]=std::uint8_t(metadata>>8);
    }
    const auto* normals=reinterpret_cast<const std::uint32_t*>(bytes+pixels*16);
    std::memcpy(result.albedo.data(),bytes+pixels*24,pixels*4);
    std::memcpy(result.normal.data(),normals,pixels*4);
    for(std::size_t i=0;i<pixels;++i)result.materialClass[i]=std::uint8_t(normals[i]>>30);
    std::memcpy(result.material.data(),bytes+pixels*20,pixels*4);
    destroyBuffer(worldMotionReadback);return result;
}

void VulkanRenderBackendImpl::ensureWorldHazeStencilPass() {
    if(worldHazeStencilRenderPass)return;
    for(auto format:{VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physicalDevice,format,&properties);
        if(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){worldHazeStencilFormat=format;break;}
    }
    if(worldHazeStencilFormat==VK_FORMAT_UNDEFINED)throw std::runtime_error("Haze stencil format unavailable");
    std::array<VkAttachmentDescription,2> attachments{};
    auto& color=attachments[0];color.format=VK_FORMAT_R16G16B16A16_SFLOAT;color.samples=VK_SAMPLE_COUNT_1_BIT;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout=color.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    auto& stencil=attachments[1];stencil.format=worldHazeStencilFormat;stencil.samples=VK_SAMPLE_COUNT_1_BIT;
    stencil.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;stencil.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    stencil.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;stencil.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    stencil.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;stencil.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference stencilRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1;sub.pColorAttachments=&colorRef;sub.pDepthStencilAttachment=&stencilRef;
    const VkPipelineStageFlags stages=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags access=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const std::array<VkSubpassDependency,2> dependencies{{
        {VK_SUBPASS_EXTERNAL,0,stages|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,stages,access|VK_ACCESS_SHADER_READ_BIT,access,0},
        {0,VK_SUBPASS_EXTERNAL,stages,stages|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,access,access|VK_ACCESS_SHADER_READ_BIT,0}}};
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=2;info.pAttachments=attachments.data();
    info.subpassCount=1;info.pSubpasses=&sub;info.dependencyCount=2;info.pDependencies=dependencies.data();
    requireSceneColorVk(vkCreateRenderPass(device,&info,nullptr,&worldHazeStencilRenderPass),"create haze stencil render pass");
}
void VulkanRenderBackendImpl::clearWorldHazeStencilTargets(std::uint64_t owner) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    for(auto it=worldHazeStencilTargets.begin();it!=worldHazeStencilTargets.end();) {
        if(owner!=~std::uint64_t{0} && it->second.owner!=owner){++it;continue;}
        auto& target=it->second;if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        destroyTexture(target.depth);it=worldHazeStencilTargets.erase(it);
    }
}
bool VulkanRenderBackendImpl::beginWorldLocalHazeComposition(int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!frameActive || !worldSceneColorPassActive || worldHazeActive || worldHazeStencilActive ||
       width<=0 || height<=0 || width>8192 || height>8192)throw std::runtime_error("Invalid haze composition scope");
    ensureWorldHazeStencilPass();
    const auto key=std::to_string(currentFrame)+"/"+std::to_string(activeEditorSurface)+"/"+std::to_string(width)+"/"+std::to_string(height);
    auto found=worldHazeStencilTargets.find(key);
    if(found==worldHazeStencilTargets.end()) {
        HazeStencilTarget target;target.owner=activeEditorSurface;auto& depth=target.depth;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
            info.format=worldHazeStencilFormat;info.extent={std::uint32_t(width),std::uint32_t(height),1};
            info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            requireSceneColorVk(vkCreateImage(device,&info,nullptr,&depth.image),"create haze stencil image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,depth.image,&needs);
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=needs.size;
            memory.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            requireSceneColorVk(vkAllocateMemory(device,&memory,nullptr,&depth.memory),"allocate haze stencil image");
            requireSceneColorVk(vkBindImageMemory(device,depth.image,depth.memory,0),"bind haze stencil image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=depth.image;
            view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=worldHazeStencilFormat;
            view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};
            requireSceneColorVk(vkCreateImageView(device,&view,nullptr,&depth.view),"view haze stencil image");
            depth.width=width;depth.height=height;depth.format=worldHazeStencilFormat;depth.allocationBytes=needs.size;
            auto color=activeEditorSurface?editorSurfaces.at(activeEditorSurface).frames[currentFrame].linearColorView:worldSceneColorViews[currentFrame];
            const std::array<VkImageView,2> views{color,depth.view};
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=worldHazeStencilRenderPass;
            fb.attachmentCount=2;fb.pAttachments=views.data();fb.width=width;fb.height=height;fb.layers=1;
            requireSceneColorVk(vkCreateFramebuffer(device,&fb,nullptr,&target.framebuffer),"create haze stencil framebuffer");
        } catch(...) {if(target.framebuffer)vkDestroyFramebuffer(device,target.framebuffer,nullptr);destroyTexture(depth);throw;}
        found=worldHazeStencilTargets.emplace(key,target).first;
    }
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    std::array<VkClearValue,2> clear{};clear[1].depthStencil={1,1};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=worldHazeStencilRenderPass;
    pass.framebuffer=found->second.framebuffer;pass.renderArea.extent={std::uint32_t(width),std::uint32_t(height)};
    pass.clearValueCount=2;pass.pClearValues=clear.data();
    beginWorldRecordingRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);invalidateSceneColorBindings(*this);
    worldHazeStencilActive=true;return true;
}
void VulkanRenderBackendImpl::endWorldLocalHazeComposition() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!worldHazeStencilActive)return;
    endWorldRecordingRenderPass(frames[currentFrame].commandBuffer);
    worldHazeStencilActive=false;resumeWorldSceneColor();
}

bool VulkanRenderBackendImpl::captureWorldProbeRadiance(std::span<const std::uint64_t> ids) {
    if(!frameActive || !worldSceneColorPassActive || placedProbes.active() || worldProbeRadianceReadback.buffer)return false;
    const auto found=worldTextures.find("placed-reflection/resident/"+std::to_string(engine::render::WorldPlacedProbeCache::atlasExtent));
    if(found==worldTextures.end() || found->second.format!=VK_FORMAT_B10G11R11_UFLOAT_PACK32)return false;
    const auto& image=found->second;
    worldProbeDiagnostic={};worldProbeDiagnostic.width=image.width;worldProbeDiagnostic.height=image.height;
    worldProbeDiagnostic.selection=placedProbes.select(ids);
    worldProbeRadianceReadback=createBuffer(VkDeviceSize(image.width)*image.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);
    const auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent={unsigned(image.width),unsigned(image.height),1};
    vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,worldProbeRadianceReadback.buffer,1,&copy);
    std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
    resumeWorldSceneColor();return true;
}
engine::render::WorldProbeRadianceImage VulkanRenderBackendImpl::readWorldProbeRadiance() {
    if(frameActive || !worldProbeRadianceReadback.buffer)return {};
    presentPendingFrame();requireSceneColorVk(vkDeviceWaitIdle(device),"wait probe readback");
    auto result=worldProbeDiagnostic;result.rgb111110.resize(std::size_t(result.width)*result.height);
    std::memcpy(result.rgb111110.data(),worldProbeRadianceReadback.mapped,result.rgb111110.size()*4);
    destroyBuffer(worldProbeRadianceReadback);worldProbeDiagnostic={};return result;
}
