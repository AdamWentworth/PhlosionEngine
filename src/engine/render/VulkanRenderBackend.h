#pragma once
#include "engine/render/WorldSunShadows.h"
#include "engine/render/IWorldDeformationPreparation.h"
#include "engine/render/WorldPreparedResources.h"
#include "engine/render/WorldPreparedView.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <vulkan/vulkan.h>

#include "engine/render/IRenderBackend.h"
#include "engine/render/RenderPhaseProfile.h"
#include "engine/render/IWorldDepthControl.h"
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/IWorldScreenReflection.h"
#include "engine/render/IWorldLocalReflection.h"
#include "engine/render/IWorldProbeScheduling.h"
#include "engine/render/WorldProbeResidency.h"
#include "engine/render/IWorldBloom.h"
#include "engine/render/IWorldSceneRadiance.h"
#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/WorldIndirectLighting.h"
#include "engine/render/IWorldPlanarReflection.h"
#include "engine/render/IWorldLocalHaze.h"
#include "engine/render/IWorldRainMap.h"
#include "engine/render/IWorldWetSurfaces.h"
#include "engine/render/IWorldLayeredMaterials.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include "engine/render/IWorldClouds.h"
#include "engine/render/IWorldWindField.h"
#include "engine/render/IWorldParticleLighting.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/IWorldLocalShadow.h"
#include "engine/render/IWorldLocalShadowCache.h"
#include "engine/render/WorldShadowSchedule.h"
#include "engine/render/WorldResourceScopeState.h"

struct SDL_Window;
struct VulkanRenderBackendImpl;

class VulkanRenderBackend final : public IRenderBackend, public IWorldResourceScope, public IWorldDepthControl, public engine::render::IWorldContactVisibility, public engine::render::IWorldScreenReflection, public engine::render::IWorldLocalReflection, public engine::render::IWorldProbeScheduling, public IWorldDirectionalShadow, public IWorldLocalShadow, public engine::render::IWorldBloom, public engine::render::IWorldSceneRadiance, public engine::render::IWorldDiffuseProbes, public engine::render::IWorldIndirectLighting, public engine::render::IWorldPlanarReflection, public engine::render::IWorldLocalHaze, public engine::render::IWorldPrecipitationLighting, public engine::render::IWorldClouds, public engine::render::IWorldWindField, public engine::render::IWorldParticleLighting,
                             public engine::render::IWorldLayeredMaterials, public engine::render::IWorldWetSurfaces, public engine::render::IWorldRainMap, public engine::render::IWorldSurfaceBuffer, public engine::render::IWorldLocalShadowCache, public engine::render::IWorldLocalShadowPlan, public engine::render::IWorldShadowScheduling, public engine::render::IRenderPhaseProfile, public engine::render::IRenderCpuPhaseProfile, public engine::render::IWorldPreparedResources, public engine::render::IWorldDeformationPreparation {
public:
    engine::render::WorldPreparedResourceToken prepareWorldGeometry(const char*,const WorldMeshVertex*,std::size_t,const std::uint32_t*,std::size_t) override;
    engine::render::WorldPreparedResourceToken prepareWorldMaterial(const WorldTextureData&) override;
    bool worldPreparedGeometryValid(engine::render::WorldPreparedResourceToken) const override;
    bool worldPreparedMaterialValid(engine::render::WorldPreparedResourceToken) const override;
    bool drawWorldPreparedIndexed(engine::render::WorldPreparedResourceToken,engine::render::WorldPreparedResourceToken,
        const WorldTextureData&,const float*,int,int) override;
    engine::render::WorldPreparedResourceStats worldPreparedResourceStats() const override;
    int beginRenderPhase(const char* name) override;
  void endRenderPhase(int token) override;
  int beginRenderCpuPhase(const char* name) override;
  void endRenderCpuPhase(int token) override;
  engine::render::backend::WorldPreparedViewStats worldPreparedViewStats() const override;
  engine::render::backend::WorldPreparedViewToken prepareWorldViewData(const WorldTextureData&) override;
  void beginWorldDeformationBatch() override;
  void commitWorldDeformationBatch() override;
  void cancelWorldDeformationBatch() noexcept override;
  void prepareWorldDeformations(std::span<const engine::render::WorldSkinPalette>) override;
  engine::render::WorldDeformationPreparationStats worldDeformationPreparationStats() const override;
  void beginWorldIndexedBatchSubmission() override;
  void endWorldIndexedBatchSubmission() override;
  bool clearWorldDepth() override;
    engine::render::WorldParticleLightingResult updateWorldParticleLighting(const engine::render::WorldParticleLightingView&) override;
    bool captureWorldParticleLighting() override;
    engine::render::WorldParticleLightingDiagnostic readWorldParticleLighting() override;
    engine::render::WorldPrecipitationLightingResult updateWorldPrecipitationLighting(const engine::render::WorldPrecipitationLightingView&) override;
    void prepareWorldClouds(const engine::render::WorldCloudView&) override;
    void resolveWorldClouds(const engine::render::WorldCloudView&) override;
    void clearWorldClouds() override;
    void prepareWorldWindField(const engine::render::WorldWindFieldInput&) override;
    void clearWorldWindField() override;
    bool captureWorldWindField() override;
    engine::render::WorldWindFieldDiagnostic readWorldWindField() override;
    bool captureWorldClouds() override;
    engine::render::WorldCloudDiagnostic readWorldClouds() override;
    void bindWorldLayeredMaterials(std::uint64_t, std::shared_ptr<const engine::render::WorldLayerMaterialSet>) override;
    void bindWorldWetSurfaces(const engine::render::WorldWetSurfaceView*) override;
    void bindWorldFog(const engine::render::WorldPrecipitationLightingView*,const engine::render::WorldPrecipitationLightingResult*,const engine::render::WorldPrecipitationLightingResult*) override;
    bool captureWorldPrecipitationLighting() override;
    engine::render::WorldPrecipitationLightingDiagnostic readWorldPrecipitationLighting() override;
    bool captureWorldRainMap(engine::render::WorldResourceScopeId) override;
    engine::render::WorldRainMapDiagnostic readWorldRainMap() override;
    void prewarmWorldRainMap(engine::render::WorldResourceScopeId,unsigned) override;
    bool beginWorldRainMap(engine::render::WorldResourceScopeId,const engine::render::WorldDirectionalShadowView&,bool) override;
    bool endWorldRainMap() override;
    bool publishWorldRainMap(engine::render::WorldResourceScopeId,unsigned) override;
    std::string worldRainMapTexture(engine::render::WorldResourceScopeId) const override;

    bool captureWorldClusteredLighting() override;
    engine::render::WorldClusteredLightingDiagnostic readWorldClusteredLighting() override;
    bool updateWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate&) override;
    bool prepareWorldIndirectLighting(std::span<const engine::render::WorldIndirectLightingUpdate>,std::uint64_t) override;
    bool bindWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate&) override;
    void clearWorldIndirectLighting() override;
    bool captureWorldIndirectLighting() override;
    engine::render::WorldIndirectLightingDiagnostic readWorldIndirectLighting() override;
    bool resolveWorldClusteredLighting(const engine::render::WorldClusteredLightingView& view) override;
    bool beginWorldTemporalScene(const engine::render::WorldTemporalView&) override;
    void releaseWorldTemporalView(std::uint64_t) override;
    bool resolveWorldTemporalScene() override;
    bool resolveWorldHeatHaze() override;
    bool resolveWorldTemporalReflection(const engine::render::WorldTemporalReflectionView&) override;
    engine::render::WorldTemporalFeedback worldTemporalFeedback() const override;
    bool captureWorldTemporalScene() override;
    engine::render::WorldTemporalImage readWorldTemporalScene() override;
    bool relightWorldProbeSurface(const engine::render::WorldClusteredLightingView&,const std::array<std::array<float,4>,32>&) override;
    void invalidateWorldPlacedProbes(std::span<const std::uint64_t>) override;
    bool reserveWorldProbes(std::span<const std::uint64_t>,float) override;
    bool worldProbePublished(std::uint64_t) const override;
    bool worldProbeBlending(std::uint64_t) const override;
    bool filterWorldProbe(std::uint64_t,unsigned) override;
    bool beginWorldPlacedProbeFace(std::uint64_t,std::uint64_t,const engine::render::WorldLocalReflectionProbe&,unsigned,std::span<const std::uint64_t>) override;
    bool endWorldPlacedProbeFace() override;
    bool selectWorldPlacedProbes(std::span<const std::uint64_t>) override;
    bool captureWorldProbeRadiance(std::span<const std::uint64_t>) override;
    engine::render::WorldProbeRadianceImage readWorldProbeRadiance() override;
    bool beginWorldProbeSurface(std::uint64_t,std::uint64_t,unsigned,const float*,int) override;
    bool endWorldProbeSurface() override;
    bool selectWorldProbeSurface(std::uint64_t,unsigned) override;
    std::uint64_t worldProbeSurfaceRevision(std::uint64_t) const override;
    bool releaseWorldProbeSurface(std::uint64_t) override;
    bool beginWorldSurfaceBuffer(const float*, int width, int height) override;
    bool setWorldSurfaceMotion(const engine::render::WorldMotionMatrix&,std::span<const engine::render::WorldMotionSample>) override;
    bool captureWorldSurfaceMotion() override;
    engine::render::WorldMotionImage readWorldSurfaceMotion() override;
    bool endWorldSurfaceBuffer() override;
    void clearWorldSurfaceBuffer() override;
    bool resolveWorldSurfaceContact(const engine::render::WorldContactUniforms&) override;
    bool restoreWorldSurfaceDepth(const engine::render::WorldContactUniforms&) override;
    bool beginWorldSurfaceCompositionCapture(const float*,int,int,const engine::render::WorldClusteredLightingView&) override;
    bool beginWorldSurfaceComposition(const engine::render::WorldContactUniforms&,const engine::render::WorldClusteredLightingView&) override;
    void endWorldSurfaceComposition() override;
    const engine::render::WorldClusteredLightingView* worldSurfaceCompositionLighting() const override;
    bool resolveWorldSurfaceScreenReflection() override;
    bool showWorldSurfaceBuffer(unsigned plane) override;
    bool beginWorldLocalHaze(int width, int height) override;
    bool endWorldLocalHaze() override;
    bool beginWorldLocalHazeComposition(int width,int height) override;
    void endWorldLocalHazeComposition() override;
  bool beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView&) override;
    bool endWorldPlanarReflection() override;
    bool beginWorldContactVisibility(const engine::render::WorldContactUniforms&) override;
    bool endWorldContactVisibility() override;
    void clearWorldContactVisibility() override;
    bool captureWorldContactVisibility() override;
    engine::render::WorldContactImage readWorldContactVisibility() override;
    bool beginWorldScreenReflection(const engine::render::WorldScreenReflectionUniforms&) override;
    bool endWorldScreenReflection() override;
    void clearWorldScreenReflection() override;
    bool beginWorldLocalReflection(const engine::render::WorldLocalReflectionProbe&) override;
    bool endWorldLocalReflectionFace() override;
    void clearWorldLocalReflection() override;
    bool captureWorldDepthSnapshot() override;
    void prewarmWorldLocalShadows() override;
    void clearWorldLocalShadows() override;
    bool beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) override;
    bool endWorldLocalShadow() override;
    engine::render::WorldShadowCacheResult beginWorldStaticShadow(std::uint32_t index,
        const engine::render::WorldDirectionalShadowView& view, engine::render::WorldShadowCacheKey key) override;
    bool endWorldStaticShadow() override;
    unsigned worldShadowScheduleBank() const override;
    engine::render::WorldShadowUpdateQuery queryWorldShadowUpdate(std::span<const std::uint32_t>,std::uint32_t,
        const engine::render::WorldDirectionalShadowView&,engine::render::WorldShadowUpdateKey) const override;
    bool reuseWorldShadowUpdate(std::uint32_t,const engine::render::WorldDirectionalShadowView&,engine::render::WorldShadowUpdateKey) override;
    void commitWorldShadowUpdate(std::uint32_t,const engine::render::WorldDirectionalShadowView&,engine::render::WorldShadowUpdateKey) override;
    engine::render::WorldShadowCacheResult queryWorldStaticShadow(std::span<const std::uint32_t>,
        std::uint32_t,const engine::render::WorldDirectionalShadowView&,engine::render::WorldShadowCacheKey) const override;
    void setWorldLocalShadowIdentities(std::span<const std::uint64_t>) override;
    void planWorldLocalShadowSources(std::span<const std::uint32_t>) override;
    void invalidateWorldStaticShadows() override;
    void prewarmWorldSunShadows(unsigned quality) override;
    bool setWorldSunShadowPlan(const engine::render::WorldSunShadowPlan&) override;
    void clearWorldSunShadows() override;
    std::uint64_t worldSunShadowIdentity() const override;
    void prewarmWorldDirectionalShadow(std::uint32_t resolution) override;
    bool beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) override;
    bool endWorldDirectionalShadow() override;
    void clearWorldDirectionalShadow() override;

  engine::render::WorldResourceScopeId createWorldResourceScope(std::uint64_t payloadByteLimit) override;
  bool bindWorldResourceAssetGroup(engine::render::WorldResourceScopeId child,
      std::uint64_t identity,std::uint64_t limit) override;
  bool beginWorldResourceScope(engine::render::WorldResourceScopeId id) override;
  bool endWorldResourceScope() override;
  bool requestWorldResourceScopeRelease(engine::render::WorldResourceScopeId id) override;
  bool getWorldResourceScopeStats(engine::render::WorldResourceScopeId id,
      engine::render::WorldResourceScopeStats& out) const override;
  engine::render::WorldResourceStats getWorldResourceStats() const override;
    using EditorSurfaceHandle = std::uint64_t;
    static constexpr EditorSurfaceHandle
        kInvalidEditorSurface = 0u;

    struct EditorUiContext {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = UINT32_MAX;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t minimumImageCount = 2u;
        std::uint32_t imageCount = 2u;
    };

    VulkanRenderBackend(SDL_Window *window,
                        int width,
                        int height,
                        bool vsyncEnabled,
                        const std::string &preferredAdapterName = {},
                        const engine::render::WorldMaterialProfile &profile = {});
    ~VulkanRenderBackend() override;
    void setWorldMaterialProfile(const engine::render::WorldMaterialProfile &profile) override;

    const char* backendId() const override { return "vulkan"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    bool beginScreenshotCaptureSequence() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return true; }
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
    std::string activeGpuName() const override;
    bool activeGpuIsDiscrete() const override;
    void setVSyncEnabled(bool enabled) override;
    void setWorldMaterialDebugView(int view) noexcept override;
    void shutdown() override;

    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    bool supportsWorldIndexedMeshInstancing() const override { return true; }
    bool supportsWorldSceneFastPath() const override;
    bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps& outCaps) const override;
    void setWorldSceneRadianceExposure(float exposure) override;
    void setWorldDisplaySettings(const engine::render::WorldDisplaySettings&) override;
    void setWorldHeatHazeNoise(const engine::render::WorldHeatHazeNoise&) override;
    void setWorldDiffuseProbes(const engine::render::WorldDiffuseProbeUniforms&) override;
    void beginWorldSceneColorPass(int surfaceWidth, int surfaceHeight) override;
    void endWorldSceneColorPass() override;
    void endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame&) override;
    void submitWorldScene(const WorldSceneFrame& frame,
                          const WorldSceneView& view) override;
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

    bool getEditorUiContext(
        EditorUiContext& outContext) const;
    // Borrowed recording segment; use before changing the render pass or submitting world draws.
    VkCommandBuffer currentEditorCommandBuffer() const;
    std::uint32_t currentEditorFrameIndex() const;
    EditorSurfaceHandle createEditorSurface(
        int width,
        int height);
    bool resizeEditorSurface(
        EditorSurfaceHandle surface,
        int width,
        int height);
    void destroyEditorSurface(
        EditorSurfaceHandle surface);
    bool beginEditorSurface(
        EditorSurfaceHandle surface);
    void endEditorSurface();
    bool getEditorSurfaceTexture(
        EditorSurfaceHandle surface,
        std::uint32_t frameIndex,
        VkSampler& outSampler,
        VkImageView& outImageView) const;

private:
    std::unique_ptr<VulkanRenderBackendImpl> impl_;
};
