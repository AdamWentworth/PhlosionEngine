#include "engine/editor/OpenGLEditorRenderSurface.h"

#include <algorithm>

#include <glad/glad.h>

namespace engine::editor {

OpenGLEditorRenderSurface::~OpenGLEditorRenderSurface() {
    shutdown();
}

bool OpenGLEditorRenderSurface::ensure(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (valid() && width_ == width && height_ == height) {
        return true;
    }

    shutdown();
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &texture_);
    glGenRenderbuffers(1, &depthStencil_);
    if (framebuffer_ == 0u || texture_ == 0u ||
        depthStencil_ == 0u) {
        shutdown();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, depthStencil_);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        width,
        height);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        texture_,
        0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        depthStencil_);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

bool OpenGLEditorRenderSurface::begin(int width, int height) {
    if (active_) {
        return false;
    }
    glGetIntegerv(
        GL_DRAW_FRAMEBUFFER_BINDING,
        &previousDrawFramebuffer_);
    glGetIntegerv(
        GL_READ_FRAMEBUFFER_BINDING,
        &previousReadFramebuffer_);
    glGetIntegerv(GL_VIEWPORT, previousViewport_.data());

    if (!ensure(width, height)) {
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(previousDrawFramebuffer_));
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(previousReadFramebuffer_));
        return false;
    }

    active_ = true;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glClearColor(0.025f, 0.031f, 0.037f, 1.0f);
    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT);
    return true;
}

void OpenGLEditorRenderSurface::end() {
    if (!active_) {
        return;
    }
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        static_cast<GLuint>(previousDrawFramebuffer_));
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer_));
    glViewport(
        previousViewport_[0],
        previousViewport_[1],
        previousViewport_[2],
        previousViewport_[3]);
    active_ = false;
}

void OpenGLEditorRenderSurface::shutdown() {
    if (active_) {
        end();
    }
    if (framebuffer_ != 0u) {
        glDeleteFramebuffers(1, &framebuffer_);
    }
    if (texture_ != 0u) {
        glDeleteTextures(1, &texture_);
    }
    if (depthStencil_ != 0u) {
        glDeleteRenderbuffers(1, &depthStencil_);
    }
    framebuffer_ = 0u;
    texture_ = 0u;
    depthStencil_ = 0u;
    width_ = 0;
    height_ = 0;
}

} // namespace engine::editor
