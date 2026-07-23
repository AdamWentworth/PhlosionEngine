#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::backend {

struct BackendFrameTimings {
    float presentWaitMs = 0.0f;
    float gpuFrameMs = 0.0f;
    bool gpuFrameValid = false;
};

struct BackendFrameStats {
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t indexedOpaqueDraws = 0u;
    std::uint32_t indexedBlendDraws = 0u;
    std::uint32_t indexedCachedDraws = 0u;
    std::uint32_t indexedDynamicDraws = 0u;
    std::uint32_t indexedInstancedDraws = 0u;
    std::uint32_t indexedOutlineBatches = 0u;
    std::uint32_t indexedGeometrySwitches = 0u;
    std::uint32_t indexedMaterialSwitches = 0u;
    std::uint32_t indexedTextureSwitches = 0u;
    std::uint32_t indexedGlTextureBindCalls = 0u;
    std::uint32_t indexedD3d12PsoSets = 0u;
    std::uint32_t indexedD3d12DescriptorTableSets = 0u;
    std::uint32_t fastSceneInstances = 0u;
    std::uint32_t fastSceneDrawClasses = 0u;
    std::uint32_t fastSceneVisibleSkeletons = 0u;
    std::uint64_t fastScenePaletteUploadBytes = 0u;
    std::uint32_t fastSceneMaterialTableBinds = 0u;
    std::uint32_t fastSceneIndirectCommands = 0u;
};

struct WorldIndexedSubmissionStats {
    std::uint32_t opaqueDraws = 0u;
    std::uint32_t blendDraws = 0u;
    std::uint32_t cachedDraws = 0u;
    std::uint32_t dynamicDraws = 0u;
    std::uint32_t instancedDraws = 0u;
    std::uint32_t outlineBatches = 0u;
    std::uint32_t geometrySwitches = 0u;
    std::uint32_t materialSwitches = 0u;
    std::uint32_t textureSwitches = 0u;
};

struct WorldMeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    float joint0 = 0.0f;
    float joint1 = 0.0f;
    float joint2 = 0.0f;
    float joint3 = 0.0f;
    float weight0 = 0.0f;
    float weight1 = 0.0f;
    float weight2 = 0.0f;
    float weight3 = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    float tw = 1.0f;
};

struct WorldTextureData {
    const char* key = nullptr;
    const char* cacheKey = nullptr;
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    const char* normalKey = nullptr;
    const char* normalCacheKey = nullptr;
    const unsigned char* normalRgba = nullptr;
    int normalWidth = 0;
    int normalHeight = 0;
    int normalWrapS = 10497;
    int normalWrapT = 10497;
    const char* metallicRoughnessKey = nullptr;
    const char* metallicRoughnessCacheKey = nullptr;
    const unsigned char* metallicRoughnessRgba = nullptr;
    int metallicRoughnessWidth = 0;
    int metallicRoughnessHeight = 0;
    int metallicRoughnessWrapS = 10497;
    int metallicRoughnessWrapT = 10497;
    const char* occlusionKey = nullptr;
    const char* occlusionCacheKey = nullptr;
    const unsigned char* occlusionRgba = nullptr;
    int occlusionWidth = 0;
    int occlusionHeight = 0;
    int occlusionWrapS = 10497;
    int occlusionWrapT = 10497;
    const char* emissiveKey = nullptr;
    const char* emissiveCacheKey = nullptr;
    const unsigned char* emissiveRgba = nullptr;
    int emissiveWidth = 0;
    int emissiveHeight = 0;
    int emissiveWrapS = 10497;
    int emissiveWrapT = 10497;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t dualSourceBlendEnabled = 0u;
    std::uint8_t depthTestEnabled = 1u;
    std::uint8_t materialMode = 0u;
    float clipSpaceDepthBias = 0.0f;
    float alphaCutoff = 0.5f;
    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    std::uint8_t characterInkingEnabled = 0u;
    float cameraPosX = 0.0f;
    float cameraPosY = 7.0f;
    float cameraPosZ = 9.0f;
    float cameraForwardX = 0.0f;
    float cameraForwardY = -0.6139406f;
    float cameraForwardZ = -0.7893522f;
    float cameraTargetX = 0.0f;
    float cameraTargetY = -1.0f;
    float cameraTargetZ = 0.0f;
    float materialTimeSec = 0.0f;
    float materialFlags = 0.0f;
    float materialAtlasWidth = 0.0f;
    float materialAtlasHeight = 0.0f;
    float materialRect0U = 0.0f;
    float materialRect0V = 0.0f;
    float materialRect0W = 1.0f;
    float materialRect0H = 1.0f;
    float materialRect1U = 0.0f;
    float materialRect1V = 0.0f;
    float materialRect1W = 1.0f;
    float materialRect1H = 1.0f;
    float materialFlipbook0Cols = 1.0f;
    float materialFlipbook0Rows = 1.0f;
    float materialFlipbook0Frames = 1.0f;
    float materialFlipbook0Fps = 0.0f;
    float materialFlipbook1Cols = 1.0f;
    float materialFlipbook1Rows = 1.0f;
    float materialFlipbook1Frames = 1.0f;
    float materialFlipbook1Fps = 0.0f;
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::uint8_t gpuSkinning = 0u;
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* skinMatrices = nullptr;
};

struct WorldMeshInstance {
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    std::uint8_t gpuSkinning = 0u;
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* skinMatrices = nullptr;
};

template <typename Tag>
struct WorldSceneHandle {
    std::uint32_t id = 0u;

    constexpr explicit operator bool() const noexcept {
        return id != 0u;
    }

    auto operator<=>(const WorldSceneHandle&) const = default;
};

using WorldSceneGeometryHandle = WorldSceneHandle<struct WorldSceneGeometryTag>;
using WorldSceneMaterialHandle = WorldSceneHandle<struct WorldSceneMaterialTag>;
using WorldSceneSkeletonLayoutHandle =
    WorldSceneHandle<struct WorldSceneSkeletonLayoutTag>;
using WorldSceneAnimationClipHandle =
    WorldSceneHandle<struct WorldSceneAnimationClipTag>;
using WorldSceneRenderObjectHandle =
    WorldSceneHandle<struct WorldSceneRenderObjectTag>;
using WorldSceneRenderInstanceHandle =
    WorldSceneHandle<struct WorldSceneRenderInstanceTag>;

struct WorldSceneFastPathCaps {
    bool supported = false;
    bool prefersD3d12SpecializedPath = false;
    bool supportsSkinnedInstancing = false;
    bool supportsComputeSkinning = false;
    bool supportsExecuteIndirect = false;
};

struct WorldSceneGeometry {
    WorldSceneGeometryHandle handle{};
    std::string geometryCacheKey;
    const WorldMeshVertex* vertices = nullptr;
    std::size_t vertexCount = 0u;
    const std::uint32_t* indices = nullptr;
    std::size_t indexCount = 0u;
};

struct WorldSceneMaterial {
    WorldSceneMaterialHandle handle{};
    std::string textureKey;
    std::string textureCacheKey;
    const unsigned char* textureRgba = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureWrapS = 10497;
    int textureWrapT = 10497;
    std::string normalTextureKey;
    std::string normalTextureCacheKey;
    const unsigned char* normalTextureRgba = nullptr;
    int normalTextureWidth = 0;
    int normalTextureHeight = 0;
    int normalTextureWrapS = 10497;
    int normalTextureWrapT = 10497;
    std::string metallicRoughnessTextureKey;
    std::string metallicRoughnessTextureCacheKey;
    const unsigned char* metallicRoughnessTextureRgba = nullptr;
    int metallicRoughnessTextureWidth = 0;
    int metallicRoughnessTextureHeight = 0;
    int metallicRoughnessTextureWrapS = 10497;
    int metallicRoughnessTextureWrapT = 10497;
    std::string occlusionTextureKey;
    std::string occlusionTextureCacheKey;
    const unsigned char* occlusionTextureRgba = nullptr;
    int occlusionTextureWidth = 0;
    int occlusionTextureHeight = 0;
    int occlusionTextureWrapS = 10497;
    int occlusionTextureWrapT = 10497;
    std::string emissiveTextureKey;
    std::string emissiveTextureCacheKey;
    const unsigned char* emissiveTextureRgba = nullptr;
    int emissiveTextureWidth = 0;
    int emissiveTextureHeight = 0;
    int emissiveTextureWrapS = 10497;
    int emissiveTextureWrapT = 10497;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t dualSourceBlendEnabled = 0u;
    std::uint8_t materialMode = 0u;
    float alphaCutoff = 0.5f;
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
    std::uint8_t characterInkingEnabled = 0u;
    float materialTimeSec = 0.0f;
    float materialFlags = 0.0f;
    float materialAtlasWidth = 0.0f;
    float materialAtlasHeight = 0.0f;
    float materialRect0U = 0.0f;
    float materialRect0V = 0.0f;
    float materialRect0W = 1.0f;
    float materialRect0H = 1.0f;
    float materialRect1U = 0.0f;
    float materialRect1V = 0.0f;
    float materialRect1W = 1.0f;
    float materialRect1H = 1.0f;
    float materialFlipbook0Cols = 1.0f;
    float materialFlipbook0Rows = 1.0f;
    float materialFlipbook0Frames = 1.0f;
    float materialFlipbook0Fps = 0.0f;
    float materialFlipbook1Cols = 1.0f;
    float materialFlipbook1Rows = 1.0f;
    float materialFlipbook1Frames = 1.0f;
    float materialFlipbook1Fps = 0.0f;
};

struct WorldSceneSkeletonLayout {
    WorldSceneSkeletonLayoutHandle handle{};
    std::uint32_t jointCount = 0u;
    std::uint32_t paletteRemapCount = 0u;
};

struct WorldSceneAnimationClip {
    WorldSceneAnimationClipHandle handle{};
    float durationSec = 0.0f;
    std::uint32_t sampleCount = 0u;
};

struct WorldSceneRenderObject {
    WorldSceneRenderObjectHandle handle{};
    WorldSceneGeometryHandle geometryHandle{};
    WorldSceneMaterialHandle materialHandle{};
    WorldSceneSkeletonLayoutHandle skeletonLayoutHandle{};
    WorldSceneAnimationClipHandle animationClipHandle{};
    std::uint8_t pipelineVariant = 0u;
    std::uint8_t outlineVariant = 0u;
    std::uint32_t cookedDrawSlot = 0u;
    bool opaque = true;
    bool skinned = false;
};

struct WorldSceneInstance {
    WorldSceneRenderInstanceHandle handle{};
    WorldSceneRenderObjectHandle objectHandle{};
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    std::uint32_t skeletonInstanceIndex = 0u;
    std::uint32_t animationStateIndex = 0u;
    std::uint8_t gpuSkinning = 0u;
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* skinMatrices = nullptr;
    float sortDepth = 0.0f;
};

struct WorldSceneDrawClass {
    WorldSceneRenderObjectHandle objectHandle{};
    std::vector<WorldSceneInstance> instances;
    std::uint32_t visibleSkeletons = 0u;
    std::uint32_t paletteUploadBytes = 0u;
};

struct WorldSceneFrame {
    std::vector<WorldSceneDrawClass> drawClasses;
    std::vector<std::uint32_t> drawClassIndexByObjectId;
    std::uint32_t visibleSkeletons = 0u;
    std::uint64_t paletteUploadBytes = 0u;
    std::uint32_t indirectCommandCount = 0u;

    void clear() {
        drawClasses.clear();
        std::fill(drawClassIndexByObjectId.begin(), drawClassIndexByObjectId.end(), 0u);
        visibleSkeletons = 0u;
        paletteUploadBytes = 0u;
        indirectCommandCount = 0u;
    }
};

struct WorldSceneView {
    const std::vector<WorldSceneGeometry>* geometries = nullptr;
    const std::vector<WorldSceneMaterial>* materials = nullptr;
    const std::vector<WorldSceneSkeletonLayout>* skeletonLayouts = nullptr;
    const std::vector<WorldSceneAnimationClip>* animationClips = nullptr;
    const std::vector<WorldSceneRenderObject>* renderObjects = nullptr;
    std::uint32_t registryGeneration = 0u;
    const float* viewProjectionMatrix4x4 = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    std::array<float, 3> cameraWorldPos{0.0f, 7.0f, 9.0f};
    std::array<float, 3> cameraForward{0.0f, -0.6139406f, -0.7893522f};
    std::array<float, 3> cameraTarget{0.0f, -1.0f, 0.0f};
};

struct WorldTriangle {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float z1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float z2 = 0.0f;
    float x3 = 0.0f;
    float y3 = 0.0f;
    float z3 = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float r1 = -1.0f;
    float g1 = -1.0f;
    float b1 = -1.0f;
    float a1 = -1.0f;
    float r2 = -1.0f;
    float g2 = -1.0f;
    float b2 = -1.0f;
    float a2 = -1.0f;
    float r3 = -1.0f;
    float g3 = -1.0f;
    float b3 = -1.0f;
    float a3 = -1.0f;
};

struct DebugQuad {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct DebugLine {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float thickness = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct DebugTriangle {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float x3 = 0.0f;
    float y3 = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct DebugSprite {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    std::string texturePath;
};

} // namespace engine::render::backend
