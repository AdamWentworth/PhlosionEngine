#include "engine/render/OpenGLRenderBackend.h"
#include <stdexcept>
#include <glad/glad.h>
#include "engine/render/ShadowMomentShader.h"
#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

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
    if (worldLocalShadows_.activeLight >= 0 && worldDirectionalShadowView_.distanceMoments[3] > 0)
        filterWorldShadowMoments();
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
    glDeleteFramebuffers(2,worldMomentFramebuffers_.data());
    glDeleteTextures(2,worldMomentTextures_.data());
    worldMomentFramebuffers_={}; worldMomentTextures_={};
    if (worldMomentProgram_) glDeleteProgram(worldMomentProgram_);
    if (worldMomentVao_) glDeleteVertexArrays(1,&worldMomentVao_);
    worldMomentProgram_=worldMomentVao_=0;
}

void OpenGLRenderBackend::prewarmWorldLocalShadows() {
    if (worldDirectionalShadowActive_) throw std::runtime_error("Cannot prepare local shadows during a shadow pass.");
    worldLocalShadows_.enabled = true;
    prewarmWorldDirectionalShadow(engine::render::localShadowAtlasResolution);
    prewarmWorldShadowMoments();
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

void OpenGLRenderBackend::prewarmWorldShadowMoments() {
    if(worldMomentProgram_) return;
    GLint draw=0,read=0,texture=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    const auto vs=opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER,R"(#version 330 core
void main() { gl_Position=vec4(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0,0.0,1.0); }
)");
    const auto fs=opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER,engine::render::shadowMomentShader(false).c_str());
    worldMomentProgram_=opengl_backend_shader_utils::linkProgram(vs,fs);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!worldMomentProgram_) throw std::runtime_error("Cannot compile shadow moment filter");
    glGenVertexArrays(1,&worldMomentVao_);
    glGenTextures(2,worldMomentTextures_.data()); glGenFramebuffers(2,worldMomentFramebuffers_.data());
    for(int i=0;i<2;++i) {
        glBindTexture(GL_TEXTURE_2D,worldMomentTextures_[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG16,i?2048:1024,i?4096:1024,0,GL_RG,GL_UNSIGNED_SHORT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER,worldMomentFramebuffers_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,worldMomentTextures_[i],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete shadow moment target");
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));
    glBindTexture(GL_TEXTURE_2D,GLuint(texture));
}

void OpenGLRenderBackend::filterWorldShadowMoments() {
    GLint program=0,vao=0,active=0,texture=0,sampler=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
    const bool blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB),dither=glIsEnabled(GL_DITHER);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_SCISSOR_TEST);
    glUseProgram(worldMomentProgram_);glBindVertexArray(worldMomentVao_);glBindSampler(0,0);
    glUniform1i(glGetUniformLocation(worldMomentProgram_,"momentInput"),0);
    for(int pass=0;pass<2;++pass) {
        const auto data=engine::render::shadowMomentConstants(worldDirectionalShadowView_,worldLocalShadows_,pass!=0);
        glBindFramebuffer(GL_FRAMEBUFFER,worldMomentFramebuffers_[pass]);
        glViewport(GLint(data[6]),GLint(data[7]),1024,1024);glScissor(GLint(data[6]),GLint(data[7]),1024,1024);
        glBindTexture(GL_TEXTURE_2D,pass?worldMomentTextures_[0]:worldDirectionalShadowTexture_);
        glUniform4fv(glGetUniformLocation(worldMomentProgram_,"momentProjection"),1,data.data());
        glUniform4fv(glGetUniformLocation(worldMomentProgram_,"momentOffsets"),1,data.data()+4);
        glUniform4fv(glGetUniformLocation(worldMomentProgram_,"momentPass"),1,data.data()+8);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));
    glBindTexture(GL_TEXTURE_2D,GLuint(texture));glBindSampler(0,GLuint(sampler));glActiveTexture(GLenum(active));
    if(blend)glEnable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);if(dither)glEnable(GL_DITHER);
    // The caller restores framebuffer, viewport, masks and depth/scissor state.
}
