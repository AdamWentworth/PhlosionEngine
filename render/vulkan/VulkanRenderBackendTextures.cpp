#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <stb_image.h>

#include "engine/render/SpriteTextureCardArt.h"

namespace {

void requireVkTexture(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result) + ".");
    }
}

VkSamplerAddressMode addressModeFromGl(int wrap) {
    if (wrap == 33071) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (wrap == 33648) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

} // namespace

VulkanRenderBackendImpl::Texture VulkanRenderBackendImpl::createTexture(
    const unsigned char* rgba,
    int width,
    int height,
    bool srgb,
    int wrapS,
    int wrapT,
    bool createStandaloneDescriptor,
    const IRenderBackend::WorldTextureMipLevel* authoredMipLevels,
    std::uint32_t authoredMipLevelCount) {
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width) *
                                   static_cast<VkDeviceSize>(height) * 4u;
    return createTextureWithFormat(
        rgba,
        byteCount,
        width,
        height,
        format,
        wrapS,
        wrapT,
        createStandaloneDescriptor,
        authoredMipLevels,
        authoredMipLevelCount);
}

VulkanRenderBackendImpl::Texture VulkanRenderBackendImpl::createTextureRgba16Float(
    const std::uint16_t* rgba16f,
    int width,
    int height,
    int wrapS,
    int wrapT,
    bool createStandaloneDescriptor) {
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width) *
                                   static_cast<VkDeviceSize>(height) * 4u *
                                   sizeof(std::uint16_t);
    return createTextureWithFormat(
        rgba16f,
        byteCount,
        width,
        height,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        wrapS,
        wrapT,
        createStandaloneDescriptor);
}

VulkanRenderBackendImpl::Texture VulkanRenderBackendImpl::createTextureWithFormat(
    const void* pixels,
    VkDeviceSize byteCount,
    int width,
    int height,
    VkFormat format,
    int wrapS,
    int wrapT,
    bool createStandaloneDescriptor,
    const IRenderBackend::WorldTextureMipLevel* authoredMipLevels,
    std::uint32_t authoredMipLevelCount) {
    if (!pixels || byteCount == 0u || width <= 0 || height <= 0 ||
        format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Vulkan texture upload received invalid pixel data.");
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
    constexpr VkFormatFeatureFlags kRequiredFormatFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((formatProperties.optimalTilingFeatures & kRequiredFormatFeatures) !=
        kRequiredFormatFeatures) {
        throw std::runtime_error(
            "Vulkan texture format does not support optimal sampled linear filtering: " +
            std::to_string(static_cast<int>(format)) + ".");
    }

    bool authoredMipChainValid =
        authoredMipLevels && authoredMipLevelCount > 0u;
    VkDeviceSize stagingByteCount = 0u;
    for (std::uint32_t level = 0u;
         authoredMipChainValid && level < authoredMipLevelCount;
         ++level) {
        const auto& mip = authoredMipLevels[level];
        authoredMipChainValid =
            mip.rgba && mip.width > 0 && mip.height > 0 &&
            (level != 0u || (mip.width == width && mip.height == height));
        if (authoredMipChainValid) {
            stagingByteCount += static_cast<VkDeviceSize>(mip.width) *
                                static_cast<VkDeviceSize>(mip.height) * 4u;
        }
    }
    const std::uint32_t mipLevelCount =
        authoredMipChainValid ? authoredMipLevelCount : 1u;
    if (!authoredMipChainValid) stagingByteCount = byteCount;

    Buffer staging = createBuffer(
        stagingByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::vector<VkBufferImageCopy> copies;
    copies.reserve(mipLevelCount);
    if (authoredMipChainValid) {
        VkDeviceSize offset = 0u;
        for (std::uint32_t level = 0u; level < mipLevelCount; ++level) {
            const auto& mip = authoredMipLevels[level];
            const VkDeviceSize mipBytes =
                static_cast<VkDeviceSize>(mip.width) *
                static_cast<VkDeviceSize>(mip.height) * 4u;
            std::memcpy(
                static_cast<unsigned char*>(staging.mapped) + offset,
                mip.rgba,
                static_cast<std::size_t>(mipBytes));
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = level;
            copy.imageSubresource.layerCount = 1u;
            copy.imageExtent = {
                static_cast<std::uint32_t>(mip.width),
                static_cast<std::uint32_t>(mip.height),
                1u};
            copies.push_back(copy);
            offset += mipBytes;
        }
    } else {
        std::memcpy(staging.mapped, pixels, static_cast<std::size_t>(byteCount));
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1u;
        copy.imageExtent = {
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            1u};
        copies.push_back(copy);
    }
    if (!staging.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = staging.memory;
        range.size = VK_WHOLE_SIZE;
        requireVkTexture(vkFlushMappedMemoryRanges(device, 1u, &range),
                         "vkFlushMappedMemoryRanges(texture)");
    }

    Texture out;
    out.format = format;
    out.width = width;
    out.height = height;
    try {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            1u};
        imageInfo.mipLevels = mipLevelCount;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        requireVkTexture(vkCreateImage(device, &imageInfo, nullptr, &out.image),
                         "vkCreateImage(texture)");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, out.image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        requireVkTexture(vkAllocateMemory(device, &allocation, nullptr, &out.memory),
                         "vkAllocateMemory(texture)");
        requireVkTexture(vkBindImageMemory(device, out.image, out.memory, 0u),
                         "vkBindImageMemory(texture)");

        VkCommandBuffer commandBuffer = beginOneTimeCommands();
        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = 0u;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = out.image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = mipLevelCount;
        toTransfer.subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0u,
                             0u,
                             nullptr,
                             0u,
                             nullptr,
                             1u,
                             &toTransfer);

        vkCmdCopyBufferToImage(commandBuffer,
                               staging.buffer,
                               out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()),
                               copies.data());

        VkImageMemoryBarrier toShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = out.image;
        toShader.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0u,
                             0u,
                             nullptr,
                             0u,
                             nullptr,
                             1u,
                             &toShader);
        endOneTimeCommands(commandBuffer);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = out.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = mipLevelCount;
        viewInfo.subresourceRange.layerCount = 1u;
        requireVkTexture(vkCreateImageView(device, &viewInfo, nullptr, &out.view),
                         "vkCreateImageView(texture)");

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = addressModeFromGl(wrapS);
        samplerInfo.addressModeV = addressModeFromGl(wrapT);
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = samplerAnisotropyEnabled ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = maxSamplerAnisotropy;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod =
            static_cast<float>(mipLevelCount > 0u ? mipLevelCount - 1u : 0u);
        requireVkTexture(vkCreateSampler(device, &samplerInfo, nullptr, &out.sampler),
                         "vkCreateSampler");

        if (createStandaloneDescriptor) {
            VkDescriptorSetAllocateInfo descriptorAllocate{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            descriptorAllocate.descriptorPool = descriptorPool;
            descriptorAllocate.descriptorSetCount = 1u;
            descriptorAllocate.pSetLayouts = &textureSetLayout;
            requireVkTexture(vkAllocateDescriptorSets(
                                 device, &descriptorAllocate, &out.descriptorSet),
                             "vkAllocateDescriptorSets(texture)");
            VkDescriptorImageInfo imageDescriptor{};
            imageDescriptor.sampler = out.sampler;
            imageDescriptor.imageView = out.view;
            imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = out.descriptorSet;
            write.dstBinding = 0u;
            write.descriptorCount = 1u;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageDescriptor;
            vkUpdateDescriptorSets(device, 1u, &write, 0u, nullptr);
        }
    } catch (...) {
        destroyBuffer(staging);
        destroyTexture(out);
        throw;
    }
    destroyBuffer(staging);
    return out;
}

void VulkanRenderBackendImpl::destroyTexture(Texture& texture) {
    if (device == VK_NULL_HANDLE) return;
    if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture.sampler, nullptr);
    if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device, texture.view, nullptr);
    if (texture.image != VK_NULL_HANDLE) vkDestroyImage(device, texture.image, nullptr);
    if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(device, texture.memory, nullptr);
    texture = {};
}

VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::ensureSpriteTexture(
    const std::string& texturePath) {
    if (texturePath.empty()) return &fallbackSpriteTexture;
    auto existing = spriteTextures.find(texturePath);
    if (existing != spriteTextures.end()) return &existing->second;

    const std::string source = engine::render::sprite_card_art::sourcePathFromProxy(texturePath);
    const std::filesystem::path resolved =
        engine::render::sprite_card_art::resolveExistingPath(source);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(false);
    unsigned char* pixels = stbi_load(
        resolved.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return &fallbackSpriteTexture;
    }

    Texture uploaded;
    try {
        uploaded = createTexture(pixels, width, height, false, 33071, 33071, true);
    } catch (...) {
        stbi_image_free(pixels);
        throw;
    }
    stbi_image_free(pixels);
    return &spriteTextures.emplace(texturePath, std::move(uploaded)).first->second;
}

void VulkanRenderBackendImpl::prewarmSpriteTexture(const char* texturePath) {
    if (!initialized || !texturePath || texturePath[0] == '\0') return;
    (void)ensureSpriteTexture(texturePath);
}
