#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

bool VulkanRenderBackendImpl::uploadWorldSkinPalette(
    const float* matrices,std::size_t floatCount,std::uint32_t& outBaseMatrixIndex,
    engine::render::WorldDeformation version) {
    outBaseMatrixIndex=0;
    if(!frameActive || !matrices || !floatCount)return false;
    const auto data=std::as_bytes(std::span(matrices,floatCount));
    const auto result=frameSkinPalettes.resolve(data,std::move(version));
    if(!result)return false;
    outBaseMatrixIndex=static_cast<std::uint32_t>(result->value);
    if(result->reused){++frameSkinPaletteReuses;frameSkinPaletteReuseBytes+=data.size();}
    else {++frameSkinPaletteUploads;frameSkinPaletteUploadBytes+=data.size();}
    return true;
}

bool VulkanRenderBackendImpl::prepareWorldInstances(
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    std::uint32_t& outInstanceCount,
    std::uint32_t& outInstanceBaseWordIndex) {
    outInstanceCount = 0u;
    outInstanceBaseWordIndex = 0u;
    if (!frameActive || !instances || instanceCount == 0u ||
        instanceCount > static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }

    namespace vulkan = engine::render::vulkan_backend;
    static thread_local std::vector<vulkan::WorldInstanceState> instanceStates;

    instanceStates.clear();
    instanceStates.reserve(instanceCount);

    for (std::size_t i = 0u; i < instanceCount; ++i) {
        const std::size_t floatCount =
            vulkan::worldInstanceSkinMatrixFloatCount(instances[i]);
        std::uint32_t skinMatrixBaseIndex = 0u;
        if (floatCount > 0u &&
            !uploadWorldSkinPalette(
                instances[i].skinMatrices,
                floatCount,
                skinMatrixBaseIndex,instances[i].deformation)) {
            return false;
        }
        instanceStates.push_back(
            vulkan::makeWorldInstanceState(instances[i], skinMatrixBaseIndex));
    }

    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize instanceOffset = 0u;
    if (!writeTransient(
            instanceStates.data(),
            static_cast<VkDeviceSize>(
                instanceStates.size() * sizeof(vulkan::WorldInstanceState)),
            sizeof(float) * 4u,
            instanceBuffer,
            instanceOffset) ||
        instanceBuffer != frames[currentFrame].transient.buffer) {
        return false;
    }

    const VkDeviceSize baseWordIndex = instanceOffset / (sizeof(float) * 4u);
    // A float in the transform UBO carries this index. The fixed 128 MiB
    // transient buffer keeps it within the exact integer range of float32.
    if (baseWordIndex > 16777216u) return false;
    outInstanceCount = static_cast<std::uint32_t>(instanceCount);
    outInstanceBaseWordIndex = static_cast<std::uint32_t>(baseWordIndex);
    return true;
}

void VulkanRenderBackendImpl::prepareWorldDeformations(
    std::span<const engine::render::WorldSkinPalette> palettes,bool commit) {
    if(palettes.empty() && !commit)return;
    if(!frameActive)throw std::logic_error("Deformation upload outside a frame");
    const auto before=frameSkinPalettes.stats();
    if(commit)frameSkinPalettes.commitBatch();
    else frameSkinPalettes.prepare(palettes);
    frameSkinPaletteUploads+=static_cast<std::uint32_t>(frameSkinPalettes.stats().preparedUploads-before.preparedUploads);
    frameSkinPaletteUploadBytes+=frameSkinPalettes.stats().preparedBytes-before.preparedBytes;
}
void VulkanRenderBackend::beginWorldDeformationBatch() {
    if(!impl_->frameActive)throw std::logic_error("Deformation admission outside a frame");
    impl_->frameSkinPalettes.beginBatch();
}
void VulkanRenderBackend::cancelWorldDeformationBatch() noexcept {impl_->frameSkinPalettes.cancelBatch();}
void VulkanRenderBackend::commitWorldDeformationBatch() {impl_->prepareWorldDeformations({},true);}
void VulkanRenderBackend::prepareWorldDeformations(
    std::span<const engine::render::WorldSkinPalette> palettes) {
    impl_->prepareWorldDeformations(palettes);
}

engine::render::WorldDeformationPreparationStats VulkanRenderBackend::worldDeformationPreparationStats() const {
    const auto& s=impl_->frameSkinPalettes.stats();
    return {s.preparedUploads,s.preparedBytes,s.preparationJoins,s.uploads,s.uploadBytes};
}
