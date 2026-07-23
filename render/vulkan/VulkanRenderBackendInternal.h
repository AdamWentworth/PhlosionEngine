#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "engine/render/vulkan/VulkanWorldInstanceState.h"
#include "engine/render/vulkan/VulkanWorldIndirectState.h"
#include "engine/render/vulkan/VulkanWorldMaterialLayout.h"
#include "engine/render/vulkan/VulkanWorldPipelinePolicy.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"
#include "engine/render/vulkan/VulkanWorldTransformState.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"

struct SDL_Window;

struct VulkanRenderBackendImpl {
    static constexpr std::uint32_t kFramesInFlight = 2u;
    static constexpr VkDeviceSize kTransientBytesPerFrame = 128ull * 1024ull * 1024ull;

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0u;
        VkDeviceSize offset = 0u;
        bool coherent = false;
    };

    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkQueryPool timestampQueries = VK_NULL_HANDLE;
        VkDescriptorSet worldStateDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet indexedWorldMaterialDescriptorSet = VK_NULL_HANDLE;
        std::uint32_t indexedWorldMaterialCount = 0u;
        bool indexedWorldMaterialSetBound = false;
        Buffer transient;
        bool timestampIssued = false;
    };

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        int width = 0;
        int height = 0;
    };

    struct WorldMaterial {
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
        VkBuffer geometryBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexByteOffset = 0u;
        VkDeviceSize indexByteOffset = 0u;
        std::uint32_t firstIndex = 0u;
        std::int32_t baseVertex = 0;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
    };

    struct CachedSkinPalette {
        std::uint64_t hash = 0u;
        VkDeviceSize offset = 0u;
        VkDeviceSize size = 0u;
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
    bool screenshotCaptureConfigured = false;
    bool screenshotCaptured = false;
    bool screenshotCopyPending = false;
    int requestedWidth = 1;
    int requestedHeight = 1;
    std::uint32_t currentFrame = 0u;
    std::uint32_t acquiredImage = 0u;
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

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthMemories;
    std::vector<VkImageView> depthViews;

    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout indexedWorldMaterialSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout worldStateSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout debugPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout texturedPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout indirectWorldPipelineLayout = VK_NULL_HANDLE;
    VkPipeline debugPipeline = VK_NULL_HANDLE;
    VkPipeline spritePipeline = VK_NULL_HANDLE;
    std::array<
        VkPipeline,
        engine::render::vulkan_backend::kWorldPipelineCount> worldPipelines{};
    std::array<
        VkPipeline,
        engine::render::vulkan_backend::kWorldPipelineCount> indirectWorldPipelines{};

    std::array<FrameResources, kFramesInFlight> frames{};
    std::unordered_map<std::string, Texture> worldTextures;
    std::unordered_map<std::string, WorldMaterial> worldMaterials;
    std::vector<WorldMaterial*> indexedWorldMaterials;
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes;
    std::vector<WorldGeometryPage> worldGeometryPages;
    std::vector<CachedSkinPalette> frameSkinPalettes;
    std::vector<CachedWorldViewState> frameWorldViewStates;
    std::vector<CachedWorldSpecializedMaterialState>
        frameWorldSpecializedMaterialStates;
    std::unordered_map<std::string, Texture> spriteTextures;
    Texture fallbackWorldTexture;
    Texture fallbackWorldNormalTexture;
    Texture fallbackWorldLinearTexture;
    Texture fallbackWorldEmissiveTexture;
    Texture neutralPmremTexture;
    Texture fallbackSpriteTexture;
    WorldMaterial fallbackWorldMaterial;
    std::vector<VkDescriptorSet> worldSceneMaterialDescriptorSets;
    std::vector<WorldMaterial*> worldSceneMaterials;
    std::uint32_t worldSceneMaterialCacheGeneration = 0u;

    VkPipeline boundGraphicsPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundTexturedDescriptorSet = VK_NULL_HANDLE;
    VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize boundVertexOffset = 0u;
    VkBuffer boundIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize boundIndexOffset = 0u;
    VkIndexType boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    std::uint32_t boundViewportWidth = 0u;
    std::uint32_t boundViewportHeight = 0u;
    bool boundViewportValid = false;

    std::string gpuName;
    bool gpuDiscrete = false;
    bool samplerAnisotropyEnabled = false;
    bool dualSourceBlendSupported = false;
    bool descriptorIndexingSupported = false;
    bool indirectWorldBatchingSupported = false;
    float maxSamplerAnisotropy = 1.0f;
    float timestampPeriodNs = 0.0f;

    IRenderBackend::BackendFrameTimings lastTimings{};
    IRenderBackend::BackendFrameStats frameStats{};
    IRenderBackend::BackendFrameStats lastStats{};
    std::uint64_t frameSkinPaletteUploadBytes = 0u;
    std::uint64_t frameSkinPaletteReuseBytes = 0u;
    std::uint32_t frameSkinPaletteUploads = 0u;
    std::uint32_t frameSkinPaletteReuses = 0u;
    std::uint64_t frameWorldStateUploadBytes = 0u;
    std::uint64_t frameWorldStateReuseBytes = 0u;
    std::uint32_t framePipelineBindCalls = 0u;
    std::uint32_t framePipelineBindSkips = 0u;
    std::uint32_t frameDescriptorBindCalls = 0u;
    std::uint32_t frameDescriptorBindSkips = 0u;
    std::uint32_t frameVertexBufferBindCalls = 0u;
    std::uint32_t frameVertexBufferBindSkips = 0u;
    std::uint32_t frameIndexBufferBindCalls = 0u;
    std::uint32_t frameIndexBufferBindSkips = 0u;
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
    std::uint32_t frameViewportUpdates = 0u;
    std::uint32_t frameViewportSkips = 0u;

    void initialize(SDL_Window* sdlWindow,
                    int width,
                    int height,
                    bool enableVsync,
                    const std::string& preferredAdapterName);
    void shutdown();
    void beginFrame(float r, float g, float b, float a);
    void endFrame();
    void requestResize(int width, int height);
    void requestVSync(bool enabled);
    void recordSubmissionStats(const IRenderBackend::WorldIndexedSubmissionStats& stats);

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
        const std::array<std::uint32_t, 4>& dynamicOffsets);
    void setViewportAndScissor(
        VkCommandBuffer commandBuffer,
        int surfaceWidth,
        int surfaceHeight);
    void resetWorldFrameStateCache();
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
    bool uploadWorldSkinPalette(
        const float* matrices,
        std::size_t floatCount,
        std::uint32_t& outBaseMatrixIndex);
    void maybeLogWorldFrameCache() const;
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer commandBuffer);
    VkShaderModule loadShaderModule(const char* fileName) const;

    Texture createTexture(const unsigned char* rgba,
                          int width,
                          int height,
                          bool srgb,
                          int wrapS,
                          int wrapT,
                          bool createStandaloneDescriptor);
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
                                    bool createStandaloneDescriptor);
    void destroyTexture(Texture& texture);
    Texture* ensureWorldTextureRaw(const char* key,
                                   const char* cacheKey,
                                   const unsigned char* rgba,
                                   int width,
                                   int height,
                                   int wrapS,
                                   int wrapT,
                                   bool srgb,
                                   std::string* outResolvedKey = nullptr);
    WorldMaterial* ensureWorldMaterial(const IRenderBackend::WorldTextureData* texture);
    WorldMaterial createWorldMaterial(Texture& base,
                                      Texture& normal,
                                      Texture& metallicRoughness,
                                      Texture& occlusion,
                                      Texture& emissive);
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
