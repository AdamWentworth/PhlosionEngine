#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "engine/core/Environment.h"
#include "engine/utils/LogSink.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <stb_image.h>
#include <stb_image_write.h>

namespace {

engine::log::Sink& tailFireTextureUploadLog() {
    static engine::log::Sink log("TailFireOpenGL", &std::cout, &std::cerr);
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

GLint sanitizeWrapMode(int wrap) {
    switch (wrap) {
    case 33071: return GL_CLAMP_TO_EDGE;
    case 33648: return GL_MIRRORED_REPEAT;
    case 10497: return GL_REPEAT;
    default: return GL_REPEAT;
    }
}

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

constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

struct CpuMipLevel {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

float srgbByteToLinear(unsigned char v) {
    const float c = static_cast<float>(v) / 255.0f;
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

unsigned char linearToSrgbByte(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float srgb =
        (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    const int quantized = static_cast<int>(std::lround(srgb * 255.0f));
    return static_cast<unsigned char>(std::clamp(quantized, 0, 255));
}

int wrapTexelIndex(int index, int size, int wrapMode) {
    if (size <= 1) return 0;
    if (wrapMode == 33071) { // GL_CLAMP_TO_EDGE
        if (index < 0) return 0;
        if (index >= size) return size - 1;
        return index;
    }
    if (wrapMode == 33648) { // GL_MIRRORED_REPEAT
        const int period = size * 2;
        int j = index % period;
        if (j < 0) j += period;
        if (j >= size) {
            j = period - 1 - j;
        }
        return j;
    }
    // Default to repeat.
    int j = index % size;
    if (j < 0) j += size;
    return j;
}

std::vector<CpuMipLevel> buildRgbaMipChain(const unsigned char* rgbaPixels,
                                           int width,
                                           int height,
                                           int wrapS,
                                           int wrapT,
                                           bool srgbColorData) {
    std::vector<CpuMipLevel> chain;
    if (!rgbaPixels || width <= 0 || height <= 0) return chain;

    CpuMipLevel base;
    base.width = width;
    base.height = height;
    base.rgba.assign(
        rgbaPixels,
        rgbaPixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    chain.push_back(std::move(base));

    while (chain.back().width > 1 || chain.back().height > 1) {
        const CpuMipLevel& prev = chain.back();
        CpuMipLevel next;
        next.width = (prev.width / 2 > 0) ? (prev.width / 2) : 1;
        next.height = (prev.height / 2 > 0) ? (prev.height / 2) : 1;
        next.rgba.resize(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height) * 4u);

        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                float sumR = 0.0f;
                float sumG = 0.0f;
                float sumB = 0.0f;
                float sumA = 0.0f;
                std::uint32_t taps = 0;

                for (int oy = 0; oy < 2; ++oy) {
                    const int srcY = wrapTexelIndex(y * 2 + oy, prev.height, wrapT);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int srcX = wrapTexelIndex(x * 2 + ox, prev.width, wrapS);
                        const std::size_t srcIndex =
                            (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(prev.width) +
                             static_cast<std::size_t>(srcX)) * 4u;
                        if (srgbColorData) {
                            sumR += srgbByteToLinear(prev.rgba[srcIndex + 0]);
                            sumG += srgbByteToLinear(prev.rgba[srcIndex + 1]);
                            sumB += srgbByteToLinear(prev.rgba[srcIndex + 2]);
                        } else {
                            sumR += static_cast<float>(prev.rgba[srcIndex + 0]) / 255.0f;
                            sumG += static_cast<float>(prev.rgba[srcIndex + 1]) / 255.0f;
                            sumB += static_cast<float>(prev.rgba[srcIndex + 2]) / 255.0f;
                        }
                        sumA += static_cast<float>(prev.rgba[srcIndex + 3]) / 255.0f;
                        ++taps;
                    }
                }

                const std::size_t dstIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width) +
                     static_cast<std::size_t>(x)) * 4u;
                const float invTaps = 1.0f / static_cast<float>((taps > 0) ? taps : 1u);
                if (srgbColorData) {
                    next.rgba[dstIndex + 0] = linearToSrgbByte(sumR * invTaps);
                    next.rgba[dstIndex + 1] = linearToSrgbByte(sumG * invTaps);
                    next.rgba[dstIndex + 2] = linearToSrgbByte(sumB * invTaps);
                } else {
                    const int qR = static_cast<int>(std::lround(sumR * invTaps * 255.0f));
                    const int qG = static_cast<int>(std::lround(sumG * invTaps * 255.0f));
                    const int qB = static_cast<int>(std::lround(sumB * invTaps * 255.0f));
                    next.rgba[dstIndex + 0] = static_cast<unsigned char>(std::clamp(qR, 0, 255));
                    next.rgba[dstIndex + 1] = static_cast<unsigned char>(std::clamp(qG, 0, 255));
                    next.rgba[dstIndex + 2] = static_cast<unsigned char>(std::clamp(qB, 0, 255));
                }
                const int alphaQ = static_cast<int>(std::lround(sumA * invTaps * 255.0f));
                next.rgba[dstIndex + 3] = static_cast<unsigned char>(std::clamp(alphaQ, 0, 255));
            }
        }

        chain.push_back(std::move(next));
    }

    return chain;
}

} // namespace

unsigned int OpenGLRenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
    if (!textureData) return 0;
    return ensureWorldTextureRaw(
        textureData->key,
        textureData->cacheKey,
        textureData->rgba,
        textureData->width,
        textureData->height,
        textureData->wrapS,
        textureData->wrapT,
        textureData->textureSrgb != 0u);
}

unsigned int OpenGLRenderBackend::ensureWorldTextureRaw(const char* keyCStr,
                                                        const char* cacheKeyCStr,
                                                        const unsigned char* rgba,
                                                        int width,
                                                        int height,
                                                        int wrapSIn,
                                                        int wrapTIn,
                                                        bool srgb) {
    if (!rgba || width <= 0 || height <= 0 || !keyCStr || keyCStr[0] == '\0') {
        return 0;
    }

    // Match D3D12 cache-key strictness to avoid accidental texture aliasing across
    // reused key strings with different dimensions/wrap modes.
    std::string cacheKey;
    if (cacheKeyCStr && cacheKeyCStr[0] != '\0') {
        auto existing = worldTextures_.find(cacheKeyCStr);
        if (existing != worldTextures_.end()) {
            return existing->second.textureId;
        }
        cacheKey = cacheKeyCStr;
    } else {
        cacheKey = keyCStr;
        cacheKey += "|";
        cacheKey += std::to_string(width);
        cacheKey += "x";
        cacheKey += std::to_string(height);
        cacheKey += "|ws=";
        cacheKey += std::to_string(wrapSIn);
        cacheKey += "|wt=";
        cacheKey += std::to_string(wrapTIn);
        cacheKey += srgb ? "|srgb" : "|lin";
        auto existing = worldTextures_.find(cacheKey);
        if (existing != worldTextures_.end()) {
            return existing->second.textureId;
        }
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;

    const bool tailFireTexture = isTailFireWorldTextureKey(keyCStr);
    const auto uploadStart = tailFireTexture ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
    const GLint wrapS = sanitizeWrapMode(wrapSIn);
    const GLint wrapT = sanitizeWrapMode(wrapTIn);
    const bool generateMipChain = worldTextureMipChainEnabled();
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMipChain ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.35f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<CpuMipLevel> mipChain;
    if (generateMipChain) {
        mipChain = buildRgbaMipChain(rgba, width, height, wrapSIn, wrapTIn, srgb);
    }
    const GLint maxLevel = generateMipChain
        ? static_cast<GLint>((mipChain.empty() ? 1u : mipChain.size()) - 1u)
        : 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
    if (generateMipChain && !mipChain.empty()) {
        for (std::size_t level = 0; level < mipChain.size(); ++level) {
            const CpuMipLevel& mip = mipChain[level];
            glTexImage2D(GL_TEXTURE_2D,
                         static_cast<GLint>(level),
                         srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                         mip.width,
                         mip.height,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         mip.rgba.data());
        }
    } else {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                     width,
                     height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     rgba);
    }
#if defined(GL_TEXTURE_MAX_ANISOTROPY_EXT) && defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
    if (generateMipChain && GLAD_GL_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        const float requested = std::max(1.0f, maxAniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, requested);
    }
#endif

    TextureCacheEntry entry;
    entry.textureId = textureId;
    entry.width = width;
    entry.height = height;
    entry.wrapS = wrapSIn;
    entry.wrapT = wrapTIn;
    worldTextures_.emplace(cacheKey, entry);
    if (tailFireTexture) {
        const auto uploadEnd = std::chrono::steady_clock::now();
        std::ostringstream msg;
        msg << "[TailFire][OpenGL][Upload] key="
            << keyCStr
            << " size="
            << width
            << "x"
            << height
            << " srgb="
            << (srgb ? 1 : 0)
            << " mips="
            << (generateMipChain ? 1 : 0)
            << " wall_ms="
            << std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count()
            << " result=ok";
        tailFireTextureUploadLog().info(msg.str());
    }
    return textureId;
}

unsigned int OpenGLRenderBackend::ensureWorldTextureRawHalfFloat(const char* keyCStr,
                                                                 const std::uint16_t* rgba16f,
                                                                 int width,
                                                                 int height,
                                                                 int wrapSIn,
                                                                 int wrapTIn) {
    if (!rgba16f || width <= 0 || height <= 0 || !keyCStr || keyCStr[0] == '\0') {
        return 0;
    }

    std::string cacheKey(keyCStr);
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapSIn);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapTIn);
    cacheKey += "|rgba16f";
    auto existing = worldTextures_.find(cacheKey);
    if (existing != worldTextures_.end()) {
        return existing->second.textureId;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;

    const GLint wrapS = sanitizeWrapMode(wrapSIn);
    const GLint wrapT = sanitizeWrapMode(wrapTIn);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA16F,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_HALF_FLOAT,
                 rgba16f);

    TextureCacheEntry entry;
    entry.textureId = textureId;
    entry.width = width;
    entry.height = height;
    entry.wrapS = wrapSIn;
    entry.wrapT = wrapTIn;
    worldTextures_.emplace(cacheKey, entry);
    return textureId;
}

unsigned int OpenGLRenderBackend::ensureSpriteTexture(const std::string& texturePath) {
    ensureSpritePipeline();
    if (texturePath.empty()) return spriteFallbackTexture_;

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) return existing->second;

    LoadedSpritePixels loaded;
    if (!loadSpritePixels(texturePath, loaded)) {
        spriteTextures_[texturePath] = spriteFallbackTexture_;
        if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
            spriteTextures_[loaded.altCacheKey] = spriteFallbackTexture_;
        }
        return spriteFallbackTexture_;
    }
    if (!loaded.altCacheKey.empty()) {
        auto altExisting = spriteTextures_.find(loaded.altCacheKey);
        if (altExisting != spriteTextures_.end()) {
            spriteTextures_[texturePath] = altExisting->second;
            return altExisting->second;
        }
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) {
        return spriteFallbackTexture_;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 loaded.width,
                 loaded.height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 loaded.rgba);

    spriteTextures_[texturePath] = textureId;
    if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
        spriteTextures_[loaded.altCacheKey] = textureId;
    }
    return textureId;
}

void OpenGLRenderBackend::prewarmDebugSpriteTexture(const char* texturePath) {
    if (!texturePath || texturePath[0] == '\0') return;
    (void)ensureSpriteTexture(texturePath);
}

void OpenGLRenderBackend::clearTextureCaches() {
    std::vector<unsigned int> texturesToDelete;
    texturesToDelete.reserve(worldTextures_.size() + spriteTextures_.size() + 2u);
    for (const auto& [_, entry] : worldTextures_) {
        if (entry.textureId != 0) texturesToDelete.push_back(entry.textureId);
    }
    for (const auto& [_, textureId] : spriteTextures_) {
        if (textureId != 0) texturesToDelete.push_back(textureId);
    }
    if (worldFallbackTexture_ != 0) texturesToDelete.push_back(worldFallbackTexture_);
    if (spriteFallbackTexture_ != 0) texturesToDelete.push_back(spriteFallbackTexture_);
    if (!texturesToDelete.empty()) {
        std::sort(texturesToDelete.begin(), texturesToDelete.end());
        texturesToDelete.erase(std::unique(texturesToDelete.begin(), texturesToDelete.end()), texturesToDelete.end());
        glDeleteTextures(static_cast<GLsizei>(texturesToDelete.size()), texturesToDelete.data());
    }
    worldTextures_.clear();
    spriteTextures_.clear();
    worldFallbackTexture_ = 0;
    worldFallbackLinearTexture_ = 0;
    worldFallbackFlatNormalTexture_ = 0;
    worldNeutralPmremTexture_ = 0;
    spriteFallbackTexture_ = 0;
}
