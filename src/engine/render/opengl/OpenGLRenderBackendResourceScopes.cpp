#include "engine/render/OpenGLRenderBackend.h"
#include <glad/glad.h>

using namespace engine::render;

WorldResourceScopeId OpenGLRenderBackend::createWorldResourceScope(std::uint64_t limit) {
    return worldScopes_.create(limit);
}
bool OpenGLRenderBackend::beginWorldResourceScope(WorldResourceScopeId id) {
    if (!worldScopes_.begin(id)) return false;
    return true;
}
bool OpenGLRenderBackend::endWorldResourceScope() {
    const bool ended = worldScopes_.end();
    return ended;
}
bool OpenGLRenderBackend::requestWorldResourceScopeRelease(WorldResourceScopeId id) {
    return worldScopes_.release(id);
}
bool OpenGLRenderBackend::getWorldResourceScopeStats(WorldResourceScopeId id, WorldResourceScopeStats& out) const {
    return worldScopes_.query(id, out);
}
WorldResourceStats OpenGLRenderBackend::getWorldResourceStats() const {
    return worldScopes_.stats();
}

bool OpenGLRenderBackend::collectWorldResourceScopes() {
    const auto pending = worldScopes_.pending();
    if (pending.empty()) return true;
    glFinish();
    if (glGetError() != GL_NO_ERROR) {
        for (const auto id : pending)
            worldScopes_.fail(id, "OpenGL completion failed", WorldResourceScopeStatus::GpuWaitFailed);
        return false;
    }
    // Collection precedes recording, so no batch may retain a deleted name.
    worldIndexedBatchSubmissionState_ = {};
    for (const auto id : pending) {
        for (auto it = cachedWorldMeshes_.begin(); it != cachedWorldMeshes_.end();) {
            auto& mesh = it->second;
            if (mesh.scope != id) { ++it; continue; }
            glDeleteBuffers(1, &mesh.indexBuffer);
            glDeleteBuffers(1, &mesh.vertexBuffer);
            glDeleteVertexArrays(1, &mesh.vao);
            it = cachedWorldMeshes_.erase(it);
        }
        for (auto it = worldTextures_.begin(); it != worldTextures_.end();) {
            if (it->second.scope != id) { ++it; continue; }
            glDeleteTextures(1, &it->second.textureId);
            it = worldTextures_.erase(it);
        }
        worldScopes_.collect(id);
    }
    return true;
}
