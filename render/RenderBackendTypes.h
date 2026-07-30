#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
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
    // Source TEXCOORD_1 and TEXCOORD_2. These channels joined the compact GPU
    // stream only after direct FieldCliffShader01 and FieldGroundShader01
    // evidence established their authored use.
    float sourceUv1U = 0.0f;
    float sourceUv1V = 0.0f;
    float sourceUv2U = 0.0f;
    float sourceUv2V = 0.0f;
};

struct WorldTextureMipLevel {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
};

struct WorldTextureData {
    const char* key = nullptr;
    const char* cacheKey = nullptr;
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    const WorldTextureMipLevel* mipLevels = nullptr;
    std::uint32_t mipLevelCount = 0u;
    int wrapS = 10497;
    int wrapT = 10497;
    std::uint8_t textureSrgb = 1u;
    const char* normalKey = nullptr;
    const char* normalCacheKey = nullptr;
    const unsigned char* normalRgba = nullptr;
    int normalWidth = 0;
    int normalHeight = 0;
    const WorldTextureMipLevel* normalMipLevels = nullptr;
    std::uint32_t normalMipLevelCount = 0u;
    int normalWrapS = 10497;
    int normalWrapT = 10497;
    std::uint8_t normalTextureSrgb = 0u;
    const char* metallicRoughnessKey = nullptr;
    const char* metallicRoughnessCacheKey = nullptr;
    const unsigned char* metallicRoughnessRgba = nullptr;
    int metallicRoughnessWidth = 0;
    int metallicRoughnessHeight = 0;
    const WorldTextureMipLevel* metallicRoughnessMipLevels = nullptr;
    std::uint32_t metallicRoughnessMipLevelCount = 0u;
    int metallicRoughnessWrapS = 10497;
    int metallicRoughnessWrapT = 10497;
    std::uint8_t metallicRoughnessTextureSrgb = 0u;
    const char* occlusionKey = nullptr;
    const char* occlusionCacheKey = nullptr;
    const unsigned char* occlusionRgba = nullptr;
    int occlusionWidth = 0;
    int occlusionHeight = 0;
    const WorldTextureMipLevel* occlusionMipLevels = nullptr;
    std::uint32_t occlusionMipLevelCount = 0u;
    int occlusionWrapS = 10497;
    int occlusionWrapT = 10497;
    std::uint8_t occlusionTextureSrgb = 0u;
    const char* emissiveKey = nullptr;
    const char* emissiveCacheKey = nullptr;
    const unsigned char* emissiveRgba = nullptr;
    int emissiveWidth = 0;
    int emissiveHeight = 0;
    const WorldTextureMipLevel* emissiveMipLevels = nullptr;
    std::uint32_t emissiveMipLevelCount = 0u;
    int emissiveWrapS = 10497;
    int emissiveWrapT = 10497;
    std::uint8_t emissiveTextureSrgb = 1u;
    const char* environmentKey = nullptr;
    const char* environmentCacheKey = nullptr;
    const unsigned char* environmentRgba = nullptr;
    int environmentWidth = 0;
    int environmentHeight = 0;
    const WorldTextureMipLevel* environmentMipLevels = nullptr;
    std::uint32_t environmentMipLevelCount = 0u;
    int environmentWrapS = 10497;
    int environmentWrapT = 10497;
    std::uint8_t environmentTextureSrgb = 0u;
    const char* lightProjectionKey = nullptr;
    const char* lightProjectionCacheKey = nullptr;
    const unsigned char* lightProjectionRgba = nullptr;
    int lightProjectionWidth = 0;
    int lightProjectionHeight = 0;
    const WorldTextureMipLevel* lightProjectionMipLevels = nullptr;
    std::uint32_t lightProjectionMipLevelCount = 0u;
    int lightProjectionWrapS = 10497;
    int lightProjectionWrapT = 10497;
    std::uint8_t lightProjectionTextureSrgb = 0u;
    std::array<float, 4> lightProjectionUvRowU{
        -0.00010391304269433f,
        0.0f,
        -0.000276669561862946f,
        0.695972776542572f};
    std::array<float, 4> lightProjectionUvRowV{
        -0.000223165191709995f,
        -0.000349375866353512f,
        0.0000838175788521767f,
        0.692474711333548f};
    const char* projectedShadowKey = nullptr;
    const char* projectedShadowCacheKey = nullptr;
    const unsigned char* projectedShadowRgba = nullptr;
    int projectedShadowWidth = 0;
    int projectedShadowHeight = 0;
    int projectedShadowWrapS = 33071;
    int projectedShadowWrapT = 33071;
    std::uint8_t projectedShadowTextureSrgb = 0u;
    std::uint8_t projectedShadowEnabled = 0u;
    float projectedShadowSamplingScale = 1.0f;
    float projectedShadowBias = 0.0f;
    std::array<float, 16> projectedShadowMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
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

enum class WorldSceneSourceMaterialFamily : std::uint8_t {
    Unknown = 0u,
    Ground,
    Grass,
    Cliff,
    Object,
    Rock,
    Tree,
    ShadowOnly,
};

enum WorldSceneSourceMaterialSwitch : std::uint32_t {
    WorldSceneSourceMaterialSwitchNone = 0u,
    WorldSceneSourceMaterialSwitchSkipMainRendering = 1u << 0u,
    WorldSceneSourceMaterialSwitchDepthWrite = 1u << 1u,
    WorldSceneSourceMaterialSwitchDepthTest = 1u << 2u,
    WorldSceneSourceMaterialSwitchDiscardEnable = 1u << 3u,
    WorldSceneSourceMaterialSwitchTextureAlphaTestEnable = 1u << 4u,
    WorldSceneSourceMaterialSwitchCastShadow = 1u << 5u,
    WorldSceneSourceMaterialSwitchReceiveShadow = 1u << 6u,
};

enum WorldSceneSourceVertexSemantic : std::uint32_t {
    WorldSceneSourceVertexSemanticNone = 0u,
    WorldSceneSourceVertexSemanticTexCoord1 = 1u << 0u,
    WorldSceneSourceVertexSemanticTexCoord2 = 1u << 1u,
    WorldSceneSourceVertexSemanticTexCoord3 = 1u << 2u,
    WorldSceneSourceVertexSemanticColor1 = 1u << 3u,
    WorldSceneSourceVertexSemanticColor2 = 1u << 4u,
    WorldSceneSourceVertexSemanticColor3 = 1u << 5u,
    WorldSceneSourceVertexSemanticNormalW = 1u << 6u,
    WorldSceneSourceVertexSemanticBitangent = 1u << 7u,
};

// Remaining source-only channels that are intentionally kept separate from
// WorldMeshVertex. TEXCOORD_1 and TEXCOORD_2 are duplicated in the compact
// stream for source-backed material families while this side stream remains
// the lossless canonical record.
struct WorldSceneSourceVertex {
    std::array<std::array<float, 2>, 3> texcoords{};
    std::array<std::array<float, 4>, 3> colors{};
    float normalW = 1.0f;
    std::array<float, 4> bitangent{};
};

struct WorldSceneSourceTextureBinding {
    std::uint32_t sourceTextureIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::string textureName;
    std::string samplerName;
    std::string textureType;
    std::int32_t textureUnit = 0;
    std::string wrapS;
    std::string wrapT;
    std::string wrapW;
    std::string minFilter;
    std::string magFilter;
    std::array<float, 2> scale{1.0f, 1.0f};
    std::array<float, 2> translate{};
    std::string sourceContainerRelativePath;
    std::string sourceFormat;
    bool sourceIsSrgb = false;
    std::uint32_t sourceArrayCount = 0u;
    std::uint32_t sourceMipCount = 0u;
    const unsigned char* baseRgba = nullptr;
    int baseWidth = 0;
    int baseHeight = 0;
    const WorldTextureMipLevel* mipLevels = nullptr;
    std::uint32_t mipLevelCount = 0u;
    int resolvedWrapS = 10497;
    int resolvedWrapT = 10497;
};

struct WorldSceneGeometry {
    WorldSceneGeometryHandle handle{};
    std::string geometryCacheKey;
    const WorldMeshVertex* vertices = nullptr;
    std::size_t vertexCount = 0u;
    const std::uint32_t* indices = nullptr;
    std::size_t indexCount = 0u;
    const WorldSceneSourceVertex* sourceVertices = nullptr;
    std::size_t sourceVertexCount = 0u;
    std::uint32_t sourceVertexSemanticMask =
        WorldSceneSourceVertexSemanticNone;
    std::uint32_t sourceMeshIndex = 0u;
    std::uint32_t sourcePolygonGroupIndex = 0u;
};

inline bool worldSceneGeometrySourceSemanticsValid(
    const WorldSceneGeometry& geometry) noexcept {
    if (!geometry.sourceVertices) {
        return geometry.sourceVertexCount == 0u &&
               geometry.sourceVertexSemanticMask ==
                   WorldSceneSourceVertexSemanticNone;
    }
    return geometry.sourceVertexCount == geometry.vertexCount;
}

struct WorldSceneMaterial {
    WorldSceneMaterialHandle handle{};
    std::string textureKey;
    std::string textureCacheKey;
    const unsigned char* textureRgba = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    const WorldTextureMipLevel* textureMipLevels = nullptr;
    std::uint32_t textureMipLevelCount = 0u;
    int textureWrapS = 10497;
    int textureWrapT = 10497;
    std::uint8_t textureSrgb = 1u;
    std::string normalTextureKey;
    std::string normalTextureCacheKey;
    const unsigned char* normalTextureRgba = nullptr;
    int normalTextureWidth = 0;
    int normalTextureHeight = 0;
    const WorldTextureMipLevel* normalTextureMipLevels = nullptr;
    std::uint32_t normalTextureMipLevelCount = 0u;
    int normalTextureWrapS = 10497;
    int normalTextureWrapT = 10497;
    std::uint8_t normalTextureSrgb = 0u;
    std::string metallicRoughnessTextureKey;
    std::string metallicRoughnessTextureCacheKey;
    const unsigned char* metallicRoughnessTextureRgba = nullptr;
    int metallicRoughnessTextureWidth = 0;
    int metallicRoughnessTextureHeight = 0;
    const WorldTextureMipLevel* metallicRoughnessTextureMipLevels = nullptr;
    std::uint32_t metallicRoughnessTextureMipLevelCount = 0u;
    int metallicRoughnessTextureWrapS = 10497;
    int metallicRoughnessTextureWrapT = 10497;
    std::uint8_t metallicRoughnessTextureSrgb = 0u;
    std::string occlusionTextureKey;
    std::string occlusionTextureCacheKey;
    const unsigned char* occlusionTextureRgba = nullptr;
    int occlusionTextureWidth = 0;
    int occlusionTextureHeight = 0;
    const WorldTextureMipLevel* occlusionTextureMipLevels = nullptr;
    std::uint32_t occlusionTextureMipLevelCount = 0u;
    int occlusionTextureWrapS = 10497;
    int occlusionTextureWrapT = 10497;
    std::uint8_t occlusionTextureSrgb = 0u;
    std::string emissiveTextureKey;
    std::string emissiveTextureCacheKey;
    const unsigned char* emissiveTextureRgba = nullptr;
    int emissiveTextureWidth = 0;
    int emissiveTextureHeight = 0;
    const WorldTextureMipLevel* emissiveTextureMipLevels = nullptr;
    std::uint32_t emissiveTextureMipLevelCount = 0u;
    int emissiveTextureWrapS = 10497;
    int emissiveTextureWrapT = 10497;
    std::uint8_t emissiveTextureSrgb = 1u;
    std::string environmentTextureKey;
    std::string environmentTextureCacheKey;
    const unsigned char* environmentTextureRgba = nullptr;
    int environmentTextureWidth = 0;
    int environmentTextureHeight = 0;
    const WorldTextureMipLevel* environmentTextureMipLevels = nullptr;
    std::uint32_t environmentTextureMipLevelCount = 0u;
    int environmentTextureWrapS = 10497;
    int environmentTextureWrapT = 10497;
    std::uint8_t environmentTextureSrgb = 0u;
    std::string lightProjectionTextureKey;
    std::string lightProjectionTextureCacheKey;
    const unsigned char* lightProjectionTextureRgba = nullptr;
    int lightProjectionTextureWidth = 0;
    int lightProjectionTextureHeight = 0;
    const WorldTextureMipLevel* lightProjectionTextureMipLevels = nullptr;
    std::uint32_t lightProjectionTextureMipLevelCount = 0u;
    int lightProjectionTextureWrapS = 10497;
    int lightProjectionTextureWrapT = 10497;
    std::uint8_t lightProjectionTextureSrgb = 0u;
    std::array<float, 4> lightProjectionUvRowU{
        -0.00010391304269433f,
        0.0f,
        -0.000276669561862946f,
        0.695972776542572f};
    std::array<float, 4> lightProjectionUvRowV{
        -0.000223165191709995f,
        -0.000349375866353512f,
        0.0000838175788521767f,
        0.692474711333548f};
    std::string projectedShadowTextureKey;
    std::string projectedShadowTextureCacheKey;
    const unsigned char* projectedShadowTextureRgba = nullptr;
    int projectedShadowTextureWidth = 0;
    int projectedShadowTextureHeight = 0;
    int projectedShadowTextureWrapS = 33071;
    int projectedShadowTextureWrapT = 33071;
    std::uint8_t projectedShadowTextureSrgb = 0u;
    std::uint8_t projectedShadowEnabled = 0u;
    float projectedShadowSamplingScale = 1.0f;
    float projectedShadowBias = 0.0f;
    std::array<float, 16> projectedShadowMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
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
    std::uint32_t sourceMaterialIndex = 0u;
    WorldSceneSourceMaterialFamily sourceMaterialFamily =
        WorldSceneSourceMaterialFamily::Unknown;
    std::string sourceMaterialName;
    std::string sourceShaderGroup;
    std::string sourceMetadataJson;
    std::uint32_t sourceSwitchMask = WorldSceneSourceMaterialSwitchNone;
    std::uint32_t sourceEnabledSwitchMask =
        WorldSceneSourceMaterialSwitchNone;
    std::int32_t sourcePreviewBindingIndex = -1;
    std::vector<WorldSceneSourceTextureBinding> sourceTextureBindings;
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
