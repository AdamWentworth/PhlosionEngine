#include "engine/core/Environment.h"
#include "engine/render/WorldLocalReflectionFilter.h"
#include "engine/render/WorldScreenReflectionTiles.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldSurfaceReflection.h"
#include "engine/render/WorldSurfaceComposition.h"

#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <stdexcept>

VulkanRenderBackend::VulkanRenderBackend(SDL_Window *window,
                                         int width,
                                         int height,
                                         bool vsyncEnabled,
                                         const std::string &preferredAdapterName,
                                         const engine::render::WorldMaterialProfile &profile)
    : IRenderBackend(profile), impl_(std::make_unique<VulkanRenderBackendImpl>()) {
    if (!window) {
        throw std::runtime_error("VulkanRenderBackend requires a valid SDL_Window.");
    }
    impl_->worldMaterialProfile = profile;
    impl_->initialize(window, width, height, vsyncEnabled, preferredAdapterName);
}

VulkanRenderBackend::~VulkanRenderBackend() {
    shutdown();
}

void VulkanRenderBackend::setWorldMaterialProfile(const engine::render::WorldMaterialProfile &profile) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    profile.validate();
    if (profile == worldMaterialProfile_) return;
    if (!impl_ || impl_->frameActive) {
        throw std::runtime_error("Material profiles must be selected between Vulkan frames.");
    }
    if (vkDeviceWaitIdle(impl_->device) != VK_SUCCESS) {
        throw std::runtime_error("Unable to wait for Vulkan material pipeline replacement.");
    }
    impl_->destroyPipelines();
    impl_->worldMaterialProfile = profile;
    try {
        impl_->createPipelines();
    } catch (...) {
        impl_->destroyPipelines();
        impl_->worldMaterialProfile = worldMaterialProfile_;
        impl_->createPipelines();
        throw;
    }
    worldMaterialProfile_ = profile;
}

void VulkanRenderBackend::beginFrame(float r, float g, float b, float a) {
    if (impl_) impl_->beginFrame(r, g, b, a);
}

void VulkanRenderBackend::endFrame() {
    if (impl_) impl_->endFrame();
}

bool VulkanRenderBackend::beginScreenshotCaptureSequence() {
    if (!impl_ || !impl_->screenshotCaptureConfigured || !impl_->screenshotCaptureDeferred ||
        impl_->screenshotCaptured) return false;
    impl_->screenshotFrameTarget += impl_->frameCounter;
    impl_->screenshotCaptureDeferred = false;
    return true;
}

void VulkanRenderBackend::setWorldDiffuseProbes(const engine::render::WorldDiffuseProbeUniforms& value) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    IWorldDiffuseProbes::setWorldDiffuseProbes(value);
    if (impl_) impl_->worldDiffuseProbes = value;
}

void VulkanRenderBackend::setWorldHeatHazeNoise(const engine::render::WorldHeatHazeNoise& noise) {
    IWorldSceneRadiance::setWorldHeatHazeNoise(noise);
    if(impl_&&impl_->worldHeatHazeNoise.key!=noise.key)impl_->worldHeatHazeNoise=noise;
}
void VulkanRenderBackend::setWorldDisplaySettings(const engine::render::WorldDisplaySettings& settings) {
    IWorldSceneRadiance::setWorldDisplaySettings(settings);
    if(impl_)impl_->worldDisplaySettings=settings;
}

void VulkanRenderBackend::setWorldSceneRadianceExposure(float exposure) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    IWorldSceneRadiance::setWorldSceneRadianceExposure(exposure);
    if (impl_) impl_->worldSceneRadianceExposure = exposure;
}

void VulkanRenderBackend::beginWorldSceneColorPass(
    int surfaceWidth,
    int surfaceHeight) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if (impl_ && !impl_->worldSceneColorPassActive) {
        clearWorldSurfaceBuffer();
        worldBloomStrength_=0;
        setWorldSceneRadianceExposure(0);
        setWorldDiffuseProbes({});
        impl_->worldContact={};impl_->worldContactDescriptor=VK_NULL_HANDLE;impl_->worldContactFilterSet=VK_NULL_HANDLE;
        impl_->worldContactFilterSources={}; // Do not rebind a previous view's resizable SSR inputs.
        clearWorldScreenReflection();
        clearWorldLocalReflection();
        impl_->worldPlanarReflection={};
        impl_->worldPlanarDescriptor=VK_NULL_HANDLE;
        impl_->worldBloomStrength=0;
    }
    if (impl_) impl_->beginWorldSceneColorPass(surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::endWorldSceneColorPass() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    engine::render::RenderPhaseScope phase(*this,"post-compose");
    if (impl_) {impl_->worldBloomStrength=worldBloomStrength_;impl_->endWorldSceneColorPass();}
}

bool VulkanRenderBackend::beginWorldContactVisibility(const engine::render::WorldContactUniforms& value) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || !impl_->worldSceneColorPassActive || impl_->worldShadowActive || impl_->worldContact[8][3]!=0) return false;
    impl_->worldContact=value;
    return true;
}
bool VulkanRenderBackend::endWorldContactVisibility() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || impl_->worldContact[8][3]>=0)return false;
    if(impl_->worldSurfaceContact && impl_->processWorldContactVisibility())return true;
    auto& state=impl_->worldContact;
    const bool buffered=state[9][3]>.5f && !engine::env::flagEnabled("PHLOSION_LEGACY_CONTACT_VISIBILITY");
    if(!impl_->worldSurfaceContact && !impl_->captureWorldDepthSnapshot(buffered?"contact-depth":"scene-depth"))return false;
    auto& texture=*impl_->worldDepthSnapshot;
    if(!texture.descriptorSet)texture.descriptorSet=impl_->createPlanarDescriptor(texture);
    impl_->worldContactDescriptor=texture.descriptorSet;
    if(buffered) {
        const auto depth=texture.descriptorSet;
        const bool history=!engine::env::flagEnabled("PHLOSION_DISABLE_CONTACT_HISTORY") && impl_->worldContactHistory.accepts(state,impl_->frameCounter,impl_->activeEditorSurface,impl_->worldScopes.active());
        state[9][2]=history?1.f:0.f;state[9][1]=float(impl_->frameCounter%8)*.125f;
        for(int row=0;row<8;++row)state[14+row]=impl_->worldContactHistory.view[row];
        state[22]=impl_->worldContactHistory.view[10];
        VkDescriptorSet normals=VK_NULL_HANDLE,input=VK_NULL_HANDLE,prepared=VK_NULL_HANDLE;
        std::array<float,4> sampling{};
        if(impl_->worldSurfaceContact) normals=impl_->worldSurfaceTarget->planes[1].descriptorSet;
        else if(!impl_->captureWorldRadianceSnapshot("contact-normal",int(state[8][0]),int(state[8][1]),normals,sampling))return false;
        const bool result=engine::render::filterWorldContact(*this,state,[&](int stage){
            impl_->worldContactFilterSet=impl_->createContactFilterSet({input,(stage==2 || stage==3)?prepared:(history?impl_->worldContactPreviousVisibility:normals),
                stage==4 && history?impl_->worldContactPreviousDepth:normals});
        },[&](int stage,int width,int height){
            const char* purpose=stage==5?"contact-prepared-depth":stage==2?"contact-raw":stage==3?"contact-spatial":"contact-history";
            if(!impl_->captureWorldRadianceSnapshot(purpose,width,height,stage==5?prepared:input,sampling))return false;
            state[23]=sampling;return true;
        });
        if(!result){clearWorldContactVisibility();return false;}
        impl_->worldContactPreviousVisibility=input;impl_->worldContactPreviousDepth=depth;
        impl_->worldContactHistory={state,impl_->frameCounter,impl_->activeEditorSurface,impl_->worldScopes.active(),true};
        return true;
    }
    state[9][3]=0;
    impl_->worldContact[8][3]=1;
    return impl_->clearWorldDepth();
}
void VulkanRenderBackend::clearWorldContactVisibility() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(impl_) {impl_->worldContact={};impl_->worldContactDescriptor=VK_NULL_HANDLE;impl_->worldContactFilterSet=VK_NULL_HANDLE;impl_->worldContactHistory={};impl_->worldContactPreviousVisibility=impl_->worldContactPreviousDepth=VK_NULL_HANDLE;}
}

bool VulkanRenderBackend::beginWorldLocalReflection(const engine::render::WorldLocalReflectionProbe& probe) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || !impl_->worldSceneColorPassActive || impl_->worldShadowActive || impl_->worldSceneRadianceExposure<=0 ||
       impl_->worldPlanarReflection[4][3]!=0 || impl_->worldScreenReflection[8][3]!=0 || impl_->worldContact[8][3]!=0 || impl_->worldLocalReflection[0][3]!=0)return false;
    impl_->worldLocalReflection=engine::render::packWorldLocalReflection(probe);
    impl_->worldLocalReflectionPrepared=probe.sampling==engine::render::WorldLocalReflectionSampling::PreparedRadiance;
    impl_->worldLocalReflectionFace=0;return true;
}
bool VulkanRenderBackend::endWorldLocalReflectionFace() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || impl_->worldLocalReflection[0][3]>=0)return false;
    const int size=int(impl_->worldLocalReflection[2][3]);
    if(!impl_->captureWorldRadianceSnapshot("local-reflection",size,size,impl_->worldLocalReflectionDescriptor,impl_->worldLocalReflection[3],impl_->worldLocalReflectionFace))return false;
    if(++impl_->worldLocalReflectionFace==6 && !impl_->worldLocalReflectionPrepared)impl_->worldLocalReflection[0][3]=-3;
    if(impl_->worldLocalReflectionFace==(impl_->worldLocalReflectionPrepared?6:engine::render::worldLocalReflectionViewCount))return engine::render::filterWorldLocalReflection(*this,impl_->worldLocalReflection,impl_->worldLocalReflectionPrepared,[&](int extent,int face,int level,int base,bool clearScene,bool batchFaces){
        std::array<float,4> sampling{};
        return impl_->captureWorldRadianceSnapshot("local-reflection",extent,extent,impl_->worldLocalReflectionDescriptor,sampling,face,level,base,0,0,0,clearScene,batchFaces);
    },impl_->activeEditorSurface?impl_->editorSurfaces.at(impl_->activeEditorSurface).width:int(impl_->swapchainExtent.width),impl_->activeEditorSurface?impl_->editorSurfaces.at(impl_->activeEditorSurface).height:int(impl_->swapchainExtent.height));
    return true;
}
void VulkanRenderBackend::clearWorldLocalReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(impl_){impl_->worldLocalReflection={};impl_->worldLocalReflectionFace=0;impl_->worldLocalReflectionDescriptor=VK_NULL_HANDLE;}
}

bool VulkanRenderBackend::beginWorldScreenReflection(const engine::render::WorldScreenReflectionUniforms& value) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || !impl_->worldSceneColorPassActive || impl_->worldShadowActive || impl_->worldSceneRadianceExposure<=0 ||
        impl_->worldPlanarReflection[4][3]<0 || impl_->worldScreenReflection[8][3]!=0 || value[8][3]!=-1)return false;
    const auto extent=impl_->activeEditorSurface
        ? VkExtent2D{std::uint32_t(impl_->editorSurfaces.at(impl_->activeEditorSurface).width),
                     std::uint32_t(impl_->editorSurfaces.at(impl_->activeEditorSurface).height)}
        : impl_->swapchainExtent;
    if(value[9][3]>.5f && (value[8][0]!=extent.width || value[8][1]!=extent.height))return false;
    impl_->worldScreenReflection=value;
    if(engine::render::worldScreenReflectionCanReuseContactDepth(value,impl_->worldContact) &&
       impl_->worldContactHistory.frame==impl_->frameCounter && impl_->worldDepthSnapshot &&
       impl_->worldDepthSnapshot->descriptorSet==impl_->worldContactPreviousDepth &&
       !impl_->restoreWorldDepthSnapshot())return false;
    return true;
}
bool VulkanRenderBackend::endWorldScreenReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || impl_->worldScreenReflection[8][3]>=0 || !impl_->captureWorldDepthSnapshot())return false;
    auto& texture=*impl_->worldDepthSnapshot;
    if(!texture.descriptorSet)texture.descriptorSet=impl_->createPlanarDescriptor(texture);
    impl_->worldContactDescriptor=texture.descriptorSet;
    auto& screen=impl_->worldScreenReflection;
    const auto layout=engine::render::worldScreenReflectionTileLayout(int(screen[8][0]),int(screen[8][1]));
    const auto quality=screen[10];
    const bool tiles=(quality[2]<=0 || quality[2]>=4096) && layout[2]<=8192 &&
        !engine::env::flagEnabled("PHLOSION_DISABLE_SCREEN_DEPTH_TILES");
    if(!impl_->captureWorldRadianceSnapshot("screen-reflection",int(screen[8][0]),int(screen[8][1]),
        impl_->worldScreenReflectionDescriptor,screen[10],-1,0,tiles?layout[2]:0))return false;
    screen[10][2]=quality[2];screen[10][3]=quality[3];
    if(tiles && !engine::render::buildWorldScreenReflectionTiles(*this,screen,[&](int width,int height,int reserve,int offset){
        std::array<float,4> sampling{};
        return impl_->captureWorldRadianceSnapshot("screen-reflection",width,height,impl_->worldScreenReflectionDescriptor,sampling,-1,0,reserve,offset);
    }))return false;
    screen[8][3]=engine::env::flagEnabled("PHLOSION_DIAG_SKIP_SCREEN_TRACE")?.25f:1.f;
    if(screen[9][3]>.5f && !impl_->restoreWorldDepthSnapshot())return false;
    return true;
}
void VulkanRenderBackend::clearWorldScreenReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(impl_) {impl_->worldScreenReflection={};impl_->worldScreenReflectionDescriptor=VK_NULL_HANDLE;}
}

bool VulkanRenderBackend::clearWorldDepth() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    return impl_ && impl_->clearWorldDepth();
}
bool VulkanRenderBackend::beginWorldLocalHaze(int width, int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->beginWorldLocalHaze(width,height);
}
bool VulkanRenderBackend::endWorldLocalHaze() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
 return impl_ && impl_->endWorldLocalHaze(); }
bool VulkanRenderBackend::beginWorldLocalHazeComposition(int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->beginWorldLocalHazeComposition(width,height);
}
void VulkanRenderBackend::endWorldLocalHazeComposition() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
if(impl_)impl_->endWorldLocalHazeComposition();}
bool VulkanRenderBackend::beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->beginWorldPlanarReflection(view);
}
bool VulkanRenderBackend::endWorldPlanarReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->endWorldPlanarReflection();
}
bool VulkanRenderBackend::captureWorldDepthSnapshot() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->captureWorldDepthSnapshot();
}

void VulkanRenderBackend::onResize(int width, int height) {
    if (impl_) impl_->requestResize(width, height);
}

bool VulkanRenderBackend::getLastFrameTimings(BackendFrameTimings& outTimings) const {
    if (!impl_) return false;
    outTimings = impl_->lastTimings;
    return true;
}

bool VulkanRenderBackend::getLastFrameStats(BackendFrameStats& outStats) const {
    if (!impl_) return false;
    outStats = impl_->lastStats;
    return true;
}

std::string VulkanRenderBackend::activeGpuName() const {
    return impl_ ? impl_->gpuName : std::string{};
}

bool VulkanRenderBackend::activeGpuIsDiscrete() const {
    return impl_ && impl_->gpuDiscrete;
}

void VulkanRenderBackend::setVSyncEnabled(bool enabled) {
    if (impl_) impl_->requestVSync(enabled);
}

void VulkanRenderBackend::setWorldMaterialDebugView(int view) noexcept {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    IRenderBackend::setWorldMaterialDebugView(view);
    if (impl_) {
        impl_->worldMaterialDebugView = worldMaterialDebugView();
    }
}

void VulkanRenderBackend::shutdown() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if (impl_) {
        impl_->shutdown();
        impl_.reset();
    }
}

void VulkanRenderBackend::recordWorldIndexedSubmissionStats(
    const WorldIndexedSubmissionStats& stats) {
    if (impl_) impl_->recordSubmissionStats(stats);
}

bool VulkanRenderBackend::getEditorUiContext(
    EditorUiContext& outContext) const {
    outContext = {};
    if (!impl_ || !impl_->initialized) {
        return false;
    }
    outContext.instance = impl_->instance;
    outContext.physicalDevice = impl_->physicalDevice;
    outContext.device = impl_->device;
    outContext.graphicsQueueFamily =
        impl_->graphicsQueueFamily;
    outContext.graphicsQueue = impl_->graphicsQueue;
    outContext.renderPass = impl_->renderPass;
    outContext.colorFormat = impl_->swapchainFormat;
    outContext.minimumImageCount = 2u;
    outContext.imageCount = std::max<std::uint32_t>(
        2u,
        static_cast<std::uint32_t>(
            impl_->swapchainImages.size()));
    return outContext.instance != VK_NULL_HANDLE &&
        outContext.physicalDevice != VK_NULL_HANDLE &&
        outContext.device != VK_NULL_HANDLE &&
        outContext.graphicsQueue != VK_NULL_HANDLE &&
        outContext.renderPass != VK_NULL_HANDLE;
}

VkCommandBuffer
VulkanRenderBackend::currentEditorCommandBuffer() const {
    if (!impl_ || !impl_->frameActive) {
        return VK_NULL_HANDLE;
    }
    return impl_->worldRecordingCommand(impl_->frames[impl_->currentFrame].commandBuffer);
}

std::uint32_t
VulkanRenderBackend::currentEditorFrameIndex() const {
    return impl_ ? impl_->currentFrame : 0u;
}

VulkanRenderBackend::EditorSurfaceHandle
VulkanRenderBackend::createEditorSurface(
    int width,
    int height) {
    return impl_
        ? impl_->createEditorSurface(width, height)
        : kInvalidEditorSurface;
}

bool VulkanRenderBackend::resizeEditorSurface(
    EditorSurfaceHandle surface,
    int width,
    int height) {
    return impl_ &&
        impl_->resizeEditorSurface(surface, width, height);
}

void VulkanRenderBackend::destroyEditorSurface(
    EditorSurfaceHandle surface) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if (impl_) {
        impl_->destroyEditorSurface(surface);
    }
}

bool VulkanRenderBackend::beginEditorSurface(
    EditorSurfaceHandle surface) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->beginEditorSurface(surface);
}

void VulkanRenderBackend::endEditorSurface() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if (impl_) {
        impl_->endEditorSurface();
    }
}

bool VulkanRenderBackend::getEditorSurfaceTexture(
    EditorSurfaceHandle surface,
    std::uint32_t frameIndex,
    VkSampler& outSampler,
    VkImageView& outImageView) const {
    return impl_ &&
        impl_->getEditorSurfaceTexture(
            surface,
            frameIndex,
            outSampler,
            outImageView);
}

void VulkanRenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                             std::size_t triangleCount,
                                             const float* viewProjectionMatrix4x4,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (impl_) {
        impl_->drawWorldTriangles(
            triangles, triangleCount, viewProjectionMatrix4x4, surfaceWidth, surfaceHeight);
    }
}

void VulkanRenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount,
                                               const float* viewProjectionMatrix4x4,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    if (impl_) {
        impl_->drawWorldIndexedMesh(vertices,
                                    vertexCount,
                                    indices,
                                    indexCount,
                                    nullptr,
                                    viewProjectionMatrix4x4,
                                    surfaceWidth,
                                    surfaceHeight);
    }
}

void VulkanRenderBackend::drawWorldIndexedMeshCached(const char* geometryKey,
                                                     const WorldMeshVertex* vertices,
                                                     std::size_t vertexCount,
                                                     const std::uint32_t* indices,
                                                     std::size_t indexCount,
                                                     const float* viewProjectionMatrix4x4,
                                                     int surfaceWidth,
                                                     int surfaceHeight) {
    if (impl_) {
        impl_->drawWorldIndexedMeshCached(geometryKey,
                                          vertices,
                                          vertexCount,
                                          indices,
                                          indexCount,
                                          nullptr,
                                          viewProjectionMatrix4x4,
                                          surfaceWidth,
                                          surfaceHeight);
    }
}

void VulkanRenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                        const WorldMeshVertex* vertices,
                                                        std::size_t vertexCount,
                                                        const std::uint32_t* indices,
                                                        std::size_t indexCount) {
    if (impl_) {
        impl_->prewarmWorldIndexedMesh(
            geometryKey, vertices, vertexCount, indices, indexCount);
    }
}

void VulkanRenderBackend::prewarmWorldIndexedMeshInstances(std::size_t instanceCount) {
    // Per-frame instance data uses the fixed transient storage buffer, so there
    // is no persistent allocation to grow during prewarm.
    (void)instanceCount;
}

void VulkanRenderBackend::prewarmWorldTextureData(const WorldTextureData* texture) {
    if (impl_) impl_->prewarmWorldTexture(texture);
}

void VulkanRenderBackend::drawWorldIndexedMeshTextured(
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (impl_) {
        impl_->drawWorldIndexedMesh(vertices,
                                    vertexCount,
                                    indices,
                                    indexCount,
                                    texture,
                                    viewProjectionMatrix4x4,
                                    surfaceWidth,
                                    surfaceHeight);
    }
}

void VulkanRenderBackend::drawWorldIndexedMeshTexturedCached(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (impl_) {
        impl_->drawWorldIndexedMeshCached(geometryKey,
                                          vertices,
                                          vertexCount,
                                          indices,
                                          indexCount,
                                          texture,
                                          viewProjectionMatrix4x4,
                                          surfaceWidth,
                                          surfaceHeight);
    }
}

void VulkanRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
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
    if (impl_) {
        impl_->drawWorldIndexedMeshCachedInstanced(
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
    }
}

void VulkanRenderBackend::drawDebugQuads(const DebugQuad* quads,
                                         std::size_t quadCount,
                                         int surfaceWidth,
                                         int surfaceHeight) {
    if (impl_) impl_->drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::drawDebugQuadsCached(const char*,
                                               const DebugQuad* quads,
                                               std::size_t quadCount,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::drawDebugLines(const DebugLine* lines,
                                         std::size_t lineCount,
                                         int surfaceWidth,
                                         int surfaceHeight) {
    if (impl_) impl_->drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::drawDebugLinesCached(const char*,
                                               const DebugLine* lines,
                                               std::size_t lineCount,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::drawDebugTriangles(const DebugTriangle* triangles,
                                             std::size_t triangleCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (impl_) impl_->drawDebugTriangles(triangles, triangleCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::drawDebugSprites(const DebugSprite* sprites,
                                           std::size_t spriteCount,
                                           int surfaceWidth,
                                           int surfaceHeight) {
    if (impl_) impl_->drawDebugSprites(sprites, spriteCount, surfaceWidth, surfaceHeight);
}

void VulkanRenderBackend::prewarmDebugSpriteTexture(const char* texturePath) {
    if (impl_) impl_->prewarmSpriteTexture(texturePath);
}

void VulkanRenderBackend::endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame& frame) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    impl_->worldBloomStrength=worldBloomStrength_;
    impl_->endWorldSceneColorPass(frame);
}

int VulkanRenderBackend::beginRenderPhase(const char* name) {
    if(!impl_ || !impl_->frameActive)return -1;
    auto& frame=impl_->frames[impl_->currentFrame];
    if(!frame.timestampIssued)return -1;
    const int token=impl_->phaseProfile.begin(frame.phaseFrame,name);
    if(token>=0)vkCmdWriteTimestamp(impl_->worldRecordingCommand(frame.commandBuffer),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        frame.timestampQueries,frame.phaseFrame.spans[token].query);
    return token;
}
void VulkanRenderBackend::endRenderPhase(int token) {
    if(!impl_ || !impl_->frameActive)return;
    auto& frame=impl_->frames[impl_->currentFrame];
    if(!frame.timestampIssued || !impl_->phaseProfile.end(frame.phaseFrame,token))return;
    vkCmdWriteTimestamp(impl_->worldRecordingCommand(frame.commandBuffer),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);
}

bool VulkanRenderBackend::beginWorldSurfaceBuffer(const float* matrix,int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    return impl_ && impl_->beginWorldSurfaceBuffer(matrix,width,height);
}
bool VulkanRenderBackend::endWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
return impl_ && impl_->endWorldSurfaceBuffer();}
void VulkanRenderBackend::clearWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
if(impl_)impl_->clearWorldSurfaceBuffer();}
bool VulkanRenderBackend::restoreWorldSurfaceDepth(const engine::render::WorldContactUniforms& view) {
    if(impl_ && impl_->worldSurfaceTarget && impl_->worldSurfaceTarget->probeScratch)return false;
    if(!impl_)return false;
    auto& s=*impl_;
    const auto extent=s.activeEditorSurface?VkExtent2D{std::uint32_t(s.editorSurfaces.at(s.activeEditorSurface).width),
        std::uint32_t(s.editorSurfaces.at(s.activeEditorSurface).height)}:s.swapchainExtent;
    const bool planar=s.worldPlanarReflection[4][3]<0;
    const int width=planar?s.worldPlanarReflectionView.width:int(extent.width);
    const int height=planar?s.worldPlanarReflectionView.heightPixels:int(extent.height);
    if(s.worldSurfacePlanar!=planar || view[8][0]!=width || view[8][1]!=height)return false;
    if(!s.frameActive || !s.worldSceneColorPassActive || !s.worldSurfaceReady || s.worldSurfaceActive ||
       s.worldShadowActive || s.worldHazeActive || !s.worldProbeSurfaceKey.empty() ||
       s.worldLocalReflection[0][3]<0 ||
       !engine::render::surfaceContactViewMatches(s.worldSurfaceView,view))return false;
    engine::render::RenderPhaseScope phase(*this,"surface-depth-reuse");
    const auto previous=s.worldDepthSnapshot;s.worldDepthSnapshot=s.worldSurfaceDepth;
    try {
        const bool result=s.restoreWorldDepthSnapshot();
        s.worldDepthSnapshot=previous;return result;
    } catch(...) {s.worldDepthSnapshot=previous;throw;}
}
bool VulkanRenderBackend::beginWorldSurfaceCompositionCapture(const float* matrix,int width,int height,
        const engine::render::WorldClusteredLightingView& lighting) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!beginWorldSurfaceBuffer(matrix,width,height))return false;
    impl_->worldCompositionLighting=lighting;impl_->worldSurfaceCompositionCapture=true;return true;
}
bool VulkanRenderBackend::beginWorldSurfaceComposition(const engine::render::WorldContactUniforms& view,
        const engine::render::WorldClusteredLightingView& lighting) {
    if(impl_ && impl_->worldSurfaceTarget && impl_->worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSurfaceCompositionCapture || s.worldSurfaceCompositionFinished || s.worldSceneRadianceExposure<=0 || s.worldSurfaceComposing || s.worldSurfaceComposed || s.worldClusteredView[4][3]<.5f ||
       (s.worldDiffuseProbes[0][3]>1.5f && !s.worldIndirectReceiver) ||
       !engine::render::worldSurfaceCompositionLightingMatches(s.worldCompositionLighting,lighting) ||
       !restoreWorldSurfaceDepth(view) || !s.ensureWorldSurfaceCompositionPipeline())return false;
    engine::render::RenderPhaseScope phase(*this,"surface-composition");
    const auto contact=s.worldContact;
    std::copy_n(view.begin(),8,s.worldContact.begin());
    s.worldSurfaceComposing=true;
    try {
        engine::render::drawWorldSurfaceReflectionResolve(*this,int(view[8][0]),int(view[8][1]),&lighting,true);
        s.worldSurfaceComposing=false;s.worldSurfaceComposed=s.worldSurfaceCompositionFinished=true;s.worldContact=contact;
        return true;
    } catch(...) {s.worldSurfaceComposing=false;s.worldContact=contact;throw;}
}
void VulkanRenderBackend::endWorldSurfaceComposition() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
if(impl_)impl_->worldSurfaceComposed=false;}
bool VulkanRenderBackend::showWorldSurfaceBuffer(unsigned plane) {
    if(impl_ && impl_->worldSurfaceTarget && impl_->worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(!impl_ || !impl_->frameActive || !impl_->worldSceneColorPassActive || !impl_->worldSurfaceReady ||
       impl_->worldSurfaceActive || plane>=4 || !impl_->ensureWorldSurfaceReflectionPipeline())return false;
    auto& s=*impl_;const auto descriptors=s.worldContactFilterSet;const auto original=s.worldScreenReflection;
    const int width=int(s.worldSurfaceView[8][0]),height=int(s.worldSurfaceView[8][1]);
    s.worldContactFilterSet=s.createContactFilterSet({s.worldSurfaceTarget->planes[plane].descriptorSet,
        s.worldSurfaceDepth->descriptorSet,s.worldSurfaceTarget->planes[2].descriptorSet});
    s.worldScreenReflection[8]={float(width),float(height),0,2};s.worldScreenReflection[10][3]=-float(plane+1);
    try {engine::render::drawWorldSurfaceReflectionResolve(*this,width,height);}
    catch(...) {s.worldContactFilterSet=descriptors;s.worldScreenReflection=original;throw;}
    s.worldContactFilterSet=descriptors;s.worldScreenReflection=original;return true;
}
bool VulkanRenderBackend::resolveWorldSurfaceContact(const engine::render::WorldContactUniforms& view) {
    if(impl_ && impl_->worldSurfaceTarget && impl_->worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || !impl_->worldSceneColorPassActive || !impl_->worldSurfaceReady || impl_->worldSurfaceActive ||
       !engine::render::surfaceContactViewMatches(impl_->worldSurfaceView,view) ||
       engine::env::flagEnabled("PHLOSION_LEGACY_CONTACT_VISIBILITY") || !beginWorldContactVisibility(view))return false;
    impl_->worldContact[13][3]=1;
    impl_->worldDepthSnapshot=impl_->worldSurfaceDepth;impl_->worldSurfaceContact=true;
    try {const bool ok=endWorldContactVisibility();impl_->worldSurfaceContact=false;return ok;}
    catch(...) {impl_->worldSurfaceContact=false;clearWorldContactVisibility();throw;}
}

bool VulkanRenderBackend::resolveWorldSurfaceScreenReflection() {
    if(impl_ && impl_->worldSurfaceTarget && impl_->worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_ || !impl_->frameActive || !impl_->worldSceneColorPassActive || !impl_->worldSurfaceReady || impl_->worldSurfaceActive ||
       !engine::render::surfaceReflectionViewMatches(impl_->worldSurfaceView,impl_->worldScreenReflection) ||
       !impl_->ensureWorldSurfaceReflectionPipeline())return false;
    auto& state=*impl_;
    const auto descriptors=state.worldContactFilterSet;
    const auto original=state.worldScreenReflection;
    const int width=int(original[8][0]),height=int(original[8][1]);
    state.worldContactFilterSet=state.createContactFilterSet({
        state.worldSurfaceTarget->planes[1].descriptorSet,state.worldSurfaceDepth->descriptorSet,state.worldSurfaceTarget->planes[2].descriptorSet});
    state.worldScreenReflection[8][3]=2;
    try {
        engine::render::RenderPhaseScope phase(*this,"surface-reflection-resolve");
        engine::render::drawWorldSurfaceReflectionResolve(*this,width,height);
        VkDescriptorSet resolved=VK_NULL_HANDLE;std::array<float,4> sampling{};
        const auto purpose=std::string("surface-reflection/")+(state.worldSurfacePlanar?"planar/":"primary/")+
            std::to_string(state.worldSurfaceAllocationSlots[state.worldSurfacePlanar?1:0]);
        if(!state.captureWorldRadianceSnapshot(purpose.c_str(),width,height,resolved,sampling))
            throw std::runtime_error("Surface reflection capture failed");
        state.worldContactFilterSet=state.createContactFilterSet({
            state.worldContactPreviousVisibility?state.worldContactPreviousVisibility:state.worldPlanarFallback,
            resolved,state.worldContactPreviousDepth?state.worldContactPreviousDepth:state.worldPlanarFallback});
        state.worldScreenReflection=original;state.worldScreenReflection[8][3]=3;
        if(original[9][3]>.5f && !state.restoreWorldDepthSnapshot())
            throw std::runtime_error("Surface reflection depth restore failed");
        return true;
    } catch(...) {
        state.worldContactFilterSet=descriptors;state.worldScreenReflection=original;throw;
    }
}

bool VulkanRenderBackend::captureWorldClusteredLighting(){
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
return impl_->captureWorldClusteredLighting();}
engine::render::WorldClusteredLightingDiagnostic VulkanRenderBackend::readWorldClusteredLighting(){return impl_->readWorldClusteredLighting();}
bool VulkanRenderBackend::resolveWorldClusteredLighting(const engine::render::WorldClusteredLightingView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
return impl_->resolveWorldClusteredLighting(view);}
bool VulkanRenderBackend::beginWorldTemporalScene(const engine::render::WorldTemporalView& v){
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
return impl_->beginWorldTemporalScene(v);}
void VulkanRenderBackend::releaseWorldTemporalView(std::uint64_t id){impl_->releaseWorldTemporalView(id);}
bool VulkanRenderBackend::resolveWorldHeatHaze(){
    engine::render::WorldPreparedViewBoundary boundary([&]{if(impl_){impl_->flushWorldDrawPackets();impl_->preparedWorldViews.invalidate();}});
    return impl_&&impl_->resolveWorldHeatHaze();
}
bool VulkanRenderBackend::resolveWorldTemporalScene(){
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
engine::render::RenderPhaseScope phase(*this,"temporal-scene");
impl_->worldBloomStrength=worldBloomStrength_;
return impl_->resolveWorldTemporalScene();}
bool VulkanRenderBackend::resolveWorldTemporalReflection(const engine::render::WorldTemporalReflectionView& v){
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
engine::render::RenderPhaseScope phase(*this,"temporal-reflection");return impl_->resolveWorldTemporalReflection(v);}
engine::render::WorldTemporalFeedback VulkanRenderBackend::worldTemporalFeedback() const{return impl_->worldTemporalFeedback();}
bool VulkanRenderBackend::captureWorldTemporalScene(){
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
return impl_->captureWorldTemporalScene();}
engine::render::WorldTemporalImage VulkanRenderBackend::readWorldTemporalScene(){return impl_->readWorldTemporalScene();}
bool VulkanRenderBackend::setWorldSurfaceMotion(const engine::render::WorldMotionMatrix& view,std::span<const engine::render::WorldMotionSample> samples) {
    return impl_->setWorldSurfaceMotion(view,samples);
}
bool VulkanRenderBackend::captureWorldSurfaceMotion() {return impl_->captureWorldSurfaceMotion();}
engine::render::WorldMotionImage VulkanRenderBackend::readWorldSurfaceMotion() {return impl_->readWorldSurfaceMotion();}

bool VulkanRenderBackend::beginWorldProbeSurface(std::uint64_t id,std::uint64_t revision,unsigned face,const float* matrix,int resolution) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!(s.frameActive && s.worldSceneColorPassActive) || s.worldSurfaceActive)return false;
    auto key=s.worldProbeSurfaces.begin(id,revision,face,resolution);
    if(key.empty())return false;
    s.worldProbeSurfaceKey=key;
    try {
        if(beginWorldSurfaceBuffer(matrix,resolution,resolution)){
            std::copy_n(s.worldSurfaceView.begin(),8,s.worldScreenReflection.begin());
            s.worldScreenReflection[10][3]=-8;return true;}
    } catch(...) {s.worldProbeSurfaces.cancel();s.worldProbeSurfaceKey.clear();throw;}
    s.worldProbeSurfaces.cancel();s.worldProbeSurfaceKey.clear();return false;
}
bool VulkanRenderBackend::endWorldProbeSurface() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldProbeSurfaces.active())return false;
    if(!endWorldSurfaceBuffer())return false;
    s.worldScreenReflection[10][3]=0;
    s.worldProbeSurfaces.finish();s.worldProbeSurfaceKey.clear();return true;
}
bool VulkanRenderBackend::selectWorldProbeSurface(std::uint64_t id,unsigned face) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(!impl_)return false;
    auto& s=*impl_;
    if(!(s.frameActive && s.worldSceneColorPassActive) || s.worldSurfaceActive || s.worldHazeActive || s.worldShadowActive ||
       s.worldContact[8][3]!=0 || s.worldScreenReflection[8][3]!=0 || s.worldPlanarReflection[4][3]<0 ||
       s.worldLocalReflection[0][3]<0)return false;
    const auto key=s.worldProbeSurfaces.select(id,face);
    if(key.empty())return false;
    const auto found=s.worldSurfaceTargets.find(key);
    if(found==s.worldSurfaceTargets.end())return false;
    if(s.worldSurfaceFrame!=s.frameCounter){s.worldSurfaceFrame=s.frameCounter;s.worldSurfaceSlot=0;s.worldSurfaceAllocationSlots={};}
    if(s.worldSurfaceSlot>=32)throw std::runtime_error("Surface views per frame exceeded");
    ++s.worldSurfaceSlot;
    auto& target=found->second;
    s.worldSurfaceTarget=&target;s.worldSurfaceView=target.view;s.worldSurfaceDepth=&target.depth;
    s.worldSurfaceComposed=s.worldSurfaceCompositionFinished=s.worldSurfaceCompositionCapture=false;
    s.worldSurfaceReady=true;s.worldClusteredView={};
    s.worldClusteredDescriptors={};
    return true;
}
std::uint64_t VulkanRenderBackend::worldProbeSurfaceRevision(std::uint64_t id) const {
    return impl_?impl_->worldProbeSurfaces.revision(id):0;
}
bool VulkanRenderBackend::releaseWorldProbeSurface(std::uint64_t id) {
    if(!impl_)return false;
    auto& s=*impl_;
    if(s.worldSurfaceActive)return false;
    // Release metadata only. GPU allocations remain resident and queue ordered.
    clearWorldSurfaceBuffer();
    return s.worldProbeSurfaces.release(id);
}

bool VulkanRenderBackend::beginWorldPlacedProbeFace(std::uint64_t id,std::uint64_t generation,const engine::render::WorldLocalReflectionProbe& probe,unsigned face,std::span<const std::uint64_t> neighbors) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSceneColorPassActive || s.worldSurfaceActive || !s.placedProbes.begin(id,generation,probe,face))return false;
    s.worldLocalReflectionDescriptor=s.placedProbeDescriptor;
    s.worldLocalReflection=s.placedProbes.select(neighbors,id);
    return true;
}
bool VulkanRenderBackend::selectWorldPlacedProbes(std::span<const std::uint64_t> ids) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });

    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSceneColorPassActive || s.placedProbes.active())return false;
    s.worldLocalReflectionDescriptor=s.placedProbeDescriptor;
    s.worldLocalReflection=s.placedProbes.select(ids);return true;
}
bool VulkanRenderBackend::endWorldPlacedProbeFace() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.placedProbes.active())return false;
    const auto saved=s.worldLocalReflection;
    const auto offset=s.placedProbes.offset();
    const auto restore=[&]{s.worldLocalReflection=saved;};
    const auto copy=[&](int size,int face,int level,int base,bool clearScene=true,bool batchFaces=false){std::array<float,4> sampling{};const bool copied=s.captureWorldRadianceSnapshot("placed-reflection",size,size,s.worldLocalReflectionDescriptor,sampling,face,level,base,offset[1],offset[0],engine::render::WorldPlacedProbeCache::atlasExtent,clearScene,batchFaces);s.placedProbeDescriptor=s.worldLocalReflectionDescriptor;return copied;};
    try {
        if(!copy(128,int(s.placedProbes.face()),0,128))throw std::runtime_error("Placed probe face copy failed");
        if(s.placedProbes.lastFace() && !s.placedProbes.deferredFiltering()) {
            s.worldLocalReflection=s.placedProbes.filterState();
            if(!engine::render::filterWorldLocalReflection(*this,s.worldLocalReflection,true,copy,s.activeEditorSurface?s.editorSurfaces.at(s.activeEditorSurface).width:int(s.swapchainExtent.width),s.activeEditorSurface?s.editorSurfaces.at(s.activeEditorSurface).height:int(s.swapchainExtent.height)))
                throw std::runtime_error("Placed probe convolution failed");
        }
        s.placedProbes.finish(!s.placedProbes.deferredFiltering());restore();return true;
    } catch(...) {s.placedProbes.cancel();restore();throw;}

}

bool VulkanRenderBackend::reserveWorldProbes(std::span<const std::uint64_t> ids,float delta) {
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSceneColorPassActive || s.worldSurfaceActive || s.placedProbes.active())return false;
    if(!std::isfinite(delta)||delta<0)throw std::invalid_argument("Invalid probe clock");
    const auto retained=s.probeResidency.reserve(ids);
    s.worldProbeSurfaces.retain(retained);s.placedProbes.retain(retained);
    s.placedProbes.deferFiltering(true);s.placedProbes.advanceBlend(delta);
    return true;
}
bool VulkanRenderBackend::worldProbePublished(std::uint64_t id) const {
    if(!impl_)return false;
    auto& s=*impl_;
    return s.placedProbes.published(id);
}
bool VulkanRenderBackend::worldProbeBlending(std::uint64_t id) const {
    if(!impl_)return false;
    auto& s=*impl_;
    return s.placedProbes.blending(id);
}
bool VulkanRenderBackend::filterWorldProbe(std::uint64_t id,unsigned jobs) {
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSceneColorPassActive || s.worldSurfaceActive || !jobs || jobs>10)return false;
    const auto saved=s.worldLocalReflection;
    for(unsigned job=0;job<jobs;++job) {
        if(!s.placedProbes.beginFilter(id))return false;
        const auto offset=s.placedProbes.offset();
    const auto copy=[&](int size,int face,int level,int base,bool clearScene=true,bool batchFaces=false){std::array<float,4> sampling{};const bool copied=s.captureWorldRadianceSnapshot("placed-reflection",size,size,s.worldLocalReflectionDescriptor,sampling,face,level,base,offset[1],offset[0],engine::render::WorldPlacedProbeCache::atlasExtent,clearScene,batchFaces);s.placedProbeDescriptor=s.worldLocalReflectionDescriptor;return copied;};
        s.worldLocalReflectionDescriptor=s.placedProbeDescriptor;
        s.worldLocalReflection=s.placedProbes.filterState();
        try {
            if(!engine::render::filterWorldLocalReflectionStep(*this,s.worldLocalReflection,
                s.placedProbes.filterStep(),copy,s.activeEditorSurface?s.editorSurfaces.at(s.activeEditorSurface).width:int(s.swapchainExtent.width),s.activeEditorSurface?s.editorSurfaces.at(s.activeEditorSurface).height:int(s.swapchainExtent.height)))
                throw std::runtime_error("Placed probe filter job failed");
            const bool done=s.placedProbes.finishFilterStep();s.worldLocalReflection=saved;
            if(done)return true;
        } catch(...) {s.placedProbes.cancel();s.worldLocalReflection=saved;throw;}
    }
    return false;
}
bool VulkanRenderBackend::relightWorldProbeSurface(const engine::render::WorldClusteredLightingView& view,const std::array<std::array<float,4>,32>& emission) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { if (impl_) { impl_->flushWorldDrawPackets(); impl_->preparedWorldViews.invalidate(); } });
    if(!impl_)return false;
    auto& s=*impl_;
    if(!s.worldSurfaceReady || s.worldSurfaceActive || !s.worldSceneColorPassActive)return false;
    if(!s.ensureWorldProbeRelightPipeline())return false;
    const auto savedScreen=s.worldScreenReflection;
    const auto savedCluster=s.worldClusteredDescriptors;
    const auto savedContact=s.worldContactFilterSet;
    const auto savedDepth=s.worldDepthSnapshot;
    const auto savedSources=s.worldContactFilterSources;
    const auto restore=[&]{s.worldScreenReflection=savedScreen;s.worldClusteredDescriptors=savedCluster;s.worldContactFilterSet=savedContact;s.worldDepthSnapshot=savedDepth;s.worldContactFilterSources=savedSources;};
    auto& surface=*s.worldSurfaceTarget;
    s.worldClusteredDescriptors={surface.planes[5].descriptorSet,surface.planes[3].descriptorSet,surface.planes[4].descriptorSet};
    s.worldContactFilterSet=s.createContactFilterSet({surface.planes[1].descriptorSet,s.worldSurfaceDepth->descriptorSet,surface.planes[2].descriptorSet});
    s.worldDepthSnapshot=s.worldSurfaceDepth;
    std::copy_n(s.worldSurfaceView.begin(),8,s.worldScreenReflection.begin());
    const int size=int(s.worldSurfaceView[8][0]);
    s.worldScreenReflection[8]={float(size),float(size),0,2};s.worldScreenReflection[10][3]=8;
    std::copy(emission.begin(),emission.end(),s.worldLocalReflection.begin()+20);
    try {engine::render::RenderPhaseScope phase(*this,"probe-relight");engine::render::drawWorldSurfaceReflectionResolve(*this,size,size,&view);}
    catch(...){restore();throw;}
    restore();return true;
}

void VulkanRenderBackend::invalidateWorldPlacedProbes(std::span<const std::uint64_t> ids) {
if(impl_)impl_->placedProbes.invalidate(ids);
}

const engine::render::WorldClusteredLightingView* VulkanRenderBackend::worldSurfaceCompositionLighting() const {
    return impl_ && impl_->worldSurfaceComposed && impl_->worldSurfaceCompositionCapture && impl_->worldMaterialDebugView==0
        ? &impl_->worldCompositionLighting : nullptr;
}

engine::render::WorldPreparedResourceToken VulkanRenderBackend::prepareWorldGeometry(
    const char* key,const WorldMeshVertex* vertices,std::size_t vertexCount,
    const std::uint32_t* indices,std::size_t indexCount) {
    if(!impl_ || !impl_->worldScopes.active() || !impl_->worldScopes.admitted())return {};
    const auto* mesh=impl_->ensureCachedWorldMesh(key,vertices,vertexCount,indices,indexCount);
    if(!mesh)return {};
    const auto token=impl_->preparedWorldGeometry.insert(impl_->worldScopes.active(),mesh);
    if(token)++impl_->preparedWorldResourceStats.geometryPreparations;
    return token;
}
engine::render::WorldPreparedResourceToken VulkanRenderBackend::prepareWorldMaterial(const WorldTextureData& texture) {
    if(!impl_ || !impl_->worldScopes.admitted() ||
       !engine::render::worldMaterialBindingIdentity(&texture,impl_->worldScopes.active()))return {};
    const auto* material=impl_->ensureWorldMaterial(&texture);
    if(!material)return {};
    const auto token=impl_->preparedWorldMaterials.insert(impl_->worldScopes.active(),material->descriptorSet);
    if(token)++impl_->preparedWorldResourceStats.materialPreparations;
    return token;
}
bool VulkanRenderBackend::worldPreparedGeometryValid(engine::render::WorldPreparedResourceToken token) const {
    return impl_ && impl_->worldScopes.admitted() && impl_->preparedWorldGeometry.get(token,impl_->worldScopes.active());
}
bool VulkanRenderBackend::worldPreparedMaterialValid(engine::render::WorldPreparedResourceToken token) const {
    return impl_ && impl_->worldScopes.admitted() && impl_->preparedWorldMaterials.get(token,impl_->worldScopes.active());
}
bool VulkanRenderBackend::drawWorldPreparedIndexed(engine::render::WorldPreparedResourceToken geometry,
    engine::render::WorldPreparedResourceToken material,const WorldTextureData& texture,
    const float* view,int width,int height) {
    if(!impl_)return false;
    const auto* mesh=impl_->preparedWorldGeometry.get(geometry,impl_->worldScopes.active());
    const auto* binding=impl_->preparedWorldMaterials.get(material,impl_->worldScopes.active());
    if(!impl_->worldScopes.admitted() || !mesh || !binding){++impl_->preparedWorldResourceStats.staleDraws;return false;}
    const auto& m=**mesh;
    impl_->drawWorldIndexedMeshBuffers(m.geometryBuffer,0u,m.vertexCount,m.geometryBuffer,0u,m.indexCount,
        m.firstIndex,m.baseVertex,*binding,&texture,nullptr,0u,view,width,height);
    ++impl_->preparedWorldResourceStats.draws;
    return true;
}
engine::render::WorldPreparedResourceStats VulkanRenderBackend::worldPreparedResourceStats() const {
    return impl_?impl_->preparedWorldResourceStats:engine::render::WorldPreparedResourceStats{};
}

int VulkanRenderBackend::beginRenderCpuPhase(const char* name) {
    if(!impl_ || !impl_->frameActive)return -1;
    return impl_->phaseProfile.begin(impl_->frames[impl_->currentFrame].phaseFrame,name,false);
}
void VulkanRenderBackend::endRenderCpuPhase(int token) {
    if(impl_ && impl_->frameActive)impl_->phaseProfile.end(impl_->frames[impl_->currentFrame].phaseFrame,token);
}

bool VulkanRenderBackend::captureWorldContactVisibility(){
    if(!impl_)return false;impl_->flushWorldDrawPackets();return impl_->captureWorldContactVisibility();
}
engine::render::WorldContactImage VulkanRenderBackend::readWorldContactVisibility(){
    return impl_?impl_->readWorldContactVisibility():engine::render::WorldContactImage{};
}

bool VulkanRenderBackend::captureWorldProbeRadiance(std::span<const std::uint64_t> ids) {
    engine::render::WorldPreparedViewBoundary boundary([&]{if(impl_){impl_->flushWorldDrawPackets();impl_->preparedWorldViews.invalidate();}});
    return impl_ && impl_->captureWorldProbeRadiance(ids);
}
engine::render::WorldProbeRadianceImage VulkanRenderBackend::readWorldProbeRadiance() {
    return impl_?impl_->readWorldProbeRadiance():engine::render::WorldProbeRadianceImage{};
}

bool VulkanRenderBackend::updateWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate& update) {
    return impl_&&impl_->updateWorldIndirectLighting(update);
}
void VulkanRenderBackend::clearWorldIndirectLighting(){if(impl_)impl_->clearWorldIndirectLighting();}
bool VulkanRenderBackend::captureWorldIndirectLighting(){return impl_&&impl_->captureWorldIndirectLighting();}
engine::render::WorldIndirectLightingDiagnostic VulkanRenderBackend::readWorldIndirectLighting(){
    return impl_?impl_->readWorldIndirectLighting():engine::render::WorldIndirectLightingDiagnostic{};
}

bool VulkanRenderBackend::prepareWorldIndirectLighting(std::span<const engine::render::WorldIndirectLightingUpdate> updates,std::uint64_t consumer) {
    return impl_&&impl_->prepareWorldIndirectLighting(updates,consumer);
}
bool VulkanRenderBackend::bindWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate& update) {
    return impl_&&impl_->bindWorldIndirectLighting(update);
}

void VulkanRenderBackend::prewarmWorldRainMap(engine::render::WorldResourceScopeId owner,unsigned res){impl_->prewarmWorldRainMap(owner,res);}
bool VulkanRenderBackend::beginWorldRainMap(engine::render::WorldResourceScopeId owner,const engine::render::WorldDirectionalShadowView& view,bool clear){return impl_->beginWorldRainMap(owner,view,clear);}
bool VulkanRenderBackend::endWorldRainMap(){return impl_->endWorldRainMap();}
bool VulkanRenderBackend::publishWorldRainMap(engine::render::WorldResourceScopeId owner,unsigned cascade){return impl_->publishWorldRainMap(owner,cascade);}
std::string VulkanRenderBackend::worldRainMapTexture(engine::render::WorldResourceScopeId owner) const{return impl_->worldRainMapTexture(owner);}

engine::render::WorldPrecipitationLightingResult VulkanRenderBackend::updateWorldPrecipitationLighting(const engine::render::WorldPrecipitationLightingView& v){return impl_->updateWorldPrecipitationLighting(v);}
bool VulkanRenderBackend::captureWorldPrecipitationLighting(){return impl_->captureWorldPrecipitationLighting();}
engine::render::WorldPrecipitationLightingDiagnostic VulkanRenderBackend::readWorldPrecipitationLighting(){return impl_->readWorldPrecipitationLighting();}

bool VulkanRenderBackend::captureWorldRainMap(engine::render::WorldResourceScopeId s){return impl_->captureWorldRainMap(s);}
engine::render::WorldRainMapDiagnostic VulkanRenderBackend::readWorldRainMap(){return impl_->readWorldRainMap();}

void VulkanRenderBackend::bindWorldFog(const engine::render::WorldPrecipitationLightingView* v,const engine::render::WorldPrecipitationLightingResult* a,const engine::render::WorldPrecipitationLightingResult* b){impl_->bindWorldFog(v,a,b);}

void VulkanRenderBackend::bindWorldLayeredMaterials(std::uint64_t owner,std::shared_ptr<const engine::render::WorldLayerMaterialSet> set){impl_->bindWorldLayeredMaterials(owner,std::move(set));}
void VulkanRenderBackend::bindWorldWetSurfaces(const engine::render::WorldWetSurfaceView* v){impl_->bindWorldWetSurfaces(v);}

engine::render::WorldParticleLightingResult VulkanRenderBackend::updateWorldParticleLighting(const engine::render::WorldParticleLightingView& v){return impl_->updateWorldParticleLighting(v);}
bool VulkanRenderBackend::captureWorldParticleLighting(){return impl_->captureWorldParticleLighting();}
engine::render::WorldParticleLightingDiagnostic VulkanRenderBackend::readWorldParticleLighting(){return impl_->readWorldParticleLighting();}

void VulkanRenderBackend::prepareWorldClouds(const engine::render::WorldCloudView& u){impl_->prepareWorldClouds(u);}
void VulkanRenderBackend::resolveWorldClouds(const engine::render::WorldCloudView& u){impl_->resolveWorldClouds(u);}
void VulkanRenderBackend::clearWorldClouds(){impl_->clearWorldClouds();}
bool VulkanRenderBackend::captureWorldClouds(){return impl_->captureWorldClouds();}
engine::render::WorldCloudDiagnostic VulkanRenderBackend::readWorldClouds(){return impl_->readWorldClouds();}

void VulkanRenderBackend::prepareWorldWindField(const engine::render::WorldWindFieldInput& u){impl_->prepareWorldWindField(u);}
void VulkanRenderBackend::clearWorldWindField(){impl_->clearWorldWindField();}
bool VulkanRenderBackend::captureWorldWindField(){return impl_->captureWorldWindField();}
engine::render::WorldWindFieldDiagnostic VulkanRenderBackend::readWorldWindField(){return impl_->readWorldWindField();}
