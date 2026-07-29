#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"
#include "engine/core/Environment.h"
#include "engine/utils/LogSink.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <stb_image.h>
#include <stb_image_write.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

namespace {

engine::log::Sink& tailFireTextureUploadLog() {
    static engine::log::Sink log("TailFireD3D12", &std::cout, &std::cerr);
    return log;
}

bool isTailFireWorldTextureKey(const char* key) {
    if (!key || key[0] == '\0') return false;
    std::string lower(key);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("__tailfire_") != std::string::npos ||
           lower.find("fire_uv_flipbook") != std::string::npos ||
           lower.find("fireuvflipbook") != std::string::npos;
}

bool worldTextureMipChainEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_TEXTURE_MIPS");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

#if defined(_WIN32)

int backendCardArtMaxDim() {
    static const int dim = []() -> int {
        constexpr int kDefault = 256;
        constexpr int kMin = 64;
        constexpr int kMax = 512;
        const auto env = engine::env::get("PAC_BACKEND_CARD_ART_MAX_DIM");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp(std::stoi(*env), kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return dim;
}

struct LoadedSpritePixels {
    std::string cacheKey;
    std::string sourcePath;
    std::string altCacheKey;
    int width = 0;
    int height = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{nullptr, stbi_image_free};
    std::vector<unsigned char> resizedPixels;
    const unsigned char* rgba = nullptr;
};

bool loadCachedProxyPixels(const std::filesystem::path& cachePath, LoadedSpritePixels& out) {
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec) || ec) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(false);
    unsigned char* loaded = stbi_load(cachePath.string().c_str(), &width, &height, &channels, 4);
    if (!loaded || width <= 0 || height <= 0) {
        if (loaded) stbi_image_free(loaded);
        return false;
    }

    out.pixels.reset(loaded);
    out.width = width;
    out.height = height;
    out.rgba = out.pixels.get();
    return true;
}

void writeCachedProxyPixels(const std::filesystem::path& cachePath, const LoadedSpritePixels& loaded) {
    if (!loaded.rgba || loaded.width <= 0 || loaded.height <= 0) return;

    static std::mutex s_proxyCacheWriteMutex;
    std::lock_guard<std::mutex> lock(s_proxyCacheWriteMutex);
    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    const int prevTgaRle = stbi_write_tga_with_rle;
    stbi_write_tga_with_rle = 0;
    (void)stbi_write_tga(
        cachePath.string().c_str(), loaded.width, loaded.height, 4, loaded.rgba);
    stbi_write_tga_with_rle = prevTgaRle;
}

bool loadSpritePixels(const std::string& texturePath, LoadedSpritePixels& out) {
    out = {};
    out.cacheKey = texturePath;
    out.sourcePath = engine::render::sprite_card_art::sourcePathFromProxy(texturePath);
    if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
        const std::string normalizedSource =
            engine::render::sprite_card_art::resolveExistingPath(out.sourcePath).generic_string();
        if (!normalizedSource.empty() && normalizedSource != out.sourcePath) {
            out.altCacheKey = engine::render::sprite_card_art::makeProxyPath(normalizedSource);
        }

        const int maxDim = backendCardArtMaxDim();
        const auto cachePath = engine::render::sprite_card_art::proxyCachePath(out.sourcePath, maxDim);
        if (loadCachedProxyPixels(cachePath, out) ||
            loadCachedProxyPixels(
                engine::render::sprite_card_art::legacyProxyCachePath(out.sourcePath, maxDim), out)) {
            return true;
        }
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(false);
    unsigned char* loaded = stbi_load(out.sourcePath.c_str(), &width, &height, &channels, 4);
    std::string altPath;
    if (!loaded) {
        altPath = out.sourcePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != out.sourcePath) {
            stbi_set_flip_vertically_on_load_thread(false);
            loaded = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!loaded || width <= 0 || height <= 0) {
        if (loaded) stbi_image_free(loaded);
        return false;
    }

    out.pixels.reset(loaded);
    out.width = width;
    out.height = height;
    out.rgba = out.pixels.get();
    if (out.altCacheKey.empty() && !altPath.empty() && altPath != out.sourcePath) {
        if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
            out.altCacheKey = engine::render::sprite_card_art::makeProxyPath(altPath);
        } else {
            out.altCacheKey = altPath;
        }
    }

    if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
        const int maxDim = backendCardArtMaxDim();
        const int scaledW =
            engine::render::sprite_card_art::scaledDimension(width, height, maxDim, true);
        const int scaledH =
            engine::render::sprite_card_art::scaledDimension(width, height, maxDim, false);
        if (scaledW > 0 && scaledH > 0 && (scaledW != width || scaledH != height)) {
            out.resizedPixels = engine::render::sprite_card_art::resizeRgbaBilinear(
                out.pixels.get(),
                width,
                height,
                scaledW,
                scaledH);
            if (!out.resizedPixels.empty()) {
                out.width = scaledW;
                out.height = scaledH;
                out.rgba = out.resizedPixels.data();
                writeCachedProxyPixels(
                    engine::render::sprite_card_art::proxyCachePath(out.sourcePath, maxDim), out);
            }
        }
    }

    return true;
}

void fillSrvDescFromTextureResource(ID3D12Resource* resource,
                                    D3D12_SHADER_RESOURCE_VIEW_DESC& outDesc) {
    outDesc = {};
    if (!resource) return;

    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    outDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    outDesc.Format = resourceDesc.Format;
    outDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    outDesc.Texture2D.MostDetailedMip = 0;
    outDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
    outDesc.Texture2D.PlaneSlice = 0;
    outDesc.Texture2D.ResourceMinLODClamp = 0.0f;
}
#endif

} // namespace

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureFallbackSpriteTexture() {
#if defined(_WIN32)
    const auto it = spriteTextures_.find(kFallbackSpriteTextureKey);
    if (it != spriteTextures_.end()) {
        return const_cast<SpriteTexture*>(&it->second);
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    static const unsigned char kFallbackRgba[16] = {
         72,  90, 108, 255,
         56,  70,  84, 255,
         56,  70,  84, 255,
         72,  90, 108, 255
    };

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    if (!engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                              commandQueue_.Get(),
                                                              fence_.Get(),
                                                              static_cast<HANDLE>(fenceEvent_),
                                                              fenceValue_,
                                                              srvHeap_.Get(),
                                                              srvDescriptorSize_,
                                                              texture.descriptorIndex,
                                                              kFallbackRgba,
                                                              2,
                                                              2,
                                                              kGlClampToEdge,
                                                              kGlClampToEdge,
                                                              true,
                                                              true,
                                                              texture.resource)) {
        return nullptr;
    }
    fillSrvDescFromTextureResource(texture.resource.Get(), texture.srvDesc);
    texture.hasSrvDesc = texture.resource != nullptr;
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(kFallbackSpriteTextureKey, std::move(texture));
    return &insertedIt->second;
#else
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureSpriteTexture(const std::string& texturePath) {
#if defined(_WIN32)
    if (texturePath.empty()) return ensureFallbackSpriteTexture();

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) {
        if (existing->second.valid) return &existing->second;
        return ensureFallbackSpriteTexture();
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return ensureFallbackSpriteTexture();
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return ensureFallbackSpriteTexture();

    LoadedSpritePixels loaded;
    if (!loadSpritePixels(texturePath, loaded)) {
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
            spriteTextures_.emplace(loaded.altCacheKey, failed);
        }
        return ensureFallbackSpriteTexture();
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          loaded.rgba,
                                                                          loaded.width,
                                                                          loaded.height,
                                                                          kGlClampToEdge,
                                                                          kGlClampToEdge,
                                                                          false,
                                                                          true,
                                                                          texture.resource);
    if (!ok) {
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
            spriteTextures_.emplace(loaded.altCacheKey, failed);
        }
        return ensureFallbackSpriteTexture();
    }

    fillSrvDescFromTextureResource(texture.resource.Get(), texture.srvDesc);
    texture.hasSrvDesc = texture.resource != nullptr;
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(texturePath, texture);
    if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
        spriteTextures_.emplace(loaded.altCacheKey, texture);
    }
    return &insertedIt->second;
#else
    (void)texturePath;
    return nullptr;
#endif
}

void D3D12RenderBackend::prewarmDebugSpriteTexture(const char* texturePath) {
#if defined(_WIN32)
    if (!texturePath || texturePath[0] == '\0') return;
    (void)ensureSpriteTexture(texturePath);
#else
    (void)texturePath;
#endif
}

void D3D12RenderBackend::prewarmDebugSpriteTextures(const char* const* texturePaths,
                                                    std::size_t textureCount) {
#if defined(_WIN32)
    if (!texturePaths || textureCount == 0u) return;
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return;

    struct PendingSpriteUpload {
        std::string originalPath;
        std::string altCacheKey;
        int width = 0;
        int height = 0;
        const unsigned char* rgba = nullptr;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{nullptr, stbi_image_free};
        std::vector<unsigned char> resizedPixels;
        std::uint32_t descriptorIndex = 0u;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };

    std::vector<PendingSpriteUpload> pending;
    pending.reserve(textureCount);
    std::unordered_set<std::string> queuedPaths;
    queuedPaths.reserve(textureCount);
    std::uint32_t reservedDescriptorIndex = nextSrvDescriptorIndex_;
    std::vector<std::string> pathsToLoad;
    pathsToLoad.reserve(textureCount);

    for (std::size_t i = 0; i < textureCount; ++i) {
        const char* rawPath = texturePaths[i];
        if (!rawPath || rawPath[0] == '\0') continue;

        const std::string texturePath(rawPath);
        if (!queuedPaths.insert(texturePath).second) continue;

        auto existing = spriteTextures_.find(texturePath);
        if (existing != spriteTextures_.end()) {
            continue;
        }
        pathsToLoad.push_back(texturePath);
        if (pathsToLoad.size() + nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) break;
    }

    struct PreparedSpriteLoad {
        std::string texturePath;
        LoadedSpritePixels loaded;
        bool success = false;
    };

    std::vector<std::future<PreparedSpriteLoad>> futures;
    futures.reserve(pathsToLoad.size());
    for (const std::string& texturePath : pathsToLoad) {
        futures.push_back(std::async(std::launch::async, [texturePath]() mutable {
            PreparedSpriteLoad result;
            result.texturePath = texturePath;
            result.success = loadSpritePixels(texturePath, result.loaded);
            return result;
        }));
    }

    for (std::future<PreparedSpriteLoad>& future : futures) {
        PreparedSpriteLoad result = future.get();
        if (!result.success) {
            SpriteTexture failed;
            failed.valid = false;
            spriteTextures_.emplace(result.texturePath, failed);
            if (!result.loaded.altCacheKey.empty() &&
                result.loaded.altCacheKey != result.texturePath) {
                spriteTextures_.emplace(result.loaded.altCacheKey, failed);
            }
            continue;
        }
        if (reservedDescriptorIndex >= kMaxSrvDescriptors) break;
        if (!result.loaded.altCacheKey.empty()) {
            auto altExisting = spriteTextures_.find(result.loaded.altCacheKey);
            if (altExisting != spriteTextures_.end()) {
                spriteTextures_.emplace(result.texturePath, altExisting->second);
                continue;
            }
        }

        PendingSpriteUpload upload;
        upload.originalPath = std::move(result.texturePath);
        upload.altCacheKey = std::move(result.loaded.altCacheKey);
        upload.width = result.loaded.width;
        upload.height = result.loaded.height;
        upload.rgba = result.loaded.rgba;
        upload.pixels = std::move(result.loaded.pixels);
        upload.resizedPixels = std::move(result.loaded.resizedPixels);
        upload.descriptorIndex = reservedDescriptorIndex++;
        pending.push_back(std::move(upload));
    }

    if (pending.empty()) return;

    std::vector<engine::render::d3d12::RgbaTextureUploadRequest> requests;
    requests.reserve(pending.size());
    for (PendingSpriteUpload& upload : pending) {
        engine::render::d3d12::RgbaTextureUploadRequest request;
        request.rgbaPixels = upload.rgba;
        request.width = upload.width;
        request.height = upload.height;
        request.wrapS = kGlClampToEdge;
        request.wrapT = kGlClampToEdge;
        request.generateMipChain = false;
        request.srgbColorData = true;
        request.descriptorIndex = upload.descriptorIndex;
        request.outTexture = &upload.resource;
        requests.push_back(request);
    }

    if (!engine::render::d3d12::createTextureResourcesFromRgbaBatch(device_.Get(),
                                                                     commandQueue_.Get(),
                                                                     fence_.Get(),
                                                                     static_cast<HANDLE>(fenceEvent_),
                                                                     fenceValue_,
                                                                     srvHeap_.Get(),
                                                                     srvDescriptorSize_,
                                                                     requests.data(),
                                                                     requests.size())) {
        for (const PendingSpriteUpload& upload : pending) {
            (void)ensureSpriteTexture(upload.originalPath);
        }
        return;
    }

    nextSrvDescriptorIndex_ = reservedDescriptorIndex;
    for (PendingSpriteUpload& upload : pending) {
        SpriteTexture texture;
        texture.resource = upload.resource;
        texture.descriptorIndex = upload.descriptorIndex;
        fillSrvDescFromTextureResource(texture.resource.Get(), texture.srvDesc);
        texture.hasSrvDesc = texture.resource != nullptr;
        texture.valid = true;
        auto [insertedIt, _] = spriteTextures_.emplace(upload.originalPath, texture);
        if (!upload.altCacheKey.empty() && upload.altCacheKey != upload.originalPath) {
            spriteTextures_.emplace(upload.altCacheKey, texture);
        }
        (void)insertedIt;
    }
#else
    (void)texturePaths;
    (void)textureCount;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
#if defined(_WIN32)
    if (!textureData) return nullptr;
    return ensureWorldTextureRaw(
        textureData->key,
        textureData->cacheKey,
        textureData->rgba,
        textureData->width,
        textureData->height,
        textureData->wrapS,
        textureData->wrapT,
        textureData->textureSrgb != 0u,
        textureData->mipLevels,
        textureData->mipLevelCount);
#else
    (void)textureData;
    return nullptr;
#endif
}

const D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldTextureByDescriptorIndex(
    std::uint32_t descriptorIndex) const {
#if defined(_WIN32)
    for (const auto& [key, texture] : worldTextures_) {
        (void)key;
        if (texture.valid && texture.descriptorIndex == descriptorIndex) {
            return &texture;
        }
    }
    return nullptr;
#else
    (void)descriptorIndex;
    return nullptr;
#endif
}

std::uint32_t D3D12RenderBackend::ensureWorldMaterialDescriptorBlock(
    std::uint32_t baseTextureDescriptorIndex,
    std::uint32_t normalTextureDescriptorIndex,
    std::uint32_t metallicRoughnessTextureDescriptorIndex,
    std::uint32_t occlusionTextureDescriptorIndex,
    std::uint32_t emissiveTextureDescriptorIndex,
    std::uint32_t envTextureDescriptorIndex,
    std::uint32_t lightProjectionTextureDescriptorIndex) {
#if defined(_WIN32)
    const bool alreadyContiguous =
        normalTextureDescriptorIndex == baseTextureDescriptorIndex + 1u &&
        metallicRoughnessTextureDescriptorIndex == baseTextureDescriptorIndex + 2u &&
        occlusionTextureDescriptorIndex == baseTextureDescriptorIndex + 3u &&
        emissiveTextureDescriptorIndex == baseTextureDescriptorIndex + 4u &&
        envTextureDescriptorIndex == baseTextureDescriptorIndex + 5u &&
        lightProjectionTextureDescriptorIndex ==
            baseTextureDescriptorIndex + 6u;
    if (alreadyContiguous) {
        return baseTextureDescriptorIndex;
    }

    const WorldMaterialDescriptorBlockKey key{
        baseTextureDescriptorIndex,
        normalTextureDescriptorIndex,
        metallicRoughnessTextureDescriptorIndex,
        occlusionTextureDescriptorIndex,
        emissiveTextureDescriptorIndex,
        envTextureDescriptorIndex,
        lightProjectionTextureDescriptorIndex};
    const auto found = worldMaterialDescriptorBlocks_.find(key);
    if (found != worldMaterialDescriptorBlocks_.end()) {
        return found->second;
    }

    if (!device_ || !srvHeap_ || srvDescriptorSize_ == 0u) {
        return 0xffffffffu;
    }
    if (nextSrvDescriptorIndex_ + 7u >
        static_cast<std::uint32_t>(kMaxSrvDescriptors)) {
        return 0xffffffffu;
    }

    const std::array<std::uint32_t, 7> sourceDescriptorIndices = {
        baseTextureDescriptorIndex,
        normalTextureDescriptorIndex,
        metallicRoughnessTextureDescriptorIndex,
        occlusionTextureDescriptorIndex,
        emissiveTextureDescriptorIndex,
        envTextureDescriptorIndex,
        lightProjectionTextureDescriptorIndex};
    std::array<const SpriteTexture*, 7> sourceTextures{};
    for (std::size_t i = 0; i < sourceDescriptorIndices.size(); ++i) {
        sourceTextures[i] = findWorldTextureByDescriptorIndex(sourceDescriptorIndices[i]);
        if (!sourceTextures[i] ||
            !sourceTextures[i]->valid ||
            !sourceTextures[i]->resource ||
            !sourceTextures[i]->hasSrvDesc) {
            return 0xffffffffu;
        }
    }

    const std::uint32_t blockBaseDescriptorIndex = nextSrvDescriptorIndex_;
    D3D12_CPU_DESCRIPTOR_HANDLE heapCpuBase = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::size_t i = 0; i < sourceTextures.size(); ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = heapCpuBase;
        dstHandle.ptr += static_cast<SIZE_T>(blockBaseDescriptorIndex + i) *
                         static_cast<SIZE_T>(srvDescriptorSize_);
        device_->CreateShaderResourceView(
            sourceTextures[i]->resource.Get(),
            &sourceTextures[i]->srvDesc,
            dstHandle);
    }

    nextSrvDescriptorIndex_ += 7u;
    worldMaterialDescriptorBlocks_.emplace(key, blockBaseDescriptorIndex);
    return blockBaseDescriptorIndex;
#else
    (void)baseTextureDescriptorIndex;
    (void)normalTextureDescriptorIndex;
    (void)metallicRoughnessTextureDescriptorIndex;
    (void)occlusionTextureDescriptorIndex;
    (void)emissiveTextureDescriptorIndex;
    (void)envTextureDescriptorIndex;
    (void)lightProjectionTextureDescriptorIndex;
    return 0xffffffffu;
#endif
}

bool D3D12RenderBackend::prepareWorldMaterialDescriptorBlock(
    const WorldTextureData* textureData,
    bool logPbrBinding,
    std::uint32_t& outDescriptorBlockIndex,
    float& outUseTexture) {
#if defined(_WIN32)
    ensureWorldFallbackEnvTexture();
    if (worldFallbackMaterialDescriptorBlockIndex_ == 0xffffffffu) {
        worldFallbackMaterialDescriptorBlockIndex_ = ensureWorldMaterialDescriptorBlock(
            worldFallbackTextureDescriptorIndex_,
            worldFallbackNormalTextureDescriptorIndex_,
            worldFallbackMetallicRoughnessTextureDescriptorIndex_,
            worldFallbackOcclusionTextureDescriptorIndex_,
            worldFallbackEmissiveTextureDescriptorIndex_,
            worldFallbackEnvTextureDescriptorIndex_,
            worldFallbackOcclusionTextureDescriptorIndex_);
    }

    if (!textureData) {
        outDescriptorBlockIndex = worldFallbackMaterialDescriptorBlockIndex_;
        outUseTexture = 0.0f;
        return outDescriptorBlockIndex != 0xffffffffu;
    }

    SpriteTexture* worldTex = ensureWorldTexture(textureData);
    const std::uint32_t baseDescriptorIndex =
        worldTex ? worldTex->descriptorIndex : worldFallbackTextureDescriptorIndex_;
    outUseTexture = (worldTex && worldTex->valid) ? 1.0f : 0.0f;

    SpriteTexture* normalTex = ensureWorldTextureRaw(
        textureData->normalKey,
        textureData->normalCacheKey,
        textureData->normalRgba,
        textureData->normalWidth,
        textureData->normalHeight,
        textureData->normalWrapS,
        textureData->normalWrapT,
        textureData->normalTextureSrgb != 0u,
        textureData->normalMipLevels,
        textureData->normalMipLevelCount);
    const std::uint32_t normalDescriptorIndex =
        normalTex ? normalTex->descriptorIndex : worldFallbackNormalTextureDescriptorIndex_;

    SpriteTexture* metallicRoughnessTex = ensureWorldTextureRaw(
        textureData->metallicRoughnessKey,
        textureData->metallicRoughnessCacheKey,
        textureData->metallicRoughnessRgba,
        textureData->metallicRoughnessWidth,
        textureData->metallicRoughnessHeight,
        textureData->metallicRoughnessWrapS,
        textureData->metallicRoughnessWrapT,
        textureData->metallicRoughnessTextureSrgb != 0u,
        textureData->metallicRoughnessMipLevels,
        textureData->metallicRoughnessMipLevelCount);
    const std::uint32_t metallicRoughnessDescriptorIndex =
        metallicRoughnessTex
            ? metallicRoughnessTex->descriptorIndex
            : worldFallbackMetallicRoughnessTextureDescriptorIndex_;

    SpriteTexture* occlusionTex = ensureWorldTextureRaw(
        textureData->occlusionKey,
        textureData->occlusionCacheKey,
        textureData->occlusionRgba,
        textureData->occlusionWidth,
        textureData->occlusionHeight,
        textureData->occlusionWrapS,
        textureData->occlusionWrapT,
        textureData->occlusionTextureSrgb != 0u,
        textureData->occlusionMipLevels,
        textureData->occlusionMipLevelCount);
    const std::uint32_t occlusionDescriptorIndex =
        occlusionTex ? occlusionTex->descriptorIndex : worldFallbackOcclusionTextureDescriptorIndex_;

    SpriteTexture* emissiveTex = ensureWorldTextureRaw(
        textureData->emissiveKey,
        textureData->emissiveCacheKey,
        textureData->emissiveRgba,
        textureData->emissiveWidth,
        textureData->emissiveHeight,
        textureData->emissiveWrapS,
        textureData->emissiveWrapT,
        textureData->emissiveTextureSrgb != 0u,
        textureData->emissiveMipLevels,
        textureData->emissiveMipLevelCount);
    const std::uint32_t emissiveDescriptorIndex =
        emissiveTex ? emissiveTex->descriptorIndex : worldFallbackEmissiveTextureDescriptorIndex_;

    SpriteTexture* environmentTex = ensureWorldTextureRaw(
        textureData->environmentKey,
        textureData->environmentCacheKey,
        textureData->environmentRgba,
        textureData->environmentWidth,
        textureData->environmentHeight,
        textureData->environmentWrapS,
        textureData->environmentWrapT,
        textureData->environmentTextureSrgb != 0u,
        textureData->environmentMipLevels,
        textureData->environmentMipLevelCount);
    const std::uint32_t environmentDescriptorIndex =
        environmentTex
            ? environmentTex->descriptorIndex
            : worldFallbackEnvTextureDescriptorIndex_;

    SpriteTexture* lightProjectionTex = ensureWorldTextureRaw(
        textureData->lightProjectionKey,
        textureData->lightProjectionCacheKey,
        textureData->lightProjectionRgba,
        textureData->lightProjectionWidth,
        textureData->lightProjectionHeight,
        textureData->lightProjectionWrapS,
        textureData->lightProjectionWrapT,
        textureData->lightProjectionTextureSrgb != 0u,
        textureData->lightProjectionMipLevels,
        textureData->lightProjectionMipLevelCount);
    const std::uint32_t lightProjectionDescriptorIndex =
        lightProjectionTex
        ? lightProjectionTex->descriptorIndex
        : worldFallbackOcclusionTextureDescriptorIndex_;

    (void)logPbrBinding;

    outDescriptorBlockIndex = ensureWorldMaterialDescriptorBlock(
        baseDescriptorIndex,
        normalDescriptorIndex,
        metallicRoughnessDescriptorIndex,
        occlusionDescriptorIndex,
        emissiveDescriptorIndex,
        environmentDescriptorIndex,
        lightProjectionDescriptorIndex);
    return outDescriptorBlockIndex != 0xffffffffu;
#else
    (void)textureData;
    (void)logPbrBinding;
    outDescriptorBlockIndex = 0u;
    outUseTexture = 0.0f;
    return false;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTextureRaw(const char* key,
                                                                              const char* cacheKey,
                                                                              const unsigned char* rgba,
                                                                              int width,
                                                                              int height,
                                                                              int wrapS,
                                                                              int wrapT,
                                                                              bool srgb,
                                                                              const WorldTextureMipLevel* authoredMipLevels,
                                                                              std::uint32_t authoredMipLevelCount) {
#if defined(_WIN32)
    if (!key || key[0] == '\0' || !rgba || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    // Include wrap + dimensions to avoid accidental cache aliasing for reused key strings.
    std::string resolvedCacheKey;
    if (cacheKey && cacheKey[0] != '\0') {
        resolvedCacheKey = cacheKey;
    } else {
        resolvedCacheKey = key;
        resolvedCacheKey += "|";
        resolvedCacheKey += std::to_string(width);
        resolvedCacheKey += "x";
        resolvedCacheKey += std::to_string(height);
        resolvedCacheKey += "|ws=";
        resolvedCacheKey += std::to_string(wrapS);
        resolvedCacheKey += "|wt=";
        resolvedCacheKey += std::to_string(wrapT);
        resolvedCacheKey += srgb ? "|srgb" : "|lin";
    }

    auto it = worldTextures_.find(resolvedCacheKey);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool generateMipChain = worldTextureMipChainEnabled();
    const bool tailFireTexture = isTailFireWorldTextureKey(key);
    const auto uploadStart = tailFireTexture ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          rgba,
                                                                          width,
                                                                          height,
                                                                          wrapS,
                                                                          wrapT,
                                                                          generateMipChain,
                                                                          srgb,
                                                                          texture.resource,
                                                                          authoredMipLevels,
                                                                          authoredMipLevelCount);
    if (tailFireTexture) {
        const auto uploadEnd = std::chrono::steady_clock::now();
        std::ostringstream msg;
        msg << "[TailFire][D3D12][Upload] key="
            << key
            << " size="
            << width
            << "x"
            << height
            << " srgb="
            << (srgb ? 1 : 0)
            << " mips="
            << (authoredMipLevels && authoredMipLevelCount > 0u
                    ? authoredMipLevelCount
                    : (generateMipChain ? 1u : 0u))
            << " wall_ms="
            << std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count()
            << " result="
            << (ok ? "ok" : "failed")
            << " note=includes_upload_and_fence_wait";
        tailFireTextureUploadLog().info(msg.str());
    }
    if (!ok) return nullptr;

    fillSrvDescFromTextureResource(texture.resource.Get(), texture.srvDesc);
    texture.hasSrvDesc = texture.resource != nullptr;
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(resolvedCacheKey, std::move(texture));
    return &insertedIt->second;
#else
    (void)key;
    (void)cacheKey;
    (void)rgba;
    (void)width;
    (void)height;
    (void)wrapS;
    (void)wrapT;
    (void)srgb;
    (void)authoredMipLevels;
    (void)authoredMipLevelCount;
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTextureRawHalfFloat(
    const char* key,
    const std::uint16_t* rgba16f,
    int width,
    int height,
    int wrapS,
    int wrapT) {
#if defined(_WIN32)
    if (!key || key[0] == '\0' || !rgba16f || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += "|rgba16f";

    auto it = worldTextures_.find(cacheKey);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba16F(
        device_.Get(),
        commandQueue_.Get(),
        fence_.Get(),
        static_cast<HANDLE>(fenceEvent_),
        fenceValue_,
        srvHeap_.Get(),
        srvDescriptorSize_,
        texture.descriptorIndex,
        rgba16f,
        width,
        height,
        texture.resource);
    if (!ok) return nullptr;

    fillSrvDescFromTextureResource(texture.resource.Get(), texture.srvDesc);
    texture.hasSrvDesc = texture.resource != nullptr;
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(cacheKey, std::move(texture));
    return &insertedIt->second;
#else
    (void)key;
    (void)rgba16f;
    (void)width;
    (void)height;
    (void)wrapS;
    (void)wrapT;
    return nullptr;
#endif
}
