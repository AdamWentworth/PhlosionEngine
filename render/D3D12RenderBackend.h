#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "engine/render/IRenderBackend.h"

struct SDL_Window;

#if defined(_WIN32)
#include <d3d12.h>
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

class D3D12RenderBackend final : public IRenderBackend {
public:
    D3D12RenderBackend(SDL_Window* window,
                       int width,
                       int height,
                       bool vsyncEnabled,
                       const std::string& preferredAdapterName = {});
    ~D3D12RenderBackend() override;

    const char* backendId() const override { return "d3d12"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return true; }
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
    std::string activeGpuName() const override { return adapterName_; }
    bool activeGpuIsDiscrete() const override { return discreteAdapter_; }
    void setVSyncEnabled(bool enabled) override { vsyncEnabled_ = enabled; }
    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    bool supportsWorldIndexedMeshInstancing() const override { return true; }
    bool supportsWorldSceneFastPath() const override { return true; }
    bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps& outCaps) const override;
    void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats& stats) override;
    void submitWorldScene(const WorldSceneFrame& frame,
                          const WorldSceneView& view) override;
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
    void drawWorldIndexedMeshTexturedCachedInstanced(const char* geometryKey,
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
    void prewarmDebugSpriteTextures(const char* const* texturePaths,
                                    std::size_t textureCount) override;
    void shutdown() override;

private:
    void configureScreenshotCapture();

    void initDeviceAndSwapchain(const std::string& preferredAdapterName);
    void createRenderTargets();
    void releaseRenderTargets();
    void createDepthResources();
    void releaseDepthResources();
    void createDebugPipeline();
    void createWorldPipeline();
    void createSpritePipeline();
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
                                         std::uint32_t authoredMipLevelCount = 0u);
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
        std::uint32_t lightProjectionTextureDescriptorIndex);
#if defined(_WIN32)
    struct WorldMaterialDescriptorBlockKey {
        std::uint32_t baseTextureDescriptorIndex = 0u;
        std::uint32_t normalTextureDescriptorIndex = 0u;
        std::uint32_t metallicRoughnessTextureDescriptorIndex = 0u;
        std::uint32_t occlusionTextureDescriptorIndex = 0u;
        std::uint32_t emissiveTextureDescriptorIndex = 0u;
        std::uint32_t envTextureDescriptorIndex = 0u;
        std::uint32_t lightProjectionTextureDescriptorIndex = 0u;

        bool operator==(const WorldMaterialDescriptorBlockKey& other) const {
            return baseTextureDescriptorIndex == other.baseTextureDescriptorIndex &&
                   normalTextureDescriptorIndex == other.normalTextureDescriptorIndex &&
                   metallicRoughnessTextureDescriptorIndex ==
                       other.metallicRoughnessTextureDescriptorIndex &&
                   occlusionTextureDescriptorIndex == other.occlusionTextureDescriptorIndex &&
                   emissiveTextureDescriptorIndex == other.emissiveTextureDescriptorIndex &&
                   envTextureDescriptorIndex == other.envTextureDescriptorIndex &&
                   lightProjectionTextureDescriptorIndex ==
                       other.lightProjectionTextureDescriptorIndex;
        }
    };
    struct WorldMaterialDescriptorBlockKeyHash {
        std::size_t operator()(const WorldMaterialDescriptorBlockKey& key) const noexcept {
            std::size_t h = static_cast<std::size_t>(key.baseTextureDescriptorIndex);
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
        const WorldTextureData* textureData) const;
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
    static constexpr std::uint32_t kTimestampQueriesPerFrame = 2;
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
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> depthBuffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampQueryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadbackBuffer_;
    void* fenceEvent_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugPipelineState_;
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
    const float* lastWorldSkinMatrices_ = nullptr;
    std::uint8_t lastWorldSkinningMode_ = 0u;
    std::uint32_t lastWorldSkinMatrixCount_ = 0u;
    std::uint64_t lastWorldSkinMatrixGpuAddress_ = 0u;
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
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes_;
#endif
};
