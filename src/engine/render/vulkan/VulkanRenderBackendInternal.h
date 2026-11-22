#include "engine/render/IWorldClouds.h"
#include "engine/render/IWorldWindField.h"
#include "engine/render/IWorldLayeredMaterials.h"
#include "engine/render/IWorldWetSurfaces.h"
#include "engine/render/IWorldRainMap.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include "engine/render/IWorldParticleLighting.h"
#include "engine/render/WorldShadowSchedule.h"
#include "engine/render/WorldIndirectLighting.h"
#include "engine/render/IWorldDeformationPreparation.h"
#include "engine/render/WorldEnvironmentPacketCache.h"
#include "engine/render/WorldMaterialBindingCache.h"
#include "engine/render/WorldPlacedProbeCache.h"
#include "engine/render/WorldProbeResidency.h"
#pragma once
#include "engine/render/WorldDisplay.h"
#include "engine/render/WorldPreparedResources.h"
#include "engine/render/WorldPreparedView.h"
#include "engine/render/WorldDeformationStorage.h"
#include "engine/render/WorldMaterialFrameCache.h"
#include "engine/render/WorldRecordingWorkers.h"
#include "engine/render/WorldProbeSurfaceCache.h"
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/WorldTemporalReflection.h"
#include "engine/render/WorldMotionGpu.h"
#include "engine/render/IWorldLocalShadowCache.h"

#include <vulkan/vulkan.h>
#include "engine/render/vulkan/VulkanGraphicsPipelineLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "engine/render/RenderPhaseProfile.h"
#include "engine/render/WorldResourceScopeState.h"
#include "engine/render/WorldContactFilter.h"
#include "engine/render/vulkan/VulkanWorldInstanceState.h"
#include "engine/render/vulkan/VulkanWorldIndirectState.h"
#include "engine/render/vulkan/VulkanWorldMaterialLayout.h"
#include "engine/render/vulkan/VulkanWorldPipelinePolicy.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldDrawPacket.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"
#include "engine/render/vulkan/VulkanWorldTransformState.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"

struct SDL_Window;

struct VulkanRenderBackendImpl {
    static constexpr std::array<VkFormat,6> kWorldSurfaceFormats{
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32_UINT};
    engine::render::WorldResourceScopeState worldScopes;
    bool collectWorldResourceScopes();
    engine::render::WorldMaterialProfile worldMaterialProfile;
    static constexpr std::uint32_t kFramesInFlight = 2u;
    static constexpr VkDeviceSize kTransientBytesPerFrame = 128ull * 1024ull * 1024ull;

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0u;
        VkDeviceSize allocationBytes = 0u;
        VkDeviceSize offset = 0u;
        bool coherent = false;
    };

    struct WorldRecordingLane {
        VkCommandPool pool=VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> lists;
        std::size_t used=0;
    };
    std::array<std::array<WorldRecordingLane,3>,kFramesInFlight> worldRecordingFrames;
    std::unique_ptr<engine::render::WorldRecordingWorkers> worldRecordingWorkers;
    std::vector<engine::render::vulkan_backend::WorldIndexedDrawPacket> worldDrawPackets;
    engine::render::WorldRecordingStats worldRecordingStats;
    VkCommandBuffer worldRecordingInline=VK_NULL_HANDLE;
    VkCommandBuffer worldRecordingCommand(VkCommandBuffer command, bool flush = true);
    VkCommandBuffer allocateWorldRecordingCommand(unsigned lane);
    void finishWorldRecordingInline();
    VkRenderPassBeginInfo worldRecordingPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    void submitWorldDrawPacket(const engine::render::vulkan_backend::WorldIndexedDrawPacket& packet);
    void flushWorldDrawPackets();
    void resetWorldRecordingFrame();
    void destroyWorldRecordingFrames();
    void beginWorldRecordingRenderPass(VkCommandBuffer,const VkRenderPassBeginInfo*,VkSubpassContents);
    void endWorldRecordingRenderPass(VkCommandBuffer);

    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkQueryPool timestampQueries = VK_NULL_HANDLE;
        VkDescriptorSet worldStateDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet shadowWorldStateDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet indexedWorldMaterialDescriptorSet = VK_NULL_HANDLE;
        std::uint32_t indexedWorldMaterialCount = 0u;
        bool indexedWorldMaterialSetBound = false;
        Buffer transient;
        std::uint64_t deformationSerial=0;
        bool timestampIssued = false;
        engine::render::RenderPhaseProfile::Frame phaseFrame;
    };

    struct Texture {
        engine::render::WorldResourceScopeId scope = 0;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkDeviceSize allocationBytes = 0u;
        VkDeviceSize payloadBytes = 0u;
        VkFormat format = VK_FORMAT_UNDEFINED;
        int width = 0;
        int height = 0;
    };

    struct EditorSurfaceFrame {
        VkImage linearColor = VK_NULL_HANDLE;
        VkDeviceMemory linearColorMemory = VK_NULL_HANDLE;
        VkImageView linearColorView = VK_NULL_HANDLE;
        VkImage displayColor = VK_NULL_HANDLE;
        VkDeviceMemory displayColorMemory = VK_NULL_HANDLE;
        VkImageView displayColorView = VK_NULL_HANDLE;
        VkImage depth = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFramebuffer linearFramebuffer = VK_NULL_HANDLE;
        VkFramebuffer displayFramebuffer = VK_NULL_HANDLE;
        VkDescriptorSet linearDescriptorSet = VK_NULL_HANDLE;
    };

    struct EditorSurface {
        std::uint64_t id = 0u;
        int width = 0;
        int height = 0;
        std::array<
            EditorSurfaceFrame,
            kFramesInFlight> frames{};
    };

    struct WorldMaterial {
        engine::render::WorldResourceScopeId scope = 0;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::array<
            Texture*,
            engine::render::vulkan_backend::kWorldMaterialTextureCount> textures{};
        std::uint32_t indexedTableSlot = UINT32_MAX;
    };

    struct WorldGeometryPage {
        Buffer buffer;
        VkDeviceSize usedBytes = 0u;
    };

    struct CachedWorldMesh {
        engine::render::WorldResourceScopeId scope = 0;
        Buffer ownedGeometry;
        VkBuffer geometryBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexByteOffset = 0u;
        VkDeviceSize indexByteOffset = 0u;
        std::uint32_t firstIndex = 0u;
        std::int32_t baseVertex = 0;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
    };

    struct CachedWorldViewState {
        engine::render::vulkan_backend::WorldViewState value{};
        VkDeviceSize offset = 0u;
    };

    struct CachedWorldSpecializedMaterialState {
        engine::render::vulkan_backend::WorldSpecializedMaterialState value{};
        VkDeviceSize offset = 0u;
    };

    struct DebugVertex {
        float x = 0.0f;
        float y = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugPushConstants {
        float surfaceWidth = 1.0f;
        float surfaceHeight = 1.0f;
        float unused0 = 0.0f;
        float unused1 = 0.0f;
    };

    using WorldPushConstants = engine::render::vulkan_backend::WorldPushConstants;

    SDL_Window* window = nullptr;
    bool initialized = false;
    bool frameActive = false;
    bool swapchainDirty = false;
    bool vsyncEnabled = true;
    bool transientOverflowLogged = false;
    bool swapchainTransferSourceSupported = false;
    bool screenshotCaptureDeferred = false;
    bool screenshotCaptureConfigured = false;
    bool screenshotCaptured = false;
    bool screenshotCopyPending = false;
    int requestedWidth = 1;
    int requestedHeight = 1;
    std::uint32_t currentFrame = 0u;
    std::uint32_t acquiredImage = 0u;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    bool canDeferPresent = false;
    VkSemaphore pendingPresentSemaphore = VK_NULL_HANDLE;
    std::uint32_t pendingPresentImage = 0u;
    std::uint64_t frameCounter = 0u;
    std::uint64_t screenshotFrameTarget = 0u;
    std::string screenshotPath;
    Buffer screenshotReadback;
    std::uint32_t screenshotWidth = 0u;
    std::uint32_t screenshotHeight = 0u;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    engine::render::VulkanGraphicsPipelineLibrary pipelineLibraries;
    std::uint32_t graphicsQueueFamily = UINT32_MAX;
    std::uint32_t presentQueueFamily = UINT32_MAX;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkFence> imagesInFlight;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass loadRenderPass = VK_NULL_HANDLE;
    VkRenderPass editorDisplayRenderPass = VK_NULL_HANDLE;
    VkRenderPass worldSceneColorRenderPass = VK_NULL_HANDLE;
    VkRenderPass worldSceneColorLoadRenderPass = VK_NULL_HANDLE;
    VkRenderPass worldShadowRenderPass = VK_NULL_HANDLE;
    VkPipeline worldShadowPipeline = VK_NULL_HANDLE;
    VkPipeline worldContactFilterPipeline = VK_NULL_HANDLE;
    VkImage worldSunFallbackImage=VK_NULL_HANDLE;
    VkDeviceMemory worldSunFallbackMemory=VK_NULL_HANDLE;
    VkImageView worldSunFallbackView=VK_NULL_HANDLE;
    void ensureWorldSunFallback();
    VkSampler worldSunSampler=VK_NULL_HANDLE;
    VkImage worldSunImage=VK_NULL_HANDLE;
    VkDeviceMemory worldSunMemory=VK_NULL_HANDLE;
    VkImageView worldSunView=VK_NULL_HANDLE;
    VkFramebuffer worldSunFramebuffer=VK_NULL_HANDLE;
    engine::render::WorldSunShadowPlan worldSunPlan{};
    engine::render::WorldDirectionalShadowView worldSunSavedView{};
    unsigned worldSunQuality=~0u;
    bool worldSunSavedReady=false;
    unsigned worldSunPending=0;
    void prewarmWorldSunShadows(unsigned quality);
    bool setWorldSunShadowPlan(const engine::render::WorldSunShadowPlan&);
    void clearWorldSunShadows();
    void destroyWorldSunShadows();
    std::array<std::uint32_t,2> worldShadowOffset() const;
    std::array<VkImage, kFramesInFlight> worldShadowImages{};
    std::array<VkDeviceMemory, kFramesInFlight> worldShadowMemories{};
    std::array<VkImageView, kFramesInFlight> worldShadowViews{};
    std::array<VkFramebuffer, kFramesInFlight> worldShadowFramebuffers{};
    VkSampler worldShadowSampler = VK_NULL_HANDLE;
    struct ShadowMomentTarget {
        VkImage image=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        VkImageView view=VK_NULL_HANDLE;
        VkFramebuffer framebuffer=VK_NULL_HANDLE;
        VkDescriptorSet input=VK_NULL_HANDLE;
    };
    std::array<std::array<ShadowMomentTarget,3>,kFramesInFlight> worldMomentTargets{};
    std::array<engine::render::WorldShadowCache,kFramesInFlight> worldShadowCaches{};
    std::array<engine::render::WorldShadowUpdateCache,kFramesInFlight> worldShadowUpdates{};
    VkRenderPass worldMomentRenderPass=VK_NULL_HANDLE;
    VkDescriptorSetLayout worldMomentSetLayout=VK_NULL_HANDLE;
    VkDescriptorPool worldMomentPool=VK_NULL_HANDLE;
    VkPipelineLayout worldMomentLayout=VK_NULL_HANDLE;
    VkPipeline worldMomentPipeline=VK_NULL_HANDLE;
    void prewarmWorldShadowMoments();
    void createWorldShadowMomentPipeline();
    void filterWorldShadowMoments();
    void destroyWorldShadowMoments();
    engine::render::WorldLocalShadowAtlas worldLocalShadows_;
    engine::render::WorldDirectionalShadowView worldShadowView{};
    std::uint32_t worldShadowResolution = 0, shadowSavedWidth = 1, shadowSavedHeight = 1;
    bool worldShadowActive = false, worldShadowReady = false;
    void createWorldShadowRenderPass();
    void updateWorldShadowDescriptors();
    void prewarmWorldLocalShadows();
    void clearWorldLocalShadows();
    bool beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view);
    bool endWorldLocalShadow();
    engine::render::WorldShadowCacheResult beginWorldStaticShadow(std::uint32_t index,
        const engine::render::WorldDirectionalShadowView& view, engine::render::WorldShadowCacheKey key);
    bool endWorldStaticShadow();
    void prewarmWorldDirectionalShadow(std::uint32_t resolution);
    bool beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view);
    bool endWorldDirectionalShadow();
    void clearWorldDirectionalShadow();
    void destroyWorldDirectionalShadow();

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthMemories;
    std::vector<VkImageView> depthViews;
    std::array<VkImage, kFramesInFlight> worldSceneColorImages{};
    std::array<VkDeviceMemory, kFramesInFlight> worldSceneColorMemories{};
    std::array<VkImageView, kFramesInFlight> worldSceneColorViews{};
    std::vector<VkFramebuffer> worldSceneColorFramebuffers;

    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout indexedWorldMaterialSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout worldStateSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout windDrawSetLayout=VK_NULL_HANDLE;
    VkDescriptorSet windFallbackSet=VK_NULL_HANDLE;
    VkDescriptorSetLayout worldSceneColorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkSampler editorSurfaceSampler = VK_NULL_HANDLE;
    VkPipelineLayout debugPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout texturedPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout indirectWorldPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout worldSceneColorPipelineLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> worldSceneColorDescriptorSets{};
    VkSampler worldSceneColorSampler = VK_NULL_HANDLE;
    VkPipeline debugPipeline = VK_NULL_HANDLE;
    VkPipeline spritePipeline = VK_NULL_HANDLE;
    VkPipeline sceneDebugPipeline = VK_NULL_HANDLE;
    VkPipeline sceneSpritePipeline = VK_NULL_HANDLE;
    VkPipeline worldSceneColorPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout feedbackSetLayout=VK_NULL_HANDLE;
    VkDescriptorPool feedbackPool=VK_NULL_HANDLE;
    VkPipelineLayout feedbackLayout=VK_NULL_HANDLE;
    VkPipeline feedbackPipeline=VK_NULL_HANDLE,feedbackDisplayPipeline=VK_NULL_HANDLE;
    VkRenderPass feedbackRenderPass=VK_NULL_HANDLE;
    std::array<Texture,2> feedbackImages{};
    std::array<VkFramebuffer,2> feedbackFramebuffers{};
    static constexpr unsigned kMaxScreenFeedbackViews=8;
    std::array<std::array<std::array<VkDescriptorSet,2>,kMaxScreenFeedbackViews>,kFramesInFlight> feedbackSets{};
    unsigned feedbackViewCount=0;
    std::uint64_t feedbackSequence=0;
    int feedbackWidth=0,feedbackHeight=0,feedbackHistory=0,feedbackDisplayInput=0;
    engine::render::ScreenFeedbackUpdate feedbackLast{};
    void createScreenFeedbackPipeline();
    void ensureScreenFeedbackResources(int width,int height);
    void destroyScreenFeedback();
    std::array<
        VkPipeline,
        engine::render::vulkan_backend::kWorldPipelineCount> worldPipelines{};
    decltype(worldPipelines) sceneWorldPipelines{};
    VkPipeline worldVolumePipeline = VK_NULL_HANDLE;
    VkPipeline sceneWorldVolumePipeline = VK_NULL_HANDLE;
    std::unordered_map<std::uint32_t,VkPipeline> worldMaterialPipelines;
    struct TransparentPipelines {
        std::array<VkPipeline, engine::render::vulkan_backend::kWorldPipelineCount> display{}, scene{};
    };
    std::vector<TransparentPipelines> transparentPipelines;
    VkPipeline worldClusteredCompositionPipeline=VK_NULL_HANDLE;
    VkPipeline worldClusteredIndirectCompositionPipeline=VK_NULL_HANDLE;
    std::array<
        VkPipeline,
        engine::render::vulkan_backend::kWorldPipelineCount> indirectWorldPipelines{};
    decltype(indirectWorldPipelines) sceneIndirectWorldPipelines{};

    std::array<FrameResources, kFramesInFlight> frames{};
    std::unordered_map<std::string, Texture> worldTextures;
    std::unordered_map<std::string, WorldMaterial> worldMaterials;
    std::vector<WorldMaterial*> indexedWorldMaterials;
    engine::render::WorldPreparedResources<const CachedWorldMesh*> preparedWorldGeometry;
    engine::render::WorldPreparedResources<VkDescriptorSet> preparedWorldMaterials;
    engine::render::WorldPreparedResourceStats preparedWorldResourceStats;
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes;
    std::vector<WorldGeometryPage> worldGeometryPages;
    engine::render::WorldDeformationStorage frameSkinPalettes;
    Buffer deformationBuffer;
    std::uint64_t deformationSerial=0,deformationCompleted=0;
    std::vector<CachedWorldViewState> frameWorldViewStates;
    engine::render::WorldMaterialFrameCache frameMaterialConstants;
    std::unordered_map<std::string, Texture> spriteTextures;
    Texture fallbackWorldTexture;
    Texture fallbackWorldNormalTexture;
    Texture fallbackWorldLinearTexture;
    Texture fallbackWorldEmissiveTexture;
    Texture neutralPmremTexture;
    Texture rectLightLutTexture;
    Texture fallbackSpriteTexture;
    struct PreparedWorldView { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceSize offset = 0; };
    engine::render::WorldPreparedViews<PreparedWorldView> preparedWorldViews;
    engine::render::backend::WorldPreparedViewToken prepareWorldViewData(const IRenderBackend::WorldTextureData*);
    engine::render::WorldEnvironmentPacketCache worldEnvironmentPackets;
    engine::render::WorldMaterialBindingCache<WorldMaterial*> retainedWorldMaterialBindings;
    WorldMaterial fallbackWorldMaterial;
    std::vector<VkDescriptorSet> worldSceneMaterialDescriptorSets;
    std::vector<WorldMaterial*> worldSceneMaterials;
    std::uint32_t worldSceneMaterialCacheGeneration = 0u;
    int worldMaterialDebugView = 0;

    engine::render::vulkan_backend::WorldCommandState worldCommandState;
    engine::render::vulkan_backend::WorldCommandCounters worldCommandCounters;
    bool mainRenderPassActive = false;
    bool worldSceneColorPassActive = false;
    float worldBloomStrength = 0;
    float worldSceneRadianceExposure = 0;
    engine::render::WorldDisplaySettings worldDisplaySettings{};
    engine::render::WorldHeatHazeNoise worldHeatHazeNoise;
    bool worldDisplayReady=false;
    engine::render::WorldDiffuseProbeUniforms worldDiffuseProbes{};
    engine::render::WorldPlanarReflectionUniforms worldPlanarReflection{};
    engine::render::WorldPlanarReflectionView worldPlanarReflectionView{};
    std::array<int,4> worldPlanarScissor{};
    struct SurfaceTarget { std::array<Texture,6> planes; Texture depth; VkFramebuffer framebuffer=VK_NULL_HANDLE;
        engine::render::WorldContactUniforms view{}; bool probeScratch=false; };
    std::unordered_map<std::string,SurfaceTarget> worldSurfaceTargets;
    std::unordered_map<std::string,Texture> worldProbeScratch;
    SurfaceTarget* worldSurfaceTarget=nullptr;
    engine::render::WorldProbeSurfaceCache worldProbeSurfaces;
    engine::render::WorldPlacedProbeCache placedProbes;
    engine::render::WorldProbeResidency probeResidency;
    VkDescriptorSet placedProbeDescriptor=VK_NULL_HANDLE;
    Buffer worldProbeRadianceReadback;
    engine::render::WorldProbeRadianceImage worldProbeDiagnostic{};
    bool captureWorldProbeRadiance(std::span<const std::uint64_t>);
    engine::render::WorldProbeRadianceImage readWorldProbeRadiance();
    std::string worldProbeSurfaceKey;
    std::vector<std::array<float,4>> worldMotionWords{{}};
    std::uint32_t worldMotionBase=0;
    bool worldMotionUploaded=false;
    Buffer worldMotionReadback;
    std::uint64_t worldMotionSurfaceBytes=0;
    int worldMotionWidth=0,worldMotionHeight=0;
    std::uint64_t worldSurfaceFrame=0;
    unsigned worldSurfaceSlot=0;
    // Retained probe views must not renumber transient scene allocations.
    std::array<unsigned,2> worldSurfaceAllocationSlots{};
    Texture* worldSurfaceDepth=nullptr;
    engine::render::WorldContactUniforms worldSurfaceView{};
    bool worldSurfacePlanar=false;
    bool worldSurfaceComposing=false, worldSurfaceComposed=false, worldSurfaceCompositionFinished=false, worldSurfaceCompositionCapture=false;
    bool worldSurfaceActive=false,worldSurfaceReady=false,worldSurfaceContact=false;
    VkRenderPass worldSurfaceRenderPass=VK_NULL_HANDLE;
    VkPipeline worldSurfacePipeline=VK_NULL_HANDLE;
    VkPipeline worldFogComposePipeline=VK_NULL_HANDLE;
    VkPipeline worldHazePipeline=VK_NULL_HANDLE;
    VkPipeline worldHazeEdgePipeline=VK_NULL_HANDLE;
    VkPipeline worldHazeStencilComposePipeline=VK_NULL_HANDLE,worldHazeStencilEdgePipeline=VK_NULL_HANDLE;
    VkRenderPass worldHazeStencilRenderPass=VK_NULL_HANDLE;
    VkFormat worldHazeStencilFormat=VK_FORMAT_UNDEFINED;
    struct HazeStencilTarget {Texture depth;VkFramebuffer framebuffer=VK_NULL_HANDLE;std::uint64_t owner=0;};
    std::unordered_map<std::string,HazeStencilTarget> worldHazeStencilTargets;
    bool worldHazeStencilActive=false;
    void ensureWorldHazeStencilPass();
    void clearWorldHazeStencilTargets(std::uint64_t owner=~std::uint64_t{0});
    bool beginWorldLocalHazeComposition(int width,int height);
    void endWorldLocalHazeComposition();
    VkPipeline worldTemporalHazePipeline=VK_NULL_HANDLE;
    VkPipeline worldSurfaceCompositionPipeline=VK_NULL_HANDLE;
    bool ensureWorldSurfaceCompositionPipeline();
    VkPipeline worldSurfaceReflectionPipeline=VK_NULL_HANDLE;
    bool ensureWorldSurfaceReflectionPipeline();
    VkPipeline worldProbeRelightPipeline=VK_NULL_HANDLE;
    bool ensureWorldProbeRelightPipeline();
    bool ensureWorldSurfacePipeline();
    bool captureWorldClusteredLighting();
    engine::render::WorldClusteredLightingDiagnostic readWorldClusteredLighting();
    bool resolveWorldClusteredLighting(const engine::render::WorldClusteredLightingView& view);
    bool beginWorldTemporalScene(const engine::render::WorldTemporalView&);
    void releaseWorldTemporalView(std::uint64_t);
    void collectWorldTemporalViews();
    bool resolveWorldTemporalScene();
    bool resolveWorldHeatHaze();
    bool processWorldTemporalScene(bool finish,bool distort=false);
    bool processWorldContactVisibility();
    bool captureWorldContactVisibility();
    engine::render::WorldContactImage readWorldContactVisibility();
    bool resolveWorldTemporalReflection(const engine::render::WorldTemporalReflectionView&);
    engine::render::WorldTemporalFeedback worldTemporalFeedback() const;
    bool captureWorldTemporalScene();
    engine::render::WorldTemporalImage readWorldTemporalScene();
    struct ClusteredState;
    std::shared_ptr<ClusteredState> clustered;
    struct IndirectState;
    std::shared_ptr<IndirectState> indirect;
    std::array<VkBuffer,2> worldIndirectBuffers{};
    VkDescriptorSet worldIndirectReceiver=VK_NULL_HANDLE;
    Buffer worldIndirectFallbackBuffer;
    bool updateWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate&);
    bool prepareWorldIndirectLighting(std::span<const engine::render::WorldIndirectLightingUpdate>,std::uint64_t);
    bool bindWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate&);
    void clearWorldIndirectLighting();
    void destroyWorldIndirectLighting();
    struct ParticleLightingState;
    std::shared_ptr<ParticleLightingState> particleLighting;
    void releaseWorldParticleLighting(std::uint64_t owner=~std::uint64_t{0});
    Texture* findWorldParticleLightingTexture(const char*);
    engine::render::WorldParticleLightingResult updateWorldParticleLighting(const engine::render::WorldParticleLightingView&);
    bool captureWorldParticleLighting();
    engine::render::WorldParticleLightingDiagnostic readWorldParticleLighting();
    struct WindState;
    std::shared_ptr<WindState> windField;
    Buffer windFallback;
    struct LayeredState;
    std::shared_ptr<LayeredState> layeredMaterials;
    VkDescriptorSet worldLayeredDescriptorSet(const Buffer&,std::uint64_t windOwner);
    void bindWorldLayeredMaterials(std::uint64_t,std::shared_ptr<const engine::render::WorldLayerMaterialSet>);
    void releaseWorldLayeredMaterials(std::uint64_t owner=~0ull);
    VkDescriptorSet worldWindDescriptorSet();
    void prepareWorldWindField(const engine::render::WorldWindFieldInput&);
    void clearWorldWindField();
    void releaseWorldWindField(std::uint64_t owner=~0ull);
    bool captureWorldWindField();
    engine::render::WorldWindFieldDiagnostic readWorldWindField();
    struct CloudState;
    std::shared_ptr<CloudState> clouds;
    engine::render::WorldCloudUniforms worldClouds{};
    engine::render::WorldCloudRows worldSkyCloud{};
    Texture* worldSkyModelTexture=nullptr;
    std::array<Texture*,2> worldCloudTextures{};
    std::uint64_t worldCloudOwner=0;
    void recordWorldClouds(const engine::render::WorldCloudView&,bool);
    void prepareWorldClouds(const engine::render::WorldCloudView&);
    void resolveWorldClouds(const engine::render::WorldCloudView&);
    void clearWorldClouds();
    bool captureWorldClouds();
    engine::render::WorldCloudDiagnostic readWorldClouds();
    void releaseWorldClouds(std::uint64_t owner=~std::uint64_t{0});
    struct PrecipitationState;
    std::shared_ptr<PrecipitationState> precipitation;
    void releaseWorldPrecipitation(std::uint64_t owner=~std::uint64_t{0});
    Texture* findWorldPrecipitationTexture(const char*);
    Texture* findWorldPrecipitationDepth(const engine::render::WorldParticleLightingView&,std::array<float,4>&);
    engine::render::WorldPrecipitationLightingResult updateWorldPrecipitationLighting(const engine::render::WorldPrecipitationLightingView&);
    engine::render::WorldFogUniforms worldFog{};
    engine::render::WorldWetUniforms worldWet{};
    std::array<Texture*,5> worldWetTextures{};
    std::uint64_t worldWetOwner=0;
    engine::render::WorldWetBindingHistory worldWetHistory;
    void bindWorldWetSurfaces(const engine::render::WorldWetSurfaceView*);
    std::array<Texture*,2> worldFogTextures{};
    void bindWorldFog(const engine::render::WorldPrecipitationLightingView*,const engine::render::WorldPrecipitationLightingResult*,const engine::render::WorldPrecipitationLightingResult*);
    bool captureWorldPrecipitationLighting();
    engine::render::WorldPrecipitationLightingDiagnostic readWorldPrecipitationLighting();
    struct RainMapState;
    std::shared_ptr<RainMapState> rainMaps;
    void releaseWorldRainMaps(std::uint64_t owner=~std::uint64_t{0});
    Texture* findWorldRainTexture(const char* key);
    bool captureWorldRainMap(engine::render::WorldResourceScopeId);
    engine::render::WorldRainMapDiagnostic readWorldRainMap();
    void prewarmWorldRainMap(engine::render::WorldResourceScopeId,unsigned);
    bool beginWorldRainMap(engine::render::WorldResourceScopeId,const engine::render::WorldDirectionalShadowView&,bool);
    bool endWorldRainMap();
    bool publishWorldRainMap(engine::render::WorldResourceScopeId,unsigned);
    std::string worldRainMapTexture(engine::render::WorldResourceScopeId) const;

    void releaseWorldIndirectScope(std::uint64_t owner);
    bool captureWorldIndirectLighting();
    engine::render::WorldIndirectLightingDiagnostic readWorldIndirectLighting();
    void destroyWorldClusteredLighting();
    engine::render::WorldClusteredLightingView worldCompositionLighting{};
    engine::render::WorldClusteredConsumerUniforms worldClusteredView{};
    std::array<VkDescriptorSet,3> worldClusteredDescriptors{},worldContactFilterSources{};
    std::array<VkDescriptorSet,4> worldStoredSurfaceDescriptors{};
    struct TemporalState;
    std::shared_ptr<TemporalState> temporal;
    VkDescriptorSet worldTemporalOutput=VK_NULL_HANDLE;
    VkImageView worldTemporalOutputView=VK_NULL_HANDLE;
    void clearWorldTemporalScene();
    void releaseWorldTemporalScene();
    bool beginWorldSurfaceBuffer(const float*,int,int);
    bool setWorldSurfaceMotion(const engine::render::WorldMotionMatrix&,std::span<const engine::render::WorldMotionSample>);
    bool captureWorldSurfaceMotion();
    engine::render::WorldMotionImage readWorldSurfaceMotion();
    bool endWorldSurfaceBuffer();
    void clearWorldSurfaceBuffer();
    void resumeWorldSceneColor();
    struct HazeTarget { Texture color, depth; VkFramebuffer framebuffer = VK_NULL_HANDLE; };
    std::unordered_map<std::string, HazeTarget> worldHazeTargets;
    VkDescriptorSet worldHazeDescriptor = VK_NULL_HANDLE;
    bool worldHazeActive = false;
    bool beginWorldLocalHaze(int width, int height);
    bool endWorldLocalHaze();
    VkDescriptorSet worldPlanarDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet worldPlanarFallback = VK_NULL_HANDLE;
    engine::render::WorldContactUniforms worldContact{};
    engine::render::WorldScreenReflectionUniforms worldScreenReflection{};
    VkDescriptorSet worldScreenReflectionDescriptor=VK_NULL_HANDLE;
    engine::render::WorldLocalReflectionUniforms worldLocalReflection{};
    VkDescriptorSet worldLocalReflectionDescriptor=VK_NULL_HANDLE;
    int worldLocalReflectionFace=0;
    bool worldLocalReflectionPrepared=false;
    bool captureWorldRadianceSnapshot(const char*,int,int,VkDescriptorSet&,std::array<float,4>&,int atlasFace=-1,int atlasLevel=0,int atlasResolution=0,int offsetY=0,int offsetX=0,int residentExtent=0,bool clearScene=true,bool batchFaces=false);
    VkDescriptorSet worldContactDescriptor=VK_NULL_HANDLE;
    VkDescriptorSetLayout worldContactFilterLayout=VK_NULL_HANDLE;
    VkDescriptorSet worldContactFilterSet=VK_NULL_HANDLE,worldContactFilterFallback=VK_NULL_HANDLE;
    std::array<std::array<VkDescriptorSet,64>,kFramesInFlight> worldContactFilterSets{};
    unsigned worldContactFilterSetCount=0;
    engine::render::WorldContactHistory worldContactHistory{};
    VkDescriptorSet worldContactPreviousVisibility=VK_NULL_HANDLE,worldContactPreviousDepth=VK_NULL_HANDLE;
    VkDescriptorSet createContactFilterSet(const std::array<VkDescriptorSet,3>&);
    bool beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView&);
    bool endWorldPlanarReflection();
    VkDescriptorSet createPlanarDescriptor(const Texture&);

    bool editorSurfacePassActive = false;
    std::uint64_t activeEditorSurface = 0u;
    std::uint64_t nextEditorSurface = 1u;
    std::unordered_map<
        std::uint64_t,
        EditorSurface> editorSurfaces;
    std::array<float, 4> frameClearColor{0.0f, 0.0f, 0.0f, 1.0f};

    std::string gpuName;
    bool gpuDiscrete = false;
    bool samplerAnisotropyEnabled = false;
    bool dualSourceBlendSupported = false;
    bool descriptorIndexingSupported = false;
    bool indirectWorldBatchingSupported = false;
    float maxSamplerAnisotropy = 1.0f;
    float timestampPeriodNs = 0.0f;
    engine::render::RenderPhaseProfile phaseProfile;

    IRenderBackend::BackendFrameTimings lastTimings{};
    IRenderBackend::BackendFrameStats frameStats{};
    IRenderBackend::BackendFrameStats lastStats{};
    std::uint64_t frameSkinPaletteUploadBytes = 0u;
    std::uint64_t frameSkinPaletteReuseBytes = 0u;
    std::uint32_t frameSkinPaletteUploads = 0u;
    std::uint32_t frameSkinPaletteReuses = 0u;
    std::uint64_t frameWorldStateUploadBytes = 0u;
    std::uint64_t frameWorldStateReuseBytes = 0u;
    std::uint32_t framePreparedMaterialCacheHits = 0u;
    std::uint32_t framePreparedMaterialCacheMisses = 0u;
    std::uint32_t frameIndirectWorldApiCalls = 0u;
    std::uint32_t frameIndirectWorldCommands = 0u;
    std::uint32_t frameIndirectWorldFallbacks = 0u;
    std::uint32_t frameSpriteInstances = 0u;
    std::uint32_t frameSpriteDrawRuns = 0u;
    std::uint32_t frameSpriteDrawsSaved = 0u;
    std::uint32_t frameSpriteUploadBatches = 0u;
    std::uint32_t frameSpriteUploadsSaved = 0u;

    void initialize(SDL_Window* sdlWindow,
                    int width,
                    int height,
                    bool enableVsync,
                    const std::string& preferredAdapterName);
    void shutdown();
    void beginFrame(float r, float g, float b, float a);
    void endFrame();
    VkResult presentPendingFrame();
    void beginWorldSceneColorPass(int surfaceWidth, int surfaceHeight);
    bool clearWorldDepth();
    bool captureWorldDepthSnapshot(const char* purpose="scene-depth");
    bool restoreWorldDepthSnapshot();
    Texture* worldDepthSnapshot = nullptr;
    void endWorldSceneColorPass();
    void endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame&);
    void requestResize(int width, int height);
    void requestVSync(bool enabled);
    void recordSubmissionStats(const IRenderBackend::WorldIndexedSubmissionStats& stats);
    std::uint64_t createEditorSurface(int width, int height);
    bool resizeEditorSurface(
        std::uint64_t surface,
        int width,
        int height);
    void destroyEditorSurface(std::uint64_t surface);
    bool beginEditorSurface(std::uint64_t surface);
    void endEditorSurface();
    bool getEditorSurfaceTexture(
        std::uint64_t surface,
        std::uint32_t frameIndex,
        VkSampler& outSampler,
        VkImageView& outImageView) const;
    bool createEditorSurfaceResources(EditorSurface& surface);
    void destroyEditorSurfaceResources(EditorSurface& surface);
    void recreateEditorSurfaceResources();
    void destroyAllEditorSurfaceResources();
    void beginEditorSurfaceDisplayPass(EditorSurface& surface);
    void resumeMainRenderPass();

    void createInstance();
    void selectPhysicalDevice(const std::string& preferredAdapterName);
    void createDevice();
    void createCommandResources();
    void createDescriptorResources();
    void createFrameResources();
    void createEnvironmentResources();
    void destroyEnvironmentResources();
    void createSwapchainResources();
    void destroySwapchainResources();
    bool recreateSwapchain();
    void createRenderPass();
    void createDepthResources();
    void createWorldSceneColorResources();
    void destroyWorldSceneColorResources();
    void createFramebuffers();
    void createPipelines();
    void destroyPipelines();
    void configureScreenshotCapture();
    bool recordScreenshotCopy(VkCommandBuffer commandBuffer);
    void finishScreenshotCapture(VkFence frameFence);

    std::uint32_t findMemoryType(std::uint32_t typeBits,
                                 VkMemoryPropertyFlags required,
                                 VkMemoryPropertyFlags preferred = 0u) const;
    Buffer createBuffer(VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred = 0u);
    void destroyBuffer(Buffer& buffer);
    bool writeTransient(const void* data,
                        VkDeviceSize size,
                        VkDeviceSize alignment,
                        VkBuffer& outBuffer,
                        VkDeviceSize& outOffset);
    bool writeCachedWorldViewState(
        const engine::render::vulkan_backend::WorldViewState& state,
        VkDeviceSize alignment,
        VkBuffer& outBuffer,
        VkDeviceSize& outOffset);
    bool writeCachedWorldSpecializedMaterialState(
        const engine::render::vulkan_backend::WorldSpecializedMaterialState& state,
        VkDeviceSize alignment,
        VkBuffer& outBuffer,
        VkDeviceSize& outOffset);
    void bindGraphicsPipeline(VkCommandBuffer commandBuffer, VkPipeline pipeline);
    void bindVertexBuffer(
        VkCommandBuffer commandBuffer,
        VkBuffer buffer,
        VkDeviceSize offset);
    void bindIndexBuffer(
        VkCommandBuffer commandBuffer,
        VkBuffer buffer,
        VkDeviceSize offset,
        VkIndexType indexType);
    void bindTextureDescriptorSet(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet descriptorSet);
    void bindWorldStateDescriptorSets(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet materialDescriptorSet,
        const std::array<std::uint32_t, 4>& dynamicOffsets, bool haze = false);
    void setViewportAndScissor(
        VkCommandBuffer commandBuffer,
        int surfaceWidth,
        int surfaceHeight);
    void resetWorldFrameStateCache();
    engine::render::vulkan_backend::WorldViewport prepareWorldViewport(int width, int height) const;
    engine::render::vulkan_backend::WorldDescriptorBindings prepareWorldStateBindings(
        VkDescriptorSet material, const std::array<std::uint32_t,4>& offsets, bool haze);
    bool prepareWorldDescriptorSets(
        engine::render::vulkan_backend::WorldDescriptorBindings& bindings,
        VkDescriptorSet materialDescriptorSet, const IRenderBackend::WorldTextureData* texture,
        bool instancingEnabled, std::uint32_t instanceBaseWordIndex);
    bool bindWorldDescriptorSets(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet materialDescriptorSet,
        const IRenderBackend::WorldTextureData* texture,
        bool instancingEnabled,
        std::uint32_t instanceBaseWordIndex);
    bool prepareWorldInstances(
        const IRenderBackend::WorldMeshInstance* instances,
        std::size_t instanceCount,
        std::uint32_t& outInstanceCount,
        std::uint32_t& outInstanceBaseWordIndex);
    void prepareWorldDeformations(std::span<const engine::render::WorldSkinPalette>,bool commit=false);
    bool uploadWorldSkinPalette(
        const float* matrices,
        std::size_t floatCount,
        std::uint32_t& outBaseMatrixIndex, engine::render::WorldDeformation version={});
    void maybeLogWorldFrameCache() const;
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer commandBuffer);
    VkShaderModule loadShaderModule(const char* fileName);

    Texture createTexture(const unsigned char* rgba,
                          int width,
                          int height,
                          bool srgb,
                          int wrapS,
                          int wrapT,
                          bool createStandaloneDescriptor,
                          const IRenderBackend::WorldTextureMipLevel*
                              authoredMipLevels = nullptr,
                          std::uint32_t authoredMipLevelCount = 0u,
                          bool linearMipSampling = false);
    Texture createTextureRgba16Float(const std::uint16_t* rgba16f,
                                     int width,
                                     int height,
                                     int wrapS,
                                     int wrapT,
                                     bool createStandaloneDescriptor);
    Texture createTextureWithFormat(const void* pixels,
                                    VkDeviceSize byteCount,
                                    int width,
                                    int height,
                                    VkFormat format,
                                    int wrapS,
                                    int wrapT,
                                    bool createStandaloneDescriptor,
                                    const IRenderBackend::WorldTextureMipLevel*
                                        authoredMipLevels = nullptr,
                                    std::uint32_t authoredMipLevelCount = 0u,
                          bool linearMipSampling = false);
    void destroyTexture(Texture& texture);
    Texture* ensureWorldTextureRaw(const char* key,
                                   const char* cacheKey,
                                   const unsigned char* rgba,
                                   int width,
                                   int height,
                                   int wrapS,
                                   int wrapT,
                                   bool srgb,
                                   std::string* outResolvedKey = nullptr,
                                   const IRenderBackend::WorldTextureMipLevel*
                                       authoredMipLevels = nullptr,
                                   std::uint32_t authoredMipLevelCount = 0u,
                          bool linearMipSampling = false, bool materialImage = false);
    WorldMaterial* ensureWorldMaterial(const IRenderBackend::WorldTextureData* texture);
    WorldMaterial createWorldMaterial(Texture& base,
                                      Texture& normal,
                                      Texture& metallicRoughness,
                                      Texture& occlusion,
                                      Texture& emissive,
                                      Texture& environment,
                                      Texture& lightProjection,
                                      Texture& projectedShadow);
    bool registerIndexedWorldMaterial(WorldMaterial& material);
    void initializeIndexedWorldMaterialSets();
    bool syncIndexedWorldMaterialSet();
    Texture* ensureSpriteTexture(const std::string& texturePath);
    void prewarmWorldTexture(const IRenderBackend::WorldTextureData* texture);
    void prewarmSpriteTexture(const char* texturePath);
    CachedWorldMesh* ensureCachedWorldMesh(
        const char* geometryKey,
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount);
    bool allocateWorldGeometry(
        VkDeviceSize vertexBytes,
        VkDeviceSize indexBytes,
        CachedWorldMesh& outMesh);
    void destroyWorldGeometryArena();
    void destroyCachedWorldMeshes();
    void prewarmWorldIndexedMesh(
        const char* geometryKey,
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount);

    void drawDebugVertices(const DebugVertex* vertices,
                           std::size_t vertexCount,
                           int surfaceWidth,
                           int surfaceHeight);
    void drawDebugQuads(const IRenderBackend::DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight);
    void drawDebugLines(const IRenderBackend::DebugLine* lines,
                        std::size_t lineCount,
                        int surfaceWidth,
                        int surfaceHeight);
    void drawDebugTriangles(const IRenderBackend::DebugTriangle* triangles,
                            std::size_t triangleCount,
                            int surfaceWidth,
                            int surfaceHeight);
    void drawDebugSprites(const IRenderBackend::DebugSprite* sprites,
                          std::size_t spriteCount,
                          int surfaceWidth,
                          int surfaceHeight);
    void drawWorldTriangles(const IRenderBackend::WorldTriangle* triangles,
                            std::size_t triangleCount,
                            const float* viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight);
    void drawWorldIndexedMesh(const IRenderBackend::WorldMeshVertex* vertices,
                              std::size_t vertexCount,
                              const std::uint32_t* indices,
                              std::size_t indexCount,
                              const IRenderBackend::WorldTextureData* texture,
                              const float* viewProjectionMatrix4x4,
                              int surfaceWidth,
                              int surfaceHeight);
    void drawWorldIndexedMeshInstanced(
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        const IRenderBackend::WorldTextureData* texture,
        const IRenderBackend::WorldMeshInstance* instances,
        std::size_t instanceCount,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight);
    void drawWorldIndexedMeshCached(
        const char* geometryKey,
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        const IRenderBackend::WorldTextureData* texture,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight);
    void drawWorldIndexedMeshCachedInstanced(
        const char* geometryKey,
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        const IRenderBackend::WorldTextureData* texture,
        const IRenderBackend::WorldMeshInstance* instances,
        std::size_t instanceCount,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight);
    void drawWorldIndexedMeshCachedPreparedInstanced(
        const char* geometryKey,
        const IRenderBackend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        VkDescriptorSet materialDescriptorSet,
        const IRenderBackend::WorldTextureData* texture,
        const IRenderBackend::WorldMeshInstance* instances,
        std::size_t instanceCount,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight);
    void drawWorldIndexedMeshBuffers(
        VkBuffer vertexBuffer,
        VkDeviceSize vertexOffset,
        std::size_t vertexCount,
        VkBuffer indexBuffer,
        VkDeviceSize indexOffset,
        std::size_t indexCount,
        std::uint32_t firstIndex,
        std::int32_t baseVertex,
        VkDescriptorSet preparedMaterialDescriptorSet,
        const IRenderBackend::WorldTextureData* texture,
        const IRenderBackend::WorldMeshInstance* instances,
        std::size_t instanceCount,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight);
    bool submitWorldSceneIndirect(
        const IRenderBackend::WorldSceneFrame& frame,
        const IRenderBackend::WorldSceneView& view);
};
