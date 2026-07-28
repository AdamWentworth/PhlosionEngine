#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/DebugGeometry.h"
#include "engine/render/IRenderBackend.h"

namespace engine::render::d3d12_internal {

using DebugVertex = engine::render::debug::Vertex2D;

struct SpriteInstanceData {
    float x;
    float y;
    float w;
    float h;
    float u0;
    float v0;
    float u1;
    float v1;
    float r;
    float g;
    float b;
    float a;
};

struct WorldVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    float nx;
    float ny;
    float nz;
    float joint0;
    float joint1;
    float joint2;
    float joint3;
    float weight0;
    float weight1;
    float weight2;
    float weight3;
    float tx;
    float ty;
    float tz;
    float tw;
    float sourceUv1U;
    float sourceUv1V;
    float sourceUv2U;
    float sourceUv2V;
};

struct WorldInstanceVertexData {
    float model0x;
    float model0y;
    float model0z;
    float model0w;
    float model1x;
    float model1y;
    float model1z;
    float model1w;
    float model2x;
    float model2y;
    float model2z;
    float model2w;
    float model3x;
    float model3y;
    float model3z;
    float model3w;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    std::uint32_t skinEnabled;
    std::uint32_t skinMatrixCount;
    std::uint32_t skinningMode;
    std::uint32_t skinFloat4Offset;
};

static_assert(
    sizeof(WorldVertex) == sizeof(IRenderBackend::WorldMeshVertex),
    "WorldVertex and WorldMeshVertex layout must match for fast memcpy upload path.");
static_assert(std::is_trivially_copyable_v<WorldVertex>, "WorldVertex must be trivially copyable.");
static_assert(
    std::is_trivially_copyable_v<IRenderBackend::WorldMeshVertex>,
    "WorldMeshVertex must be trivially copyable.");

static_assert(std::is_trivially_copyable_v<SpriteInstanceData>, "SpriteInstanceData must be trivially copyable.");

inline constexpr std::size_t kMaxSpriteQuads = 2048;
inline constexpr std::size_t kMaxDebugQuads = 4096;
inline constexpr std::size_t kMaxDebugLines = 8192;
inline constexpr std::size_t kMaxDebugTriangles = 65536;
inline constexpr std::size_t kMaxDebugVertices = kMaxDebugTriangles * 3;
// Shared world path now also carries large animated capture props (e.g. animated pokeball.glb).
// The previous cap could truncate indexed draws in D3D12, producing corrupted/partial meshes.
inline constexpr std::size_t kMaxWorldTriangles = 900000;
inline constexpr std::size_t kMaxWorldVertices = kMaxWorldTriangles * 3;
inline constexpr std::size_t kMaxWorldIndices = kMaxWorldTriangles * 3;
// D3D12 world-material descriptor blocks consume additional shader-visible SRVs
// beyond the raw texture descriptors, so keep extra headroom for prewarmed
// world materials plus runtime-loaded effects.
inline constexpr std::size_t kMaxSrvDescriptors = 4096;
inline constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";
inline constexpr int kGlRepeat = 10497;
inline constexpr int kGlMirroredRepeat = 33648;
inline constexpr int kGlClampToEdge = 33071;

inline std::size_t alignUp(std::size_t value, std::size_t alignment) {
    if (alignment == 0u) return value;
    const std::size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

inline std::size_t frameSliceBase(std::uint32_t frameIndex, std::size_t bytesPerFrame) {
    return static_cast<std::size_t>(frameIndex) * bytesPerFrame;
}

inline std::size_t frameSliceEnd(std::uint32_t frameIndex, std::size_t bytesPerFrame) {
    return frameSliceBase(frameIndex, bytesPerFrame) + bytesPerFrame;
}

inline float sanitizeWrapMode(int wrapMode) {
    if (wrapMode == kGlClampToEdge || wrapMode == kGlMirroredRepeat || wrapMode == kGlRepeat) {
        return static_cast<float>(wrapMode);
    }
    return static_cast<float>(kGlRepeat);
}

struct WorldPsConstants {
    float useTexture = 0.0f;
    float wrapS = static_cast<float>(kGlRepeat);
    float wrapT = static_cast<float>(kGlRepeat);
    float alphaMode = 0.0f;
    float alphaCutoff = 0.5f;
    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    float materialMode = 0.0f;
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

inline WorldPsConstants makeWorldPsConstants(const IRenderBackend::WorldTextureData* textureData, float useTexture) {
    WorldPsConstants constants;
    constants.useTexture = useTexture;
    if (!textureData) return constants;
    constants.wrapS = sanitizeWrapMode(textureData->wrapS);
    constants.wrapT = sanitizeWrapMode(textureData->wrapT);
    constants.alphaMode = static_cast<float>(std::min<std::uint8_t>(2u, textureData->alphaMode));
    constants.alphaCutoff = std::clamp(textureData->alphaCutoff, 0.0f, 1.0f);
    constants.alphaWindowMin = std::clamp(textureData->alphaWindowMin, 0.0f, 1.0f);
    constants.alphaWindowMax = std::clamp(textureData->alphaWindowMax, 0.0f, 1.0f);
    constants.vertexColorMulR = textureData->vertexColorMulR;
    constants.vertexColorMulG = textureData->vertexColorMulG;
    constants.vertexColorMulB = textureData->vertexColorMulB;
    constants.vertexColorMulA = textureData->vertexColorMulA;
    constants.materialMode = static_cast<float>(textureData->materialMode);
    constants.materialTimeSec = textureData->materialTimeSec;
    constants.materialFlags = textureData->materialFlags;
    constants.materialAtlasWidth = (std::max)(0.0f, textureData->materialAtlasWidth);
    constants.materialAtlasHeight = (std::max)(0.0f, textureData->materialAtlasHeight);
    constants.materialRect0U = textureData->materialRect0U;
    constants.materialRect0V = textureData->materialRect0V;
    constants.materialRect0W = textureData->materialRect0W;
    constants.materialRect0H = textureData->materialRect0H;
    constants.materialRect1U = textureData->materialRect1U;
    constants.materialRect1V = textureData->materialRect1V;
    constants.materialRect1W = textureData->materialRect1W;
    constants.materialRect1H = textureData->materialRect1H;
    constants.materialFlipbook0Cols = textureData->materialFlipbook0Cols;
    constants.materialFlipbook0Rows = textureData->materialFlipbook0Rows;
    constants.materialFlipbook0Frames = textureData->materialFlipbook0Frames;
    constants.materialFlipbook0Fps = textureData->materialFlipbook0Fps;
    constants.materialFlipbook1Cols = textureData->materialFlipbook1Cols;
    constants.materialFlipbook1Rows = textureData->materialFlipbook1Rows;
    constants.materialFlipbook1Frames = textureData->materialFlipbook1Frames;
    constants.materialFlipbook1Fps = textureData->materialFlipbook1Fps;
    // D3D12 root signature is constrained to 64 DWORD. For lit model mode (materialMode >= 2),
    // repurpose fire-tail payload slots to carry PBR/camera data needed for three-gltf-viewer parity.
    if (textureData->materialMode >= 2u) {
        const bool hasNormal =
            textureData->normalRgba && textureData->normalWidth > 0 && textureData->normalHeight > 0;
        const bool hasMetallicRoughness =
            textureData->metallicRoughnessRgba &&
            textureData->metallicRoughnessWidth > 0 &&
            textureData->metallicRoughnessHeight > 0;
        const bool hasOcclusion =
            textureData->occlusionRgba && textureData->occlusionWidth > 0 && textureData->occlusionHeight > 0;
        const bool hasEmissive =
            textureData->emissiveRgba && textureData->emissiveWidth > 0 && textureData->emissiveHeight > 0;

        std::uint32_t pbrFlags = 0u;
        if (hasNormal) pbrFlags |= 1u << 0;             // useNormalTexture
        if (hasMetallicRoughness) pbrFlags |= 1u << 1;  // useMetallicRoughnessTexture
        if (hasOcclusion) pbrFlags |= 1u << 2;          // useOcclusionTexture
        if (hasEmissive) pbrFlags |= 1u << 3;           // useEmissiveTexture
        constants.materialFlags = static_cast<float>(pbrFlags);

        // PBR factor packing.
        constants.materialAtlasWidth = (std::max)(0.0f, textureData->normalScale);
        constants.materialAtlasHeight = std::clamp(textureData->metallicFactor, 0.0f, 1.0f);
        constants.materialRect0U = std::clamp(textureData->roughnessFactor, 0.0f, 1.0f);
        constants.materialRect0V = std::clamp(textureData->occlusionStrength, 0.0f, 1.0f);
        constants.materialRect0W = (std::max)(0.0f, textureData->emissiveFactorR);
        constants.materialRect0H = (std::max)(0.0f, textureData->emissiveFactorG);
        constants.materialRect1U = (std::max)(0.0f, textureData->emissiveFactorB);

        // Camera packing.
        constants.materialRect1V = textureData->cameraPosX;
        constants.materialRect1W = textureData->cameraPosY;
        constants.materialRect1H = textureData->cameraPosZ;
        constants.materialFlipbook0Cols = textureData->cameraForwardX;
        constants.materialFlipbook0Rows = textureData->cameraForwardY;
        constants.materialFlipbook0Frames = textureData->cameraForwardZ;
        constants.materialFlipbook0Fps = textureData->cameraTargetX;
        constants.materialFlipbook1Cols = textureData->cameraTargetY;
        constants.materialFlipbook1Rows = textureData->cameraTargetZ;

        // FieldTreeShader05 mode 6 uses a typed source-material payload, not
        // the generic PBR interpretation above. Preserve its raw specialized
        // values and move Shadow_Color into otherwise-unused PS multiplier
        // constants without changing the vertex instance color.
        if (textureData->materialMode == 6u) {
            constants.vertexColorMulR = textureData->normalScale;
            constants.vertexColorMulG = textureData->metallicFactor;
            constants.vertexColorMulB = textureData->roughnessFactor;
            constants.vertexColorMulA = 1.0f;
            constants.materialFlags = textureData->materialFlags;
            constants.materialAtlasWidth = textureData->materialAtlasWidth;
            constants.materialAtlasHeight = textureData->materialAtlasHeight;
            constants.materialRect0U = textureData->materialRect0U;
            constants.materialRect0V = textureData->materialRect0V;
            constants.materialRect0W = textureData->materialRect0W;
            constants.materialRect0H = textureData->materialRect0H;
            constants.materialRect1U = textureData->materialRect1U;
            constants.materialFlipbook0Fps =
                textureData->materialFlipbook0Fps;
        }
    }

    return constants;
}

} // namespace engine::render::d3d12_internal

