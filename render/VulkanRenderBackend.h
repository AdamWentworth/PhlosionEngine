#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/render/IRenderBackend.h"

struct SDL_Window;
struct VulkanRenderBackendImpl;

class VulkanRenderBackend final : public IRenderBackend {
public:
    VulkanRenderBackend(SDL_Window* window,
                        int width,
                        int height,
                        bool vsyncEnabled,
                        const std::string& preferredAdapterName = {});
    ~VulkanRenderBackend() override;

    const char* backendId() const override { return "vulkan"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return true; }
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
    std::string activeGpuName() const override;
    bool activeGpuIsDiscrete() const override;
    void setVSyncEnabled(bool enabled) override;
    void shutdown() override;

    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    bool supportsWorldIndexedMeshInstancing() const override { return true; }
    bool supportsWorldSceneFastPath() const override;
    bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps& outCaps) const override;
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

private:
    std::unique_ptr<VulkanRenderBackendImpl> impl_;
};
