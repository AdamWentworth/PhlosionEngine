#include "engine/render/WorldEnvironmentPacketCache.h"
#include "engine/render/WorldMaterialBindingCache.h"
#include "engine/render/WorldPlacedProbeCache.h"
#pragma once
#include "engine/render/WorldSunShadows.h"
#include "engine/render/IWorldDeformationPreparation.h"
#include "engine/render/WorldPreparedResources.h"
#include "engine/render/WorldPreparedView.h"
#include "engine/render/WorldDeformationStorage.h"
#include "engine/render/WorldMaterialFrameCache.h"
#include "engine/render/WorldRecordingWorkers.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "engine/render/IRenderBackend.h"
#include "engine/render/RenderPhaseProfile.h"
#include "engine/render/IWorldDepthControl.h"
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/WorldContactFilter.h"
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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include "engine/render/d3d12/D3D12WorldDrawPacket.h"
#include <wrl/client.h>

struct IDXGIAdapter1;
struct IDXGIFactory4;
struct IDXGISwapChain3;
struct ID3D12CommandAllocator;
struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12GraphicsCommandList;
struct ID3D12PipelineState;
struct ID3D12QueryHeap;
struct ID3D12Resource;
struct ID3D12RootSignature;
#endif

class D3D12RenderBackend final : public IRenderBackend, public IWorldResourceScope, public IWorldDepthControl, public engine::render::IWorldContactVisibility, public engine::render::IWorldScreenReflection, public engine::render::IWorldLocalReflection, public engine::render::IWorldProbeScheduling, public IWorldDirectionalShadow, public IWorldLocalShadow, public engine::render::IWorldBloom, public engine::render::IWorldSceneRadiance, public engine::render::IWorldDiffuseProbes, public engine::render::IWorldIndirectLighting, public engine::render::IWorldPlanarReflection, public engine::render::IWorldLocalHaze, public engine::render::IWorldPrecipitationLighting, public engine::render::IWorldClouds, public engine::render::IWorldWindField, public engine::render::IWorldParticleLighting,
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
  void invalidatePreparedWorldViews();
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
  bool prefersWorldSurfaceDepthReuse() const override { return true; }
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
  D3D12RenderBackend(SDL_Window *window,
                     int width,
                     int height,
                     bool vsyncEnabled,
                     const std::string &preferredAdapterName = {},
                     const engine::render::WorldMaterialProfile &profile = {});
  ~D3D12RenderBackend() override;
  void setWorldMaterialProfile(const engine::render::WorldMaterialProfile &profile) override;

  const char *backendId() const override { return "d3d12"; }
  void beginFrame(float r, float g, float b, float a) override;
  void endFrame() override;
  bool beginScreenshotCaptureSequence() override;
  void onResize(int width, int height) override;
  bool requiresOpenGLContext() const override { return false; }
  bool handlesPresentation() const override { return true; }
  bool getLastFrameTimings(BackendFrameTimings &outTimings) const override;
  bool getLastFrameStats(BackendFrameStats &outStats) const override;
  std::string activeGpuName() const override { return adapterName_; }
  bool activeGpuIsDiscrete() const override { return discreteAdapter_; }
  void setVSyncEnabled(bool enabled) override { vsyncEnabled_ = enabled; }
  bool supportsWorldTriangles3D() const override { return true; }
  bool supportsWorldIndexedMeshes() const override { return true; }
  bool supportsWorldIndexedMeshInstancing() const override { return true; }
  bool supportsWorldSceneFastPath() const override { return true; }
  bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps &outCaps) const override;
  void beginWorldSceneColorPass(int surfaceWidth, int surfaceHeight) override;
  void endWorldSceneColorPass() override;
    void endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame&) override;
  void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats &stats) override;
  void submitWorldScene(const WorldSceneFrame &frame,
                        const WorldSceneView &view) override;
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
  void drawWorldIndexedMeshTexturedCachedInstanced(const char *geometryKey,
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
  void prewarmDebugSpriteTextures(const char *const *texturePaths,
                                  std::size_t textureCount) override;
  void shutdown() override;

#if defined(_WIN32)
    struct EditorSurfaceTarget {
        ID3D12Resource* linearColor = nullptr;
        ID3D12Resource* displayColor = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE linearRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE displayRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv{};
        ID3D12Resource* depthResource = nullptr;
        std::uint32_t linearSrvDescriptorIndex = 0xffffffffu;
        int width = 0;
        int height = 0;
    };

    ID3D12Device* nativeDevice() const noexcept {
        return device_.Get();
    }
    ID3D12CommandQueue* nativeCommandQueue() const noexcept {
        return commandQueue_.Get();
    }
    ID3D12GraphicsCommandList* nativeCommandList() const noexcept {
        return commandList_.Get();
    }
    std::uint32_t reserveEditorSceneColorDescriptor();
    bool writeEditorSceneColorDescriptor(
        std::uint32_t descriptorIndex,
        ID3D12Resource* resource);
    bool beginEditorSurface(const EditorSurfaceTarget& target);
    void endEditorSurface();
    void bindBackbufferForEditorUi();
    void waitUntilIdle();
#endif

private:
    bool captureWorldDepthSnapshotFor(const char* purpose);
    bool restoreWorldDepthSnapshot();
    engine::render::WorldResourceScopeState worldScopes_;
    bool collectWorldResourceScopes();
    void configureScreenshotCapture();

    void initDeviceAndSwapchain(const std::string& preferredAdapterName);
    void createRenderTargets();
    void releaseRenderTargets();
    void createDepthResources();
    void releaseDepthResources();
    void createDebugPipeline();
    void createWorldPipeline(bool createBuffers = true);
    void bindWorldDiffuseProbes(bool haze = false, bool clustered = false, bool composable = false, bool additiveFog = false);
#if defined(_WIN32)
    engine::render::d3d12_internal::WorldDiffuseBindings prepareWorldDiffuseBindings(bool haze, bool clustered, bool composable, bool additiveFog = false);
    D3D12_GPU_VIRTUAL_ADDRESS prepareWorldMaterialConstants(const void*, std::size_t viewOffset, bool outline = false);
    D3D12_GPU_VIRTUAL_ADDRESS prepareWorldSurfaceMotion();
#endif
    void bindWorldMaterialConstants(const void*, std::size_t viewOffset, bool outline = false);
    void createSpritePipeline();
    void createWorldSceneColorPipeline();
    void createScreenFeedbackPipeline();
    void createWorldSceneColorResource();
    void releaseWorldSceneColorResource();
    struct SpriteTexture;
    SpriteTexture* ensureSpriteTexture(const std::string& texturePath);
    SpriteTexture* ensureFallbackSpriteTexture();
    SpriteTexture* ensureWorldTextureRaw(const char* key,
                                         const unsigned char* rgba,
                                         int width,
                                         int height,
                                         int wrapS,
                                         int wrapT,
                                         bool srgb) {
        return ensureWorldTextureRaw(key, nullptr, rgba, width, height, wrapS, wrapT, srgb);
    }
    SpriteTexture* ensureWorldTextureRaw(const char* key,
                                         const char* cacheKey,
                                         const unsigned char* rgba,
                                         int width,
                                         int height,
                                         int wrapS,
                                         int wrapT,
                                         bool srgb,
                                         const WorldTextureMipLevel*
                                             authoredMipLevels = nullptr,
                                         std::uint32_t authoredMipLevelCount = 0u,
                                         bool materialImage = false);
    SpriteTexture* ensureWorldTextureRawHalfFloat(const char* key,
                                                  const std::uint16_t* rgba16f,
                                                  int width,
                                                  int height,
                                                  int wrapS,
                                                  int wrapT);
    SpriteTexture* ensureWorldTexture(const WorldTextureData* textureData);
    void ensureWorldFallbackEnvTexture();
    const SpriteTexture* findWorldTextureByDescriptorIndex(
        std::uint32_t descriptorIndex) const;
    bool prepareWorldMaterialDescriptorBlock(const WorldTextureData* textureData,
                                             bool logPbrBinding,
                                             std::uint32_t& outDescriptorBlockIndex,
                                             float& outUseTexture);
    std::uint32_t ensureWorldMaterialDescriptorBlock(
        std::uint32_t baseTextureDescriptorIndex,
        std::uint32_t normalTextureDescriptorIndex,
        std::uint32_t metallicRoughnessTextureDescriptorIndex,
        std::uint32_t occlusionTextureDescriptorIndex,
        std::uint32_t emissiveTextureDescriptorIndex,
        std::uint32_t envTextureDescriptorIndex,
        std::uint32_t lightProjectionTextureDescriptorIndex,
        std::uint32_t projectedShadowTextureDescriptorIndex);
#if defined(_WIN32)
    struct WorldMaterialDescriptorBlockKey {
        std::uint32_t baseTextureDescriptorIndex = 0u;
        std::uint32_t normalTextureDescriptorIndex = 0u;
        std::uint32_t metallicRoughnessTextureDescriptorIndex = 0u;
        std::uint32_t occlusionTextureDescriptorIndex = 0u;
        std::uint32_t emissiveTextureDescriptorIndex = 0u;
        std::uint32_t envTextureDescriptorIndex = 0u;
        std::uint32_t lightProjectionTextureDescriptorIndex = 0u;
        std::uint32_t projectedShadowTextureDescriptorIndex = 0u;
        engine::render::WorldResourceScopeId scope = 0;

        bool operator==(const WorldMaterialDescriptorBlockKey& other) const {
            return scope == other.scope &&
                   baseTextureDescriptorIndex == other.baseTextureDescriptorIndex &&
                   normalTextureDescriptorIndex == other.normalTextureDescriptorIndex &&
                   metallicRoughnessTextureDescriptorIndex ==
                       other.metallicRoughnessTextureDescriptorIndex &&
                   occlusionTextureDescriptorIndex == other.occlusionTextureDescriptorIndex &&
                   emissiveTextureDescriptorIndex == other.emissiveTextureDescriptorIndex &&
                   envTextureDescriptorIndex == other.envTextureDescriptorIndex &&
                   lightProjectionTextureDescriptorIndex ==
                        other.lightProjectionTextureDescriptorIndex &&
                   projectedShadowTextureDescriptorIndex ==
                        other.projectedShadowTextureDescriptorIndex;
        }
    };
    struct WorldMaterialDescriptorBlockKeyHash {
        std::size_t operator()(const WorldMaterialDescriptorBlockKey& key) const noexcept {
            std::size_t h = std::hash<engine::render::WorldResourceScopeId>{}(key.scope);
            h ^= static_cast<std::size_t>(key.baseTextureDescriptorIndex);
            h ^= static_cast<std::size_t>(key.normalTextureDescriptorIndex) + 0x9e3779b9u +
                 (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.metallicRoughnessTextureDescriptorIndex) +
                 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.occlusionTextureDescriptorIndex) + 0x9e3779b9u +
                 (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.emissiveTextureDescriptorIndex) + 0x9e3779b9u +
                 (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.envTextureDescriptorIndex) + 0x9e3779b9u +
                 (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(
                     key.lightProjectionTextureDescriptorIndex) +
                  0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(
                     key.projectedShadowTextureDescriptorIndex) +
                  0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct CachedDebugGeometry {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
        std::uint64_t gpuAddress = 0u;
        std::size_t vertexCount = 0u;
        std::size_t vertexBytes = 0u;
        bool valid = false;
    };
    struct CachedWorldMesh {
        engine::render::WorldResourceScopeId scope = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
        std::uint64_t vertexGpuAddress = 0;
        std::uint64_t indexGpuAddress = 0;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
        std::size_t vertexBytes = 0u;
        std::size_t indexBytes = 0u;
        bool valid = false;
    };
    CachedWorldMesh* ensureCachedWorldMesh(const char* geometryKey,
                                           const WorldMeshVertex* vertices,
                                           std::size_t vertexCount,
                                           const std::uint32_t* indices,
                                           std::size_t indexCount);
    void drawWorldIndexedMeshCachedInternal(const CachedWorldMesh& mesh,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight);
    void drawWorldIndexedMeshTexturedCachedInternal(const CachedWorldMesh& mesh,
                                                    const WorldMeshVertex* vertices,
                                                    std::size_t vertexCount,
                                                    const std::uint32_t* indices,
                                                    std::size_t indexCount,
                                                    std::uint32_t materialDescriptorBlockIndex,
                                                    const WorldTextureData* textureData,
                                                    float useTexture,
                                                    const float* viewProjectionMatrix4x4,
                                                    std::uint64_t instanceDataGpuAddress,
                                                    std::uint32_t instanceCount,
                                                    int surfaceWidth,
                                                    int surfaceHeight);
    void drawWorldIndexedMeshTexturedCachedPreparedInstanced(
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
        float materialPrepMs);
    ID3D12PipelineState* selectWorldPipelineState(
        const WorldTextureData* textureData, float materialMode, const engine::render::WorldEnvironmentPacket& packet);
    static unsigned worldPipelineVariant(const WorldTextureData* textureData);
    ID3D12PipelineState* ensureWorldPointLightPipeline(float mode, unsigned variant);
#endif
    void drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      std::uint32_t materialDescriptorBlockIndex,
                                      const WorldTextureData* textureData,
                                      float useTexture,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight);
    void waitForGpu();
    bool waitForFenceValue(std::uint64_t fenceValue);
    void ensureWindowHandle();
    void initializeWorldSceneFastPathCaps();

private:
    SDL_Window* window_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    bool initialized_ = false;
    bool recording_ = false;
    bool discreteAdapter_ = false;
    std::string adapterName_;
    float lastPresentWaitMs_ = 0.0f;
    float lastGpuFrameMs_ = 0.0f;
    bool lastGpuFrameValid_ = false;
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
    std::uint32_t frameIndexedD3d12PsoSets_ = 0u;
    std::uint32_t frameIndexedD3d12DescriptorTableSets_ = 0u;
    std::uint32_t frameFastSceneInstances_ = 0u;
    std::uint32_t frameFastSceneDrawClasses_ = 0u;
    std::uint32_t frameFastSceneVisibleSkeletons_ = 0u;
    std::uint64_t frameFastScenePaletteUploadBytes_ = 0u;
    std::uint32_t frameFastSceneMaterialTableBinds_ = 0u;
    std::uint32_t frameFastSceneIndirectCommands_ = 0u;
    std::uint32_t lastFrameIndexedOpaqueDraws_ = 0u;
    std::uint32_t lastFrameIndexedBlendDraws_ = 0u;
    std::uint32_t lastFrameIndexedCachedDraws_ = 0u;
    std::uint32_t lastFrameIndexedDynamicDraws_ = 0u;
    std::uint32_t lastFrameIndexedInstancedDraws_ = 0u;
    std::uint32_t lastFrameIndexedOutlineBatches_ = 0u;
    std::uint32_t lastFrameIndexedGeometrySwitches_ = 0u;
    std::uint32_t lastFrameIndexedMaterialSwitches_ = 0u;
    std::uint32_t lastFrameIndexedTextureSwitches_ = 0u;
    std::uint32_t lastFrameIndexedD3d12PsoSets_ = 0u;
    std::uint32_t lastFrameIndexedD3d12DescriptorTableSets_ = 0u;
    std::uint32_t lastFrameFastSceneInstances_ = 0u;
    std::uint32_t lastFrameFastSceneDrawClasses_ = 0u;
    std::uint32_t lastFrameFastSceneVisibleSkeletons_ = 0u;
    std::uint64_t lastFrameFastScenePaletteUploadBytes_ = 0u;
    std::uint32_t lastFrameFastSceneMaterialTableBinds_ = 0u;
    std::uint32_t lastFrameFastSceneIndirectCommands_ = 0u;
    WorldSceneFastPathCaps worldSceneFastPathCaps_{};
    bool screenshotCaptureDeferred_ = false;
    bool screenshotCaptureConfigured_ = false;
    bool screenshotCaptured_ = false;
    bool vsyncEnabled_ = true;
    bool allowTearingSupported_ = false;
    std::uint64_t screenshotFrameTarget_ = 0u;
    std::uint64_t frameCounter_ = 0u;
    bool worldFallbackEnvTextureReady_ = false;
    std::string screenshotPath_;

    float clearColor_[4] = {0.1f, 0.1f, 0.1f, 1.0f};

    static constexpr std::uint32_t kFrameCount = 2;
    static constexpr std::uint32_t kTimestampQueriesPerFrame = engine::render::RenderPhaseProfile::queryCapacity;
    engine::render::RenderPhaseProfile phaseProfile_;
    std::array<engine::render::RenderPhaseProfile::Frame,kFrameCount> phaseFrames_{};
    static constexpr std::uint32_t kTimestampQueryCount = kFrameCount * kTimestampQueriesPerFrame;
    std::uint32_t frameIndex_ = 0;
    std::uint64_t fenceValue_ = 0;
    std::uint64_t timestampFrequency_ = 0;
    std::array<std::uint64_t, kFrameCount> frameFenceValues_{};

#if defined(_WIN32)
    void* hwnd_ = nullptr;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    std::uint32_t rtvDescriptorSize_ = 0;
    std::uint32_t dsvDescriptorSize_ = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldSceneColorTarget_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> feedbackRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> feedbackPipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> feedbackRtvHeap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> feedbackImages_;
    std::array<std::uint32_t,2> feedbackSrv_{};
    std::uint64_t feedbackSequence_=0;
    int feedbackWidth_=0,feedbackHeight_=0,feedbackHistory_=0,feedbackDisplayInput_=0;
    engine::render::ScreenFeedbackUpdate feedbackLast_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldSceneColorRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldDepthRestorePipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldSceneColorPipelineState_;
    std::uint32_t worldSceneColorSrvDescriptorIndex_ = 0xffffffffu;
    bool worldSceneColorPassActive_ = false;
    EditorSurfaceTarget editorSurfaceTarget_{};
    bool editorSurfaceActive_ = false;
    bool editorSurfaceScenePassActive_ = false;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> depthBuffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampQueryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadbackBuffer_;
    void* fenceEvent_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugSceneColorPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugVertexBuffer_;
    std::uint64_t debugVertexBufferGpuAddress_ = 0;
    std::uint32_t debugVertexStride_ = 0;
    std::uint32_t debugVertexBufferSize_ = 0;
    std::uint32_t debugVertexBufferBytesPerFrame_ = 0;
    std::uint32_t debugVertexFrameBaseOffset_ = 0;
    std::uint32_t debugVertexFrameOffset_ = 0;
    std::uint8_t* debugVertexMappedData_ = nullptr;
    std::unordered_map<std::string, CachedDebugGeometry> cachedDebugQuads_;
    std::unordered_map<std::string, CachedDebugGeometry> cachedDebugLines_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldSkyPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldSceneSkyPipelineState_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 11> worldScenePipelines_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldShadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldContactFilterPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldShadowDepth_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,3> worldMomentTargets_;
    engine::render::WorldShadowCache worldShadowCache_;
    engine::render::WorldShadowUpdateCache worldShadowUpdates_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> worldMomentRtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldMomentRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldMomentPipeline_;
    std::uint32_t worldMomentSrv_=0xffffffffu;
    void prewarmWorldShadowMoments();
    void filterWorldShadowMoments();
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> worldShadowDsvHeap_;
    engine::render::WorldLocalShadowAtlas worldLocalShadows_;
    engine::render::WorldDirectionalShadowView worldShadowView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> worldSunDepth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> worldSunDsv_;
    engine::render::WorldSunShadowPlan worldSunPlan_{};
    engine::render::WorldSunShadowUniforms lastWorldSun_{};
    engine::render::WorldDirectionalShadowView worldSunSavedView_{};
    std::uint32_t worldSunDescriptor_=0xffffffffu,worldSunQuality_=~0u;
    bool worldSunSavedReady_=false;
    unsigned worldSunPending_=0;
    std::array<std::uint32_t,2> worldShadowOffset() const;
    std::uint32_t worldShadowResolution_ = 0;
    bool worldShadowActive_ = false, worldShadowReady_ = false;
    D3D12_VIEWPORT worldLastViewport_{}, worldShadowSavedViewport_{};
    D3D12_RECT worldLastScissor_{}, worldShadowSavedScissor_{};
    void writeWorldShadowSrv(std::uint32_t descriptorIndex);
    std::uint32_t worldRectLightLutDescriptor_ = 0;
    std::function<Microsoft::WRL::ComPtr<ID3D12PipelineState>(float, unsigned)>
        worldPointLightPipelineFactory_;
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> worldPointLightPipelines_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldAdditiveBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldPremultipliedBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldNoDepthBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldNoDepthAdditiveBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldNoDepthPremultipliedBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldDualSourceBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldDualSourceAdditiveBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldNoDepthDualSourceBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldNoDepthDualSourceAdditiveBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldVertexBuffer_;
    std::uint64_t worldVertexBufferGpuAddress_ = 0;
    std::uint32_t worldVertexStride_ = 0;
    std::uint32_t worldVertexBufferSize_ = 0;
    std::uint32_t worldVertexBufferBytesPerFrame_ = 0;
    std::uint32_t worldVertexFrameBaseOffset_ = 0;
    std::uint32_t worldVertexFrameOffset_ = 0;
    std::uint8_t* worldVertexMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldIndexBuffer_;
    std::uint64_t worldIndexBufferGpuAddress_ = 0;
    std::uint32_t worldIndexBufferSize_ = 0;
    std::uint32_t worldIndexBufferBytesPerFrame_ = 0;
    std::uint32_t worldIndexFrameBaseOffset_ = 0;
    std::uint32_t worldIndexFrameOffset_ = 0;
    std::uint8_t* worldIndexMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldDiffuseProbeBuffer_;
    std::uint8_t* worldDiffuseProbeMapped_ = nullptr;
    std::uint32_t worldDiffuseProbePacketCount_ = 0;
    engine::render::WorldMaterialFrameCache worldDiffusePackets_;
    std::uint64_t worldDiffusePacketAddress_ = 0;
    engine::render::WorldDiffuseProbeUniforms lastWorldDiffuseProbes_{};
    engine::render::WorldContactUniforms lastWorldContact_{};
    engine::render::WorldScreenReflectionUniforms lastWorldScreenReflection_{};
    std::uint32_t worldScreenReflectionDescriptor_=0xffffffffu;
    bool screenSourceSpecialization_ = true;
    struct PreparedWorldView {
        std::uint64_t address = 0;
        engine::render::WorldEnvironmentPacket packet{};
    };
    engine::render::WorldPreparedViews<PreparedWorldView> preparedWorldViews_;
    struct WorldRecordingLane {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> lists;
        std::size_t used=0;
    };
    std::array<std::array<WorldRecordingLane,2>,kFrameCount> worldRecordingFrames_;
    std::unique_ptr<engine::render::WorldRecordingWorkers> worldRecordingWorkers_;
    std::vector<engine::render::d3d12_internal::WorldIndexedDrawPacket> worldDrawPackets_;
    engine::render::WorldRecordingStats worldRecordingStats_;
    void submitWorldDrawPacket(const engine::render::d3d12_internal::WorldIndexedDrawPacket& packet);
    void flushWorldDrawPackets();
    void resetWorldRecordingFrame();
    engine::render::backend::WorldPreparedViewToken prepareWorldViewDataInternal(const WorldTextureData*);
    const PreparedWorldView& resolvePreparedWorldView(const WorldTextureData*);
    void uploadPreparedWorldDeformations(std::span<const engine::render::WorldSkinPalette>,bool commit);
    bool uploadWorldSkinPalette(const float*, std::size_t, std::uint64_t&, engine::render::WorldDeformation version={});
    engine::render::WorldEnvironmentPacketCache worldEnvironmentPackets_;
    engine::render::WorldMaterialBindingCache<std::pair<std::uint32_t,float>> retainedWorldMaterialBindings_;
    std::uint32_t worldLocalReflectionDescriptor_=0xffffffffu;
    engine::render::WorldLocalReflectionUniforms lastWorldLocalReflection_{};
    bool captureWorldRadianceSnapshot(const char*,int,int,std::uint32_t&,std::array<float,4>&,int atlasFace=-1,int atlasLevel=0,int atlasResolution=0,int offsetY=0,int offsetX=0,int residentExtent=0,bool clearScene=true,bool batchFaces=false);
    Microsoft::WRL::ComPtr<ID3D12Resource> worldVsConstantBuffer_;
    std::uint64_t worldVsConstantBufferGpuAddress_ = 0;
    std::uint32_t worldVsConstantBufferSize_ = 0;
    std::uint32_t worldVsConstantBufferBytesPerFrame_ = 0;
    std::uint32_t worldVsConstantFrameBaseOffset_ = 0;
    std::uint32_t worldVsConstantFrameOffset_ = 0;
    std::uint8_t* worldVsConstantMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldSkinMatrixBuffer_;
    std::uint64_t worldSkinMatrixBufferGpuAddress_ = 0;
    std::uint32_t worldSkinMatrixBufferSize_ = 0;
    std::uint32_t worldSkinMatrixBufferBytesPerFrame_ = 0;
    std::uint32_t worldSkinMatrixFrameBaseOffset_ = 0;
    std::uint32_t worldSkinMatrixFrameOffset_ = 0;
    std::uint8_t* worldSkinMatrixMappedData_ = nullptr;
    engine::render::WorldDeformationStorage frameSkinPalettes_;
    engine::render::WorldMaterialFrameCache frameMaterialConstants_;
    std::uint64_t deformationSerial_=0,deformationCompleted_=0;
    std::array<std::uint64_t,kFrameCount> deformationFrameSerials_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> worldInstanceBuffer_;
    std::uint64_t worldInstanceBufferGpuAddress_ = 0;
    std::uint32_t worldInstanceBufferSize_ = 0;
    std::uint32_t worldInstanceBufferBytesPerFrame_ = 0;
    std::uint32_t worldInstanceFrameBaseOffset_ = 0;
    std::uint32_t worldInstanceFrameOffset_ = 0;
    std::uint8_t* worldInstanceMappedData_ = nullptr;
    std::uint32_t worldFallbackTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackNormalTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackMetallicRoughnessTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackOcclusionTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackEmissiveTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackEnvTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackMaterialDescriptorBlockIndex_ = 0xffffffffu;

    struct SpriteTexture {
        engine::render::WorldResourceScopeId scope = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint32_t descriptorIndex = 0;
#if defined(_WIN32)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        bool hasSrvDesc = false;
#endif
        bool valid = false;
    };
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    std::uint32_t srvDescriptorSize_ = 0;
    std::uint32_t nextSrvDescriptorIndex_ = 0;
    std::vector<std::uint32_t> freeWorldTextureDescriptors_;
    std::vector<std::uint32_t> freeWorldMaterialBlocks_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> spriteRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spriteVertexBuffer_;
    std::uint64_t spriteVertexBufferGpuAddress_ = 0;
    std::uint32_t spriteVertexStride_ = 0;
    std::uint32_t spriteVertexBufferSize_ = 0;
    std::uint32_t spriteVertexBufferBytesPerFrame_ = 0;
    std::uint32_t spriteVertexFrameBaseOffset_ = 0;
    std::uint32_t spriteVertexFrameOffset_ = 0;
    std::uint8_t* spriteVertexMappedData_ = nullptr;
    std::unordered_map<std::string, SpriteTexture> spriteTextures_;
    std::unordered_map<std::string, SpriteTexture> worldTextures_;
    struct IndirectState;
    std::shared_ptr<IndirectState> indirect_;
    std::array<std::uint32_t,2> worldIndirectDescriptors_{0xffffffffu,0xffffffffu};
    std::uint32_t worldIndirectReceiver_=0xffffffffu;
    std::uint32_t ensureWorldIndirectNull();
    void destroyWorldIndirectLighting();
    struct ParticleLightingState;
    std::shared_ptr<ParticleLightingState> particleLighting_;
    void releaseWorldParticleLighting(std::uint64_t owner=~std::uint64_t{0});
    SpriteTexture* findWorldParticleLightingTexture(const char*);
    const SpriteTexture* findWorldParticleLightingTexture(std::uint32_t descriptor) const;
    struct WindState;
    std::shared_ptr<WindState> windField_;
    struct LayeredState;
    std::shared_ptr<LayeredState> layeredMaterials_;
    D3D12_GPU_VIRTUAL_ADDRESS worldLayeredAddress();
    D3D12_GPU_DESCRIPTOR_HANDLE worldLayeredTextures();
    void releaseWorldLayeredMaterials(std::uint64_t owner=~0ull);
    D3D12_GPU_VIRTUAL_ADDRESS worldWindAddress();
    void releaseWorldWindField(std::uint64_t owner=~0ull);
    struct CloudState;
    std::shared_ptr<CloudState> clouds_;
    engine::render::WorldCloudUniforms worldClouds_{},lastWorldClouds_{};
    engine::render::WorldCloudRows worldSkyCloud_{},lastWorldSkyCloud_{};
    std::uint32_t worldSkyModelDescriptor_=0xffffffffu;
    std::array<std::uint32_t,2> worldCloudDescriptors_{0xffffffffu,0xffffffffu};
    std::uint64_t worldCloudOwner_=0;
    void recordWorldClouds(const engine::render::WorldCloudView&,bool);
    void releaseWorldClouds(std::uint64_t owner=~std::uint64_t{0});
    const SpriteTexture* findWorldCloudTexture(std::uint32_t) const;
    struct PrecipitationState;
    std::shared_ptr<PrecipitationState> precipitation_;
    engine::render::WorldFogUniforms worldFog_{},lastWorldFog_{};
    engine::render::WorldWetUniforms worldWet_{},lastWorldWet_{};
    std::uint32_t worldWetDescriptor_=0xffffffffu;
    std::array<std::uint32_t,5> worldWetInputs_{};
    std::uint64_t worldWetOwner_=0;
    engine::render::WorldWetBindingHistory worldWetHistory_;
    std::unordered_map<std::uint64_t,std::uint32_t> worldWetFallbackDescriptors_;
    std::array<std::uint32_t,2> worldFogDescriptors_{0xffffffffu,0xffffffffu};
    void releaseWorldPrecipitation(std::uint64_t owner=~std::uint64_t{0});
    SpriteTexture* findWorldPrecipitationTexture(const char*);
    const SpriteTexture* findWorldPrecipitationDepth(const engine::render::WorldParticleLightingView&,std::array<float,4>&) const;
    const SpriteTexture* findWorldPrecipitationTexture(std::uint32_t descriptor) const;
    struct RainMapState;
    std::shared_ptr<RainMapState> rainMaps_;
    void releaseWorldRainMaps(std::uint64_t owner=~std::uint64_t{0});
    SpriteTexture* findWorldRainTexture(const char* key);

    void releaseWorldIndirectScope(std::uint64_t owner);
    struct ClusteredState;
    std::shared_ptr<ClusteredState> clustered_;
    void destroyWorldClusteredLighting();
    engine::render::WorldClusteredLightingView worldCompositionLighting{};
    engine::render::WorldClusteredConsumerUniforms worldClusteredView_{};
    engine::render::WorldClusteredConsumerUniforms lastWorldClusteredView_{};
    std::array<std::uint32_t,4> worldStoredSurfaceDescriptors_{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
    std::array<std::uint32_t,3> worldClusteredDescriptors_{0xffffffffu,0xffffffffu,0xffffffffu};
    struct TemporalState;
    std::shared_ptr<TemporalState> temporal_;
    std::uint32_t worldTemporalOutput_=0xffffffffu;
    bool worldDisplayReady_=false;
    void clearWorldTemporalScene();
    void releaseWorldTemporalScene();
    void collectWorldTemporalViews();
    bool processWorldTemporalScene(bool finish,bool distort=false);
    bool processWorldContactVisibility();
    struct SurfaceTarget {
        std::array<SpriteTexture,6> planes;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs, probeDsv;
        SpriteTexture probeDepth;
        bool probeScratch=false;
        engine::render::WorldContactUniforms view{};
    };
    std::unordered_map<std::string,SurfaceTarget> worldSurfaceTargets_;
    std::unordered_map<std::string,SpriteTexture> worldProbeScratch_;
    SurfaceTarget* worldSurfaceTarget_=nullptr;
    engine::render::WorldProbeSurfaceCache worldProbeSurfaces_;
    engine::render::WorldPlacedProbeCache placedProbes;
    engine::render::WorldProbeResidency probeResidency;
    std::uint32_t placedProbeDescriptor=0xffffffffu;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldProbeRadianceReadback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT worldProbeReadbackFootprint_{};
    std::uint64_t worldProbeReadbackBytes_=0;
    engine::render::WorldProbeRadianceImage worldProbeDiagnostic_{};
    std::uint32_t worldProbeCopyUav_=0xffffffffu;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldProbeCopyRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldProbeCopyPipeline_;
    std::string worldProbeSurfaceKey_;
    std::vector<std::array<float,4>> worldMotionWords_{{}};
    D3D12_GPU_VIRTUAL_ADDRESS worldMotionGpu_=0;
    void bindWorldSurfaceMotion();
    Microsoft::WRL::ComPtr<ID3D12Resource> worldMotionReadback_;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,5> worldMotionFootprints_{};
    std::uint64_t worldMotionReadbackBytes_=0,worldMotionSurfaceBytes_=0;
    int worldMotionWidth_=0,worldMotionHeight_=0;
    std::uint64_t worldSurfaceFrame_=0;
    unsigned worldSurfaceSlot_=0;
    // Retained probe views must not renumber transient scene allocations.
    std::array<unsigned,2> worldSurfaceAllocationSlots_{};
    engine::render::WorldContactUniforms worldSurfaceView_{};
    std::uint32_t worldSurfaceDepth_=0xffffffffu;
    bool worldSurfacePlanar_=false;
    bool worldSurfaceFoliageComposing_=false, worldSurfaceComposing_=false, worldSurfaceComposed_=false, worldSurfaceCompositionFinished_=false, worldSurfaceCompositionCapture_=false;
    bool worldSurfaceActive_=false,worldSurfaceReady_=false,worldSurfaceContact_=false;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> worldHazeRtvHeap_;
    ID3D12Resource* worldHazeTarget_ = nullptr;
    std::uint32_t worldHazeDescriptor_ = 0xffffffffu;
    bool worldHazeActive_ = false;
    bool worldHazeStencilActive_ = false;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> worldHazeDsvHeap_;
    std::array<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>, kFrameCount> worldHazeStencil_;
    std::size_t worldHazeStencilSlot_ = 0;
    std::uint32_t worldPlanarDescriptor_ = 0xffffffffu;
    std::array<int,4> worldPlanarScissor_{};
    std::uint32_t worldDepthSnapshotDescriptor_ = 0xffffffffu;
    engine::render::WorldContactHistory worldContactHistory_{};
    std::array<std::uint32_t,3> worldContactFilterDescriptors_{0xffffffffu,0xffffffffu,0xffffffffu};
    std::uint32_t worldContactPreviousVisibility_=0xffffffffu,worldContactPreviousDepth_=0xffffffffu;
    std::unordered_map<WorldMaterialDescriptorBlockKey,
                       std::uint32_t,
                       WorldMaterialDescriptorBlockKeyHash>
        worldMaterialDescriptorBlocks_;
    struct WorldSceneMaterialBindingCacheEntry {
        std::uint32_t descriptorBlockIndex = 0xffffffffu;
        float useTexture = 0.0f;
        bool valid = false;
    };
    std::vector<WorldSceneMaterialBindingCacheEntry> worldSceneMaterialBindingCache_;
    std::uint32_t worldSceneMaterialBindingCacheGeneration_ = 0u;
    const void* worldSceneMaterialBindingCacheRegistryIdentity_ = nullptr;
    engine::render::WorldPreparedResources<const CachedWorldMesh*> preparedWorldGeometry_;
    engine::render::WorldPreparedResources<std::pair<std::uint32_t,float>> preparedWorldMaterials_;
    engine::render::WorldPreparedResourceStats preparedWorldResourceStats_;
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes_;
#endif
};
