#pragma once

#include "engine/render/RenderBackendTypes.h"

class IRenderBackendDebug {
public:
    using DebugQuad = engine::render::backend::DebugQuad;
    using DebugLine = engine::render::backend::DebugLine;
    using DebugTriangle = engine::render::backend::DebugTriangle;
    using DebugSprite = engine::render::backend::DebugSprite;

    virtual ~IRenderBackendDebug() = default;

    virtual void drawDebugQuads(const DebugQuad* quads,
                                std::size_t quadCount,
                                int surfaceWidth,
                                int surfaceHeight) {
        (void)quads;
        (void)quadCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugQuadsCached(const char* cacheKey,
                                      const DebugQuad* quads,
                                      std::size_t quadCount,
                                      int surfaceWidth,
                                      int surfaceHeight) {
        (void)cacheKey;
        drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
    }
    virtual void drawDebugLines(const DebugLine* lines,
                                std::size_t lineCount,
                                int surfaceWidth,
                                int surfaceHeight) {
        (void)lines;
        (void)lineCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugLinesCached(const char* cacheKey,
                                      const DebugLine* lines,
                                      std::size_t lineCount,
                                      int surfaceWidth,
                                      int surfaceHeight) {
        (void)cacheKey;
        drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
    }
    virtual void drawDebugTriangles(const DebugTriangle* triangles,
                                    std::size_t triangleCount,
                                    int surfaceWidth,
                                    int surfaceHeight) {
        (void)triangles;
        (void)triangleCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugSprites(const DebugSprite* sprites,
                                  std::size_t spriteCount,
                                  int surfaceWidth,
                                  int surfaceHeight) {
        (void)sprites;
        (void)spriteCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void prewarmDebugSpriteTexture(const char* texturePath) {
        (void)texturePath;
    }
    virtual void prewarmDebugSpriteTextures(const char* const* texturePaths,
                                            std::size_t textureCount) {
        if (!texturePaths) return;
        for (std::size_t i = 0; i < textureCount; ++i) {
            prewarmDebugSpriteTexture(texturePaths[i]);
        }
    }
};
