#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>

#include <glad/glad.h>

#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

namespace {

constexpr const char* kSceneColorVertexShader = R"GLSL(
    #version 330 core
    out vec2 vUv;

    void main() {
        vec2 position = vec2(
            (gl_VertexID == 1) ? 3.0 : -1.0,
            (gl_VertexID == 2) ? 3.0 : -1.0);
        vUv = position * 0.5 + 0.5;
        gl_Position = vec4(position, 0.0, 1.0);
    }
)GLSL";

constexpr const char* kSceneColorFragmentShader = R"GLSL(
    #version 330 core
    in vec2 vUv;
    uniform sampler2D uLinearSceneColor;
    out vec4 FragColor;

    vec3 linearToSrgb(vec3 linearColor) {
        vec3 c = clamp(linearColor, 0.0, 1.0);
        vec3 low = 12.9200001 * c;
        vec3 high =
            1.05499995 * pow(abs(c), vec3(0.416666657)) - 0.0549999997;
        return mix(high, low, lessThanEqual(c, vec3(0.00313080009)));
    }

    void main() {
        vec4 scene = texture(uLinearSceneColor, vUv);
        FragColor = vec4(linearToSrgb(scene.rgb), scene.a);
    }
)GLSL";

} // namespace

bool OpenGLRenderBackend::ensureWorldSceneColorResources(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);

    if (worldSceneColorPostProgram_ == 0u) {
        const GLuint vs = opengl_backend_shader_utils::compileShader(
            GL_VERTEX_SHADER, kSceneColorVertexShader);
        const GLuint fs = opengl_backend_shader_utils::compileShader(
            GL_FRAGMENT_SHADER, kSceneColorFragmentShader);
        if (vs == 0u || fs == 0u) {
            if (vs != 0u) glDeleteShader(vs);
            if (fs != 0u) glDeleteShader(fs);
            return false;
        }
        worldSceneColorPostProgram_ =
            opengl_backend_shader_utils::linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (worldSceneColorPostProgram_ == 0u) return false;
        worldSceneColorPostSamplerLoc_ =
            glGetUniformLocation(worldSceneColorPostProgram_, "uLinearSceneColor");
        glGenVertexArrays(1, &worldSceneColorPostVao_);
        if (worldSceneColorPostSamplerLoc_ < 0 || worldSceneColorPostVao_ == 0u) {
            destroyWorldSceneColorResources();
            return false;
        }
    }

    if (worldSceneColorFbo_ != 0u &&
        worldSceneColorWidth_ == width &&
        worldSceneColorHeight_ == height) {
        return true;
    }

    if (worldSceneColorFbo_ != 0u) glDeleteFramebuffers(1, &worldSceneColorFbo_);
    if (worldSceneColorTexture_ != 0u) glDeleteTextures(1, &worldSceneColorTexture_);
    if (worldSceneDepthRenderbuffer_ != 0u) {
        glDeleteRenderbuffers(1, &worldSceneDepthRenderbuffer_);
    }
    worldSceneColorFbo_ = 0u;
    worldSceneColorTexture_ = 0u;
    worldSceneDepthRenderbuffer_ = 0u;
    worldSceneColorWidth_ = 0;
    worldSceneColorHeight_ = 0;

    glGenFramebuffers(1, &worldSceneColorFbo_);
    glGenTextures(1, &worldSceneColorTexture_);
    glGenRenderbuffers(1, &worldSceneDepthRenderbuffer_);
    if (worldSceneColorFbo_ == 0u ||
        worldSceneColorTexture_ == 0u ||
        worldSceneDepthRenderbuffer_ == 0u) {
        destroyWorldSceneColorResources();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, worldSceneColorTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // The captured pre-gamma target is a four-byte-per-pixel UNORM image.
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

    glBindRenderbuffer(GL_RENDERBUFFER, worldSceneDepthRenderbuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, worldSceneColorFbo_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        worldSceneColorTexture_,
        0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        worldSceneDepthRenderbuffer_);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroyWorldSceneColorResources();
        return false;
    }

    worldSceneColorWidth_ = width;
    worldSceneColorHeight_ = height;
    return true;
}

void OpenGLRenderBackend::destroyWorldSceneColorResources() {
    worldSceneColorPassActive_ = false;
    if (worldSceneColorFbo_ != 0u) glDeleteFramebuffers(1, &worldSceneColorFbo_);
    if (worldSceneColorTexture_ != 0u) glDeleteTextures(1, &worldSceneColorTexture_);
    if (worldSceneDepthRenderbuffer_ != 0u) {
        glDeleteRenderbuffers(1, &worldSceneDepthRenderbuffer_);
    }
    if (worldSceneColorPostVao_ != 0u) {
        glDeleteVertexArrays(1, &worldSceneColorPostVao_);
    }
    if (worldSceneColorPostProgram_ != 0u) {
        glDeleteProgram(worldSceneColorPostProgram_);
    }
    worldSceneColorFbo_ = 0u;
    worldSceneColorTexture_ = 0u;
    worldSceneDepthRenderbuffer_ = 0u;
    worldSceneColorPostProgram_ = 0u;
    worldSceneColorPostVao_ = 0u;
    worldSceneColorPostSamplerLoc_ = -1;
    worldSceneColorWidth_ = 0;
    worldSceneColorHeight_ = 0;
}

void OpenGLRenderBackend::beginWorldSceneColorPass(
    int surfaceWidth,
    int surfaceHeight) {
    if (worldSceneColorPassActive_) return;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &worldSceneColorPrevDrawFbo_);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &worldSceneColorPrevReadFbo_);
    glGetIntegerv(GL_VIEWPORT, worldSceneColorPrevViewport_.data());
    if (!ensureWorldSceneColorResources(surfaceWidth, surfaceHeight)) {
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(worldSceneColorPrevDrawFbo_));
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(worldSceneColorPrevReadFbo_));
        return;
    }

    worldSceneColorPassActive_ = true;
    glBindFramebuffer(GL_FRAMEBUFFER, worldSceneColorFbo_);
    glViewport(0, 0, worldSceneColorWidth_, worldSceneColorHeight_);
    glClearColor(
        frameClearColor_[0],
        frameClearColor_[1],
        frameClearColor_[2],
        frameClearColor_[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLRenderBackend::endWorldSceneColorPass() {
    if (!worldSceneColorPassActive_) return;

    GLint previousProgram = 0;
    GLint previousVao = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture0 = 0;
    GLboolean previousDepthMask = GL_TRUE;
    const bool depthEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    const bool blendEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
    const bool cullEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);

    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        static_cast<GLuint>(worldSceneColorPrevDrawFbo_));
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(worldSceneColorPrevReadFbo_));
    glViewport(
        worldSceneColorPrevViewport_[0],
        worldSceneColorPrevViewport_[1],
        worldSceneColorPrevViewport_[2],
        worldSceneColorPrevViewport_[3]);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glUseProgram(worldSceneColorPostProgram_);
    glBindVertexArray(worldSceneColorPostVao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, worldSceneColorTexture_);
    glUniform1i(worldSceneColorPostSamplerLoc_, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++frameDrawCalls_;
    ++frameTriangles_;

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glDepthMask(previousDepthMask);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

    worldSceneColorPassActive_ = false;
}
