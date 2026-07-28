#include "engine/render/D3D12RenderBackend.h"

#include <algorithm>
#include <vector>

namespace {

IRenderBackend::WorldTextureData makeWorldSceneTextureData(
    const IRenderBackend::WorldSceneMaterial& material,
    const IRenderBackend::WorldSceneView& view) {
    IRenderBackend::WorldTextureData tex{};
    tex.key = material.textureKey.empty() ? "" : material.textureKey.c_str();
    tex.cacheKey =
        material.textureCacheKey.empty() ? nullptr : material.textureCacheKey.c_str();
    tex.rgba = material.textureRgba;
    tex.width = material.textureWidth;
    tex.height = material.textureHeight;
    tex.wrapS = material.textureWrapS;
    tex.wrapT = material.textureWrapT;
    tex.normalKey =
        material.normalTextureKey.empty() ? "" : material.normalTextureKey.c_str();
    tex.normalCacheKey = material.normalTextureCacheKey.empty()
        ? nullptr
        : material.normalTextureCacheKey.c_str();
    tex.normalRgba = material.normalTextureRgba;
    tex.normalWidth = material.normalTextureWidth;
    tex.normalHeight = material.normalTextureHeight;
    tex.normalWrapS = material.normalTextureWrapS;
    tex.normalWrapT = material.normalTextureWrapT;
    tex.metallicRoughnessKey = material.metallicRoughnessTextureKey.empty()
        ? ""
        : material.metallicRoughnessTextureKey.c_str();
    tex.metallicRoughnessCacheKey = material.metallicRoughnessTextureCacheKey.empty()
        ? nullptr
        : material.metallicRoughnessTextureCacheKey.c_str();
    tex.metallicRoughnessRgba = material.metallicRoughnessTextureRgba;
    tex.metallicRoughnessWidth = material.metallicRoughnessTextureWidth;
    tex.metallicRoughnessHeight = material.metallicRoughnessTextureHeight;
    tex.metallicRoughnessWrapS = material.metallicRoughnessTextureWrapS;
    tex.metallicRoughnessWrapT = material.metallicRoughnessTextureWrapT;
    tex.occlusionKey =
        material.occlusionTextureKey.empty() ? "" : material.occlusionTextureKey.c_str();
    tex.occlusionCacheKey = material.occlusionTextureCacheKey.empty()
        ? nullptr
        : material.occlusionTextureCacheKey.c_str();
    tex.occlusionRgba = material.occlusionTextureRgba;
    tex.occlusionWidth = material.occlusionTextureWidth;
    tex.occlusionHeight = material.occlusionTextureHeight;
    tex.occlusionWrapS = material.occlusionTextureWrapS;
    tex.occlusionWrapT = material.occlusionTextureWrapT;
    tex.emissiveKey =
        material.emissiveTextureKey.empty() ? "" : material.emissiveTextureKey.c_str();
    tex.emissiveCacheKey = material.emissiveTextureCacheKey.empty()
        ? nullptr
        : material.emissiveTextureCacheKey.c_str();
    tex.emissiveRgba = material.emissiveTextureRgba;
    tex.emissiveWidth = material.emissiveTextureWidth;
    tex.emissiveHeight = material.emissiveTextureHeight;
    tex.emissiveWrapS = material.emissiveTextureWrapS;
    tex.emissiveWrapT = material.emissiveTextureWrapT;
    tex.alphaMode = material.alphaMode;
    tex.blendMode = material.blendMode;
    tex.dualSourceBlendEnabled = material.dualSourceBlendEnabled;
    tex.materialMode = material.materialMode;
    tex.alphaCutoff = material.alphaCutoff;
    tex.normalScale = material.normalScale;
    tex.metallicFactor = material.metallicFactor;
    tex.roughnessFactor = material.roughnessFactor;
    tex.occlusionStrength = material.occlusionStrength;
    tex.emissiveFactorR = material.emissiveFactorR;
    tex.emissiveFactorG = material.emissiveFactorG;
    tex.emissiveFactorB = material.emissiveFactorB;
    tex.characterInkingEnabled = material.characterInkingEnabled;
    tex.materialTimeSec = material.materialTimeSec;
    tex.materialFlags = material.materialFlags;
    tex.materialAtlasWidth = material.materialAtlasWidth;
    tex.materialAtlasHeight = material.materialAtlasHeight;
    tex.materialRect0U = material.materialRect0U;
    tex.materialRect0V = material.materialRect0V;
    tex.materialRect0W = material.materialRect0W;
    tex.materialRect0H = material.materialRect0H;
    tex.materialRect1U = material.materialRect1U;
    tex.materialRect1V = material.materialRect1V;
    tex.materialRect1W = material.materialRect1W;
    tex.materialRect1H = material.materialRect1H;
    tex.materialFlipbook0Cols = material.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = material.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = material.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = material.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = material.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = material.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = material.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = material.materialFlipbook1Fps;
    tex.cameraPosX = view.cameraWorldPos[0];
    tex.cameraPosY = view.cameraWorldPos[1];
    tex.cameraPosZ = view.cameraWorldPos[2];
    tex.cameraForwardX = view.cameraForward[0];
    tex.cameraForwardY = view.cameraForward[1];
    tex.cameraForwardZ = view.cameraForward[2];
    tex.cameraTargetX = view.cameraTarget[0];
    tex.cameraTargetY = view.cameraTarget[1];
    tex.cameraTargetZ = view.cameraTarget[2];
    return tex;
}

} // namespace

void D3D12RenderBackend::submitWorldScene(const WorldSceneFrame& frame,
                                          const WorldSceneView& view) {
#if defined(_WIN32)
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
    if (worldSceneMaterialBindingCacheGeneration_ != view.registryGeneration) {
        worldSceneMaterialBindingCache_.clear();
        worldSceneMaterialBindingCacheGeneration_ = view.registryGeneration;
    }
    std::uint32_t previousGeometryId = 0u;
    std::uint32_t previousMaterialId = 0u;
    bool havePreviousDrawClass = false;

    frameFastSceneDrawClasses_ += static_cast<std::uint32_t>(frame.drawClasses.size());
    frameFastSceneVisibleSkeletons_ += frame.visibleSkeletons;
    frameFastScenePaletteUploadBytes_ += frame.paletteUploadBytes;
    frameFastSceneIndirectCommands_ += frame.indirectCommandCount;

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
        for (const WorldSceneInstance& instance : drawClass.instances) {
            WorldMeshInstance outInstance{};
            outInstance.modelMatrix = instance.modelMatrix;
            outInstance.vertexColorMulR = instance.vertexColorMulR;
            outInstance.vertexColorMulG = instance.vertexColorMulG;
            outInstance.vertexColorMulB = instance.vertexColorMulB;
            outInstance.vertexColorMulA = instance.vertexColorMulA;
            outInstance.gpuSkinning = instance.gpuSkinning;
            outInstance.gpuSkinningMode = instance.gpuSkinningMode;
            outInstance.skinMatrixCount = instance.skinMatrixCount;
            outInstance.skinMatrices = instance.skinMatrices;
            instances.push_back(std::move(outInstance));
        }

        WorldTextureData textureData = makeWorldSceneTextureData(material, view);
        if (worldSceneMaterialBindingCache_.size() <= materialIndex) {
            worldSceneMaterialBindingCache_.resize(materialIndex + 1u);
        }
        auto& materialBinding = worldSceneMaterialBindingCache_[materialIndex];
        if (!materialBinding.valid) {
            if (!prepareWorldMaterialDescriptorBlock(
                    &textureData,
                    /*logPbrBinding=*/false,
                    materialBinding.descriptorBlockIndex,
                    materialBinding.useTexture)) {
                continue;
            }
            materialBinding.valid = true;
        }

        drawWorldIndexedMeshTexturedCachedPreparedInstanced(
            geometry.geometryCacheKey.empty() ? nullptr : geometry.geometryCacheKey.c_str(),
            geometry.vertices,
            geometry.vertexCount,
            geometry.indices,
            geometry.indexCount,
            materialBinding.descriptorBlockIndex,
            &textureData,
            materialBinding.useTexture,
            instances.data(),
            instances.size(),
            view.viewProjectionMatrix4x4,
            view.surfaceWidth,
            view.surfaceHeight,
            0.0f);

        frameFastSceneInstances_ += static_cast<std::uint32_t>(instances.size());
        ++frameFastSceneMaterialTableBinds_;
        ++frameIndexedOpaqueDraws_;
        ++frameIndexedCachedDraws_;
        ++frameIndexedInstancedDraws_;
        if (material.characterInkingEnabled != 0u &&
            material.materialMode >= 2u) {
            ++frameIndexedOutlineBatches_;
        }
        if (!havePreviousDrawClass || previousGeometryId != object.geometryHandle.id) {
            ++frameIndexedGeometrySwitches_;
        }
        if (!havePreviousDrawClass || previousMaterialId != object.materialHandle.id) {
            ++frameIndexedMaterialSwitches_;
            ++frameIndexedTextureSwitches_;
        }
        previousGeometryId = object.geometryHandle.id;
        previousMaterialId = object.materialHandle.id;
        havePreviousDrawClass = true;
    }
#else
    (void)frame;
    (void)view;
#endif
}
