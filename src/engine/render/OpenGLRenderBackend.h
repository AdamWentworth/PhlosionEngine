#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldDepthControl.h"
#include "engine/render/IWorldBloom.h"
#include "engine/render/IWorldSceneRadiance.h"
#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/IWorldPlanarReflection.h"
#include "engine/render/IWorldLocalHaze.h"
#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/IWorldLocalShadow.h"
#include "engine/render/WorldResourceScopeState.h"

class Renderer;

class OpenGLRenderBackend final : public IRenderBackend, public IWorldResourceScope, public IWorldDepthControl, public IWorldDirectionalShadow, public IWorldLocalShadow, public engine::render::IWorldBloom, public engine::render::IWorldSceneRadiance, public engine::render::IWorldDiffuseProbes, public engine::render::IWorldPlanarReflection, public engine::render::IWorldLocalHaze {
public:
  bool clearWorldDepth() override;
    bool beginWorldLocalHaze(int width, int height) override;
    bool endWorldLocalHaze() override;
  bool beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView&) override;
    bool endWorldPlanarReflection() override;
    bool captureWorldDepthSnapshot() override;
    void prewarmWorldLocalShadows() override;
    void clearWorldLocalShadows() override;
    bool beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) override;
    bool endWorldLocalShadow() override;
    void prewarmWorldDirectionalShadow(std::uint32_t resolution) override;
    bool beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) override;
    bool endWorldDirectionalShadow() override;
    void clearWorldDirectionalShadow() override;

  engine::render::WorldResourceScopeId createWorldResourceScope(std::uint64_t payloadByteLimit) override;
  bool beginWorldResourceScope(engine::render::WorldResourceScopeId id) override;
  bool endWorldResourceScope() override;
  bool requestWorldResourceScopeRelease(engine::render::WorldResourceScopeId id) override;
  bool getWorldResourceScopeStats(engine::render::WorldResourceScopeId id,
      engine::render::WorldResourceScopeStats& out) const override;
  engine::render::WorldResourceStats getWorldResourceStats() const override;
  explicit OpenGLRenderBackend(const engine::render::WorldMaterialProfile &profile = {});
  ~OpenGLRenderBackend() override;
  void setWorldMaterialProfile(const engine::render::WorldMaterialProfile &profile) override;

  const char *backendId() const override { return "opengl"; }
  void beginFrame(float r, float g, float b, float a) override;
  void endFrame() override;
  bool beginScreenshotCaptureSequence() override;
  void onResize(int width, int height) override;
  bool requiresOpenGLContext() const override { return true; }
  bool handlesPresentation() const override { return false; }
  bool getLastFrameTimings(BackendFrameTimings &outTimings) const override;
  bool getLastFrameStats(BackendFrameStats &outStats) const override;
  bool supportsWorldTriangles3D() const override { return true; }
  bool supportsWorldIndexedMeshes() const override { return true; }
  bool supportsWorldIndexedMeshInstancing() const override { return true; }
  std::string activeGpuName() const override;
  bool activeGpuIsDiscrete() const override;
  void beginWorldSceneColorPass(int surfaceWidth, int surfaceHeight) override;
  void endWorldSceneColorPass() override;
    void endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame&) override;
  void beginWorldIndexedBatchSubmission() override;
  void endWorldIndexedBatchSubmission() override;
  void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats &stats) override;
  void drawWorldTriangles(const WorldTriangle *triangles,
                          std::size_t triangleCount,
                          const float *viewProjectionMatrix4x4,
                          int surfaceWidth,
                          int surfaceHeight) override;
  void drawWorldIndexedMesh(const WorldMeshVertex *vertices,
                            std::size_t vertexCount,
                            const std::uint32_t *indices,
                            std::size_t indexCount,
                            const float *viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight) override;
  void drawWorldIndexedMeshCached(const char *geometryKey,
                                  const WorldMeshVertex *vertices,
                                  std::size_t vertexCount,
                                  const std::uint32_t *indices,
                                  std::size_t indexCount,
                                  const float *viewProjectionMatrix4x4,
                                  int surfaceWidth,
                                  int surfaceHeight) override;
  void prewarmWorldIndexedMeshCached(const char *geometryKey,
                                     const WorldMeshVertex *vertices,
                                     std::size_t vertexCount,
                                     const std::uint32_t *indices,
                                     std::size_t indexCount) override;
  void prewarmWorldIndexedMeshInstances(std::size_t instanceCount) override;
  void prewarmWorldTextureData(const WorldTextureData *texture) override;
  void prewarmWorldRenderAssets() override;
  void drawWorldIndexedMeshTextured(const WorldMeshVertex *vertices,
                                    std::size_t vertexCount,
                                    const std::uint32_t *indices,
                                    std::size_t indexCount,
                                    const WorldTextureData *texture,
                                    const float *viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) override;
  void drawWorldIndexedMeshTexturedCached(const char *geometryKey,
                                          const WorldMeshVertex *vertices,
                                          std::size_t vertexCount,
                                          const std::uint32_t *indices,
                                          std::size_t indexCount,
                                          const WorldTextureData *texture,
                                          const float *viewProjectionMatrix4x4,
                                          int surfaceWidth,
                                          int surfaceHeight) override;
  void drawWorldIndexedMeshTexturedCachedInstanced(
      const char *geometryKey,
      const WorldMeshVertex *vertices,
      std::size_t vertexCount,
      const std::uint32_t *indices,
      std::size_t indexCount,
      const WorldTextureData *texture,
      const WorldMeshInstance *instances,
      std::size_t instanceCount,
      const float *viewProjectionMatrix4x4,
      int surfaceWidth,
      int surfaceHeight) override;
  void drawDebugQuads(const DebugQuad *quads,
                      std::size_t quadCount,
                      int surfaceWidth,
                      int surfaceHeight) override;
  void drawDebugQuadsCached(const char *cacheKey,
                            const DebugQuad *quads,
                            std::size_t quadCount,
                            int surfaceWidth,
                            int surfaceHeight) override;
  void drawDebugLines(const DebugLine *lines,
                      std::size_t lineCount,
                      int surfaceWidth,
                      int surfaceHeight) override;
  void drawDebugLinesCached(const char *cacheKey,
                            const DebugLine *lines,
                            std::size_t lineCount,
                            int surfaceWidth,
                            int surfaceHeight) override;
  void drawDebugTriangles(const DebugTriangle *triangles,
                          std::size_t triangleCount,
                          int surfaceWidth,
                          int surfaceHeight) override;
  void drawDebugSprites(const DebugSprite *sprites,
                        std::size_t spriteCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
  void prewarmDebugSpriteTexture(const char *texturePath) override;
  void shutdown() override;

private:
    engine::render::WorldResourceScopeState worldScopes_;
    bool collectWorldResourceScopes();
    void configureScreenshotCapture();
    void captureScreenshotIfRequested();

    void ensureDebugPipeline();
    void destroyDebugPipeline();
    void destroyCachedDebugGeometry();
    void ensureWorldPipeline();
    void destroyWorldPipeline();
    bool ensureWorldSceneColorResources(int width, int height);
    void destroyWorldSceneColorResources();
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
    int worldLightProjectionUvRowULoc_ = -1;
    int worldLightProjectionUvRowVLoc_ = -1;
    int worldProjectedShadowTextureSamplerLoc_ = -1;
    int worldProjectedShadowMatrixLoc_ = -1;
    int worldProjectedShadowParamsLoc_ = -1;
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
    std::array<int, 19> worldEnvironmentLocs_{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    int worldPointLightsLoc_ = -1;
    int worldRectLightsLoc_ = -1, worldRectLightLutLoc_ = -1;
    int worldPlanarLoc_ = -1;
    int worldPlanarTextureLoc_ = -1;
    unsigned int worldHazeTexture_ = 0, worldHazeFbo_ = 0;
    int worldHazeSize_ = 0, worldHazePreviousFbo_ = 0;
    bool worldHazeActive_ = false;
    unsigned int worldPlanarTexture_ = 0;
    int worldPlanarWidth_ = 0, worldPlanarHeight_ = 0;
    int worldDiffuseProbesLoc_ = -1;
    bool worldDiffuseProbesUploaded_ = false;
    engine::render::WorldDiffuseProbeUniforms lastWorldDiffuseProbes_{};
    unsigned int worldRectLightLut_ = 0;
    struct ShadowProgram { unsigned int program = 0; std::vector<int> uniforms; };
    std::array<ShadowProgram, 256> worldShadowPrograms_;
    const ShadowProgram* worldActiveShadowProgram_ = nullptr;
    std::function<unsigned int(std::uint8_t)> worldShadowProgramFactory_;
    std::vector<std::pair<int, std::string>> worldUniformNames_;
    unsigned int specializedWorldShadowProgram(std::uint8_t mode);
    unsigned int worldVolumeProgram_ = 0;
    bool worldVolumeActive_ = false;
    std::vector<int> worldVolumeUniformLocations_;
    int worldUniformLocation(int location) const noexcept {
        if (worldDirectionalShadowActive_ && worldActiveShadowProgram_)
            return location >= 0 && static_cast<std::size_t>(location) < worldActiveShadowProgram_->uniforms.size()
                ? worldActiveShadowProgram_->uniforms[static_cast<std::size_t>(location)] : -1;
        if (worldVolumeActive_)
            return location >= 0 && static_cast<std::size_t>(location) < worldVolumeUniformLocations_.size()
                ? worldVolumeUniformLocations_[static_cast<std::size_t>(location)] : -1;
        return location;
    }
    int worldShadowAtlasLoc_ = -1, worldLocalShadowsLoc_ = -1;
    int worldDirectionalShadowMatrixLoc_ = -1;
    int worldDirectionalShadowParamsLoc_ = -1;
    int worldDirectionalShadowTextureLoc_ = -1;
    int worldMomentTextureLoc_ = -1;
    engine::render::WorldLocalShadowAtlas worldLocalShadows_;
    unsigned int worldDirectionalShadowTexture_ = 0, worldDirectionalShadowFramebuffer_ = 0;
    std::array<unsigned int,2> worldMomentTextures_{}, worldMomentFramebuffers_{};
    unsigned int worldMomentProgram_ = 0, worldMomentVao_ = 0;
    void prewarmWorldShadowMoments();
    void filterWorldShadowMoments();
    std::uint32_t worldDirectionalShadowResolution_ = 0;
    bool worldDirectionalShadowActive_ = false, worldDirectionalShadowReady_ = false;
    engine::render::WorldDirectionalShadowView worldDirectionalShadowView_;
    struct DirectionalShadowSavedState {
        int drawFramebuffer = 0, readFramebuffer = 0, depthFunction = 0;
        std::array<int, 4> viewport{}, scissor{};
        std::array<unsigned char, 4> colorMask{};
        unsigned char depthMask = 1;
        bool depthEnabled = false, scissorEnabled = false;
    } worldDirectionalShadowSaved_;
    void destroyWorldDirectionalShadowResources();
    int worldCameraTargetLoc_ = -1;
    int worldNormalScaleLoc_ = -1;
    int worldMetallicFactorLoc_ = -1;
    int worldRoughnessFactorLoc_ = -1;
    int worldOcclusionStrengthLoc_ = -1;
    int worldEmissiveFactorLoc_ = -1;
    int worldCharacterInkingEnabledLoc_ = -1;
    int worldSceneColorPostEnabledLoc_ = -1;
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

    unsigned int worldSceneColorFbo_ = 0;
    unsigned int worldSceneColorTexture_ = 0;
    unsigned int worldSceneDepthRenderbuffer_ = 0;
    unsigned int worldDepthSnapshotTexture_ = 0;
    unsigned int worldDepthSnapshotFbo_ = 0;
    int worldDepthSnapshotWidth_ = 0, worldDepthSnapshotHeight_ = 0;
    unsigned int worldSceneColorPostProgram_ = 0;
    unsigned int feedbackProgram_=0, feedbackVao_=0, feedbackMask_=0;
    std::array<unsigned int,2> feedbackTextures_{}, feedbackFbos_{};
    std::uint64_t feedbackSequence_=0;
    int feedbackWidth_=0,feedbackHeight_=0,feedbackHistory_=0,feedbackDisplayInput_=0;
    engine::render::ScreenFeedbackUpdate feedbackLast_{};
    void destroyScreenFeedback();
    unsigned int worldSceneColorPostVao_ = 0;
    int worldSceneColorPostSamplerLoc_ = -1;
    int worldSceneBloomLoc_ = -1;
    int worldSceneColorWidth_ = 0;
    int worldSceneColorHeight_ = 0;
    bool worldSceneColorPassActive_ = false;
    int worldSceneColorPrevDrawFbo_ = 0;
    int worldSceneColorPrevReadFbo_ = 0;
    std::array<int, 4> worldSceneColorPrevViewport_{0, 0, 1, 1};
    std::array<float, 4> frameClearColor_{0.1f, 0.1f, 0.1f, 1.0f};

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
        engine::render::WorldResourceScopeId scope = 0;
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
        engine::render::WorldResourceScopeId scope = 0;
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
        std::array<int, 12> prevTexture2DOnUnit{};
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
        std::array<int, 12> currentTexture2DOnUnit{};
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

    bool screenshotCaptureDeferred_ = false;
    bool screenshotCaptureConfigured_ = false;
    bool screenshotCaptured_ = false;
    std::uint64_t screenshotFrameTarget_ = 0u;
    std::uint64_t frameCounter_ = 0u;
    std::string screenshotPath_;
};
