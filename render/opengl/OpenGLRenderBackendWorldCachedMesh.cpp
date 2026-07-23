#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <string>

#include <glad/glad.h>

OpenGLRenderBackend::CachedWorldMesh* OpenGLRenderBackend::ensureCachedWorldMesh(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
    if (!geometryKey || geometryKey[0] == '\0' || !vertices || !indices || vertexCount == 0u || indexCount < 3u) {
        return nullptr;
    }
    ensureWorldPipeline();
    if (worldInstanceVbo_ == 0u) return nullptr;

    constexpr std::size_t kMaxWorldVertices = 540000u;
    constexpr std::size_t kMaxWorldIndices = 900000u;
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount == 0u || safeIndexCount < 3u) return nullptr;
    for (std::size_t i = 0; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return nullptr;
    }

    const std::string key(geometryKey);
    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldMeshVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);

    auto existing = cachedWorldMeshes_.find(key);
    if (existing != cachedWorldMeshes_.end()) {
        CachedWorldMesh& mesh = existing->second;
        if (mesh.valid &&
            mesh.vertexCount == safeVertexCount &&
            mesh.indexCount == safeIndexCount &&
            mesh.vertexBytes == vertexBytes &&
            mesh.indexBytes == indexBytes) {
            return &mesh;
        }
        if (mesh.indexBuffer != 0u) glDeleteBuffers(1, &mesh.indexBuffer);
        if (mesh.vertexBuffer != 0u) glDeleteBuffers(1, &mesh.vertexBuffer);
        if (mesh.vao != 0u) glDeleteVertexArrays(1, &mesh.vao);
        cachedWorldMeshes_.erase(existing);
    }

    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    CachedWorldMesh mesh{};
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vertexBuffer);
    glGenBuffers(1, &mesh.indexBuffer);
    if (mesh.vao == 0u || mesh.vertexBuffer == 0u || mesh.indexBuffer == 0u) {
        if (mesh.indexBuffer != 0u) glDeleteBuffers(1, &mesh.indexBuffer);
        if (mesh.vertexBuffer != 0u) glDeleteBuffers(1, &mesh.vertexBuffer);
        if (mesh.vao != 0u) glDeleteVertexArrays(1, &mesh.vao);
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        return nullptr;
    }

    configureWorldMeshVertexLayout(mesh.vao, mesh.vertexBuffer, mesh.indexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertexBytes),
                 vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indexBytes),
                 indices,
                 GL_STATIC_DRAW);

    mesh.vertexCount = safeVertexCount;
    mesh.indexCount = safeIndexCount;
    mesh.vertexBytes = vertexBytes;
    mesh.indexBytes = indexBytes;
    mesh.valid = true;

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));

    auto [it, _] = cachedWorldMeshes_.emplace(key, std::move(mesh));
    return &it->second;
}

void OpenGLRenderBackend::drawWorldIndexedMeshCached(const char* geometryKey,
                                                     const WorldMeshVertex* vertices,
                                                     std::size_t vertexCount,
                                                     const std::uint32_t* indices,
                                                     std::size_t indexCount,
                                                     const float* viewProjectionMatrix4x4,
                                                     int surfaceWidth,
                                                     int surfaceHeight) {
    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             nullptr,
                                             nullptr,
                                             0u,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    drawWorldIndexedMesh(vertices,
                         vertexCount,
                         indices,
                         indexCount,
                         viewProjectionMatrix4x4,
                         surfaceWidth,
                         surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                                             const WorldMeshVertex* vertices,
                                                             std::size_t vertexCount,
                                                             const std::uint32_t* indices,
                                                             std::size_t indexCount,
                                                             const WorldTextureData* texture,
                                                             const float* viewProjectionMatrix4x4,
                                                             int surfaceWidth,
                                                             int surfaceHeight) {
    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             texture,
                                             nullptr,
                                             0u,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    drawWorldIndexedMeshTextured(vertices,
                                 vertexCount,
                                 indices,
                                 indexCount,
                                 texture,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
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
    const bool hasPerInstanceSkinning = [&]() {
        if (!instances) return false;
        for (std::size_t i = 0; i < instanceCount; ++i) {
            const WorldMeshInstance& instance = instances[i];
            if (instance.gpuSkinning != 0u &&
                instance.skinMatrices != nullptr &&
                instance.skinMatrixCount > 0u) {
                return true;
            }
        }
        return false;
    }();
    if (hasPerInstanceSkinning) {
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(geometryKey,
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
        return;
    }

    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             texture,
                                             instances,
                                             instanceCount,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(geometryKey,
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

void OpenGLRenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                        const WorldMeshVertex* vertices,
                                                        std::size_t vertexCount,
                                                        const std::uint32_t* indices,
                                                        std::size_t indexCount) {
    ensureWorldPipeline();
    (void)ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
}
