#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/NeutralPmrem.h"

#include <chrono>
#include <iostream>

void OpenGLRenderBackend::prewarmWorldTextureData(const WorldTextureData* texture) {
    if (!texture) return;

    (void)ensureWorldTexture(texture);
    if (texture->normalRgba && texture->normalWidth > 0 && texture->normalHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->normalKey,
            texture->normalCacheKey,
            texture->normalRgba,
            texture->normalWidth,
            texture->normalHeight,
            texture->normalWrapS,
            texture->normalWrapT,
            texture->normalTextureSrgb != 0u,
            texture->normalMipLevels,
            texture->normalMipLevelCount);
    }
    if (texture->metallicRoughnessRgba &&
        texture->metallicRoughnessWidth > 0 &&
        texture->metallicRoughnessHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->metallicRoughnessKey,
            texture->metallicRoughnessCacheKey,
            texture->metallicRoughnessRgba,
            texture->metallicRoughnessWidth,
            texture->metallicRoughnessHeight,
            texture->metallicRoughnessWrapS,
            texture->metallicRoughnessWrapT,
            texture->metallicRoughnessTextureSrgb != 0u,
            texture->metallicRoughnessMipLevels,
            texture->metallicRoughnessMipLevelCount);
    }
    if (texture->occlusionRgba && texture->occlusionWidth > 0 && texture->occlusionHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->occlusionKey,
            texture->occlusionCacheKey,
            texture->occlusionRgba,
            texture->occlusionWidth,
            texture->occlusionHeight,
            texture->occlusionWrapS,
            texture->occlusionWrapT,
            texture->occlusionTextureSrgb != 0u,
            texture->occlusionMipLevels,
            texture->occlusionMipLevelCount);
    }
    if (texture->emissiveRgba && texture->emissiveWidth > 0 && texture->emissiveHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->emissiveKey,
            texture->emissiveCacheKey,
            texture->emissiveRgba,
            texture->emissiveWidth,
            texture->emissiveHeight,
            texture->emissiveWrapS,
            texture->emissiveWrapT,
            texture->emissiveTextureSrgb != 0u,
            texture->emissiveMipLevels,
            texture->emissiveMipLevelCount);
    }
    if (texture->environmentRgba &&
        texture->environmentWidth > 0 &&
        texture->environmentHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->environmentKey,
            texture->environmentCacheKey,
            texture->environmentRgba,
            texture->environmentWidth,
            texture->environmentHeight,
            texture->environmentWrapS,
            texture->environmentWrapT,
            texture->environmentTextureSrgb != 0u,
            texture->environmentMipLevels,
            texture->environmentMipLevelCount);
    }
    if (texture->lightProjectionRgba &&
        texture->lightProjectionWidth > 0 &&
        texture->lightProjectionHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->lightProjectionKey,
            texture->lightProjectionCacheKey,
            texture->lightProjectionRgba,
            texture->lightProjectionWidth,
            texture->lightProjectionHeight,
            texture->lightProjectionWrapS,
            texture->lightProjectionWrapT,
            texture->lightProjectionTextureSrgb != 0u,
            texture->lightProjectionMipLevels,
            texture->lightProjectionMipLevelCount);
    }
}

void OpenGLRenderBackend::prewarmWorldRenderAssets() {
    const auto t0 = std::chrono::high_resolution_clock::now();
    ensureWorldPipeline();
    const auto t1 = std::chrono::high_resolution_clock::now();

    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    (void)ensureWorldTextureRaw(
        "__world_fallback_white_srgb_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/true);
    (void)ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    (void)ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const auto t2 = std::chrono::high_resolution_clock::now();

    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const auto t3 = std::chrono::high_resolution_clock::now();
    if (!neutralPmremAtlas.rgba16f.empty()) {
        (void)ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v2__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071);
    } else if (!neutralPmremAtlas.rgba.empty()) {
        (void)ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v2__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071,
            /*srgb=*/false);
    }
    const auto t4 = std::chrono::high_resolution_clock::now();

    const auto ms = [](const auto& a, const auto& b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::cout
        << "[Renderer][OpenGL][Prewarm] worldPipeline=" << ms(t0, t1) << "ms"
        << " fallbackTex=" << ms(t1, t2) << "ms"
        << " pmremAtlas=" << ms(t2, t3) << "ms"
        << " pmremUpload=" << ms(t3, t4) << "ms\n";
}
