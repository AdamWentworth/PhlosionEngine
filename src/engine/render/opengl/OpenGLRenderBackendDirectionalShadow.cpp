#include "engine/render/OpenGLRenderBackend.h"
#include <stdexcept>
#include <glad/glad.h>

void OpenGLRenderBackend::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
    if (resolution < 128 || resolution > 4096 || (resolution & (resolution - 1)))
        throw std::invalid_argument("Invalid shadow resolution.");
    if (worldLocalShadows_.enabled) resolution = engine::render::localShadowAtlasResolution;
    if (worldDirectionalShadowActive_ || resolution < 128 || resolution > 4096 ||
        (resolution & (resolution - 1)) != 0)
        throw std::runtime_error("Invalid directional shadow preparation.");
    if (worldDirectionalShadowTexture_ && worldDirectionalShadowResolution_ == resolution) return;
    destroyWorldDirectionalShadowResources();
    GLint drawFramebuffer = 0, readFramebuffer = 0, texture = 0;
    GLboolean depthMask = GL_TRUE;
    GLdouble clearDepth = 1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    glGenTextures(1, &worldDirectionalShadowTexture_);
    glBindTexture(GL_TEXTURE_2D, worldDirectionalShadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
        static_cast<GLsizei>(resolution), static_cast<GLsizei>(resolution), 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glGenFramebuffers(1, &worldDirectionalShadowFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, worldDirectionalShadowFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, worldDirectionalShadowTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearDepth(1);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    glClearDepth(clearDepth);
    glDepthMask(depthMask);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroyWorldDirectionalShadowResources();
        throw std::runtime_error("Directional shadow depth framebuffer is incomplete.");
    }
    worldDirectionalShadowResolution_ = resolution;
}

bool OpenGLRenderBackend::beginWorldDirectionalShadow(
    const engine::render::WorldDirectionalShadowView& view) {
    if (worldDirectionalShadowActive_) return false;
    if (worldLocalShadows_.activeLight < 0) clearWorldDirectionalShadow();
    if (!engine::render::validWorldDirectionalShadowView(view) ||
        !worldDirectionalShadowTexture_ || view.resolution != (worldLocalShadows_.enabled ? (worldLocalShadows_.activeLight >= 0 ? 1024u : 2048u) : worldDirectionalShadowResolution_) ||
        worldIndexedBatchSubmissionState_.active) return false;
    worldDirectionalShadowView_ = view;
    if (worldLocalShadows_.activeLight < 0) worldLocalShadows_.directionalView = view;
    auto& saved = worldDirectionalShadowSaved_;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, saved.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, saved.scissor.data());
    glGetBooleanv(GL_COLOR_WRITEMASK, saved.colorMask.data());
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved.depthMask);
    glGetIntegerv(GL_DEPTH_FUNC, &saved.depthFunction);
    saved.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    saved.depthEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    glBindFramebuffer(GL_FRAMEBUFFER, worldDirectionalShadowFramebuffer_);
    const auto tile = worldLocalShadows_.offset();
    glViewport(tile[0], tile[1], view.resolution, view.resolution);
    glEnable(GL_SCISSOR_TEST);
    glScissor(tile[0], tile[1], view.resolution, view.resolution);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    GLdouble clearDepth = 1;
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    glClearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(clearDepth);
    worldDirectionalShadowActive_ = true;
    return true;
}

bool OpenGLRenderBackend::endWorldDirectionalShadow() {
    if (!worldDirectionalShadowActive_) return false;
    const auto& saved = worldDirectionalShadowSaved_;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(saved.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(saved.readFramebuffer));
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
    glScissor(saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3]);
    if (saved.scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glColorMask(saved.colorMask[0], saved.colorMask[1], saved.colorMask[2], saved.colorMask[3]);
    glDepthMask(saved.depthMask);
    glDepthFunc(static_cast<GLenum>(saved.depthFunction));
    if (saved.depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    worldDirectionalShadowActive_ = false;
    worldDirectionalShadowReady_ = true;
    return true;
}

void OpenGLRenderBackend::clearWorldDirectionalShadow() {
    if (worldDirectionalShadowActive_) endWorldDirectionalShadow();
    worldDirectionalShadowReady_ = false;
    worldDirectionalShadowView_ = {};
    worldLocalShadows_.clear();
}

void OpenGLRenderBackend::destroyWorldDirectionalShadowResources() {
    clearWorldDirectionalShadow();
    if (worldDirectionalShadowFramebuffer_) glDeleteFramebuffers(1, &worldDirectionalShadowFramebuffer_);
    if (worldDirectionalShadowTexture_) glDeleteTextures(1, &worldDirectionalShadowTexture_);
    worldDirectionalShadowFramebuffer_ = worldDirectionalShadowTexture_ = 0;
    worldDirectionalShadowResolution_ = 0;
}

void OpenGLRenderBackend::prewarmWorldLocalShadows() {
    if (worldDirectionalShadowActive_) throw std::runtime_error("Cannot prepare local shadows during a shadow pass.");
    worldLocalShadows_.enabled = true;
    prewarmWorldDirectionalShadow(engine::render::localShadowAtlasResolution);
}
void OpenGLRenderBackend::clearWorldLocalShadows() {
    if (worldDirectionalShadowActive_ && worldLocalShadows_.activeLight >= 0) endWorldLocalShadow();
    else if (worldDirectionalShadowActive_) endWorldDirectionalShadow();
    worldLocalShadows_.clear();
    worldLocalShadows_.directionalView = worldDirectionalShadowView_;
}
bool OpenGLRenderBackend::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    if (worldDirectionalShadowActive_ || !worldLocalShadows_.begin(index, view)) return false;
    if (beginWorldDirectionalShadow(view)) return true;
    worldLocalShadows_.activeLight = -1;
    return false;
}
bool OpenGLRenderBackend::endWorldLocalShadow() {
    if (worldLocalShadows_.activeLight < 0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.finish(worldDirectionalShadowView_);
    worldDirectionalShadowView_ = worldLocalShadows_.directionalView;
    return true;
}
