#include "engine/render/VulkanRenderBackend.h"

#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanWorldSceneData.h"

#include <utility>
#include <vector>

namespace engine::render::vulkan_backend {

IRenderBackend::WorldTextureData makeWorldSceneTextureData(
    const IRenderBackend::WorldSceneMaterial& material,
    const IRenderBackend::WorldSceneView& view) {
    IRenderBackend::WorldTextureData texture{};
    texture.key = material.textureKey.empty() ? "" : material.textureKey.c_str();
    texture.cacheKey = material.textureCacheKey.empty()
        ? nullptr
        : material.textureCacheKey.c_str();
    texture.rgba = material.textureRgba;
    texture.width = material.textureWidth;
    texture.height = material.textureHeight;
    texture.mipLevels = material.textureMipLevels;
    texture.mipLevelCount = material.textureMipLevelCount;
    texture.wrapS = material.textureWrapS;
    texture.wrapT = material.textureWrapT;
    texture.textureSrgb = material.textureSrgb;
    texture.normalKey = material.normalTextureKey.empty()
        ? ""
        : material.normalTextureKey.c_str();
    texture.normalCacheKey = material.normalTextureCacheKey.empty()
        ? nullptr
        : material.normalTextureCacheKey.c_str();
    texture.normalRgba = material.normalTextureRgba;
    texture.normalWidth = material.normalTextureWidth;
    texture.normalHeight = material.normalTextureHeight;
    texture.normalMipLevels = material.normalTextureMipLevels;
    texture.normalMipLevelCount = material.normalTextureMipLevelCount;
    texture.normalWrapS = material.normalTextureWrapS;
    texture.normalWrapT = material.normalTextureWrapT;
    texture.normalTextureSrgb = material.normalTextureSrgb;
    texture.metallicRoughnessKey = material.metallicRoughnessTextureKey.empty()
        ? ""
        : material.metallicRoughnessTextureKey.c_str();
    texture.metallicRoughnessCacheKey =
        material.metallicRoughnessTextureCacheKey.empty()
        ? nullptr
        : material.metallicRoughnessTextureCacheKey.c_str();
    texture.metallicRoughnessRgba = material.metallicRoughnessTextureRgba;
    texture.metallicRoughnessWidth = material.metallicRoughnessTextureWidth;
    texture.metallicRoughnessHeight = material.metallicRoughnessTextureHeight;
    texture.metallicRoughnessMipLevels =
        material.metallicRoughnessTextureMipLevels;
    texture.metallicRoughnessMipLevelCount =
        material.metallicRoughnessTextureMipLevelCount;
    texture.metallicRoughnessWrapS = material.metallicRoughnessTextureWrapS;
    texture.metallicRoughnessWrapT = material.metallicRoughnessTextureWrapT;
    texture.metallicRoughnessTextureSrgb =
        material.metallicRoughnessTextureSrgb;
    texture.occlusionKey = material.occlusionTextureKey.empty()
        ? ""
        : material.occlusionTextureKey.c_str();
    texture.occlusionCacheKey = material.occlusionTextureCacheKey.empty()
        ? nullptr
        : material.occlusionTextureCacheKey.c_str();
    texture.occlusionRgba = material.occlusionTextureRgba;
    texture.occlusionWidth = material.occlusionTextureWidth;
    texture.occlusionHeight = material.occlusionTextureHeight;
    texture.occlusionMipLevels = material.occlusionTextureMipLevels;
    texture.occlusionMipLevelCount = material.occlusionTextureMipLevelCount;
    texture.occlusionWrapS = material.occlusionTextureWrapS;
    texture.occlusionWrapT = material.occlusionTextureWrapT;
    texture.occlusionTextureSrgb = material.occlusionTextureSrgb;
    texture.emissiveKey = material.emissiveTextureKey.empty()
        ? ""
        : material.emissiveTextureKey.c_str();
    texture.emissiveCacheKey = material.emissiveTextureCacheKey.empty()
        ? nullptr
        : material.emissiveTextureCacheKey.c_str();
    texture.emissiveRgba = material.emissiveTextureRgba;
    texture.emissiveWidth = material.emissiveTextureWidth;
    texture.emissiveHeight = material.emissiveTextureHeight;
    texture.emissiveMipLevels = material.emissiveTextureMipLevels;
    texture.emissiveMipLevelCount = material.emissiveTextureMipLevelCount;
    texture.emissiveWrapS = material.emissiveTextureWrapS;
    texture.emissiveWrapT = material.emissiveTextureWrapT;
    texture.emissiveTextureSrgb = material.emissiveTextureSrgb;
    texture.environmentKey = material.environmentTextureKey.empty()
        ? ""
        : material.environmentTextureKey.c_str();
    texture.environmentCacheKey = material.environmentTextureCacheKey.empty()
        ? nullptr
        : material.environmentTextureCacheKey.c_str();
    texture.environmentRgba = material.environmentTextureRgba;
    texture.environmentWidth = material.environmentTextureWidth;
    texture.environmentHeight = material.environmentTextureHeight;
    texture.environmentMipLevels = material.environmentTextureMipLevels;
    texture.environmentMipLevelCount = material.environmentTextureMipLevelCount;
    texture.environmentWrapS = material.environmentTextureWrapS;
    texture.environmentWrapT = material.environmentTextureWrapT;
    texture.environmentTextureSrgb = material.environmentTextureSrgb;
    texture.alphaMode = material.alphaMode;
    texture.blendMode = material.blendMode;
    texture.dualSourceBlendEnabled = material.dualSourceBlendEnabled;
    texture.materialMode = material.materialMode;
    texture.alphaCutoff = material.alphaCutoff;
    texture.normalScale = material.normalScale;
    texture.metallicFactor = material.metallicFactor;
    texture.roughnessFactor = material.roughnessFactor;
    texture.occlusionStrength = material.occlusionStrength;
    texture.emissiveFactorR = material.emissiveFactorR;
    texture.emissiveFactorG = material.emissiveFactorG;
    texture.emissiveFactorB = material.emissiveFactorB;
    texture.characterInkingEnabled = material.characterInkingEnabled;
    texture.materialTimeSec = material.materialTimeSec;
    texture.materialFlags = material.materialFlags;
    texture.materialAtlasWidth = material.materialAtlasWidth;
    texture.materialAtlasHeight = material.materialAtlasHeight;
    texture.materialRect0U = material.materialRect0U;
    texture.materialRect0V = material.materialRect0V;
    texture.materialRect0W = material.materialRect0W;
    texture.materialRect0H = material.materialRect0H;
    texture.materialRect1U = material.materialRect1U;
    texture.materialRect1V = material.materialRect1V;
    texture.materialRect1W = material.materialRect1W;
    texture.materialRect1H = material.materialRect1H;
    texture.materialFlipbook0Cols = material.materialFlipbook0Cols;
    texture.materialFlipbook0Rows = material.materialFlipbook0Rows;
    texture.materialFlipbook0Frames = material.materialFlipbook0Frames;
    texture.materialFlipbook0Fps = material.materialFlipbook0Fps;
    texture.materialFlipbook1Cols = material.materialFlipbook1Cols;
    texture.materialFlipbook1Rows = material.materialFlipbook1Rows;
    texture.materialFlipbook1Frames = material.materialFlipbook1Frames;
    texture.materialFlipbook1Fps = material.materialFlipbook1Fps;
    texture.cameraPosX = view.cameraWorldPos[0];
    texture.cameraPosY = view.cameraWorldPos[1];
    texture.cameraPosZ = view.cameraWorldPos[2];
    texture.cameraForwardX = view.cameraForward[0];
    texture.cameraForwardY = view.cameraForward[1];
    texture.cameraForwardZ = view.cameraForward[2];
    texture.cameraTargetX = view.cameraTarget[0];
    texture.cameraTargetY = view.cameraTarget[1];
    texture.cameraTargetZ = view.cameraTarget[2];
    return texture;
}

} // namespace engine::render::vulkan_backend

bool VulkanRenderBackend::supportsWorldSceneFastPath() const {
    return impl_ && impl_->initialized;
}

bool VulkanRenderBackend::getWorldSceneFastPathCaps(
    WorldSceneFastPathCaps& outCaps) const {
    outCaps = WorldSceneFastPathCaps{};
    outCaps.supported = supportsWorldSceneFastPath();
    outCaps.supportsSkinnedInstancing = outCaps.supported;
    outCaps.supportsExecuteIndirect =
        outCaps.supported && impl_->indirectWorldBatchingSupported;
    return outCaps.supported;
}

void VulkanRenderBackend::submitWorldScene(const WorldSceneFrame& frame,
                                           const WorldSceneView& view) {
    if (!supportsWorldSceneFastPath() ||
        !view.viewProjectionMatrix4x4 ||
        view.surfaceWidth <= 0 ||
        view.surfaceHeight <= 0 ||
        !view.geometries ||
        !view.materials ||
        !view.renderObjects ||
        frame.drawClasses.empty()) {
        return;
    }

    static thread_local std::vector<WorldMeshInstance> instances;
    if (impl_->worldSceneMaterialCacheGeneration != view.registryGeneration) {
        impl_->worldSceneMaterialDescriptorSets.clear();
        impl_->worldSceneMaterials.clear();
        impl_->worldSceneMaterialCacheGeneration = view.registryGeneration;
    }
    std::uint32_t previousGeometryId = 0u;
    std::uint32_t previousMaterialId = 0u;
    bool havePreviousDrawClass = false;

    impl_->frameStats.fastSceneDrawClasses +=
        static_cast<std::uint32_t>(frame.drawClasses.size());
    impl_->frameStats.fastSceneVisibleSkeletons += frame.visibleSkeletons;
    impl_->frameStats.fastScenePaletteUploadBytes += frame.paletteUploadBytes;
    impl_->frameStats.fastSceneIndirectCommands += frame.indirectCommandCount;

    if (impl_->submitWorldSceneIndirect(frame, view)) return;
    ++impl_->frameIndirectWorldFallbacks;

    for (const WorldSceneDrawClass& drawClass : frame.drawClasses) {
        if (!drawClass.objectHandle || drawClass.instances.empty()) continue;
        const std::size_t objectIndex =
            static_cast<std::size_t>(drawClass.objectHandle.id - 1u);
        if (objectIndex >= view.renderObjects->size()) continue;

        const WorldSceneRenderObject& object = (*view.renderObjects)[objectIndex];
        if (!object.geometryHandle || !object.materialHandle) continue;
        const std::size_t geometryIndex =
            static_cast<std::size_t>(object.geometryHandle.id - 1u);
        const std::size_t materialIndex =
            static_cast<std::size_t>(object.materialHandle.id - 1u);
        if (geometryIndex >= view.geometries->size() ||
            materialIndex >= view.materials->size()) {
            continue;
        }

        const WorldSceneGeometry& geometry = (*view.geometries)[geometryIndex];
        const WorldSceneMaterial& material = (*view.materials)[materialIndex];
        if (!geometry.vertices || !geometry.indices ||
            geometry.vertexCount == 0u || geometry.indexCount < 3u ||
            !worldSceneGeometrySourceSemanticsValid(geometry)) {
            continue;
        }

        instances.clear();
        instances.reserve(drawClass.instances.size());
        for (const WorldSceneInstance& sceneInstance : drawClass.instances) {
            WorldMeshInstance instance{};
            instance.modelMatrix = sceneInstance.modelMatrix;
            instance.vertexColorMulR = sceneInstance.vertexColorMulR;
            instance.vertexColorMulG = sceneInstance.vertexColorMulG;
            instance.vertexColorMulB = sceneInstance.vertexColorMulB;
            instance.vertexColorMulA = sceneInstance.vertexColorMulA;
            instance.gpuSkinning = sceneInstance.gpuSkinning;
            instance.gpuSkinningMode = sceneInstance.gpuSkinningMode;
            instance.skinMatrixCount = sceneInstance.skinMatrixCount;
            instance.skinMatrices = sceneInstance.skinMatrices;
            instances.push_back(std::move(instance));
        }

        WorldTextureData texture =
            engine::render::vulkan_backend::makeWorldSceneTextureData(
                material, view);
        if (impl_->worldSceneMaterialDescriptorSets.size() <= materialIndex) {
            impl_->worldSceneMaterialDescriptorSets.resize(materialIndex + 1u);
        }
        VkDescriptorSet& materialDescriptorSet =
            impl_->worldSceneMaterialDescriptorSets[materialIndex];
        if (materialDescriptorSet == VK_NULL_HANDLE) {
            ++impl_->framePreparedMaterialCacheMisses;
            VulkanRenderBackendImpl::WorldMaterial* worldMaterial =
                impl_->ensureWorldMaterial(&texture);
            if (!worldMaterial || worldMaterial->descriptorSet == VK_NULL_HANDLE) {
                continue;
            }
            materialDescriptorSet = worldMaterial->descriptorSet;
        } else {
            ++impl_->framePreparedMaterialCacheHits;
        }

        impl_->drawWorldIndexedMeshCachedPreparedInstanced(
            geometry.geometryCacheKey.empty()
                ? nullptr
                : geometry.geometryCacheKey.c_str(),
            geometry.vertices,
            geometry.vertexCount,
            geometry.indices,
            geometry.indexCount,
            materialDescriptorSet,
            &texture,
            instances.data(),
            instances.size(),
            view.viewProjectionMatrix4x4,
            view.surfaceWidth,
            view.surfaceHeight);

        impl_->frameStats.fastSceneInstances +=
            static_cast<std::uint32_t>(instances.size());
        ++impl_->frameStats.fastSceneMaterialTableBinds;
        ++impl_->frameStats.indexedOpaqueDraws;
        ++impl_->frameStats.indexedCachedDraws;
        ++impl_->frameStats.indexedInstancedDraws;
        if (material.characterInkingEnabled != 0u &&
            material.materialMode >= 2u) {
            ++impl_->frameStats.indexedOutlineBatches;
        }
        if (!havePreviousDrawClass ||
            previousGeometryId != object.geometryHandle.id) {
            ++impl_->frameStats.indexedGeometrySwitches;
        }
        if (!havePreviousDrawClass ||
            previousMaterialId != object.materialHandle.id) {
            ++impl_->frameStats.indexedMaterialSwitches;
            ++impl_->frameStats.indexedTextureSwitches;
        }
        previousGeometryId = object.geometryHandle.id;
        previousMaterialId = object.materialHandle.id;
        havePreviousDrawClass = true;
    }
}
