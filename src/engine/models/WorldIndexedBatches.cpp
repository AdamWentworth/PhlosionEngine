#include "engine/models/WorldIndexedBatches.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "engine/core/Environment.h"

namespace engine::models::batches {

namespace {

using Clock = std::chrono::steady_clock;

float elapsedMs(Clock::time_point start, Clock::time_point end) {
    return static_cast<float>(
        std::chrono::duration<double, std::milli>(end - start).count());
}

bool indexedSubmitPerfLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PHLOSION_WORLD_INDEXED_SUBMIT_PERF_LOG");
        if (!env.has_value()) return false;
        return engine::env::flagEnabled("PHLOSION_WORLD_INDEXED_SUBMIT_PERF_LOG");
    }();
    return enabled;
}

float indexedSubmitPerfLogThresholdMs() {
    static const float threshold = []() -> float {
        const auto env = engine::env::get("PHLOSION_WORLD_INDEXED_SUBMIT_PERF_THRESHOLD_MS");
        if (!env.has_value()) return 2.0f;
        return (std::max)(0.0f, static_cast<float>(std::atof(env->c_str())));
    }();
    return threshold;
}

int indexedSubmitPerfLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PHLOSION_WORLD_INDEXED_SUBMIT_PERF_LOG_MAX");
        if (!env.has_value()) return 32;
        return (std::max)(1, std::atoi(env->c_str()));
    }();
    return maxEntries;
}

bool consumeIndexedSubmitPerfLogSlot() {
    static int emitted = 0;
    if (emitted >= indexedSubmitPerfLogMaxEntries()) return false;
    ++emitted;
    return true;
}

template <typename T>
int compareOrdered(const T& lhs, const T& rhs) {
    if (lhs < rhs) return -1;
    if (rhs < lhs) return 1;
    return 0;
}

int compareStringViews(std::string_view lhs, std::string_view rhs) {
    const int cmp = lhs.compare(rhs);
    if (cmp < 0) return -1;
    if (cmp > 0) return 1;
    return 0;
}

struct AutoInstanceKey {
    render::WorldEnvironmentUniforms environment{};
    render::WorldPointLightUniforms pointLights{};
    std::string_view geometryCacheKey{};
    bool hasBaseTexture = false;
    bool hasNormalTexture = false;
    bool hasMetallicRoughnessTexture = false;
    bool hasOcclusionTexture = false;
    bool hasEmissiveTexture = false;
    bool hasEnvironmentTexture = false;
    std::uint8_t textureSrgb = 1u;
    std::uint8_t normalTextureSrgb = 0u;
    std::uint8_t metallicRoughnessTextureSrgb = 0u;
    std::uint8_t occlusionTextureSrgb = 0u;
    std::uint8_t emissiveTextureSrgb = 1u;
    std::uint8_t environmentTextureSrgb = 0u;
    std::string_view textureCacheKey{};
    std::string_view textureKey{};
    std::string_view normalTextureCacheKey{};
    std::string_view normalTextureKey{};
    std::string_view metallicRoughnessTextureCacheKey{};
    std::string_view metallicRoughnessTextureKey{};
    std::string_view occlusionTextureCacheKey{};
    std::string_view occlusionTextureKey{};
    std::string_view emissiveTextureCacheKey{};
    std::string_view emissiveTextureKey{};
    std::string_view environmentTextureCacheKey{};
    std::string_view environmentTextureKey{};
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t dualSourceBlendEnabled = 0u;
    std::uint8_t materialMode = 0u;
    std::uint8_t characterInkingEnabled = 0u;
    std::uint8_t directionalShadowCaster = 1u, directionalShadowReceiver = 1u;
    float clipSpaceDepthBias = 0.0f;
    float alphaCutoff = 0.0f;
    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
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

    bool operator==(const AutoInstanceKey& other) const {
        return environment == other.environment && pointLights == other.pointLights && geometryCacheKey == other.geometryCacheKey &&
               hasBaseTexture == other.hasBaseTexture &&
               hasNormalTexture == other.hasNormalTexture &&
               hasMetallicRoughnessTexture == other.hasMetallicRoughnessTexture &&
               hasOcclusionTexture == other.hasOcclusionTexture &&
               hasEmissiveTexture == other.hasEmissiveTexture &&
               hasEnvironmentTexture == other.hasEnvironmentTexture &&
               textureSrgb == other.textureSrgb &&
               normalTextureSrgb == other.normalTextureSrgb &&
               metallicRoughnessTextureSrgb ==
                   other.metallicRoughnessTextureSrgb &&
               occlusionTextureSrgb == other.occlusionTextureSrgb &&
               emissiveTextureSrgb == other.emissiveTextureSrgb &&
               environmentTextureSrgb == other.environmentTextureSrgb &&
               textureCacheKey == other.textureCacheKey &&
               textureKey == other.textureKey &&
               normalTextureCacheKey == other.normalTextureCacheKey &&
               normalTextureKey == other.normalTextureKey &&
               metallicRoughnessTextureCacheKey == other.metallicRoughnessTextureCacheKey &&
               metallicRoughnessTextureKey == other.metallicRoughnessTextureKey &&
               occlusionTextureCacheKey == other.occlusionTextureCacheKey &&
               occlusionTextureKey == other.occlusionTextureKey &&
               emissiveTextureCacheKey == other.emissiveTextureCacheKey &&
               emissiveTextureKey == other.emissiveTextureKey &&
               environmentTextureCacheKey == other.environmentTextureCacheKey &&
               environmentTextureKey == other.environmentTextureKey &&
               alphaMode == other.alphaMode &&
               blendMode == other.blendMode &&
               dualSourceBlendEnabled == other.dualSourceBlendEnabled &&
               materialMode == other.materialMode &&
               characterInkingEnabled == other.characterInkingEnabled &&
               directionalShadowCaster == other.directionalShadowCaster &&
               directionalShadowReceiver == other.directionalShadowReceiver &&
               clipSpaceDepthBias == other.clipSpaceDepthBias &&
               alphaCutoff == other.alphaCutoff &&
               alphaWindowMin == other.alphaWindowMin &&
               alphaWindowMax == other.alphaWindowMax &&
               normalScale == other.normalScale &&
               metallicFactor == other.metallicFactor &&
               roughnessFactor == other.roughnessFactor &&
               occlusionStrength == other.occlusionStrength &&
               emissiveFactorR == other.emissiveFactorR &&
               emissiveFactorG == other.emissiveFactorG &&
               emissiveFactorB == other.emissiveFactorB &&
               materialTimeSec == other.materialTimeSec &&
               materialFlags == other.materialFlags &&
               materialAtlasWidth == other.materialAtlasWidth &&
               materialAtlasHeight == other.materialAtlasHeight &&
               materialRect0U == other.materialRect0U &&
               materialRect0V == other.materialRect0V &&
               materialRect0W == other.materialRect0W &&
               materialRect0H == other.materialRect0H &&
               materialRect1U == other.materialRect1U &&
               materialRect1V == other.materialRect1V &&
               materialRect1W == other.materialRect1W &&
               materialRect1H == other.materialRect1H &&
               materialFlipbook0Cols == other.materialFlipbook0Cols &&
               materialFlipbook0Rows == other.materialFlipbook0Rows &&
               materialFlipbook0Frames == other.materialFlipbook0Frames &&
               materialFlipbook0Fps == other.materialFlipbook0Fps &&
               materialFlipbook1Cols == other.materialFlipbook1Cols &&
               materialFlipbook1Rows == other.materialFlipbook1Rows &&
               materialFlipbook1Frames == other.materialFlipbook1Frames &&
               materialFlipbook1Fps == other.materialFlipbook1Fps;
    }
};

void hashCombine(std::size_t& hash, std::size_t value) {
    hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
}

std::size_t hashFloat(float value) {
    std::uint32_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return std::hash<std::uint32_t>{}(bits);
}

struct AutoInstanceKeyHash {
    std::size_t operator()(const AutoInstanceKey& key) const {
        std::size_t h = 0u;
        for (const auto& vector : key.environment)
            for (float value : vector) hashCombine(h, std::hash<float>{}(value));
        for (const auto& vector : key.pointLights)
            for (float value : vector) hashCombine(h, std::hash<float>{}(value));
        hashCombine(h, std::hash<std::string_view>{}(key.geometryCacheKey));
        hashCombine(h, std::hash<bool>{}(key.hasBaseTexture));
        hashCombine(h, std::hash<bool>{}(key.hasNormalTexture));
        hashCombine(h, std::hash<bool>{}(key.hasMetallicRoughnessTexture));
        hashCombine(h, std::hash<bool>{}(key.hasOcclusionTexture));
        hashCombine(h, std::hash<bool>{}(key.hasEmissiveTexture));
        hashCombine(h, std::hash<bool>{}(key.hasEnvironmentTexture));
        hashCombine(h, std::hash<std::uint8_t>{}(key.textureSrgb));
        hashCombine(h, std::hash<std::uint8_t>{}(key.normalTextureSrgb));
        hashCombine(
            h,
            std::hash<std::uint8_t>{}(key.metallicRoughnessTextureSrgb));
        hashCombine(h, std::hash<std::uint8_t>{}(key.occlusionTextureSrgb));
        hashCombine(h, std::hash<std::uint8_t>{}(key.emissiveTextureSrgb));
        hashCombine(h, std::hash<std::uint8_t>{}(key.environmentTextureSrgb));
        hashCombine(h, std::hash<std::string_view>{}(key.textureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.textureKey));
        hashCombine(h, std::hash<std::string_view>{}(key.normalTextureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.normalTextureKey));
        hashCombine(h, std::hash<std::string_view>{}(key.metallicRoughnessTextureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.metallicRoughnessTextureKey));
        hashCombine(h, std::hash<std::string_view>{}(key.occlusionTextureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.occlusionTextureKey));
        hashCombine(h, std::hash<std::string_view>{}(key.emissiveTextureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.emissiveTextureKey));
        hashCombine(h, std::hash<std::string_view>{}(key.environmentTextureCacheKey));
        hashCombine(h, std::hash<std::string_view>{}(key.environmentTextureKey));
        hashCombine(h, std::hash<std::uint8_t>{}(key.alphaMode));
        hashCombine(h, std::hash<std::uint8_t>{}(key.blendMode));
        hashCombine(h, std::hash<std::uint8_t>{}(key.dualSourceBlendEnabled));
        hashCombine(h, std::hash<std::uint8_t>{}(key.materialMode));
        hashCombine(h, std::hash<std::uint8_t>{}(key.characterInkingEnabled));
        hashCombine(h, std::hash<std::uint8_t>{}(key.directionalShadowCaster));
        hashCombine(h, std::hash<std::uint8_t>{}(key.directionalShadowReceiver));
        hashCombine(h, hashFloat(key.clipSpaceDepthBias));
        hashCombine(h, hashFloat(key.alphaCutoff));
        hashCombine(h, hashFloat(key.alphaWindowMin));
        hashCombine(h, hashFloat(key.alphaWindowMax));
        hashCombine(h, hashFloat(key.normalScale));
        hashCombine(h, hashFloat(key.metallicFactor));
        hashCombine(h, hashFloat(key.roughnessFactor));
        hashCombine(h, hashFloat(key.occlusionStrength));
        hashCombine(h, hashFloat(key.emissiveFactorR));
        hashCombine(h, hashFloat(key.emissiveFactorG));
        hashCombine(h, hashFloat(key.emissiveFactorB));
        hashCombine(h, hashFloat(key.materialTimeSec));
        hashCombine(h, hashFloat(key.materialFlags));
        hashCombine(h, hashFloat(key.materialAtlasWidth));
        hashCombine(h, hashFloat(key.materialAtlasHeight));
        hashCombine(h, hashFloat(key.materialRect0U));
        hashCombine(h, hashFloat(key.materialRect0V));
        hashCombine(h, hashFloat(key.materialRect0W));
        hashCombine(h, hashFloat(key.materialRect0H));
        hashCombine(h, hashFloat(key.materialRect1U));
        hashCombine(h, hashFloat(key.materialRect1V));
        hashCombine(h, hashFloat(key.materialRect1W));
        hashCombine(h, hashFloat(key.materialRect1H));
        hashCombine(h, hashFloat(key.materialFlipbook0Cols));
        hashCombine(h, hashFloat(key.materialFlipbook0Rows));
        hashCombine(h, hashFloat(key.materialFlipbook0Frames));
        hashCombine(h, hashFloat(key.materialFlipbook0Fps));
        hashCombine(h, hashFloat(key.materialFlipbook1Cols));
        hashCombine(h, hashFloat(key.materialFlipbook1Rows));
        hashCombine(h, hashFloat(key.materialFlipbook1Frames));
        hashCombine(h, hashFloat(key.materialFlipbook1Fps));
        return h;
    }
};

bool canAutoInstance(const IRenderBackend& renderer, const WorldIndexedBatch& batch) {
    if (!renderer.supportsWorldIndexedMeshInstancing()) return false;
    if (!batch.instances.empty()) return false;
    if (batch.geometryCacheKey.empty()) return false;
    if (batch.gpuSkinning != 0u) return false;
    if (batch.skinMatrixCount != 0u ||
        batch.sharedSkinMatrices != nullptr ||
        !batch.skinMatrices.empty()) {
        return false;
    }
    return batch.hasGeometry();
}

IRenderBackend::WorldMeshInstance makeWorldMeshInstance(const WorldIndexedBatch& batch) {
    IRenderBackend::WorldMeshInstance instance{};
    instance.modelMatrix = batch.modelMatrix;
    instance.vertexColorMulR = batch.vertexColorMulR;
    instance.vertexColorMulG = batch.vertexColorMulG;
    instance.vertexColorMulB = batch.vertexColorMulB;
    instance.vertexColorMulA = batch.vertexColorMulA;
    return instance;
}

const WorldIndexedBatch& materialTemplateOrSelf(const WorldIndexedBatch& batch) {
    return batch.sharedTemplate ? *batch.sharedTemplate : batch;
}

std::string_view resolvedStringMember(const WorldIndexedBatch& batch,
                                      const std::string WorldIndexedBatch::*member) {
    if (!(batch.*member).empty()) return batch.*member;
    if (batch.sharedTemplate && !((batch.sharedTemplate->*member).empty())) {
        return batch.sharedTemplate->*member;
    }
    return {};
}

std::string batchKeyForPerfLog(const WorldIndexedBatch& batch) {
    std::string_view texture = resolvedStringMember(batch, &WorldIndexedBatch::textureCacheKey);
    if (texture.empty()) texture = resolvedStringMember(batch, &WorldIndexedBatch::textureKey);
    std::string out = batch.geometryCacheKey.empty() ? "<dynamic>" : batch.geometryCacheKey;
    out += "|";
    out += texture.empty() ? "<no_texture>" : std::string(texture);
    return out;
}

bool resolvedTexturePresent(const WorldIndexedBatch& batch,
                            const unsigned char* WorldIndexedBatch::*rgbaMember,
                            const std::vector<unsigned char> WorldIndexedBatch::*ownedMember,
                            int WorldIndexedBatch::*widthMember,
                            int WorldIndexedBatch::*heightMember) {
    const auto hasLocal = [&]() {
        return (batch.*rgbaMember) != nullptr ||
               !(batch.*ownedMember).empty() ||
               (batch.*widthMember) > 0 ||
               (batch.*heightMember) > 0;
    };
    if (hasLocal()) {
        return ((batch.*rgbaMember) != nullptr || !(batch.*ownedMember).empty()) &&
               (batch.*widthMember) > 0 &&
               (batch.*heightMember) > 0;
    }
    if (!batch.sharedTemplate) return false;
    const WorldIndexedBatch& templ = *batch.sharedTemplate;
    return ((templ.*rgbaMember) != nullptr || !(templ.*ownedMember).empty()) &&
           (templ.*widthMember) > 0 &&
           (templ.*heightMember) > 0;
}

std::uint8_t effectiveAlphaMode(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return batch.materialAlphaOverride ? batch.alphaMode : materialBatch.alphaMode;
}

std::uint8_t effectiveBlendMode(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return batch.materialAlphaOverride ? batch.blendMode : materialBatch.blendMode;
}

std::uint8_t effectiveDualSourceBlendEnabled(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return batch.materialAlphaOverride ? batch.dualSourceBlendEnabled
                                       : materialBatch.dualSourceBlendEnabled;
}

float effectiveAlphaCutoff(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return batch.materialAlphaOverride ? batch.alphaCutoff : materialBatch.alphaCutoff;
}

bool resolvedTextureHasStableIdentity(const WorldIndexedBatch& batch,
                                      const std::string WorldIndexedBatch::*keyMember,
                                      const std::string WorldIndexedBatch::*cacheKeyMember,
                                      const unsigned char* WorldIndexedBatch::*rgbaMember,
                                      const std::vector<unsigned char> WorldIndexedBatch::*ownedMember,
                                      int WorldIndexedBatch::*widthMember,
                                      int WorldIndexedBatch::*heightMember) {
    if (!resolvedTexturePresent(batch, rgbaMember, ownedMember, widthMember, heightMember)) {
        return true;
    }
    return !resolvedStringMember(batch, cacheKeyMember).empty() ||
           !resolvedStringMember(batch, keyMember).empty();
}

AutoInstanceKey makeAutoInstanceKey(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    AutoInstanceKey key{};
    key.environment = render::packWorldEnvironment(batch.environment);
    key.pointLights = render::packWorldPointLights(batch.environment);
    key.geometryCacheKey = batch.geometryCacheKey;
    key.hasBaseTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::textureRgba,
        &WorldIndexedBatch::ownedTextureRgba,
        &WorldIndexedBatch::textureWidth,
        &WorldIndexedBatch::textureHeight);
    key.hasNormalTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::normalTextureRgba,
        &WorldIndexedBatch::ownedNormalTextureRgba,
        &WorldIndexedBatch::normalTextureWidth,
        &WorldIndexedBatch::normalTextureHeight);
    key.hasMetallicRoughnessTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::metallicRoughnessTextureRgba,
        &WorldIndexedBatch::ownedMetallicRoughnessTextureRgba,
        &WorldIndexedBatch::metallicRoughnessTextureWidth,
        &WorldIndexedBatch::metallicRoughnessTextureHeight);
    key.hasOcclusionTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::occlusionTextureRgba,
        &WorldIndexedBatch::ownedOcclusionTextureRgba,
        &WorldIndexedBatch::occlusionTextureWidth,
        &WorldIndexedBatch::occlusionTextureHeight);
    key.hasEmissiveTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::emissiveTextureRgba,
        &WorldIndexedBatch::ownedEmissiveTextureRgba,
        &WorldIndexedBatch::emissiveTextureWidth,
        &WorldIndexedBatch::emissiveTextureHeight);
    key.hasEnvironmentTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::environmentTextureRgba,
        &WorldIndexedBatch::ownedEnvironmentTextureRgba,
        &WorldIndexedBatch::environmentTextureWidth,
        &WorldIndexedBatch::environmentTextureHeight);
    key.textureCacheKey = resolvedStringMember(batch, &WorldIndexedBatch::textureCacheKey);
    key.textureKey = resolvedStringMember(batch, &WorldIndexedBatch::textureKey);
    key.normalTextureCacheKey = resolvedStringMember(
        batch, &WorldIndexedBatch::normalTextureCacheKey);
    key.normalTextureKey = resolvedStringMember(batch, &WorldIndexedBatch::normalTextureKey);
    key.metallicRoughnessTextureCacheKey = resolvedStringMember(
        batch, &WorldIndexedBatch::metallicRoughnessTextureCacheKey);
    key.metallicRoughnessTextureKey = resolvedStringMember(
        batch, &WorldIndexedBatch::metallicRoughnessTextureKey);
    key.occlusionTextureCacheKey = resolvedStringMember(
        batch, &WorldIndexedBatch::occlusionTextureCacheKey);
    key.occlusionTextureKey = resolvedStringMember(batch, &WorldIndexedBatch::occlusionTextureKey);
    key.emissiveTextureCacheKey = resolvedStringMember(
        batch, &WorldIndexedBatch::emissiveTextureCacheKey);
    key.emissiveTextureKey = resolvedStringMember(batch, &WorldIndexedBatch::emissiveTextureKey);
    key.environmentTextureCacheKey = resolvedStringMember(
        batch, &WorldIndexedBatch::environmentTextureCacheKey);
    key.environmentTextureKey = resolvedStringMember(
        batch, &WorldIndexedBatch::environmentTextureKey);
    key.textureSrgb = materialBatch.textureSrgb;
    key.normalTextureSrgb = materialBatch.normalTextureSrgb;
    key.metallicRoughnessTextureSrgb =
        materialBatch.metallicRoughnessTextureSrgb;
    key.occlusionTextureSrgb = materialBatch.occlusionTextureSrgb;
    key.emissiveTextureSrgb = materialBatch.emissiveTextureSrgb;
    key.environmentTextureSrgb = materialBatch.environmentTextureSrgb;
    key.alphaMode = effectiveAlphaMode(batch);
    key.blendMode = effectiveBlendMode(batch);
    key.dualSourceBlendEnabled = effectiveDualSourceBlendEnabled(batch);
    key.materialMode = materialBatch.materialMode;
    key.characterInkingEnabled = materialBatch.characterInkingEnabled;
    key.directionalShadowCaster = materialBatch.directionalShadowCaster;
    key.directionalShadowReceiver = materialBatch.directionalShadowReceiver;
    key.clipSpaceDepthBias = batch.clipSpaceDepthBias;
    key.alphaCutoff = effectiveAlphaCutoff(batch);
    key.alphaWindowMin =
        batch.materialAlphaOverride ? batch.alphaWindowMin : materialBatch.alphaWindowMin;
    key.alphaWindowMax =
        batch.materialAlphaOverride ? batch.alphaWindowMax : materialBatch.alphaWindowMax;
    key.normalScale = materialBatch.normalScale;
    key.metallicFactor = materialBatch.metallicFactor;
    key.roughnessFactor = materialBatch.roughnessFactor;
    key.occlusionStrength = materialBatch.occlusionStrength;
    key.emissiveFactorR = materialBatch.emissiveFactorR;
    key.emissiveFactorG = materialBatch.emissiveFactorG;
    key.emissiveFactorB = materialBatch.emissiveFactorB;
    key.materialTimeSec = materialBatch.materialTimeSec;
    key.materialFlags = materialBatch.materialFlags;
    key.materialAtlasWidth = materialBatch.materialAtlasWidth;
    key.materialAtlasHeight = materialBatch.materialAtlasHeight;
    key.materialRect0U = materialBatch.materialRect0U;
    key.materialRect0V = materialBatch.materialRect0V;
    key.materialRect0W = materialBatch.materialRect0W;
    key.materialRect0H = materialBatch.materialRect0H;
    key.materialRect1U = materialBatch.materialRect1U;
    key.materialRect1V = materialBatch.materialRect1V;
    key.materialRect1W = materialBatch.materialRect1W;
    key.materialRect1H = materialBatch.materialRect1H;
    key.materialFlipbook0Cols = materialBatch.materialFlipbook0Cols;
    key.materialFlipbook0Rows = materialBatch.materialFlipbook0Rows;
    key.materialFlipbook0Frames = materialBatch.materialFlipbook0Frames;
    key.materialFlipbook0Fps = materialBatch.materialFlipbook0Fps;
    key.materialFlipbook1Cols = materialBatch.materialFlipbook1Cols;
    key.materialFlipbook1Rows = materialBatch.materialFlipbook1Rows;
    key.materialFlipbook1Frames = materialBatch.materialFlipbook1Frames;
    key.materialFlipbook1Fps = materialBatch.materialFlipbook1Fps;
    return key;
}

bool canAutoInstanceWithResolvedPayload(const IRenderBackend& renderer, const WorldIndexedBatch& batch) {
    if (!canAutoInstance(renderer, batch)) {
        return false;
    }
    return resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::textureKey,
               &WorldIndexedBatch::textureCacheKey,
               &WorldIndexedBatch::textureRgba,
               &WorldIndexedBatch::ownedTextureRgba,
               &WorldIndexedBatch::textureWidth,
               &WorldIndexedBatch::textureHeight) &&
           resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::normalTextureKey,
               &WorldIndexedBatch::normalTextureCacheKey,
               &WorldIndexedBatch::normalTextureRgba,
               &WorldIndexedBatch::ownedNormalTextureRgba,
               &WorldIndexedBatch::normalTextureWidth,
               &WorldIndexedBatch::normalTextureHeight) &&
           resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::metallicRoughnessTextureKey,
               &WorldIndexedBatch::metallicRoughnessTextureCacheKey,
               &WorldIndexedBatch::metallicRoughnessTextureRgba,
               &WorldIndexedBatch::ownedMetallicRoughnessTextureRgba,
               &WorldIndexedBatch::metallicRoughnessTextureWidth,
               &WorldIndexedBatch::metallicRoughnessTextureHeight) &&
           resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::occlusionTextureKey,
               &WorldIndexedBatch::occlusionTextureCacheKey,
               &WorldIndexedBatch::occlusionTextureRgba,
               &WorldIndexedBatch::ownedOcclusionTextureRgba,
               &WorldIndexedBatch::occlusionTextureWidth,
               &WorldIndexedBatch::occlusionTextureHeight) &&
           resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::emissiveTextureKey,
               &WorldIndexedBatch::emissiveTextureCacheKey,
               &WorldIndexedBatch::emissiveTextureRgba,
               &WorldIndexedBatch::ownedEmissiveTextureRgba,
               &WorldIndexedBatch::emissiveTextureWidth,
               &WorldIndexedBatch::emissiveTextureHeight) &&
           resolvedTextureHasStableIdentity(
               batch,
               &WorldIndexedBatch::environmentTextureKey,
               &WorldIndexedBatch::environmentTextureCacheKey,
               &WorldIndexedBatch::environmentTextureRgba,
               &WorldIndexedBatch::ownedEnvironmentTextureRgba,
               &WorldIndexedBatch::environmentTextureWidth,
               &WorldIndexedBatch::environmentTextureHeight);
}

struct SubmissionMaterialStateKey {
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t dualSourceBlendEnabled = 0u;
    std::uint8_t materialMode = 0u;
    std::uint8_t characterInkingEnabled = 0u;
    float clipSpaceDepthBias = 0.0f;
    bool instanced = false;
    std::uint8_t gpuSkinning = 0u;
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    float alphaCutoff = 0.0f;
    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
    float materialFlags = 0.0f;
    bool outlineEligible = false;
};

struct SubmissionTextureStateKey {
    bool hasBaseTexture = false;
    bool hasNormalTexture = false;
    bool hasMetallicRoughnessTexture = false;
    bool hasOcclusionTexture = false;
    bool hasEmissiveTexture = false;
    bool hasEnvironmentTexture = false;
    std::uint8_t textureSrgb = 1u;
    std::uint8_t normalTextureSrgb = 0u;
    std::uint8_t metallicRoughnessTextureSrgb = 0u;
    std::uint8_t occlusionTextureSrgb = 0u;
    std::uint8_t emissiveTextureSrgb = 1u;
    std::uint8_t environmentTextureSrgb = 0u;
    std::string_view textureCacheKey{};
    std::string_view textureKey{};
    std::string_view normalTextureCacheKey{};
    std::string_view normalTextureKey{};
    std::string_view metallicRoughnessTextureCacheKey{};
    std::string_view metallicRoughnessTextureKey{};
    std::string_view occlusionTextureCacheKey{};
    std::string_view occlusionTextureKey{};
    std::string_view emissiveTextureCacheKey{};
    std::string_view emissiveTextureKey{};
    std::string_view environmentTextureCacheKey{};
    std::string_view environmentTextureKey{};
};

struct SubmissionGeometryStateKey {
    bool cachedGeometry = false;
    std::string_view geometryCacheKey{};
};

struct SubmissionSortKey {
    SubmissionMaterialStateKey material{};
    SubmissionTextureStateKey textures{};
    SubmissionGeometryStateKey geometry{};
    float sortDepth = 0.0f;
};

struct OpaqueBatchEntry {
    const WorldIndexedBatch* batch = nullptr;
    SubmissionSortKey key{};
};

struct SubmissionScratch {
    std::vector<OpaqueBatchEntry> orderedOpaqueBatches;
    std::vector<OpaqueBatchEntry> opaqueBatches;
    std::vector<const WorldIndexedBatch*> blendBatches;
    std::vector<WorldIndexedBatch> autoInstancedOpaqueBatches;
    std::unordered_map<AutoInstanceKey, std::size_t, AutoInstanceKeyHash> autoInstanceBatchIndex;
};

SubmissionSortKey makeSubmissionSortKey(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    SubmissionSortKey key{};
    key.material.alphaMode = effectiveAlphaMode(batch);
    key.material.blendMode = effectiveBlendMode(batch);
    key.material.dualSourceBlendEnabled = effectiveDualSourceBlendEnabled(batch);
    key.material.materialMode = materialBatch.materialMode;
    key.material.characterInkingEnabled = materialBatch.characterInkingEnabled;
    key.material.clipSpaceDepthBias = batch.clipSpaceDepthBias;
    key.material.instanced = batch.instances.size() > 1u;
    key.material.gpuSkinning = batch.gpuSkinning;
    key.material.gpuSkinningMode = batch.gpuSkinningMode;
    key.material.skinMatrixCount = batch.skinMatrixCount;
    key.material.alphaCutoff = effectiveAlphaCutoff(batch);
    key.material.alphaWindowMin =
        batch.materialAlphaOverride ? batch.alphaWindowMin : materialBatch.alphaWindowMin;
    key.material.alphaWindowMax =
        batch.materialAlphaOverride ? batch.alphaWindowMax : materialBatch.alphaWindowMax;
    key.material.normalScale = materialBatch.normalScale;
    key.material.metallicFactor = materialBatch.metallicFactor;
    key.material.roughnessFactor = materialBatch.roughnessFactor;
    key.material.occlusionStrength = materialBatch.occlusionStrength;
    key.material.emissiveFactorR = materialBatch.emissiveFactorR;
    key.material.emissiveFactorG = materialBatch.emissiveFactorG;
    key.material.emissiveFactorB = materialBatch.emissiveFactorB;
    key.material.materialFlags = materialBatch.materialFlags;
    key.material.outlineEligible =
        materialBatch.characterInkingEnabled != 0u && materialBatch.materialMode >= 2u;

    key.textures.hasBaseTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::textureRgba,
        &WorldIndexedBatch::ownedTextureRgba,
        &WorldIndexedBatch::textureWidth,
        &WorldIndexedBatch::textureHeight);
    key.textures.hasNormalTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::normalTextureRgba,
        &WorldIndexedBatch::ownedNormalTextureRgba,
        &WorldIndexedBatch::normalTextureWidth,
        &WorldIndexedBatch::normalTextureHeight);
    key.textures.hasMetallicRoughnessTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::metallicRoughnessTextureRgba,
        &WorldIndexedBatch::ownedMetallicRoughnessTextureRgba,
        &WorldIndexedBatch::metallicRoughnessTextureWidth,
        &WorldIndexedBatch::metallicRoughnessTextureHeight);
    key.textures.hasOcclusionTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::occlusionTextureRgba,
        &WorldIndexedBatch::ownedOcclusionTextureRgba,
        &WorldIndexedBatch::occlusionTextureWidth,
        &WorldIndexedBatch::occlusionTextureHeight);
    key.textures.hasEmissiveTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::emissiveTextureRgba,
        &WorldIndexedBatch::ownedEmissiveTextureRgba,
        &WorldIndexedBatch::emissiveTextureWidth,
        &WorldIndexedBatch::emissiveTextureHeight);
    key.textures.hasEnvironmentTexture = resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::environmentTextureRgba,
        &WorldIndexedBatch::ownedEnvironmentTextureRgba,
        &WorldIndexedBatch::environmentTextureWidth,
        &WorldIndexedBatch::environmentTextureHeight);
    key.textures.textureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::textureCacheKey);
    key.textures.textureKey = resolvedStringMember(batch, &WorldIndexedBatch::textureKey);
    key.textures.normalTextureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::normalTextureCacheKey);
    key.textures.normalTextureKey =
        resolvedStringMember(batch, &WorldIndexedBatch::normalTextureKey);
    key.textures.metallicRoughnessTextureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::metallicRoughnessTextureCacheKey);
    key.textures.metallicRoughnessTextureKey =
        resolvedStringMember(batch, &WorldIndexedBatch::metallicRoughnessTextureKey);
    key.textures.occlusionTextureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::occlusionTextureCacheKey);
    key.textures.occlusionTextureKey =
        resolvedStringMember(batch, &WorldIndexedBatch::occlusionTextureKey);
    key.textures.emissiveTextureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::emissiveTextureCacheKey);
    key.textures.emissiveTextureKey =
        resolvedStringMember(batch, &WorldIndexedBatch::emissiveTextureKey);
    key.textures.environmentTextureCacheKey =
        resolvedStringMember(batch, &WorldIndexedBatch::environmentTextureCacheKey);
    key.textures.environmentTextureKey =
        resolvedStringMember(batch, &WorldIndexedBatch::environmentTextureKey);
    key.textures.textureSrgb = materialBatch.textureSrgb;
    key.textures.normalTextureSrgb = materialBatch.normalTextureSrgb;
    key.textures.metallicRoughnessTextureSrgb =
        materialBatch.metallicRoughnessTextureSrgb;
    key.textures.occlusionTextureSrgb = materialBatch.occlusionTextureSrgb;
    key.textures.emissiveTextureSrgb = materialBatch.emissiveTextureSrgb;
    key.textures.environmentTextureSrgb =
        materialBatch.environmentTextureSrgb;

    key.geometry.cachedGeometry = !batch.geometryCacheKey.empty();
    key.geometry.geometryCacheKey = batch.geometryCacheKey;
    key.sortDepth = batch.sortDepth;
    return key;
}

bool submissionSortKeyLess(const SubmissionSortKey& lhs, const SubmissionSortKey& rhs) {
    int cmp = compareOrdered(lhs.material.alphaMode, rhs.material.alphaMode);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.blendMode, rhs.material.blendMode);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.material.dualSourceBlendEnabled,
        rhs.material.dualSourceBlendEnabled);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.materialMode, rhs.material.materialMode);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.material.characterInkingEnabled,
        rhs.material.characterInkingEnabled);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.clipSpaceDepthBias, rhs.material.clipSpaceDepthBias);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.instanced, rhs.material.instanced);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(!lhs.geometry.cachedGeometry, !rhs.geometry.cachedGeometry);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.gpuSkinning, rhs.material.gpuSkinning);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.gpuSkinningMode, rhs.material.gpuSkinningMode);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.skinMatrixCount, rhs.material.skinMatrixCount);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(!lhs.textures.hasBaseTexture, !rhs.textures.hasBaseTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(!lhs.textures.hasNormalTexture, !rhs.textures.hasNormalTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        !lhs.textures.hasMetallicRoughnessTexture,
        !rhs.textures.hasMetallicRoughnessTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(!lhs.textures.hasOcclusionTexture, !rhs.textures.hasOcclusionTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(!lhs.textures.hasEmissiveTexture, !rhs.textures.hasEmissiveTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        !lhs.textures.hasEnvironmentTexture,
        !rhs.textures.hasEnvironmentTexture);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.textures.textureSrgb, rhs.textures.textureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.textures.normalTextureSrgb,
        rhs.textures.normalTextureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.textures.metallicRoughnessTextureSrgb,
        rhs.textures.metallicRoughnessTextureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.textures.occlusionTextureSrgb,
        rhs.textures.occlusionTextureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.textures.emissiveTextureSrgb,
        rhs.textures.emissiveTextureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(
        lhs.textures.environmentTextureSrgb,
        rhs.textures.environmentTextureSrgb);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.textures.textureCacheKey, rhs.textures.textureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.textures.textureKey, rhs.textures.textureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.normalTextureCacheKey,
        rhs.textures.normalTextureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.textures.normalTextureKey, rhs.textures.normalTextureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.metallicRoughnessTextureCacheKey,
        rhs.textures.metallicRoughnessTextureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.metallicRoughnessTextureKey,
        rhs.textures.metallicRoughnessTextureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.occlusionTextureCacheKey,
        rhs.textures.occlusionTextureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.textures.occlusionTextureKey, rhs.textures.occlusionTextureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.emissiveTextureCacheKey,
        rhs.textures.emissiveTextureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.textures.emissiveTextureKey, rhs.textures.emissiveTextureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.environmentTextureCacheKey,
        rhs.textures.environmentTextureCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(
        lhs.textures.environmentTextureKey,
        rhs.textures.environmentTextureKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.alphaCutoff, rhs.material.alphaCutoff);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.alphaWindowMin, rhs.material.alphaWindowMin);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.alphaWindowMax, rhs.material.alphaWindowMax);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.normalScale, rhs.material.normalScale);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.metallicFactor, rhs.material.metallicFactor);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.roughnessFactor, rhs.material.roughnessFactor);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.occlusionStrength, rhs.material.occlusionStrength);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.emissiveFactorR, rhs.material.emissiveFactorR);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.emissiveFactorG, rhs.material.emissiveFactorG);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.emissiveFactorB, rhs.material.emissiveFactorB);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.material.materialFlags, rhs.material.materialFlags);
    if (cmp != 0) return cmp < 0;
    cmp = compareStringViews(lhs.geometry.geometryCacheKey, rhs.geometry.geometryCacheKey);
    if (cmp != 0) return cmp < 0;
    cmp = compareOrdered(lhs.sortDepth, rhs.sortDepth);
    if (cmp != 0) return cmp < 0;
    return false;
}

bool sameMaterialState(const SubmissionSortKey& lhs, const SubmissionSortKey& rhs) {
    return lhs.material.alphaMode == rhs.material.alphaMode &&
           lhs.material.blendMode == rhs.material.blendMode &&
           lhs.material.dualSourceBlendEnabled == rhs.material.dualSourceBlendEnabled &&
           lhs.material.materialMode == rhs.material.materialMode &&
           lhs.material.characterInkingEnabled == rhs.material.characterInkingEnabled &&
           lhs.material.clipSpaceDepthBias == rhs.material.clipSpaceDepthBias &&
           lhs.material.instanced == rhs.material.instanced &&
           lhs.material.gpuSkinning == rhs.material.gpuSkinning &&
           lhs.material.gpuSkinningMode == rhs.material.gpuSkinningMode &&
           lhs.material.skinMatrixCount == rhs.material.skinMatrixCount &&
           lhs.material.alphaCutoff == rhs.material.alphaCutoff &&
           lhs.material.alphaWindowMin == rhs.material.alphaWindowMin &&
           lhs.material.alphaWindowMax == rhs.material.alphaWindowMax &&
           lhs.material.normalScale == rhs.material.normalScale &&
           lhs.material.metallicFactor == rhs.material.metallicFactor &&
           lhs.material.roughnessFactor == rhs.material.roughnessFactor &&
           lhs.material.occlusionStrength == rhs.material.occlusionStrength &&
           lhs.material.emissiveFactorR == rhs.material.emissiveFactorR &&
           lhs.material.emissiveFactorG == rhs.material.emissiveFactorG &&
           lhs.material.emissiveFactorB == rhs.material.emissiveFactorB &&
           lhs.material.materialFlags == rhs.material.materialFlags;
}

bool sameTextureState(const SubmissionSortKey& lhs, const SubmissionSortKey& rhs) {
    return lhs.textures.hasBaseTexture == rhs.textures.hasBaseTexture &&
           lhs.textures.hasNormalTexture == rhs.textures.hasNormalTexture &&
           lhs.textures.hasMetallicRoughnessTexture == rhs.textures.hasMetallicRoughnessTexture &&
           lhs.textures.hasOcclusionTexture == rhs.textures.hasOcclusionTexture &&
           lhs.textures.hasEmissiveTexture == rhs.textures.hasEmissiveTexture &&
           lhs.textures.hasEnvironmentTexture == rhs.textures.hasEnvironmentTexture &&
           lhs.textures.textureSrgb == rhs.textures.textureSrgb &&
           lhs.textures.normalTextureSrgb == rhs.textures.normalTextureSrgb &&
           lhs.textures.metallicRoughnessTextureSrgb ==
               rhs.textures.metallicRoughnessTextureSrgb &&
           lhs.textures.occlusionTextureSrgb ==
               rhs.textures.occlusionTextureSrgb &&
           lhs.textures.emissiveTextureSrgb ==
               rhs.textures.emissiveTextureSrgb &&
           lhs.textures.environmentTextureSrgb ==
               rhs.textures.environmentTextureSrgb &&
           lhs.textures.textureCacheKey == rhs.textures.textureCacheKey &&
           lhs.textures.textureKey == rhs.textures.textureKey &&
           lhs.textures.normalTextureCacheKey == rhs.textures.normalTextureCacheKey &&
           lhs.textures.normalTextureKey == rhs.textures.normalTextureKey &&
           lhs.textures.metallicRoughnessTextureCacheKey ==
               rhs.textures.metallicRoughnessTextureCacheKey &&
           lhs.textures.metallicRoughnessTextureKey ==
               rhs.textures.metallicRoughnessTextureKey &&
           lhs.textures.occlusionTextureCacheKey == rhs.textures.occlusionTextureCacheKey &&
           lhs.textures.occlusionTextureKey == rhs.textures.occlusionTextureKey &&
           lhs.textures.emissiveTextureCacheKey == rhs.textures.emissiveTextureCacheKey &&
           lhs.textures.emissiveTextureKey == rhs.textures.emissiveTextureKey &&
           lhs.textures.environmentTextureCacheKey ==
               rhs.textures.environmentTextureCacheKey &&
           lhs.textures.environmentTextureKey ==
               rhs.textures.environmentTextureKey;
}

bool sameGeometryState(const SubmissionSortKey& lhs, const SubmissionSortKey& rhs) {
    return lhs.geometry.cachedGeometry &&
           rhs.geometry.cachedGeometry &&
           lhs.geometry.geometryCacheKey == rhs.geometry.geometryCacheKey;
}

SubmissionScratch& submissionScratch() {
    static thread_local SubmissionScratch scratch;
    return scratch;
}

void clearSubmissionScratch(SubmissionScratch& scratch) {
    scratch.orderedOpaqueBatches.clear();
    scratch.opaqueBatches.clear();
    scratch.blendBatches.clear();
    scratch.autoInstancedOpaqueBatches.clear();
    scratch.autoInstanceBatchIndex.clear();
}

SubmissionScratch& buildSubmissionScratch(const IRenderBackend& renderer,
                                          const std::vector<WorldIndexedBatch>& batches) {
    SubmissionScratch& scratch = submissionScratch();
    clearSubmissionScratch(scratch);
    if (scratch.opaqueBatches.capacity() < batches.size()) {
        scratch.opaqueBatches.reserve(batches.size());
    }
    if (scratch.orderedOpaqueBatches.capacity() < batches.size()) {
        scratch.orderedOpaqueBatches.reserve(batches.size());
    }
    if (scratch.blendBatches.capacity() < batches.size()) {
        scratch.blendBatches.reserve(batches.size());
    }
    if (scratch.autoInstancedOpaqueBatches.capacity() < batches.size()) {
        scratch.autoInstancedOpaqueBatches.reserve(batches.size());
    }
    scratch.autoInstanceBatchIndex.reserve(batches.size());

    for (const WorldIndexedBatch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (effectiveAlphaMode(batch) == 2u) {
            scratch.blendBatches.push_back(&batch);
        } else if (batch.preserveSubmissionOrder) {
            scratch.orderedOpaqueBatches.push_back(
                OpaqueBatchEntry{&batch, makeSubmissionSortKey(batch)});
        } else if (canAutoInstanceWithResolvedPayload(renderer, batch)) {
            const AutoInstanceKey key = makeAutoInstanceKey(batch);
            auto it = scratch.autoInstanceBatchIndex.find(key);
            if (it == scratch.autoInstanceBatchIndex.end()) {
                scratch.autoInstancedOpaqueBatches.push_back(batch);
                WorldIndexedBatch& instancedBatch = scratch.autoInstancedOpaqueBatches.back();
                instancedBatch.instances.clear();
                instancedBatch.instances.push_back(makeWorldMeshInstance(batch));
                instancedBatch.vertexColorMulR = 1.0f;
                instancedBatch.vertexColorMulG = 1.0f;
                instancedBatch.vertexColorMulB = 1.0f;
                instancedBatch.vertexColorMulA = 1.0f;
                instancedBatch.modelMatrix = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};
                const std::size_t index = scratch.autoInstancedOpaqueBatches.size() - 1u;
                scratch.autoInstanceBatchIndex.emplace(key, index);
                scratch.opaqueBatches.push_back(
                    OpaqueBatchEntry{&scratch.autoInstancedOpaqueBatches.back(),
                                     makeSubmissionSortKey(scratch.autoInstancedOpaqueBatches.back())});
            } else {
                scratch.autoInstancedOpaqueBatches[it->second].instances.push_back(
                    makeWorldMeshInstance(batch));
            }
        } else {
            scratch.opaqueBatches.push_back(OpaqueBatchEntry{&batch, makeSubmissionSortKey(batch)});
        }
    }

    // Auto-instancing grows the retained batch after its first sort key is
    // recorded. Refresh those keys once grouping is complete so submission
    // statistics and state ordering see the final instance count.
    for (OpaqueBatchEntry& entry : scratch.opaqueBatches) {
        if (entry.batch) {
            entry.key = makeSubmissionSortKey(*entry.batch);
        }
    }

    if (scratch.opaqueBatches.size() > 1u) {
        std::stable_sort(
            scratch.opaqueBatches.begin(),
            scratch.opaqueBatches.end(),
            [](const OpaqueBatchEntry& lhs, const OpaqueBatchEntry& rhs) {
                if (lhs.batch == nullptr || rhs.batch == nullptr) return lhs.batch < rhs.batch;
                if (submissionSortKeyLess(lhs.key, rhs.key)) return true;
                if (submissionSortKeyLess(rhs.key, lhs.key)) return false;
                return lhs.batch < rhs.batch;
            });
    }

    if (scratch.blendBatches.size() > 1u) {
        std::stable_sort(
            scratch.blendBatches.begin(),
            scratch.blendBatches.end(),
            [](const WorldIndexedBatch* lhs, const WorldIndexedBatch* rhs) {
                return lhs->sortDepth > rhs->sortDepth;
            });
    }

    return scratch;
}

IRenderBackend::WorldTextureData toWorldTextureData(const WorldIndexedBatch& batch,
                                                    const float* cameraWorldPos3,
                                                    const float* cameraForward3,
                                                    const float* cameraTarget3) {
    const WorldIndexedBatch* templateBatch = batch.sharedTemplate;
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    const auto resolveKey = [](const std::string& localKey, const std::string* templateKey) {
        if (!localKey.empty()) return localKey.c_str();
        if (templateKey && !templateKey->empty()) return templateKey->c_str();
        return "";
    };
    const auto resolveCacheKey = [](const std::string& localKey,
                                    const std::string* templateKey) {
        if (!localKey.empty()) return localKey.c_str();
        if (templateKey && !templateKey->empty()) return templateKey->c_str();
        return static_cast<const char*>(nullptr);
    };
    const auto resolveRgba = [](const unsigned char* localPtr,
                                const std::vector<unsigned char>& localOwned,
                                const unsigned char* templatePtr,
                                const std::vector<unsigned char>* templateOwned) {
        if (localPtr) return localPtr;
        if (!localOwned.empty()) return localOwned.data();
        if (templatePtr) return templatePtr;
        if (templateOwned && !templateOwned->empty()) return templateOwned->data();
        return static_cast<const unsigned char*>(nullptr);
    };
    const auto resolveMipLevels = [](
        const IRenderBackend::WorldTextureMipLevel* localPtr,
        std::uint32_t localCount,
        const IRenderBackend::WorldTextureMipLevel* templatePtr,
        std::uint32_t templateCount) {
        if (localPtr && localCount > 0u) {
            return std::pair{localPtr, localCount};
        }
        if (templatePtr && templateCount > 0u) {
            return std::pair{templatePtr, templateCount};
        }
        return std::pair{
            static_cast<const IRenderBackend::WorldTextureMipLevel*>(nullptr),
            0u};
    };

    const unsigned char* rgbaData = resolveRgba(
        batch.textureRgba,
        batch.ownedTextureRgba,
        templateBatch ? templateBatch->textureRgba : nullptr,
        templateBatch ? &templateBatch->ownedTextureRgba : nullptr);
    const unsigned char* normalRgbaData = resolveRgba(
        batch.normalTextureRgba,
        batch.ownedNormalTextureRgba,
        templateBatch ? templateBatch->normalTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedNormalTextureRgba : nullptr);
    const unsigned char* mrRgbaData = resolveRgba(
        batch.metallicRoughnessTextureRgba,
        batch.ownedMetallicRoughnessTextureRgba,
        templateBatch ? templateBatch->metallicRoughnessTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedMetallicRoughnessTextureRgba : nullptr);
    const unsigned char* occlusionRgbaData = resolveRgba(
        batch.occlusionTextureRgba,
        batch.ownedOcclusionTextureRgba,
        templateBatch ? templateBatch->occlusionTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedOcclusionTextureRgba : nullptr);
    const unsigned char* emissiveRgbaData = resolveRgba(
        batch.emissiveTextureRgba,
        batch.ownedEmissiveTextureRgba,
        templateBatch ? templateBatch->emissiveTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedEmissiveTextureRgba : nullptr);
    const unsigned char* environmentRgbaData = resolveRgba(
        batch.environmentTextureRgba,
        batch.ownedEnvironmentTextureRgba,
        templateBatch ? templateBatch->environmentTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedEnvironmentTextureRgba : nullptr);
    const unsigned char* lightProjectionRgbaData = resolveRgba(
        batch.lightProjectionTextureRgba,
        batch.ownedLightProjectionTextureRgba,
        templateBatch ? templateBatch->lightProjectionTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedLightProjectionTextureRgba : nullptr);
    const unsigned char* projectedShadowRgbaData = resolveRgba(
        batch.projectedShadowTextureRgba,
        batch.ownedProjectedShadowTextureRgba,
        templateBatch ? templateBatch->projectedShadowTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedProjectedShadowTextureRgba : nullptr);

    IRenderBackend::WorldTextureData tex;
    tex.key = resolveKey(
        batch.textureKey, templateBatch ? &templateBatch->textureKey : nullptr);
    tex.cacheKey = resolveCacheKey(
        batch.textureCacheKey, templateBatch ? &templateBatch->textureCacheKey : nullptr);
    tex.rgba = rgbaData;
    tex.width = batch.textureWidth > 0
        ? batch.textureWidth
        : (templateBatch ? templateBatch->textureWidth : batch.textureWidth);
    tex.height = batch.textureHeight > 0
        ? batch.textureHeight
        : (templateBatch ? templateBatch->textureHeight : batch.textureHeight);
    const auto textureMips = resolveMipLevels(
        batch.textureMipLevels,
        batch.textureMipLevelCount,
        templateBatch ? templateBatch->textureMipLevels : nullptr,
        templateBatch ? templateBatch->textureMipLevelCount : 0u);
    tex.mipLevels = textureMips.first;
    tex.mipLevelCount = textureMips.second;
    tex.wrapS = (batch.textureWidth > 0 && batch.textureHeight > 0)
        ? batch.textureWrapS
        : (templateBatch ? templateBatch->textureWrapS : batch.textureWrapS);
    tex.wrapT = (batch.textureWidth > 0 && batch.textureHeight > 0)
        ? batch.textureWrapT
        : (templateBatch ? templateBatch->textureWrapT : batch.textureWrapT);
    tex.textureSrgb = materialBatch.textureSrgb;
    tex.normalKey = resolveKey(
        batch.normalTextureKey, templateBatch ? &templateBatch->normalTextureKey : nullptr);
    tex.normalCacheKey = resolveCacheKey(
        batch.normalTextureCacheKey,
        templateBatch ? &templateBatch->normalTextureCacheKey : nullptr);
    tex.normalRgba = normalRgbaData;
    tex.normalWidth = batch.normalTextureWidth > 0
        ? batch.normalTextureWidth
        : (templateBatch ? templateBatch->normalTextureWidth : batch.normalTextureWidth);
    tex.normalHeight = batch.normalTextureHeight > 0
        ? batch.normalTextureHeight
        : (templateBatch ? templateBatch->normalTextureHeight : batch.normalTextureHeight);
    const auto normalMips = resolveMipLevels(
        batch.normalTextureMipLevels,
        batch.normalTextureMipLevelCount,
        templateBatch ? templateBatch->normalTextureMipLevels : nullptr,
        templateBatch ? templateBatch->normalTextureMipLevelCount : 0u);
    tex.normalMipLevels = normalMips.first;
    tex.normalMipLevelCount = normalMips.second;
    tex.normalWrapS = (batch.normalTextureWidth > 0 && batch.normalTextureHeight > 0)
        ? batch.normalTextureWrapS
        : (templateBatch ? templateBatch->normalTextureWrapS : batch.normalTextureWrapS);
    tex.normalWrapT = (batch.normalTextureWidth > 0 && batch.normalTextureHeight > 0)
        ? batch.normalTextureWrapT
        : (templateBatch ? templateBatch->normalTextureWrapT : batch.normalTextureWrapT);
    tex.normalTextureSrgb = materialBatch.normalTextureSrgb;
    tex.metallicRoughnessKey = resolveKey(
        batch.metallicRoughnessTextureKey,
        templateBatch ? &templateBatch->metallicRoughnessTextureKey : nullptr);
    tex.metallicRoughnessCacheKey = resolveCacheKey(
        batch.metallicRoughnessTextureCacheKey,
        templateBatch ? &templateBatch->metallicRoughnessTextureCacheKey : nullptr);
    tex.metallicRoughnessRgba = mrRgbaData;
    tex.metallicRoughnessWidth = batch.metallicRoughnessTextureWidth > 0
        ? batch.metallicRoughnessTextureWidth
        : (templateBatch ? templateBatch->metallicRoughnessTextureWidth
                         : batch.metallicRoughnessTextureWidth);
    tex.metallicRoughnessHeight = batch.metallicRoughnessTextureHeight > 0
        ? batch.metallicRoughnessTextureHeight
        : (templateBatch ? templateBatch->metallicRoughnessTextureHeight
                         : batch.metallicRoughnessTextureHeight);
    const auto metallicRoughnessMips = resolveMipLevels(
        batch.metallicRoughnessTextureMipLevels,
        batch.metallicRoughnessTextureMipLevelCount,
        templateBatch ? templateBatch->metallicRoughnessTextureMipLevels : nullptr,
        templateBatch ? templateBatch->metallicRoughnessTextureMipLevelCount : 0u);
    tex.metallicRoughnessMipLevels = metallicRoughnessMips.first;
    tex.metallicRoughnessMipLevelCount = metallicRoughnessMips.second;
    tex.metallicRoughnessWrapS =
        (batch.metallicRoughnessTextureWidth > 0 && batch.metallicRoughnessTextureHeight > 0)
        ? batch.metallicRoughnessTextureWrapS
        : (templateBatch ? templateBatch->metallicRoughnessTextureWrapS
                         : batch.metallicRoughnessTextureWrapS);
    tex.metallicRoughnessWrapT =
        (batch.metallicRoughnessTextureWidth > 0 && batch.metallicRoughnessTextureHeight > 0)
        ? batch.metallicRoughnessTextureWrapT
        : (templateBatch ? templateBatch->metallicRoughnessTextureWrapT
                         : batch.metallicRoughnessTextureWrapT);
    tex.metallicRoughnessTextureSrgb =
        materialBatch.metallicRoughnessTextureSrgb;
    tex.occlusionKey = resolveKey(
        batch.occlusionTextureKey, templateBatch ? &templateBatch->occlusionTextureKey : nullptr);
    tex.occlusionCacheKey = resolveCacheKey(
        batch.occlusionTextureCacheKey,
        templateBatch ? &templateBatch->occlusionTextureCacheKey : nullptr);
    tex.occlusionRgba = occlusionRgbaData;
    tex.occlusionWidth = batch.occlusionTextureWidth > 0
        ? batch.occlusionTextureWidth
        : (templateBatch ? templateBatch->occlusionTextureWidth : batch.occlusionTextureWidth);
    tex.occlusionHeight = batch.occlusionTextureHeight > 0
        ? batch.occlusionTextureHeight
        : (templateBatch ? templateBatch->occlusionTextureHeight : batch.occlusionTextureHeight);
    const auto occlusionMips = resolveMipLevels(
        batch.occlusionTextureMipLevels,
        batch.occlusionTextureMipLevelCount,
        templateBatch ? templateBatch->occlusionTextureMipLevels : nullptr,
        templateBatch ? templateBatch->occlusionTextureMipLevelCount : 0u);
    tex.occlusionMipLevels = occlusionMips.first;
    tex.occlusionMipLevelCount = occlusionMips.second;
    tex.occlusionWrapS = (batch.occlusionTextureWidth > 0 && batch.occlusionTextureHeight > 0)
        ? batch.occlusionTextureWrapS
        : (templateBatch ? templateBatch->occlusionTextureWrapS : batch.occlusionTextureWrapS);
    tex.occlusionWrapT = (batch.occlusionTextureWidth > 0 && batch.occlusionTextureHeight > 0)
        ? batch.occlusionTextureWrapT
        : (templateBatch ? templateBatch->occlusionTextureWrapT : batch.occlusionTextureWrapT);
    tex.occlusionTextureSrgb = materialBatch.occlusionTextureSrgb;
    tex.emissiveKey = resolveKey(
        batch.emissiveTextureKey, templateBatch ? &templateBatch->emissiveTextureKey : nullptr);
    tex.emissiveCacheKey = resolveCacheKey(
        batch.emissiveTextureCacheKey,
        templateBatch ? &templateBatch->emissiveTextureCacheKey : nullptr);
    tex.emissiveRgba = emissiveRgbaData;
    tex.emissiveWidth = batch.emissiveTextureWidth > 0
        ? batch.emissiveTextureWidth
        : (templateBatch ? templateBatch->emissiveTextureWidth : batch.emissiveTextureWidth);
    tex.emissiveHeight = batch.emissiveTextureHeight > 0
        ? batch.emissiveTextureHeight
        : (templateBatch ? templateBatch->emissiveTextureHeight : batch.emissiveTextureHeight);
    const auto emissiveMips = resolveMipLevels(
        batch.emissiveTextureMipLevels,
        batch.emissiveTextureMipLevelCount,
        templateBatch ? templateBatch->emissiveTextureMipLevels : nullptr,
        templateBatch ? templateBatch->emissiveTextureMipLevelCount : 0u);
    tex.emissiveMipLevels = emissiveMips.first;
    tex.emissiveMipLevelCount = emissiveMips.second;
    tex.emissiveWrapS = (batch.emissiveTextureWidth > 0 && batch.emissiveTextureHeight > 0)
        ? batch.emissiveTextureWrapS
        : (templateBatch ? templateBatch->emissiveTextureWrapS : batch.emissiveTextureWrapS);
    tex.emissiveWrapT = (batch.emissiveTextureWidth > 0 && batch.emissiveTextureHeight > 0)
        ? batch.emissiveTextureWrapT
        : (templateBatch ? templateBatch->emissiveTextureWrapT : batch.emissiveTextureWrapT);
    tex.emissiveTextureSrgb = materialBatch.emissiveTextureSrgb;
    tex.environmentKey = resolveKey(
        batch.environmentTextureKey,
        templateBatch ? &templateBatch->environmentTextureKey : nullptr);
    tex.environmentCacheKey = resolveCacheKey(
        batch.environmentTextureCacheKey,
        templateBatch ? &templateBatch->environmentTextureCacheKey : nullptr);
    tex.environmentRgba = environmentRgbaData;
    tex.environmentWidth = batch.environmentTextureWidth > 0
        ? batch.environmentTextureWidth
        : (templateBatch
            ? templateBatch->environmentTextureWidth
            : batch.environmentTextureWidth);
    tex.environmentHeight = batch.environmentTextureHeight > 0
        ? batch.environmentTextureHeight
        : (templateBatch
            ? templateBatch->environmentTextureHeight
            : batch.environmentTextureHeight);
    const auto environmentMips = resolveMipLevels(
        batch.environmentTextureMipLevels,
        batch.environmentTextureMipLevelCount,
        templateBatch ? templateBatch->environmentTextureMipLevels : nullptr,
        templateBatch ? templateBatch->environmentTextureMipLevelCount : 0u);
    tex.environmentMipLevels = environmentMips.first;
    tex.environmentMipLevelCount = environmentMips.second;
    tex.environmentWrapS =
        (batch.environmentTextureWidth > 0 &&
         batch.environmentTextureHeight > 0)
        ? batch.environmentTextureWrapS
        : (templateBatch
            ? templateBatch->environmentTextureWrapS
            : batch.environmentTextureWrapS);
    tex.environmentWrapT =
        (batch.environmentTextureWidth > 0 &&
         batch.environmentTextureHeight > 0)
        ? batch.environmentTextureWrapT
        : (templateBatch
            ? templateBatch->environmentTextureWrapT
            : batch.environmentTextureWrapT);
    tex.environmentTextureSrgb = materialBatch.environmentTextureSrgb;
    tex.lightProjectionKey = resolveKey(
        batch.lightProjectionTextureKey,
        templateBatch ? &templateBatch->lightProjectionTextureKey : nullptr);
    tex.lightProjectionCacheKey = resolveCacheKey(
        batch.lightProjectionTextureCacheKey,
        templateBatch
            ? &templateBatch->lightProjectionTextureCacheKey
            : nullptr);
    tex.lightProjectionRgba = lightProjectionRgbaData;
    tex.lightProjectionWidth = batch.lightProjectionTextureWidth > 0
        ? batch.lightProjectionTextureWidth
        : (templateBatch
            ? templateBatch->lightProjectionTextureWidth
            : batch.lightProjectionTextureWidth);
    tex.lightProjectionHeight = batch.lightProjectionTextureHeight > 0
        ? batch.lightProjectionTextureHeight
        : (templateBatch
            ? templateBatch->lightProjectionTextureHeight
            : batch.lightProjectionTextureHeight);
    const auto lightProjectionMips = resolveMipLevels(
        batch.lightProjectionTextureMipLevels,
        batch.lightProjectionTextureMipLevelCount,
        templateBatch ? templateBatch->lightProjectionTextureMipLevels : nullptr,
        templateBatch
            ? templateBatch->lightProjectionTextureMipLevelCount
            : 0u);
    tex.lightProjectionMipLevels = lightProjectionMips.first;
    tex.lightProjectionMipLevelCount = lightProjectionMips.second;
    tex.lightProjectionWrapS =
        (batch.lightProjectionTextureWidth > 0 &&
         batch.lightProjectionTextureHeight > 0)
        ? batch.lightProjectionTextureWrapS
        : (templateBatch
            ? templateBatch->lightProjectionTextureWrapS
            : batch.lightProjectionTextureWrapS);
    tex.lightProjectionWrapT =
        (batch.lightProjectionTextureWidth > 0 &&
         batch.lightProjectionTextureHeight > 0)
        ? batch.lightProjectionTextureWrapT
        : (templateBatch
            ? templateBatch->lightProjectionTextureWrapT
            : batch.lightProjectionTextureWrapT);
    tex.lightProjectionTextureSrgb =
        materialBatch.lightProjectionTextureSrgb;
    tex.lightProjectionUvRowU = materialBatch.lightProjectionUvRowU;
    tex.lightProjectionUvRowV = materialBatch.lightProjectionUvRowV;

    tex.projectedShadowKey = resolveKey(
        batch.projectedShadowTextureKey,
        templateBatch ? &templateBatch->projectedShadowTextureKey : nullptr);
    tex.projectedShadowCacheKey = resolveCacheKey(
        batch.projectedShadowTextureCacheKey,
        templateBatch
            ? &templateBatch->projectedShadowTextureCacheKey
            : nullptr);
    tex.projectedShadowRgba = projectedShadowRgbaData;
    tex.projectedShadowWidth = batch.projectedShadowTextureWidth > 0
        ? batch.projectedShadowTextureWidth
        : (templateBatch
            ? templateBatch->projectedShadowTextureWidth
            : batch.projectedShadowTextureWidth);
    tex.projectedShadowHeight = batch.projectedShadowTextureHeight > 0
        ? batch.projectedShadowTextureHeight
        : (templateBatch
            ? templateBatch->projectedShadowTextureHeight
            : batch.projectedShadowTextureHeight);
    tex.projectedShadowWrapS = materialBatch.projectedShadowTextureWrapS;
    tex.projectedShadowWrapT = materialBatch.projectedShadowTextureWrapT;
    tex.projectedShadowTextureSrgb =
        materialBatch.projectedShadowTextureSrgb;
    tex.projectedShadowEnabled = materialBatch.projectedShadowEnabled;
    tex.projectedShadowSamplingScale =
        materialBatch.projectedShadowSamplingScale;
    tex.projectedShadowBias = materialBatch.projectedShadowBias;
    tex.projectedShadowMatrix = materialBatch.projectedShadowMatrix;
    tex.alphaMode = batch.materialAlphaOverride ? batch.alphaMode : materialBatch.alphaMode;
    tex.blendMode = batch.materialAlphaOverride ? batch.blendMode : materialBatch.blendMode;
    tex.dualSourceBlendEnabled = batch.materialAlphaOverride
        ? batch.dualSourceBlendEnabled
        : materialBatch.dualSourceBlendEnabled;
    tex.depthTestEnabled = batch.materialAlphaOverride
        ? batch.depthTestEnabled
        : materialBatch.depthTestEnabled;
    tex.clipSpaceDepthBias = batch.clipSpaceDepthBias > 0.0f
        ? batch.clipSpaceDepthBias
        : materialBatch.clipSpaceDepthBias;
    tex.materialMode = batch.unlit ? 255u : materialBatch.materialMode;
    tex.alphaCutoff = batch.materialAlphaOverride ? batch.alphaCutoff : materialBatch.alphaCutoff;
    tex.alphaWindowMin =
        batch.materialAlphaOverride ? batch.alphaWindowMin : materialBatch.alphaWindowMin;
    tex.alphaWindowMax =
        batch.materialAlphaOverride ? batch.alphaWindowMax : materialBatch.alphaWindowMax;
    tex.normalScale = materialBatch.normalScale;
    tex.metallicFactor = materialBatch.metallicFactor;
    tex.roughnessFactor = materialBatch.roughnessFactor;
    tex.occlusionStrength = materialBatch.occlusionStrength;
    tex.emissiveFactorR = materialBatch.emissiveFactorR;
    tex.emissiveFactorG = materialBatch.emissiveFactorG;
    tex.emissiveFactorB = materialBatch.emissiveFactorB;
    tex.vertexColorMulR = batch.vertexColorMulR;
    tex.vertexColorMulG = batch.vertexColorMulG;
    tex.vertexColorMulB = batch.vertexColorMulB;
    tex.vertexColorMulA = batch.vertexColorMulA;
    tex.characterInkingEnabled = materialBatch.characterInkingEnabled;
    tex.cameraPosX = (cameraWorldPos3 ? cameraWorldPos3[0] : tex.cameraPosX);
    tex.cameraPosY = (cameraWorldPos3 ? cameraWorldPos3[1] : tex.cameraPosY);
    tex.cameraPosZ = (cameraWorldPos3 ? cameraWorldPos3[2] : tex.cameraPosZ);
    tex.cameraForwardX = (cameraForward3 ? cameraForward3[0] : tex.cameraForwardX);
    tex.cameraForwardY = (cameraForward3 ? cameraForward3[1] : tex.cameraForwardY);
    tex.cameraForwardZ = (cameraForward3 ? cameraForward3[2] : tex.cameraForwardZ);
    tex.cameraTargetX = (cameraTarget3 ? cameraTarget3[0] : tex.cameraTargetX);
    tex.cameraTargetY = (cameraTarget3 ? cameraTarget3[1] : tex.cameraTargetY);
    tex.cameraTargetZ = (cameraTarget3 ? cameraTarget3[2] : tex.cameraTargetZ);
    tex.materialTimeSec = materialBatch.materialTimeSec;
    tex.materialFlags = materialBatch.materialFlags;
    tex.materialAtlasWidth = materialBatch.materialAtlasWidth;
    tex.materialAtlasHeight = materialBatch.materialAtlasHeight;
    tex.materialRect0U = materialBatch.materialRect0U;
    tex.materialRect0V = materialBatch.materialRect0V;
    tex.materialRect0W = materialBatch.materialRect0W;
    tex.materialRect0H = materialBatch.materialRect0H;
    tex.materialRect1U = materialBatch.materialRect1U;
    tex.materialRect1V = materialBatch.materialRect1V;
    tex.materialRect1W = materialBatch.materialRect1W;
    tex.materialRect1H = materialBatch.materialRect1H;
    tex.materialFlipbook0Cols = materialBatch.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = materialBatch.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = materialBatch.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = materialBatch.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = materialBatch.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = materialBatch.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = materialBatch.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = materialBatch.materialFlipbook1Fps;
    tex.environment = batch.environment;
    tex.directionalShadowCaster = materialBatch.directionalShadowCaster;
    tex.directionalShadowReceiver = materialBatch.directionalShadowReceiver;
    tex.modelMatrix = batch.modelMatrix;
    tex.gpuSkinning = batch.gpuSkinning;
    tex.gpuSkinningMode = batch.gpuSkinningMode;
    tex.skinMatrixCount = batch.skinMatrixCount;
    tex.skinMatrices = batch.sharedSkinMatrices
        ? batch.sharedSkinMatrices
        : (batch.skinMatrices.empty() ? nullptr : batch.skinMatrices.data());
    return tex;
}

void drawOneBatch(IRenderBackend& renderer,
                  const WorldIndexedBatch& batch,
                  const float* viewProjectionMatrix4x4,
                  int surfaceWidth,
                  int surfaceHeight,
                  const float* cameraWorldPos3,
                  const float* cameraForward3,
                  const float* cameraTarget3) {
    const bool useSharedVertices = batch.sharedVertices && batch.sharedVertexCount > 0u;
    const bool useSharedIndices = batch.sharedIndices && batch.sharedIndexCount > 0u;
    const IRenderBackend::WorldMeshVertex* vertices = useSharedVertices
        ? batch.sharedVertices
        : batch.vertices.data();
    const std::size_t vertexCount = useSharedVertices
        ? batch.sharedVertexCount
        : batch.vertices.size();
    const std::uint32_t* indices = useSharedIndices
        ? batch.sharedIndices
        : batch.indices.data();
    const std::size_t indexCount = useSharedIndices
        ? batch.sharedIndexCount
        : batch.indices.size();
    if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) return;

    IRenderBackend::WorldTextureData tex =
        toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
    if (batch.instances.size() == 1u) {
        const IRenderBackend::WorldMeshInstance& instance =
            batch.instances.front();
        tex.modelMatrix = instance.modelMatrix;
        tex.vertexColorMulR = instance.vertexColorMulR;
        tex.vertexColorMulG = instance.vertexColorMulG;
        tex.vertexColorMulB = instance.vertexColorMulB;
        tex.vertexColorMulA = instance.vertexColorMulA;
        tex.gpuSkinning = instance.gpuSkinning;
        tex.gpuSkinningMode = instance.gpuSkinningMode;
        tex.skinMatrixCount = instance.skinMatrixCount;
        tex.skinMatrices = instance.skinMatrices;
        if (!batch.geometryCacheKey.empty()) {
            renderer.drawWorldIndexedMeshTexturedCached(
                batch.geometryCacheKey.c_str(),
                vertices,
                vertexCount,
                indices,
                indexCount,
                &tex,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
        } else {
            renderer.drawWorldIndexedMeshTextured(
                vertices,
                vertexCount,
                indices,
                indexCount,
                &tex,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
        }
    } else if (!batch.instances.empty()) {
        renderer.drawWorldIndexedMeshTexturedCachedInstanced(
            batch.geometryCacheKey.empty() ? nullptr : batch.geometryCacheKey.c_str(),
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            batch.instances.data(),
            batch.instances.size(),
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else if (!batch.geometryCacheKey.empty()) {
        renderer.drawWorldIndexedMeshTexturedCached(
            batch.geometryCacheKey.c_str(),
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else {
        renderer.drawWorldIndexedMeshTextured(
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    }
}

} // namespace

const WorldIndexedBatch& resolvedMaterialBatch(const WorldIndexedBatch& batch) {
    return materialTemplateOrSelf(batch);
}

bool resolvedHasBaseTexture(const WorldIndexedBatch& batch) {
    return resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::textureRgba,
        &WorldIndexedBatch::ownedTextureRgba,
        &WorldIndexedBatch::textureWidth,
        &WorldIndexedBatch::textureHeight);
}

bool resolvedHasNormalTexture(const WorldIndexedBatch& batch) {
    return resolvedTexturePresent(
        batch,
        &WorldIndexedBatch::normalTextureRgba,
        &WorldIndexedBatch::ownedNormalTextureRgba,
        &WorldIndexedBatch::normalTextureWidth,
        &WorldIndexedBatch::normalTextureHeight);
}

void prewarmWorldIndexedSubmissionWorkingSet(IRenderBackend& renderer,
                                             const std::vector<WorldIndexedBatch>& batches) {
    if (batches.empty()) return;
    SubmissionScratch& scratch = buildSubmissionScratch(renderer, batches);
    clearSubmissionScratch(scratch);
}

std::size_t prewarmWorldIndexedBatches(IRenderBackend& renderer,
                                       const std::vector<WorldIndexedBatch>& batches,
                                       const float* cameraWorldPos3,
                                       const float* cameraForward3,
                                       const float* cameraTarget3) {
    prewarmWorldIndexedSubmissionWorkingSet(renderer, batches);
    std::size_t warmed = 0u;
    for (const WorldIndexedBatch& batch : batches) {
        const bool useSharedVertices = batch.sharedVertices && batch.sharedVertexCount > 0u;
        const bool useSharedIndices = batch.sharedIndices && batch.sharedIndexCount > 0u;
        const IRenderBackend::WorldMeshVertex* vertices = useSharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = useSharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const std::uint32_t* indices = useSharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const std::size_t indexCount = useSharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) continue;

        IRenderBackend::WorldTextureData tex =
            toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
        renderer.prewarmWorldTextureData(&tex);
        if (!batch.geometryCacheKey.empty()) {
            renderer.prewarmWorldIndexedMeshCached(
                batch.geometryCacheKey.c_str(),
                vertices,
                vertexCount,
                indices,
                indexCount);
        }
        ++warmed;
    }
    return warmed;
}

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight,
                               const float* cameraWorldPos3,
                               const float* cameraForward3,
                               const float* cameraTarget3) {
    if (batches.empty() || !viewProjectionMatrix4x4 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    const bool perfLog = indexedSubmitPerfLogEnabled();
    Clock::time_point totalStart{};
    if (perfLog) totalStart = Clock::now();
    SubmissionScratch& scratch = buildSubmissionScratch(renderer, batches);
    Clock::time_point afterScratch{};
    if (perfLog) afterScratch = Clock::now();

    renderer.beginWorldIndexedBatchSubmission();
    Clock::time_point afterBegin{};
    if (perfLog) afterBegin = Clock::now();

    IRenderBackend::WorldIndexedSubmissionStats submissionStats{};
    bool havePreviousSubmissionKey = false;
    SubmissionSortKey previousSubmissionKey{};
    float opaqueDrawMs = 0.0f;
    float blendKeyMs = 0.0f;
    float blendDrawMs = 0.0f;
    float maxDrawMs = 0.0f;
    std::string maxDrawKey;
    std::size_t maxDrawInstances = 0u;
    const auto recordAndDraw = [&](const WorldIndexedBatch& batch,
                                   const SubmissionSortKey& key,
                                   bool opaquePass) {
        if (opaquePass) {
            ++submissionStats.opaqueDraws;
        } else {
            ++submissionStats.blendDraws;
        }
        if (key.geometry.cachedGeometry) {
            ++submissionStats.cachedDraws;
        } else {
            ++submissionStats.dynamicDraws;
        }
        if (key.material.instanced) {
            ++submissionStats.instancedDraws;
        }
        if (key.material.outlineEligible) {
            ++submissionStats.outlineBatches;
        }
        if (!havePreviousSubmissionKey || !sameGeometryState(previousSubmissionKey, key)) {
            ++submissionStats.geometrySwitches;
        }
        if (!havePreviousSubmissionKey || !sameMaterialState(previousSubmissionKey, key)) {
            ++submissionStats.materialSwitches;
        }
        if (!havePreviousSubmissionKey || !sameTextureState(previousSubmissionKey, key)) {
            ++submissionStats.textureSwitches;
        }
        previousSubmissionKey = key;
        havePreviousSubmissionKey = true;
        if (perfLog) {
            const Clock::time_point drawStart = Clock::now();
            drawOneBatch(
                renderer,
                batch,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight,
                cameraWorldPos3,
                cameraForward3,
                cameraTarget3);
            const float drawMs = elapsedMs(drawStart, Clock::now());
            if (opaquePass) {
                opaqueDrawMs += drawMs;
            } else {
                blendDrawMs += drawMs;
            }
            if (drawMs > maxDrawMs) {
                maxDrawMs = drawMs;
                maxDrawKey = batchKeyForPerfLog(batch);
                maxDrawInstances = batch.instances.size();
            }
        } else {
            drawOneBatch(
                renderer,
                batch,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight,
                cameraWorldPos3,
                cameraForward3,
                cameraTarget3);
        }
    };

    for (const OpaqueBatchEntry& entry : scratch.orderedOpaqueBatches) {
        if (!entry.batch) continue;
        recordAndDraw(*entry.batch, entry.key, true);
    }
    for (const OpaqueBatchEntry& entry : scratch.opaqueBatches) {
        if (!entry.batch) continue;
        recordAndDraw(*entry.batch, entry.key, true);
    }
    for (const WorldIndexedBatch* batch : scratch.blendBatches) {
        if (!batch) continue;
        Clock::time_point keyStart{};
        if (perfLog) keyStart = Clock::now();
        const SubmissionSortKey key = makeSubmissionSortKey(*batch);
        if (perfLog) blendKeyMs += elapsedMs(keyStart, Clock::now());
        recordAndDraw(*batch, key, false);
    }
    Clock::time_point afterDraws{};
    if (perfLog) afterDraws = Clock::now();

    renderer.recordWorldIndexedSubmissionStats(submissionStats);
    Clock::time_point afterStats{};
    if (perfLog) afterStats = Clock::now();
    renderer.endWorldIndexedBatchSubmission();
    Clock::time_point afterEnd{};
    if (perfLog) afterEnd = Clock::now();

    const float totalMs = perfLog ? elapsedMs(totalStart, afterEnd) : 0.0f;
    if (perfLog &&
        totalMs >= indexedSubmitPerfLogThresholdMs() &&
        consumeIndexedSubmitPerfLogSlot()) {
        std::cout << "[WorldIndexedSubmitPerf] total_ms=" << totalMs
                  << " scratch_ms=" << elapsedMs(totalStart, afterScratch)
                  << " begin_ms=" << elapsedMs(afterScratch, afterBegin)
                  << " opaque_draw_ms=" << opaqueDrawMs
                  << " blend_key_ms=" << blendKeyMs
                  << " blend_draw_ms=" << blendDrawMs
                  << " stats_ms=" << elapsedMs(afterDraws, afterStats)
                  << " end_ms=" << elapsedMs(afterStats, afterEnd)
                  << " input=" << batches.size()
                  << " ordered_opaque_entries="
                  << scratch.orderedOpaqueBatches.size()
                  << " opaque_entries=" << scratch.opaqueBatches.size()
                  << " blend_entries=" << scratch.blendBatches.size()
                  << " auto_instanced=" << scratch.autoInstancedOpaqueBatches.size()
                  << " opaque_draws=" << submissionStats.opaqueDraws
                  << " blend_draws=" << submissionStats.blendDraws
                  << " cached=" << submissionStats.cachedDraws
                  << " dynamic=" << submissionStats.dynamicDraws
                  << " instanced=" << submissionStats.instancedDraws
                  << " max_draw_ms=" << maxDrawMs
                  << " max_draw_instances=" << maxDrawInstances
                  << " max_draw_key=" << maxDrawKey
                  << "\n";
    }
}

} // namespace engine::models::batches
