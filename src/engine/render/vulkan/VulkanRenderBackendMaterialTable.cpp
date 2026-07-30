#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void updateMaterialRange(
    VulkanRenderBackendImpl& backend,
    VulkanRenderBackendImpl::FrameResources& frame,
    std::uint32_t firstMaterial,
    std::uint32_t materialCount,
    bool fillMissingWithFallback) {
    if (materialCount == 0u) return;
    namespace vulkan = engine::render::vulkan_backend;
    std::array<
        std::vector<VkDescriptorImageInfo>,
        vulkan::kWorldMaterialTextureCount> imageInfos;
    std::array<
        VkWriteDescriptorSet,
        vulkan::kWorldMaterialTextureCount> writes{};
    for (std::uint32_t binding = 0u; binding < imageInfos.size(); ++binding) {
        imageInfos[binding].resize(materialCount);
        for (std::uint32_t localIndex = 0u;
             localIndex < materialCount;
             ++localIndex) {
            const std::uint32_t materialIndex = firstMaterial + localIndex;
            VulkanRenderBackendImpl::WorldMaterial* material =
                materialIndex < backend.indexedWorldMaterials.size()
                ? backend.indexedWorldMaterials[materialIndex]
                : nullptr;
            if (!material && fillMissingWithFallback) {
                material = &backend.fallbackWorldMaterial;
            }
            if (!material || !material->textures[binding]) {
                throw std::runtime_error(
                    "Vulkan indexed world material table contains an incomplete texture set.");
            }
            const VulkanRenderBackendImpl::Texture& texture =
                *material->textures[binding];
            VkDescriptorImageInfo& imageInfo = imageInfos[binding][localIndex];
            imageInfo.sampler = texture.sampler;
            imageInfo.imageView = texture.view;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[binding].dstSet = frame.indexedWorldMaterialDescriptorSet;
        writes[binding].dstBinding = binding;
        writes[binding].dstArrayElement = firstMaterial;
        writes[binding].descriptorCount = materialCount;
        writes[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[binding].pImageInfo = imageInfos[binding].data();
    }
    vkUpdateDescriptorSets(
        backend.device,
        static_cast<std::uint32_t>(writes.size()),
        writes.data(),
        0u,
        nullptr);
}

} // namespace

bool VulkanRenderBackendImpl::registerIndexedWorldMaterial(
    WorldMaterial& material) {
    namespace vulkan = engine::render::vulkan_backend;
    if (!descriptorIndexingSupported) return false;
    if (material.indexedTableSlot != UINT32_MAX) return true;
    if (indexedWorldMaterials.size() >= vulkan::kMaxIndexedWorldMaterials) {
        return false;
    }
    material.indexedTableSlot =
        static_cast<std::uint32_t>(indexedWorldMaterials.size());
    indexedWorldMaterials.push_back(&material);
    return true;
}

void VulkanRenderBackendImpl::initializeIndexedWorldMaterialSets() {
    if (!descriptorIndexingSupported || indexedWorldMaterials.empty()) return;
    namespace vulkan = engine::render::vulkan_backend;
    for (FrameResources& frame : frames) {
        if (frame.indexedWorldMaterialDescriptorSet == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "Vulkan indexed world material descriptor set was not allocated.");
        }
        updateMaterialRange(
            *this,
            frame,
            0u,
            vulkan::kMaxIndexedWorldMaterials,
            true);
        frame.indexedWorldMaterialCount =
            static_cast<std::uint32_t>(indexedWorldMaterials.size());
    }
}

bool VulkanRenderBackendImpl::syncIndexedWorldMaterialSet() {
    if (!descriptorIndexingSupported ||
        indexedWorldMaterials.size() >
            engine::render::vulkan_backend::kMaxIndexedWorldMaterials) {
        return false;
    }
    FrameResources& frame = frames[currentFrame];
    const std::uint32_t materialCount =
        static_cast<std::uint32_t>(indexedWorldMaterials.size());
    if (frame.indexedWorldMaterialCount >= materialCount) return true;
    if (frame.indexedWorldMaterialSetBound) return false;
    updateMaterialRange(
        *this,
        frame,
        frame.indexedWorldMaterialCount,
        materialCount - frame.indexedWorldMaterialCount,
        false);
    frame.indexedWorldMaterialCount = materialCount;
    return true;
}
