#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <stb_image.h>

#include "engine/core/Environment.h"
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

bool worldTextureMipChainEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get(
            "PHLOSION_BACKEND_WORLD_TEXTURE_MIPS");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        return raw != "0" && raw != "false" && raw != "FALSE" &&
               raw != "off" && raw != "OFF";
    }();
    return enabled;
}

struct CpuMipLevel {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

float srgbByteToLinear(unsigned char value) {
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f
        ? c / 12.92f
        : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

unsigned char linearToSrgbByte(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float srgb = c <= 0.0031308f
        ? c * 12.92f
        : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::clamp(
        static_cast<int>(std::lround(srgb * 255.0f)),
        0,
        255));
}

int wrapTexelIndex(int index, int size, int wrapMode) {
    if (size <= 1) return 0;
    if (wrapMode == 33071) {
        return std::clamp(index, 0, size - 1);
    }
    if (wrapMode == 33648) {
        const int period = size * 2;
        int wrapped = index % period;
        if (wrapped < 0) wrapped += period;
        return wrapped >= size ? period - 1 - wrapped : wrapped;
    }
    int wrapped = index % size;
    if (wrapped < 0) wrapped += size;
    return wrapped;
}

std::vector<CpuMipLevel> buildRgbaMipChain(
    const unsigned char* pixels,
    int width,
    int height,
    int wrapS,
    int wrapT,
    bool srgb) {
    std::vector<CpuMipLevel> chain;
    if (!pixels || width <= 0 || height <= 0) return chain;
    chain.push_back(CpuMipLevel{
        width,
        height,
        std::vector<unsigned char>(
            pixels,
            pixels + static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 4u)});
    while (chain.back().width > 1 || chain.back().height > 1) {
        const CpuMipLevel& previous = chain.back();
        CpuMipLevel next;
        next.width = std::max(previous.width / 2, 1);
        next.height = std::max(previous.height / 2, 1);
        next.rgba.resize(
            static_cast<std::size_t>(next.width) *
            static_cast<std::size_t>(next.height) * 4u);
        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                float sums[4]{};
                for (int oy = 0; oy < 2; ++oy) {
                    const int sourceY = wrapTexelIndex(
                        y * 2 + oy,
                        previous.height,
                        wrapT);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int sourceX = wrapTexelIndex(
                            x * 2 + ox,
                            previous.width,
                            wrapS);
                        const std::size_t sourceOffset =
                            (static_cast<std::size_t>(sourceY) *
                                 static_cast<std::size_t>(previous.width) +
                             static_cast<std::size_t>(sourceX)) * 4u;
                        for (int channel = 0; channel < 3; ++channel) {
                            sums[channel] += srgb
                                ? srgbByteToLinear(
                                      previous.rgba[sourceOffset + channel])
                                : static_cast<float>(
                                      previous.rgba[sourceOffset + channel]) /
                                      255.0f;
                        }
                        sums[3] += static_cast<float>(
                            previous.rgba[sourceOffset + 3u]) / 255.0f;
                    }
                }
                const std::size_t targetOffset =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(next.width) +
                     static_cast<std::size_t>(x)) * 4u;
                for (int channel = 0; channel < 3; ++channel) {
                    const float average = sums[channel] * 0.25f;
                    next.rgba[targetOffset + channel] = srgb
                        ? linearToSrgbByte(average)
                        : static_cast<unsigned char>(std::clamp(
                              static_cast<int>(
                                  std::lround(average * 255.0f)),
                              0,
                              255));
                }
                next.rgba[targetOffset + 3u] =
                    static_cast<unsigned char>(std::clamp(
                        static_cast<int>(
                            std::lround(sums[3] * 0.25f * 255.0f)),
                        0,
                        255));
            }
        }
        chain.push_back(std::move(next));
    }
    return chain;
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
    std::vector<CpuMipLevel> generatedMipChain;
    std::vector<IRenderBackend::WorldTextureMipLevel> generatedMipLevels;
    const bool rgba8 =
        format == VK_FORMAT_R8G8B8A8_SRGB ||
        format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!authoredMipChainValid && rgba8 && worldTextureMipChainEnabled()) {
        generatedMipChain = buildRgbaMipChain(
            static_cast<const unsigned char*>(pixels),
            width,
            height,
            wrapS,
            wrapT,
            format == VK_FORMAT_R8G8B8A8_SRGB);
        generatedMipLevels.reserve(generatedMipChain.size());
        for (const CpuMipLevel& mip : generatedMipChain) {
            generatedMipLevels.push_back({
                mip.rgba.data(),
                mip.width,
                mip.height});
        }
        if (generatedMipLevels.size() > 1u) {
            authoredMipLevels = generatedMipLevels.data();
            authoredMipLevelCount = static_cast<std::uint32_t>(
                generatedMipLevels.size());
            authoredMipChainValid = true;
            stagingByteCount = 0u;
            for (const auto& mip : generatedMipLevels) {
                stagingByteCount +=
                    static_cast<VkDeviceSize>(mip.width) *
                    static_cast<VkDeviceSize>(mip.height) * 4u;
            }
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
