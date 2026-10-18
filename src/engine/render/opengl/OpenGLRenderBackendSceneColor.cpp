#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include "engine/render/ScreenFeedbackShaders.h"
#include "engine/render/SceneBloomShader.h"

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
    uniform float uBloomStrength;
    uniform float uSceneExposure;
    #define SCENE_EXPOSURE uSceneExposure
    #define SCENE_SIZE vec2(textureSize(uLinearSceneColor,0))
    #define SCENE_SAMPLE(uv) textureLod(uLinearSceneColor,uv,0.0)
    __PHLOSION_SCENE_BLOOM__

    vec3 linearToSrgb(vec3 linearColor) {
        vec3 c = clamp(linearColor, 0.0, 1.0);
        vec3 low = 12.9200001 * c;
        vec3 high =
            1.05499995 * pow(abs(c), vec3(0.416666657)) - 0.0549999997;
        return mix(high, low, lessThanEqual(c, vec3(0.00313080009)));
    }

    void main() {
        vec4 scene = texture(uLinearSceneColor, vUv);
        scene.rgb=sceneDisplayMap(sceneBloom(vUv,scene.rgb,uBloomStrength));
        // This is a completed view with its background already composited.
        // Material alpha must not fade the whole view again in the editor UI.
        FragColor = vec4(sceneDitherDisplay(linearToSrgb(scene.rgb),vUv), 1.0);
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
            GL_FRAGMENT_SHADER, engine::render::injectSceneBloom(kSceneColorFragmentShader).c_str());
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
        worldSceneBloomLoc_=glGetUniformLocation(worldSceneColorPostProgram_,"uBloomStrength");
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
    // Preserve world radiance until the completed view is mapped to display.
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        width,
        height,
        0,
        GL_RGBA,
        GL_HALF_FLOAT,
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

bool OpenGLRenderBackend::captureWorldDepthSnapshot() {
    if (!worldSceneColorPassActive_ || worldSceneColorFbo_ == 0) return false;
    GLint readFbo = 0, drawFbo = 0, boundTexture = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    if (!worldDepthSnapshotTexture_) glGenTextures(1, &worldDepthSnapshotTexture_);
    if (!worldDepthSnapshotFbo_) glGenFramebuffers(1, &worldDepthSnapshotFbo_);
    glBindTexture(GL_TEXTURE_2D, worldDepthSnapshotTexture_);
    if (worldDepthSnapshotWidth_ != worldSceneColorWidth_ || worldDepthSnapshotHeight_ != worldSceneColorHeight_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, worldSceneColorWidth_, worldSceneColorHeight_,
            0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        worldDepthSnapshotWidth_ = worldSceneColorWidth_;
        worldDepthSnapshotHeight_ = worldSceneColorHeight_;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, worldDepthSnapshotFbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, worldDepthSnapshotTexture_, 0);
    glDrawBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, worldSceneColorFbo_);
        glBlitFramebuffer(0, 0, worldSceneColorWidth_, worldSceneColorHeight_,
            0, 0, worldSceneColorWidth_, worldSceneColorHeight_, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundTexture));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
    return complete;
}

bool OpenGLRenderBackend::beginWorldLocalHaze(int width,int height) {
    if (!worldSceneColorPassActive_ || worldHazeActive_ || width<=0 || height<=0 || width>2048 || height>2048) return false;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&worldHazePreviousFbo_);
    int size=64;while(size<std::max(width,height))size*=2;
    if (!worldHazeTexture_)glGenTextures(1,&worldHazeTexture_);
    if (!worldHazeFbo_)glGenFramebuffers(1,&worldHazeFbo_);
    glBindTexture(GL_TEXTURE_2D,worldHazeTexture_);
    if (size!=worldHazeSize_) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,size,size,0,GL_RGBA,GL_HALF_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        worldHazeSize_=size;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,worldHazeFbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,worldHazeTexture_,0);
    const GLenum draw=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&draw);
    if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,worldHazePreviousFbo_);return false;
    }
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    worldHazeActive_=true;return true;
}
bool OpenGLRenderBackend::endWorldLocalHaze() {
    if(!worldHazeActive_)return false;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,worldHazePreviousFbo_);
    worldHazeActive_=false;return true;
}

bool OpenGLRenderBackend::beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView& view) {
    if (!worldSceneColorPassActive_ || worldDirectionalShadowActive_ || worldPlanarReflection_[4][3] < 0 ||
        view.width <= 0 || view.heightPixels <= 0 || view.width > worldSceneColorWidth_ || view.heightPixels > worldSceneColorHeight_)
        return false;
    worldPlanarReflectionView_=view;
    worldPlanarReflection_=engine::render::packWorldPlanarReflection(view);
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    return true;
}

bool OpenGLRenderBackend::endWorldPlanarReflection() {
    if (!worldSceneColorPassActive_ || worldPlanarReflection_[4][3] >= 0) return false;
    const auto& view=worldPlanarReflectionView_;
    GLint bound=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    if (!worldPlanarTexture_) glGenTextures(1,&worldPlanarTexture_);
    glBindTexture(GL_TEXTURE_2D,worldPlanarTexture_);
    if (worldPlanarWidth_!=view.width || worldPlanarHeight_!=view.heightPixels) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,view.width,view.heightPixels,0,GL_RGBA,GL_HALF_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        worldPlanarWidth_=view.width; worldPlanarHeight_=view.heightPixels;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,view.width,view.heightPixels);
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(bound));
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    worldPlanarReflection_[4][3]=1;
    return true;
}

void OpenGLRenderBackend::destroyWorldSceneColorResources() {
    if(worldHazeFbo_)glDeleteFramebuffers(1,&worldHazeFbo_);
    if(worldHazeTexture_)glDeleteTextures(1,&worldHazeTexture_);
    worldHazeFbo_=worldHazeTexture_=0;worldHazeSize_=0;worldHazeActive_=false;
    if (worldPlanarTexture_) glDeleteTextures(1, &worldPlanarTexture_);
    worldPlanarTexture_=0; worldPlanarWidth_=worldPlanarHeight_=0; worldPlanarReflection_={};
    if (worldDepthSnapshotFbo_) glDeleteFramebuffers(1, &worldDepthSnapshotFbo_);
    if (worldDepthSnapshotTexture_) glDeleteTextures(1, &worldDepthSnapshotTexture_);
    worldDepthSnapshotFbo_ = worldDepthSnapshotTexture_ = 0;
    worldDepthSnapshotWidth_ = worldDepthSnapshotHeight_ = 0;
    destroyScreenFeedback();
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
    worldBloomStrength_=0;
    worldSceneRadianceExposure_=0;
    worldDiffuseProbes_={};
    worldPlanarReflection_={};

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

bool OpenGLRenderBackend::clearWorldDepth() {
    if (worldDirectionalShadowActive_ || !worldSceneColorPassActive_) return false;
    GLboolean mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    GLdouble previousClear = 1.0;
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &previousClear);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(previousClear);
    glDepthMask(mask);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return true;
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
    glUniform1f(worldSceneBloomLoc_,worldBloomStrength_);
    glUniform1f(glGetUniformLocation(worldSceneColorPostProgram_,"uSceneExposure"),worldSceneRadianceExposure_);
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

void OpenGLRenderBackend::destroyScreenFeedback() {
    glDeleteTextures(2,feedbackTextures_.data()); glDeleteFramebuffers(2,feedbackFbos_.data());
    if(feedbackMask_) glDeleteTextures(1,&feedbackMask_);
    if(feedbackVao_) glDeleteVertexArrays(1,&feedbackVao_);
    if(feedbackProgram_) glDeleteProgram(feedbackProgram_);
    feedbackTextures_={}; feedbackFbos_={}; feedbackMask_=feedbackVao_=feedbackProgram_=0;
    feedbackSequence_=0; feedbackWidth_=feedbackHeight_=0; feedbackLast_={};
}

void OpenGLRenderBackend::endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame& frame) {
    using namespace engine::render;
    validateScreenFeedback(frame);
    if(!worldSceneColorPassActive_) throw std::runtime_error("Screen feedback requires an active world pass");
    endWorldSceneColorPass();
    GLint oldFbo=0,oldRead=0,oldProgram=0,oldVao=0,oldActive=0,oldUnpack=0;
    std::array<GLboolean,4> oldColorMask{};
    std::array<GLint,4> oldPixelStore{};
    constexpr std::array<GLenum,4> pixelStore={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS};
    glGetBooleanv(GL_COLOR_WRITEMASK,oldColorMask.data());
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&oldUnpack);
    for(std::size_t i=0;i<pixelStore.size();++i)glGetIntegerv(pixelStore[i],&oldPixelStore[i]);
    std::array<GLint,4> oldViewport{}; std::array<GLint,3> oldTextures{},oldSamplers{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldFbo); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive); glGetIntegerv(GL_VIEWPORT,oldViewport.data());
    const bool depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB),cull=glIsEnabled(GL_CULL_FACE),stencil=glIsEnabled(GL_STENCIL_TEST);
    for(int i=0;i<3;++i) {glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTextures[i]);glGetIntegeri_v(GL_SAMPLER_BINDING,i,&oldSamplers[i]);glBindSampler(i,0);}
    auto restore=[&] {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);
        glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);glUseProgram(oldProgram);glBindVertexArray(oldVao);
        for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,oldTextures[i]);glBindSampler(i,oldSamplers[i]);}
        glActiveTexture(oldActive);
        glColorMask(oldColorMask[0],oldColorMask[1],oldColorMask[2],oldColorMask[3]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,oldUnpack);
        for(std::size_t i=0;i<pixelStore.size();++i)glPixelStorei(pixelStore[i],oldPixelStore[i]);
        for(auto item : {std::pair<GLenum,bool>{GL_DEPTH_TEST,depth},{GL_BLEND,blend},{GL_SCISSOR_TEST,scissor},{GL_FRAMEBUFFER_SRGB,srgb},{GL_CULL_FACE,cull},{GL_STENCIL_TEST,stencil}})
            if(item.second)glEnable(item.first);else glDisable(item.first);
    };
    try {
        if(feedbackProgram_ && (feedbackWidth_!=frame.width || feedbackHeight_!=frame.height))
            throw std::runtime_error("Screen feedback dimensions must remain fixed for a renderer session");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(auto parameter:pixelStore)glPixelStorei(parameter,parameter==GL_UNPACK_ALIGNMENT?1:0);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_STENCIL_TEST);
        if(!feedbackProgram_) {
            const auto vs=opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER,screenFeedbackGlVs);
            const auto fs=opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER,injectSceneBloom(screenFeedbackGlFs).c_str());
            if(vs && fs) feedbackProgram_=opengl_backend_shader_utils::linkProgram(vs,fs);
            if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);
            if(!feedbackProgram_)throw std::runtime_error("Could not compile screen feedback shaders");
            glGenVertexArrays(1,&feedbackVao_);glGenTextures(2,feedbackTextures_.data());glGenFramebuffers(2,feedbackFbos_.data());
            for(int i=0;i<2;++i) {
                glBindTexture(GL_TEXTURE_2D,feedbackTextures_[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame.width,frame.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                glBindFramebuffer(GL_FRAMEBUFFER,feedbackFbos_[i]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,feedbackTextures_[i],0);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Invalid screen feedback framebuffer");
            }
            glGenTextures(1,&feedbackMask_);feedbackWidth_=frame.width;feedbackHeight_=frame.height;
        }
        if(frame.seed) {
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,feedbackMask_);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame.maskWidth,frame.maskHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,frame.maskRgba);
            feedbackSequence_=frame.sequence;feedbackHistory_=0;feedbackDisplayInput_=0;feedbackLast_={};feedbackLast_.count=1;feedbackLast_.passes[0].rectangle={0,0,float(frame.width),float(frame.height)};
        } else if(feedbackSequence_!=frame.sequence) throw std::runtime_error("Screen feedback must be seeded before playback");
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_CULL_FACE);
        glUseProgram(feedbackProgram_);glBindVertexArray(feedbackVao_);
        glUniform4f(glGetUniformLocation(feedbackProgram_,"scenePost"),worldBloomStrength_,worldSceneRadianceExposure_,0,0);
        glUniform1i(glGetUniformLocation(feedbackProgram_,"sceneImage"),0);
        glUniform1i(glGetUniformLocation(feedbackProgram_,"historyImage"),1);
        glUniform1i(glGetUniformLocation(feedbackProgram_,"maskImage"),2);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,worldSceneColorTexture_);
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,feedbackMask_);
        auto draw=[&](int mode,int input,GLuint target,int width,int height,const ScreenFeedbackUpdate& update) {
            glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(0,0,width,height);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,feedbackTextures_[input]);
            glUniform4f(glGetUniformLocation(feedbackProgram_,"control"),float(frame.width),float(frame.height),float(mode),float(update.count));
            glUniform4fv(glGetUniformLocation(feedbackProgram_,"passes"),12,update.passes[0].rectangle.data());
            glDrawArrays(GL_TRIANGLES,0,3);
        };
        if(frame.seed) draw(0,1,feedbackFbos_[0],frame.width,frame.height,{});
        for(const auto& update : frame.updates) {
            feedbackDisplayInput_=feedbackHistory_;feedbackHistory_=1-feedbackHistory_;feedbackLast_=update;
            draw(1,feedbackDisplayInput_,feedbackFbos_[feedbackHistory_],frame.width,frame.height,update);
        }
        if(!frame.seed || !frame.updates.empty()) draw(2,feedbackDisplayInput_,oldFbo,oldViewport[2],oldViewport[3],feedbackLast_);
    } catch(...) {restore();throw;}
    restore();
}
