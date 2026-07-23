#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldBlendPolicy.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

#if defined(_WIN32)

namespace {
using Clock = std::chrono::steady_clock;

void packWorldVsConstants(const float* viewProjectionMatrix4x4,
                          const float* modelMatrix4x4,
                          bool skinningEnabled,
                          std::uint32_t skinMatrixCount,
                          std::uint32_t skinningMode,
                          float clipSpaceDepthBias,
                          float* out40) {
    if (!out40) return;
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* vp = viewProjectionMatrix4x4 ? viewProjectionMatrix4x4 : kIdentity;
    const float* model = modelMatrix4x4 ? modelMatrix4x4 : kIdentity;
    std::memcpy(out40, vp, sizeof(float) * 16u);
    std::memcpy(out40 + 16u, model, sizeof(float) * 16u);
    out40[32] = skinningEnabled ? 1.0f : 0.0f;
    out40[33] = static_cast<float>(skinMatrixCount);
    out40[34] = static_cast<float>(skinningMode);
    out40[35] = 0.0f;
    out40[36] = (clipSpaceDepthBias > 0.0f) ? clipSpaceDepthBias : 0.0f;
    out40[37] = 0.0f;
    out40[38] = 0.0f;
    out40[39] = 0.0f;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

constexpr std::uint32_t kMaxGpuSkinMatrices = 128u;

bool hasPerInstanceSkinning(const IRenderBackend::WorldMeshInstance& instance) {
    return instance.gpuSkinning != 0u &&
           instance.skinMatrices != nullptr &&
           instance.skinMatrixCount > 0u;
}

std::size_t skinMatrixFloatCount(std::uint32_t skinMatrixCount, std::uint8_t gpuSkinningMode) {
    return static_cast<std::size_t>(skinMatrixCount) *
           static_cast<std::size_t>(gpuSkinningMode == 1u ? 32u : 16u);
}

float toMs(const Clock::time_point& start, const Clock::time_point& end) {
    return static_cast<float>(
        std::chrono::duration<double, std::milli>(end - start).count());
}

bool worldDrawPerfLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_DRAW_PERF_LOG");
        if (!env.has_value()) return false;
        return engine::env::flagEnabled("PAC_BACKEND_WORLD_DRAW_PERF_LOG");
    }();
    return enabled;
}

float worldDrawPerfLogThresholdMs() {
    static const float threshold = []() -> float {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_DRAW_PERF_THRESHOLD_MS");
        if (!env.has_value()) return 0.5f;
        return (std::max)(0.0f, static_cast<float>(std::atof(env->c_str())));
    }();
    return threshold;
}

int worldDrawPerfLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_DRAW_PERF_LOG_MAX");
        if (!env.has_value()) return 24;
        return (std::max)(1, std::atoi(env->c_str()));
    }();
    return maxEntries;
}

bool consumeWorldDrawPerfLogSlot() {
    static int emitted = 0;
    if (emitted >= worldDrawPerfLogMaxEntries()) return false;
    ++emitted;
    return true;
}

const char* worldTextureKeyForLog(const IRenderBackend::WorldTextureData* texture) {
    if (!texture) return "<none>";
    if (texture->key && texture->key[0] != '\0') return texture->key;
    if (texture->cacheKey && texture->cacheKey[0] != '\0') return texture->cacheKey;
    return "<generated>";
}

void maybeLogWorldDrawPerf(std::uint64_t frameCounter,
                           const char* path,
                           const char* outcome,
                           const char* reason,
                           const char* geometryKey,
                           const IRenderBackend::WorldTextureData* texture,
                           std::size_t instanceCount,
                           bool hadCachedMesh,
                           float materialPrepMs,
                           float meshPrepareMs,
                           float instancePrepMs,
                           float internalDrawMs,
                           float totalMs) {
    if (!worldDrawPerfLogEnabled() ||
        frameCounter == 0u ||
        totalMs < worldDrawPerfLogThresholdMs() ||
        !consumeWorldDrawPerfLogSlot()) {
        return;
    }

    std::ostringstream msg;
    msg << std::fixed << std::setprecision(3)
        << "[WorldDrawPerf][D3D12] path=" << (path ? path : "<unknown>")
        << " outcome=" << (outcome ? outcome : "<unknown>")
        << " reason=" << (reason ? reason : "ok")
        << " frame=" << frameCounter
        << " geometry=" << ((geometryKey && geometryKey[0] != '\0') ? geometryKey : "<dynamic>")
        << " texture=" << worldTextureKeyForLog(texture)
        << " inst=" << instanceCount
        << " had_cached_mesh=" << (hadCachedMesh ? 1 : 0)
        << " alpha=" << static_cast<int>(texture ? texture->alphaMode : 0u)
        << " blend=" << static_cast<int>(texture ? texture->blendMode : 0u)
        << " material=" << static_cast<int>(texture ? texture->materialMode : 0u)
        << " material_ms=" << materialPrepMs
        << " mesh_ms=" << meshPrepareMs
        << " instance_ms=" << instancePrepMs
        << " internal_ms=" << internalDrawMs
        << " total_ms=" << totalMs;
    std::cout << msg.str() << "\n";
}

void maybeLogPbrBindingD3D12(const IRenderBackend::WorldTextureData* texture,
                             bool hasBase,
                             bool hasNormal,
                             bool hasMetallicRoughness,
                             bool hasOcclusion,
                             bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][D3D12] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

void packWorldInstanceVertexData(const IRenderBackend::WorldMeshInstance& instance,
                                 WorldInstanceVertexData& out) {
    out.model0x = instance.modelMatrix[0];
    out.model0y = instance.modelMatrix[1];
    out.model0z = instance.modelMatrix[2];
    out.model0w = instance.modelMatrix[3];
    out.model1x = instance.modelMatrix[4];
    out.model1y = instance.modelMatrix[5];
    out.model1z = instance.modelMatrix[6];
    out.model1w = instance.modelMatrix[7];
    out.model2x = instance.modelMatrix[8];
    out.model2y = instance.modelMatrix[9];
    out.model2z = instance.modelMatrix[10];
    out.model2w = instance.modelMatrix[11];
    out.model3x = instance.modelMatrix[12];
    out.model3y = instance.modelMatrix[13];
    out.model3z = instance.modelMatrix[14];
    out.model3w = instance.modelMatrix[15];
    out.colorR = instance.vertexColorMulR;
    out.colorG = instance.vertexColorMulG;
    out.colorB = instance.vertexColorMulB;
    out.colorA = instance.vertexColorMulA;
    out.skinEnabled = 0u;
    out.skinMatrixCount = 0u;
    out.skinningMode = 0u;
    out.skinFloat4Offset = 0u;
}
} // namespace

#endif

#if defined(_WIN32)
ID3D12PipelineState* D3D12RenderBackend::selectWorldPipelineState(
    const WorldTextureData* textureData) const {
    const auto blendState = engine::render::world_blend::resolve(
        textureData ? textureData->alphaMode : 0u,
        textureData ? textureData->blendMode : 0u,
        !textureData || textureData->depthTestEnabled != 0u,
        textureData && textureData->dualSourceBlendEnabled != 0u,
        true);
    if (!blendState.enabled) return worldPipelineState_.Get();

    if (blendState.dualSourceEnabled) {
        if (blendState.depthTestEnabled) {
            return blendState.mode == engine::render::world_blend::Mode::Additive
                ? worldDualSourceAdditiveBlendPipelineState_.Get()
                : worldDualSourceBlendPipelineState_.Get();
        }
        return blendState.mode == engine::render::world_blend::Mode::Additive
            ? worldNoDepthDualSourceAdditiveBlendPipelineState_.Get()
            : worldNoDepthDualSourceBlendPipelineState_.Get();
    }

    if (blendState.depthTestEnabled) {
        if (blendState.mode == engine::render::world_blend::Mode::Additive) {
            return worldAdditiveBlendPipelineState_.Get();
        }
        if (blendState.mode == engine::render::world_blend::Mode::Premultiplied) {
            return worldPremultipliedBlendPipelineState_.Get();
        }
        return worldBlendPipelineState_.Get();
    }
    if (blendState.mode == engine::render::world_blend::Mode::Additive) {
        return worldNoDepthAdditiveBlendPipelineState_.Get();
    }
    if (blendState.mode == engine::render::world_blend::Mode::Premultiplied) {
        return worldNoDepthPremultipliedBlendPipelineState_.Get();
    }
    return worldNoDepthBlendPipelineState_.Get();
}
#endif

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
#if defined(_WIN32)
    const bool perfLog = worldDrawPerfLogEnabled();
    Clock::time_point materialPrepStart{};
    if (perfLog) materialPrepStart = Clock::now();
    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            texture,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }
    const float materialPrepMs = perfLog ? toMs(materialPrepStart, Clock::now()) : 0.0f;

    drawWorldIndexedMeshTexturedCachedPreparedInstanced(
        geometryKey,
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        texture,
        useTexture,
        instances,
        instanceCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight,
        materialPrepMs);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)instances;
    (void)instanceCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

#if defined(_WIN32)
void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedPreparedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    std::uint32_t materialDescriptorBlockIndex,
    const WorldTextureData* texture,
    float useTexture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight,
    float materialPrepMs) {
    const bool perfLog = worldDrawPerfLogEnabled();
    Clock::time_point totalStart{};
    if (perfLog) totalStart = Clock::now();
    const bool hadCachedMesh =
        perfLog &&
        geometryKey && geometryKey[0] != '\0' &&
        cachedWorldMeshes_.find(geometryKey) != cachedWorldMeshes_.end();
    float meshPrepareMs = 0.0f;
    float instancePrepMs = 0.0f;
    float internalDrawMs = 0.0f;
    const auto maybeLog = [&](const char* outcome, const char* reason) {
        if (!perfLog) return;
        maybeLogWorldDrawPerf(
            frameCounter_,
            "cached_instanced",
            outcome,
            reason,
            geometryKey,
            texture,
            instanceCount,
            hadCachedMesh,
            materialPrepMs,
            meshPrepareMs,
            instancePrepMs,
            internalDrawMs,
            materialPrepMs + toMs(totalStart, Clock::now()));
    };

    if (!instances || instanceCount == 0u) {
        maybeLog("fallback", "empty_instances");
        drawWorldIndexedMeshTexturedCached(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    if (!geometryKey || geometryKey[0] == '\0' || !worldInstanceBuffer_ || !worldInstanceMappedData_) {
        maybeLog("fallback", "uncached_or_no_instance_buffer");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    Clock::time_point meshPrepareStart{};
    if (perfLog) meshPrepareStart = Clock::now();
    CachedWorldMesh* cached =
        ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (perfLog) meshPrepareMs = toMs(meshPrepareStart, Clock::now());
    if (!cached || !cached->valid) {
        maybeLog("fallback", "mesh_cache_prepare_failed");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    if (instanceCount > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
        maybeLog("fallback", "instance_count_overflow");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    bool anyPerInstanceSkinning = false;
    bool allInstancesCarrySkinning = true;
    std::size_t instanceSkinFloatCountTotal = 0u;
    for (std::size_t i = 0; i < instanceCount; ++i) {
        const WorldMeshInstance& instance = instances[i];
        const bool instanceHasSkinning = hasPerInstanceSkinning(instance);
        if (instance.gpuSkinning != 0u && !instanceHasSkinning) {
            maybeLog("fallback", "missing_per_instance_skinning");
            IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
                geometryKey,
                vertices,
                vertexCount,
                indices,
                indexCount,
                texture,
                instances,
                instanceCount,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
            return;
        }
        anyPerInstanceSkinning |= instanceHasSkinning;
        allInstancesCarrySkinning &= instanceHasSkinning;
        if (!instanceHasSkinning) {
            continue;
        }
        if (instance.skinMatrixCount > kMaxGpuSkinMatrices ||
            instance.gpuSkinningMode > 1u) {
            IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
                geometryKey,
                vertices,
                vertexCount,
                indices,
                indexCount,
                texture,
                instances,
                instanceCount,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
            return;
        }
        instanceSkinFloatCountTotal +=
            skinMatrixFloatCount(instance.skinMatrixCount, instance.gpuSkinningMode);
    }
    if (anyPerInstanceSkinning &&
        (!allInstancesCarrySkinning || (texture && texture->gpuSkinning != 0u))) {
        maybeLog("fallback", "mixed_skinning_modes");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    const std::size_t instanceBytes = instanceCount * sizeof(WorldInstanceVertexData);
    const std::size_t instanceWriteOffset = static_cast<std::size_t>(worldInstanceFrameOffset_);
    const std::size_t instanceFrameEnd =
        static_cast<std::size_t>(worldInstanceFrameBaseOffset_) +
        static_cast<std::size_t>(worldInstanceBufferBytesPerFrame_);
    if (instanceWriteOffset + instanceBytes > instanceFrameEnd) {
        maybeLog("fallback", "instance_ring_exhausted");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    const std::size_t instanceSkinBytes =
        instanceSkinFloatCountTotal * sizeof(float);
    const std::size_t skinWriteOffset = anyPerInstanceSkinning
        ? alignUp(static_cast<std::size_t>(worldSkinMatrixFrameOffset_), 256u)
        : 0u;
    const std::size_t skinWriteEnd = anyPerInstanceSkinning
        ? (skinWriteOffset + alignUp(instanceSkinBytes, 256u))
        : static_cast<std::size_t>(worldSkinMatrixFrameOffset_);
    const std::size_t skinFrameEnd =
        static_cast<std::size_t>(worldSkinMatrixFrameBaseOffset_) +
        static_cast<std::size_t>(worldSkinMatrixBufferBytesPerFrame_);
    if (anyPerInstanceSkinning && skinWriteEnd > skinFrameEnd) {
        maybeLog("fallback", "skin_ring_exhausted");
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    auto* instanceData =
        reinterpret_cast<WorldInstanceVertexData*>(worldInstanceMappedData_ + instanceWriteOffset);
    float* instanceSkinData = anyPerInstanceSkinning
        ? reinterpret_cast<float*>(worldSkinMatrixMappedData_ + skinWriteOffset)
        : nullptr;
    Clock::time_point instancePrepStart{};
    if (perfLog) instancePrepStart = Clock::now();
    std::size_t instanceSkinFloatOffset = 0u;
    for (std::size_t i = 0; i < instanceCount; ++i) {
        packWorldInstanceVertexData(instances[i], instanceData[i]);
        if (!anyPerInstanceSkinning) {
            continue;
        }
        const WorldMeshInstance& instance = instances[i];
        const std::uint8_t gpuSkinningMode = std::min<std::uint8_t>(instance.gpuSkinningMode, 1u);
        const std::size_t floatCount =
            skinMatrixFloatCount(instance.skinMatrixCount, gpuSkinningMode);
        std::memcpy(instanceSkinData + instanceSkinFloatOffset,
                    instance.skinMatrices,
                    floatCount * sizeof(float));
        instanceData[i].skinEnabled = 1u;
        instanceData[i].skinMatrixCount = instance.skinMatrixCount;
        instanceData[i].skinningMode = gpuSkinningMode;
        instanceData[i].skinFloat4Offset =
            static_cast<std::uint32_t>(((skinWriteOffset -
                                         static_cast<std::size_t>(worldSkinMatrixFrameBaseOffset_)) /
                                        (sizeof(float) * 4u)) +
                                       (instanceSkinFloatOffset / 4u));
        instanceSkinFloatOffset += floatCount;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress =
        worldInstanceBufferGpuAddress_ + static_cast<std::uint64_t>(instanceWriteOffset);
    worldInstanceFrameOffset_ =
        static_cast<std::uint32_t>(instanceWriteOffset + instanceBytes);
    if (anyPerInstanceSkinning) {
        worldSkinMatrixFrameOffset_ = static_cast<std::uint32_t>(skinWriteEnd);
    }

    IRenderBackend::WorldTextureData perInstanceTexture{};
    const WorldTextureData* drawTexture = texture;
    if (anyPerInstanceSkinning && texture) {
        perInstanceTexture = *texture;
        perInstanceTexture.gpuSkinning = 0u;
        perInstanceTexture.gpuSkinningMode = 0u;
        perInstanceTexture.skinMatrixCount = 0u;
        perInstanceTexture.skinMatrices = nullptr;
        drawTexture = &perInstanceTexture;
    }

    if (perfLog) instancePrepMs = toMs(instancePrepStart, Clock::now());
    Clock::time_point internalDrawStart{};
    if (perfLog) internalDrawStart = Clock::now();
    drawWorldIndexedMeshTexturedCachedInternal(
        *cached,
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        drawTexture,
        useTexture,
        viewProjectionMatrix4x4,
        instanceDataGpuAddress,
        static_cast<std::uint32_t>(instanceCount),
        surfaceWidth,
        surfaceHeight);
    if (perfLog) internalDrawMs = toMs(internalDrawStart, Clock::now());
    maybeLog("ok", "record");
}
#endif

void D3D12RenderBackend::drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
                                                      std::size_t vertexCount,
                                                      const std::uint32_t* indices,
                                                      std::size_t indexCount,
                                                      std::uint32_t materialDescriptorBlockIndex,
                                                      const WorldTextureData* textureData,
                                                      float useTexture,
                                                      const float* viewProjectionMatrix4x4,
                                                      int surfaceWidth,
                                                      int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !vertices || !indices || vertexCount == 0 || indexCount == 0 || !viewProjectionMatrix4x4) {
        return;
    }
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVertexBuffer_ ||
        !worldIndexBuffer_ ||
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVertexMappedData_ ||
        !worldIndexMappedData_ ||
        !worldVsConstantMappedData_ ||
        !worldSkinMatrixMappedData_) {
        return;
    }

    const std::size_t maxVertexCapacity =
        static_cast<std::size_t>(worldVertexBufferBytesPerFrame_) / sizeof(WorldVertex);
    const std::size_t maxIndexCapacity =
        static_cast<std::size_t>(worldIndexBufferBytesPerFrame_) / sizeof(std::uint32_t);
    const std::size_t vertexFrameEnd =
        static_cast<std::size_t>(worldVertexFrameBaseOffset_) +
        static_cast<std::size_t>(worldVertexBufferBytesPerFrame_);
    const std::size_t indexFrameEnd =
        static_cast<std::size_t>(worldIndexFrameBaseOffset_) +
        static_cast<std::size_t>(worldIndexBufferBytesPerFrame_);
    const std::size_t vsConstantsFrameEnd =
        static_cast<std::size_t>(worldVsConstantFrameBaseOffset_) +
        static_cast<std::size_t>(worldVsConstantBufferBytesPerFrame_);
    const std::size_t skinFrameEnd =
        static_cast<std::size_t>(worldSkinMatrixFrameBaseOffset_) +
        static_cast<std::size_t>(worldSkinMatrixBufferBytesPerFrame_);
    const std::size_t safeVertexCount = (std::min)(vertexCount, maxVertexCapacity);
    const std::size_t safeIndexCount = (std::min)(indexCount, maxIndexCapacity);
    if (safeVertexCount == 0 || safeIndexCount < 3u) return;
    // Refuse truncated indexed draws instead of rendering corrupted geometry when
    // a mesh exceeds the shared world dynamic buffer capacity.
    if (safeVertexCount != vertexCount || safeIndexCount != indexCount) {
        return;
    }

    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0) return;
    const std::size_t vertexWriteOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t indexWriteOffset = alignUp(static_cast<std::size_t>(worldIndexFrameOffset_), 256u);
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vertexWriteOffset + vertexBytes > vertexFrameEnd ||
        indexWriteOffset + indexBytes > indexFrameEnd ||
        vsConstantsWriteOffset + 256u > vsConstantsFrameEnd) {
        return;
    }

    std::memcpy(worldVertexMappedData_ + vertexWriteOffset, vertices, vertexBytes);
    auto* outIndices = reinterpret_cast<std::uint32_t*>(worldIndexMappedData_ + indexWriteOffset);
    std::memcpy(outIndices, indices, indexBytes);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress =
        worldSkinMatrixBufferGpuAddress_ +
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    if (gpuSkinningEnabled) {
        if (textureData->skinMatrices == lastWorldSkinMatrices_ &&
            gpuSkinningMode == lastWorldSkinningMode_ &&
            gpuSkinMatrixCount == lastWorldSkinMatrixCount_) {
            skinMatrixGpuAddress = lastWorldSkinMatrixGpuAddress_;
        } else {
            const std::size_t copyBytes =
                static_cast<std::size_t>(gpuSkinMatrixCount) *
                (gpuSkinningMode == 1u ? 32u : 16u) * sizeof(float);
            const std::size_t skinWriteOffset =
                alignUp(static_cast<std::size_t>(worldSkinMatrixFrameOffset_), 256u);
            const std::size_t skinWriteEnd = skinWriteOffset + alignUp(copyBytes, 256u);
            if (skinWriteEnd <= skinFrameEnd) {
                std::memcpy(
                    worldSkinMatrixMappedData_ + skinWriteOffset,
                    textureData->skinMatrices,
                    copyBytes);
                skinMatrixGpuAddress += static_cast<std::uint64_t>(skinWriteOffset);
                worldSkinMatrixFrameOffset_ = static_cast<UINT>(skinWriteEnd);
                lastWorldSkinMatrices_ = textureData->skinMatrices;
                lastWorldSkinningMode_ = static_cast<std::uint8_t>(gpuSkinningMode);
                lastWorldSkinMatrixCount_ = gpuSkinMatrixCount;
                lastWorldSkinMatrixGpuAddress_ = skinMatrixGpuAddress;
            } else {
                gpuSkinningEnabled = false;
                gpuSkinMatrixCount = 0u;
            }
        }
    }

    float vsConstants[40] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        textureData ? textureData->clipSpaceDepthBias : 0.0f,
        vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootShaderResourceView(2, skinMatrixGpuAddress);
    commandList_->SetGraphicsRootShaderResourceView(
        4,
        worldInstanceBufferGpuAddress_ +
            static_cast<std::uint64_t>(worldInstanceFrameBaseOffset_));
    WorldPsConstants worldPs = makeWorldPsConstants(textureData, useTexture);
    if (textureData && textureData->materialMode >= 2u) {
        // Reuse an unused packed slot in lit model mode for shader debug-view selection.
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    materialHandle.ptr += static_cast<SIZE_T>(materialDescriptorBlockIndex) *
                          static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, materialHandle);
    frameIndexedD3d12DescriptorTableSets_ += 1u;
    ID3D12PipelineState* pso = selectWorldPipelineState(textureData);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(vertexWriteOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = worldIndexBufferGpuAddress_ + static_cast<std::uint64_t>(indexWriteOffset);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(indexBytes);
    commandList_->IASetIndexBuffer(&ibv);

    const bool drawCharacterOutline =
        textureData &&
        textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u &&
        safeVertexCount > 0u;
    // The textured surface must follow the inverted hull because blended
    // character materials do not guarantee a depth write.
    if (drawCharacterOutline) {
        WorldPsConstants outlinePs = makeWorldPsConstants(textureData, 0.0f);
        outlinePs.materialMode = 3.0f;
        commandList_->SetGraphicsRoot32BitConstants(
            1,
            static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
            &outlinePs,
            0);
        commandList_->SetPipelineState(
            textureData->depthTestEnabled != 0u
                ? worldBlendPipelineState_.Get()
                : worldNoDepthBlendPipelineState_.Get());
        ++frameIndexedD3d12PsoSets_;
        commandList_->DrawIndexedInstanced(
            static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);
    }
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    commandList_->SetPipelineState(pso);
    ++frameIndexedD3d12PsoSets_;
    commandList_->DrawIndexedInstanced(
        static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);

    worldVertexFrameOffset_ = static_cast<UINT>(vertexWriteOffset + vertexBytes);
    worldIndexFrameOffset_ = static_cast<UINT>(indexWriteOffset + indexBytes);
    worldVsConstantFrameOffset_ = static_cast<UINT>(vsConstantsWriteOffset + 256u);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)materialDescriptorBlockIndex;
    (void)textureData;
    (void)useTexture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

#if defined(_WIN32)
void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInternal(
    const CachedWorldMesh& mesh,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    std::uint32_t materialDescriptorBlockIndex,
    const WorldTextureData* textureData,
    float useTexture,
    const float* viewProjectionMatrix4x4,
    D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress,
    std::uint32_t instanceCount,
    int surfaceWidth,
    int surfaceHeight) {
    if (!recording_ || !viewProjectionMatrix4x4) return;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    if (!mesh.valid || !mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount < 3u) return;
    if (instanceCount == 0u || instanceDataGpuAddress == 0u) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVsConstantMappedData_ || !worldSkinMatrixMappedData_) return;

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    const std::size_t vsConstantsFrameEnd =
        static_cast<std::size_t>(worldVsConstantFrameBaseOffset_) +
        static_cast<std::size_t>(worldVsConstantBufferBytesPerFrame_);
    const std::size_t skinFrameEnd =
        static_cast<std::size_t>(worldSkinMatrixFrameBaseOffset_) +
        static_cast<std::size_t>(worldSkinMatrixBufferBytesPerFrame_);

    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress =
        worldSkinMatrixBufferGpuAddress_ +
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    if (gpuSkinningEnabled) {
        if (textureData->skinMatrices == lastWorldSkinMatrices_ &&
            gpuSkinningMode == lastWorldSkinningMode_ &&
            gpuSkinMatrixCount == lastWorldSkinMatrixCount_) {
            skinMatrixGpuAddress = lastWorldSkinMatrixGpuAddress_;
        } else {
            const std::size_t copyBytes =
                static_cast<std::size_t>(gpuSkinMatrixCount) *
                (gpuSkinningMode == 1u ? 32u : 16u) * sizeof(float);
            const std::size_t skinWriteOffset =
                alignUp(static_cast<std::size_t>(worldSkinMatrixFrameOffset_), 256u);
            const std::size_t skinWriteEnd = skinWriteOffset + alignUp(copyBytes, 256u);
            if (skinWriteEnd <= skinFrameEnd) {
                std::memcpy(
                    worldSkinMatrixMappedData_ + skinWriteOffset,
                    textureData->skinMatrices,
                    copyBytes);
                skinMatrixGpuAddress += static_cast<std::uint64_t>(skinWriteOffset);
                worldSkinMatrixFrameOffset_ = static_cast<UINT>(skinWriteEnd);
                lastWorldSkinMatrices_ = textureData->skinMatrices;
                lastWorldSkinningMode_ = static_cast<std::uint8_t>(gpuSkinningMode);
                lastWorldSkinMatrixCount_ = gpuSkinMatrixCount;
                lastWorldSkinMatrixGpuAddress_ = skinMatrixGpuAddress;
            } else {
                gpuSkinningEnabled = false;
                gpuSkinMatrixCount = 0u;
            }
        }
    }

    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vsConstantsWriteOffset + 256u > vsConstantsFrameEnd) return;

    float vsConstants[40] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        textureData ? textureData->clipSpaceDepthBias : 0.0f,
        vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootShaderResourceView(2, skinMatrixGpuAddress);
    commandList_->SetGraphicsRootShaderResourceView(4, instanceDataGpuAddress);
    WorldPsConstants worldPs = makeWorldPsConstants(textureData, useTexture);
    if (textureData && textureData->materialMode >= 2u) {
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    materialHandle.ptr += static_cast<SIZE_T>(materialDescriptorBlockIndex) *
                          static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, materialHandle);
    frameIndexedD3d12DescriptorTableSets_ += 1u;

    ID3D12PipelineState* pso = selectWorldPipelineState(textureData);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = mesh.vertexGpuAddress;
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(mesh.vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = mesh.indexGpuAddress;
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(mesh.indexBytes);
    commandList_->IASetIndexBuffer(&ibv);

    const bool drawCharacterOutline =
        textureData &&
        textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u;
    // Keep the cached path in the same outline-then-surface order.
    if (drawCharacterOutline) {
        WorldPsConstants outlinePs = makeWorldPsConstants(textureData, 0.0f);
        outlinePs.materialMode = 3.0f;
        commandList_->SetGraphicsRoot32BitConstants(
            1,
            static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
            &outlinePs,
            0);
        commandList_->SetPipelineState(
            textureData->depthTestEnabled != 0u
                ? worldBlendPipelineState_.Get()
                : worldNoDepthBlendPipelineState_.Get());
        ++frameIndexedD3d12PsoSets_;
        commandList_->DrawIndexedInstanced(
            static_cast<UINT>(mesh.indexCount), instanceCount, 0, 0, 0);
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u) *
                           static_cast<std::uint64_t>(instanceCount);
    }
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    commandList_->SetPipelineState(pso);
    ++frameIndexedD3d12PsoSets_;
    commandList_->DrawIndexedInstanced(
        static_cast<UINT>(mesh.indexCount), instanceCount, 0, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u) *
                       static_cast<std::uint64_t>(instanceCount);

    worldVsConstantFrameOffset_ = static_cast<UINT>(vsConstantsWriteOffset + 256u);
}
#endif
