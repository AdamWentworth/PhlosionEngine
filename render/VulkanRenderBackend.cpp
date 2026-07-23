#include "engine/render/VulkanRenderBackend.h"

#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <stdexcept>

VulkanRenderBackend::VulkanRenderBackend(SDL_Window* window,
                                         int width,
                                         int height,
                                         bool vsyncEnabled,
                                         const std::string& preferredAdapterName)
    : impl_(std::make_unique<VulkanRenderBackendImpl>()) {
    if (!window) {
        throw std::runtime_error("VulkanRenderBackend requires a valid SDL_Window.");
    }
    impl_->initialize(window, width, height, vsyncEnabled, preferredAdapterName);
}

VulkanRenderBackend::~VulkanRenderBackend() {
    shutdown();
}

void VulkanRenderBackend::beginFrame(float r, float g, float b, float a) {
    if (impl_) impl_->beginFrame(r, g, b, a);
}

void VulkanRenderBackend::endFrame() {
    if (impl_) impl_->endFrame();
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

void VulkanRenderBackend::shutdown() {
    if (impl_) {
        impl_->shutdown();
        impl_.reset();
    }
}

void VulkanRenderBackend::recordWorldIndexedSubmissionStats(
    const WorldIndexedSubmissionStats& stats) {
    if (impl_) impl_->recordSubmissionStats(stats);
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
