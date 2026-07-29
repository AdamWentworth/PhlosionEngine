#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engine/render/IRenderBackend.h"

class Renderer;

class OpenGLRenderBackend final : public IRenderBackend {
public:
    OpenGLRenderBackend();
    ~OpenGLRenderBackend() override;

    const char* backendId() const override { return "opengl"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return true; }
    bool handlesPresentation() const override { return false; }
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    bool supportsWorldIndexedMeshInstancing() const override { return true; }
    std::string activeGpuName() const override;
    bool activeGpuIsDiscrete() const override;
    void beginWorldIndexedBatchSubmission() override;
    void endWorldIndexedBatchSubmission() override;
    void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats& stats) override;
    void drawWorldTriangles(const WorldTriangle* triangles,
                            std::size_t triangleCount,
                            const float* viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                              std::size_t vertexCount,
                              const std::uint32_t* indices,
                              std::size_t indexCount,
                              const float* viewProjectionMatrix4x4,
                              int surfaceWidth,
                              int surfaceHeight) override;
    void drawWorldIndexedMeshCached(const char* geometryKey,
                                    const WorldMeshVertex* vertices,
                                    std::size_t vertexCount,
                                    const std::uint32_t* indices,
                                    std::size_t indexCount,
                                    const float* viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) override;
    void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                       const WorldMeshVertex* vertices,
                                       std::size_t vertexCount,
                                       const std::uint32_t* indices,
                                       std::size_t indexCount) override;
    void prewarmWorldIndexedMeshInstances(std::size_t instanceCount) override;
    void prewarmWorldTextureData(const WorldTextureData* texture) override;
    void prewarmWorldRenderAssets() override;
    void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const WorldTextureData* texture,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight) override;
    void drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                            const WorldMeshVertex* vertices,
                                            std::size_t vertexCount,
                                            const std::uint32_t* indices,
                                            std::size_t indexCount,
                                            const WorldTextureData* texture,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) override;
    void drawWorldIndexedMeshTexturedCachedInstanced(
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
        int surfaceHeight) override;
    void drawDebugQuads(const DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugQuadsCached(const char* cacheKey,
                              const DebugQuad* quads,
                              std::size_t quadCount,
                              int surfaceWidth,
                              int surfaceHeight) override;
    void drawDebugLines(const DebugLine* lines,
                        std::size_t lineCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugLinesCached(const char* cacheKey,
                              const DebugLine* lines,
                              std::size_t lineCount,
                              int surfaceWidth,
                              int surfaceHeight) override;
    void drawDebugTriangles(const DebugTriangle* triangles,
                            std::size_t triangleCount,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawDebugSprites(const DebugSprite* sprites,
                          std::size_t spriteCount,
                          int surfaceWidth,
                          int surfaceHeight) override;
    void prewarmDebugSpriteTexture(const char* texturePath) override;
    void shutdown() override;

private:
    void configureScreenshotCapture();
    void captureScreenshotIfRequested();

    void ensureDebugPipeline();
    void destroyDebugPipeline();
    void destroyCachedDebugGeometry();
    void ensureWorldPipeline();
    void destroyWorldPipeline();
    void destroyCachedWorldMeshes();
    void configureWorldMeshVertexLayout(unsigned int vao,
                                        unsigned int vertexBuffer,
                                        unsigned int indexBuffer);
    struct CachedWorldMesh;
    CachedWorldMesh* ensureCachedWorldMesh(const char* geometryKey,
                                           const WorldMeshVertex* vertices,
                                           std::size_t vertexCount,
                                           const std::uint32_t* indices,
                                           std::size_t indexCount);
    void drawWorldIndexedMeshTexturedInternal(unsigned int vao,
                                              unsigned int vertexBuffer,
                                              unsigned int indexBuffer,
                                              const WorldMeshVertex* vertices,
                                              std::size_t vertexCount,
                                              const std::uint32_t* indices,
                                              std::size_t indexCount,
                                              bool uploadGeometry,
                                              const WorldTextureData* texture,
                                              const WorldMeshInstance* instances,
                                              std::size_t instanceCount,
                                              const float* viewProjectionMatrix4x4,
                                              int surfaceWidth,
                                              int surfaceHeight);
    void ensureSpritePipeline();
    void destroySpritePipeline();
    unsigned int ensureWorldTexture(const WorldTextureData* textureData);
    unsigned int ensureWorldTextureRaw(const char* key,
                                       const unsigned char* rgba,
                                       int width,
                                       int height,
                                       int wrapS,
                                       int wrapT,
                                       bool srgb) {
        return ensureWorldTextureRaw(key, nullptr, rgba, width, height, wrapS, wrapT, srgb);
    }
    unsigned int ensureWorldTextureRaw(const char* key,
                                       const char* cacheKey,
                                       const unsigned char* rgba,
                                       int width,
                                       int height,
                                       int wrapS,
                                       int wrapT,
                                       bool srgb,
                                       const WorldTextureMipLevel* authoredMipLevels = nullptr,
                                       std::uint32_t authoredMipLevelCount = 0u);
    unsigned int ensureWorldTextureRawHalfFloat(const char* key,
                                                const std::uint16_t* rgba16f,
                                                int width,
                                                int height,
                                                int wrapS,
                                                int wrapT);
    unsigned int ensureSpriteTexture(const std::string& texturePath);
    void clearTextureCaches();

    std::unique_ptr<Renderer> renderer_;
    unsigned int debugProgram_ = 0;
    unsigned int debugVao_ = 0;
    unsigned int debugVbo_ = 0;
    int debugSurfaceSizeLoc_ = -1;
    struct CachedDebugGeometry {
        unsigned int vao = 0;
        unsigned int vertexBuffer = 0;
        std::size_t vertexCount = 0u;
        std::size_t vertexBytes = 0u;
        bool valid = false;
    };
    std::unordered_map<std::string, CachedDebugGeometry> cachedDebugQuads_;
    std::unordered_map<std::string, CachedDebugGeometry> cachedDebugLines_;

    unsigned int worldProgram_ = 0;
    unsigned int worldVao_ = 0;
    unsigned int worldVbo_ = 0;
    unsigned int worldIbo_ = 0;
    unsigned int worldInstanceVbo_ = 0;
    std::size_t worldInstanceBufferBytes_ = 0u;
    unsigned int worldSkinUbo_ = 0;
    int worldViewProjLoc_ = -1;
    int worldModelLoc_ = -1;
    int worldClipSpaceDepthBiasLoc_ = -1;
    int worldUseTextureLoc_ = -1;
    int worldTextureSamplerLoc_ = -1;
    int worldUseNormalTextureLoc_ = -1;
    int worldUseMetallicRoughnessTextureLoc_ = -1;
    int worldUseOcclusionTextureLoc_ = -1;
    int worldUseEmissiveTextureLoc_ = -1;
    int worldNormalTextureSamplerLoc_ = -1;
    int worldMetallicRoughnessTextureSamplerLoc_ = -1;
    int worldOcclusionTextureSamplerLoc_ = -1;
    int worldEmissiveTextureSamplerLoc_ = -1;
    int worldEnvTextureSamplerLoc_ = -1;
    int worldLightProjectionTextureSamplerLoc_ = -1;
    int worldEnvTexelSizeLoc_ = -1;
    int worldEnvMaxMipLoc_ = -1;
    int worldEnvRgbmRangeLoc_ = -1;
    int worldWrapSLoc_ = -1;
    int worldWrapTLoc_ = -1;
    int worldVertexColorMulLoc_ = -1;
    int worldDualSourceBlendEnabledLoc_ = -1;
    int worldAlphaModeLoc_ = -1;
    int worldAlphaCutoffLoc_ = -1;
    int worldAlphaWindowMinLoc_ = -1;
    int worldAlphaWindowMaxLoc_ = -1;
    int worldCameraPosLoc_ = -1;
    int worldCameraForwardLoc_ = -1;
    int worldCameraTargetLoc_ = -1;
    int worldNormalScaleLoc_ = -1;
    int worldMetallicFactorLoc_ = -1;
    int worldRoughnessFactorLoc_ = -1;
    int worldOcclusionStrengthLoc_ = -1;
    int worldEmissiveFactorLoc_ = -1;
    int worldCharacterInkingEnabledLoc_ = -1;
    int worldMaterialModeLoc_ = -1;
    int worldMaterialTimeLoc_ = -1;
    int worldMaterialFlagsLoc_ = -1;
    int worldMaterialAtlasSizeLoc_ = -1;
    int worldMaterialRect0Loc_ = -1;
    int worldMaterialRect1Loc_ = -1;
    int worldMaterialFlipbook0Loc_ = -1;
    int worldMaterialFlipbook1Loc_ = -1;
    int worldSkinningEnabledLoc_ = -1;
    int worldSkinningModeLoc_ = -1;
    int worldSkinMatrixCountLoc_ = -1;

    unsigned int spriteProgram_ = 0;
    unsigned int spriteVao_ = 0;
    unsigned int spriteVbo_ = 0;
    int spriteSurfaceSizeLoc_ = -1;
    int spriteSamplerLoc_ = -1;
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

    struct TextureCacheEntry {
        unsigned int textureId = 0;
        int width = 0;
        int height = 0;
        int wrapS = 10497;
        int wrapT = 10497;
    };

    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }

        std::size_t operator()(const std::string& value) const noexcept {
            return (*this)(std::string_view(value));
        }

        std::size_t operator()(const char* value) const noexcept {
            return (*this)(std::string_view(value ? value : ""));
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }

        bool operator()(const std::string& lhs, const std::string& rhs) const noexcept {
            return lhs == rhs;
        }

        bool operator()(const std::string& lhs, const char* rhs) const noexcept {
            return std::string_view(lhs) == std::string_view(rhs ? rhs : "");
        }

        bool operator()(const char* lhs, const std::string& rhs) const noexcept {
            return std::string_view(lhs ? lhs : "") == std::string_view(rhs);
        }

        bool operator()(const char* lhs, const char* rhs) const noexcept {
            return std::string_view(lhs ? lhs : "") == std::string_view(rhs ? rhs : "");
        }
    };

    std::unordered_map<std::string, TextureCacheEntry, TransparentStringHash, TransparentStringEqual>
        worldTextures_;
    std::unordered_map<std::string, unsigned int> spriteTextures_;
    struct CachedWorldMesh {
        unsigned int vao = 0;
        unsigned int vertexBuffer = 0;
        unsigned int indexBuffer = 0;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        std::size_t vertexBytes = 0;
        std::size_t indexBytes = 0;
        bool valid = false;
    };
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes_;
    unsigned int worldFallbackTexture_ = 0;
    unsigned int worldFallbackLinearTexture_ = 0;
    unsigned int worldFallbackFlatNormalTexture_ = 0;
    unsigned int worldNeutralPmremTexture_ = 0;
    unsigned int spriteFallbackTexture_ = 0;

    std::uint32_t frameDrawCalls_ = 0u;
    std::uint64_t frameTriangles_ = 0u;
    std::uint32_t lastFrameDrawCalls_ = 0u;
    std::uint64_t lastFrameTriangles_ = 0u;
    std::uint32_t frameIndexedOpaqueDraws_ = 0u;
    std::uint32_t frameIndexedBlendDraws_ = 0u;
    std::uint32_t frameIndexedCachedDraws_ = 0u;
    std::uint32_t frameIndexedDynamicDraws_ = 0u;
    std::uint32_t frameIndexedInstancedDraws_ = 0u;
    std::uint32_t frameIndexedOutlineBatches_ = 0u;
    std::uint32_t frameIndexedGeometrySwitches_ = 0u;
    std::uint32_t frameIndexedMaterialSwitches_ = 0u;
    std::uint32_t frameIndexedTextureSwitches_ = 0u;
    std::uint32_t frameIndexedGlTextureBindCalls_ = 0u;
    std::uint32_t lastFrameIndexedOpaqueDraws_ = 0u;
    std::uint32_t lastFrameIndexedBlendDraws_ = 0u;
    std::uint32_t lastFrameIndexedCachedDraws_ = 0u;
    std::uint32_t lastFrameIndexedDynamicDraws_ = 0u;
    std::uint32_t lastFrameIndexedInstancedDraws_ = 0u;
    std::uint32_t lastFrameIndexedOutlineBatches_ = 0u;
    std::uint32_t lastFrameIndexedGeometrySwitches_ = 0u;
    std::uint32_t lastFrameIndexedMaterialSwitches_ = 0u;
    std::uint32_t lastFrameIndexedTextureSwitches_ = 0u;
    std::uint32_t lastFrameIndexedGlTextureBindCalls_ = 0u;
    struct WorldIndexedBatchSubmissionState {
        bool active = false;
        int depth = 0;
        int prevProgram = 0;
        int prevVao = 0;
        int prevArrayBuffer = 0;
        int prevElementArrayBuffer = 0;
        int prevActiveTexture = 0;
        int prevTexture2DOnActive = 0;
        std::array<int, 7> prevTexture2DOnUnit{0, 0, 0, 0, 0, 0, 0};
        bool depthEnabled = false;
        bool blendEnabled = false;
        bool cullEnabled = false;
        int prevFrontFace = 0;
        bool prevDepthMask = true;
        int prevDepthFunc = 0;
        int prevBlendSrcRgb = 0;
        int prevBlendDstRgb = 0;
        int prevBlendSrcAlpha = 0;
        int prevBlendDstAlpha = 0;
        int prevBlendEqRgb = 0;
        int prevBlendEqAlpha = 0;
        int currentProgram = 0;
        int currentVao = 0;
        int currentArrayBuffer = 0;
        int currentElementArrayBuffer = 0;
        int currentActiveTexture = 0;
        std::array<int, 7> currentTexture2DOnUnit{0, 0, 0, 0, 0, 0, 0};
        bool currentDepthEnabled = false;
        bool currentBlendEnabled = false;
        bool currentCullEnabled = false;
        int currentFrontFace = 0;
        bool currentDepthMask = true;
        int currentDepthFunc = 0;
        int currentBlendSrcRgb = 0;
        int currentBlendDstRgb = 0;
        int currentBlendSrcAlpha = 0;
        int currentBlendDstAlpha = 0;
        int currentBlendEqRgb = 0;
        int currentBlendEqAlpha = 0;
        bool worldProgramStaticUniformsApplied = false;
        bool lastWorldSkinningEnabled = false;
        std::uint8_t lastWorldSkinningMode = 0u;
        std::uint32_t lastWorldSkinMatrixCount = 0u;
        const float* lastWorldSkinMatrices = nullptr;
    };
    WorldIndexedBatchSubmissionState worldIndexedBatchSubmissionState_{};
    bool gpuTimingSupported_ = false;
    std::array<unsigned int, 2> gpuTimerQueries_{0u, 0u};
    std::array<bool, 2> gpuTimerIssued_{false, false};
    std::uint8_t gpuTimerWriteIndex_ = 0u;
    float lastGpuFrameMs_ = 0.0f;
    bool lastGpuFrameValid_ = false;

    bool screenshotCaptureConfigured_ = false;
    bool screenshotCaptured_ = false;
    std::uint64_t screenshotFrameTarget_ = 0u;
    std::uint64_t frameCounter_ = 0u;
    std::string screenshotPath_;
};
