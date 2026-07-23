// src/engine/vfx/ParticleSystem.cpp
#include "ParticleSystem.h"

#include "engine/utils/Shader.h"
#include "engine/utils/ShaderCache.h"
#include "engine/render/Camera3D.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <glad/glad.h>

// stb_image implementation is compiled in src/engine/utils/stb_image_impl.cpp.
#include <stb_image.h>

static GLuint create1x1WhiteTextureRGBA() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char white[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

    return tex;
}

// If path is empty or load fails, returns 1x1 white.
static unsigned int loadTextureRGBAOrWhite(const std::string& path) {
    if (path.empty()) {
        return create1x1WhiteTextureRGBA();
    }

    int w = 0, h = 0, comp = 0;

    // Keep consistent with your current engine expectations.
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    stbi_set_flip_vertically_on_load(false);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return create1x1WhiteTextureRGBA();
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    return tex;
}

ParticleSystem::~ParticleSystem() {
    shutdown();
}

void ParticleSystem::deleteShaderCache(ShaderCache* p) {
    delete p;
}

void ParticleSystem::setShaderCache(ShaderCache& cache) {
    shaderCache = &cache;
    shaderDirty = true;
}

void ParticleSystem::ensureFlipbookLoaded() {
    if (!flipbookDirty && flipbookTex != 0) return;

    if (flipbookTex) glDeleteTextures(1, &flipbookTex);
    flipbookTex = loadTextureRGBAOrWhite(flipbookPath);
    flipbookDirty = false;
}

void ParticleSystem::ensureSecondaryFlipbookLoaded() {
    if (!useSecondaryFlipbook) return;
    if (!flipbookDirty2 && flipbookTex2 != 0) return;

    if (flipbookTex2) glDeleteTextures(1, &flipbookTex2);
    flipbookTex2 = loadTextureRGBAOrWhite(flipbookPath2);
    flipbookDirty2 = false;
}

void ParticleSystem::ensureShaderLoaded() {
    if (!shaderDirty && shader) return;

    ShaderCache* cache = shaderCache;
    if (!cache) {
        if (!internalShaderCache) internalShaderCache.reset(new ShaderCache());
        cache = internalShaderCache.get();
    }

    shader = cache->get(shaderVertPath, shaderFragPath);
    shaderDirty = false;
}

void ParticleSystem::init() {
    if (initialized) return;

    ensureShaderLoaded();

    if (useFlipbook) ensureFlipbookLoaded();
    if (useFlipbook && useSecondaryFlipbook) ensureSecondaryFlipbookLoaded();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(GPUParticle), nullptr, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, age01));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, sizePx));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, seed));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    initialized = true;
}

void ParticleSystem::shutdown() {
    if (!initialized) return;

    if (flipbookTex) glDeleteTextures(1, &flipbookTex);
    flipbookTex = 0;

    if (flipbookTex2) glDeleteTextures(1, &flipbookTex2);
    flipbookTex2 = 0;

    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);

    vbo = 0;
    vao = 0;

    particles.clear();
    gpuBuffer.clear();
    updateAccumulatorSec = 0.0f;

    initialized = false;
}

void ParticleSystem::emit(const Particle& p) {
    particles.push_back(p);
}

void ParticleSystem::update(float dt) {
    if (!initialized) {
        SDL_GLContext currentGlContext = SDL_GL_GetCurrentContext();
        if (currentGlContext != nullptr) {
            init();
        }
    }

    dt = std::clamp(dt, 0.0f, 0.05f);
    timeSec += dt;

    if (particles.empty()) return;

    constexpr float kUpdateStepSec = 1.0f / 120.0f;
    constexpr int kMaxUpdateStepsPerFrame = 8;
    updateAccumulatorSec += dt;

    auto simulateStep = [&](float stepDt) {
        const glm::vec3 baseAccelDt = updateSettings.acceleration * stepDt;
        const float damp = std::pow(updateSettings.dampingBase, stepDt);

        // Update + compact in one pass to avoid a second remove_if walk.
        std::size_t writeIndex = 0u;
        for (std::size_t readIndex = 0u; readIndex < particles.size(); ++readIndex) {
            Particle p = particles[readIndex];
            p.vel += baseAccelDt + (p.accel * stepDt);
            p.vel *= damp;
            p.pos += p.vel * stepDt;
            p.lifeSec -= stepDt;
            if (p.lifeSec <= 0.0f) continue;
            if (writeIndex != readIndex) {
                particles[writeIndex] = p;
            } else {
                particles[writeIndex] = std::move(p);
            }
            ++writeIndex;
        }
        particles.resize(writeIndex);
    };

    int steps = 0;
    while (updateAccumulatorSec >= kUpdateStepSec && steps < kMaxUpdateStepsPerFrame) {
        simulateStep(kUpdateStepSec);
        updateAccumulatorSec -= kUpdateStepSec;
        ++steps;
    }

    if (steps == 0) {
        // Keep small systems responsive even at very high FPS where dt < fixed step.
        if (particles.size() <= 32u && updateAccumulatorSec > 0.0f) {
            simulateStep(updateAccumulatorSec);
            updateAccumulatorSec = 0.0f;
        }
    }

    // Bound accumulated debt after hitches; visual simulation can recover gradually.
    const float maxAccumulated = kUpdateStepSec * static_cast<float>(kMaxUpdateStepsPerFrame);
    if (updateAccumulatorSec > maxAccumulated) {
        updateAccumulatorSec = maxAccumulated;
    }
}

static void applyBlendMode(ParticleSystem::BlendMode mode) {
    glEnable(GL_BLEND);

    switch (mode) {
        case ParticleSystem::BlendMode::Alpha:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case ParticleSystem::BlendMode::Additive:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case ParticleSystem::BlendMode::Premultiplied:
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void ParticleSystem::render(const Camera3D& camera) {
    if (!initialized) init();
    ensureShaderLoaded();

    if (!shader || vao == 0) return;
    if (particles.empty()) return;

    if (useFlipbook) {
        ensureFlipbookLoaded();
        if (flipbookTex == 0) return;
    }

    if (useFlipbook && useSecondaryFlipbook) {
        ensureSecondaryFlipbookLoaded();
    }

    gpuBuffer.resize(particles.size());
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];

        float age01 = 1.0f - (p.lifeSec / std::max(0.0001f, p.maxLifeSec));
        age01 = std::clamp(age01, 0.0f, 1.0f);

        gpuBuffer[i] = GPUParticle{ p.pos, age01, p.sizePx, p.seed };
    }

    // Save GL state
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasProgPoint = glIsEnabled(GL_PROGRAM_POINT_SIZE);

    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0, prevBlendSrcA = 0, prevBlendDstA = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    GLint prevBlendEqRGB = 0, prevBlendEqA = 0;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqA);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    if (renderSettings.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(renderSettings.depthWrite ? GL_TRUE : GL_FALSE);

    if (renderSettings.programPointSize) glEnable(GL_PROGRAM_POINT_SIZE); else glDisable(GL_PROGRAM_POINT_SIZE);

    applyBlendMode(renderSettings.blend);

    shader->use();

    // IMPORTANT: your particle vertex shader expects u_ViewProj, not u_View/u_Proj.
    shader->setUniform("u_ViewProj", camera.getProjectionMatrix() * camera.getViewMatrix());
    shader->setUniform("u_Time", timeSec);
    shader->setUniform("u_PointScale", pointScale);

    // Flipbook 1
    if (useFlipbook) {
        shader->setUniform("u_UseFlipbook", 1);
        shader->setUniform("u_FlipbookGrid", glm::vec2((float)flipbookCols, (float)flipbookRows));
        shader->setUniform("u_FrameCount", (float)flipbookFrames);
        shader->setUniform("u_Fps", flipbookFps);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, flipbookTex);
        shader->setUniform("u_Flipbook", 0);
    } else {
        shader->setUniform("u_UseFlipbook", 0);
    }

    // Flipbook 2 (optional) - required by fire_tail.frag when enabled.
    if (useFlipbook) {
        int has2 = (useSecondaryFlipbook && flipbookTex2 != 0) ? 1 : 0;
        {
            GLint locHas2 = glGetUniformLocation(shader->getID(), "u_HasFlipbook2");
            if (locHas2 != -1) glUniform1i(locHas2, has2);
        }

        if (has2) {
            GLint locSampler2 = glGetUniformLocation(shader->getID(), "u_Flipbook2");
            if (locSampler2 != -1) {
                glUniform1i(locSampler2, 1);

                GLint locGrid2 = glGetUniformLocation(shader->getID(), "u_FlipbookGrid2");
                if (locGrid2 != -1) glUniform2f(locGrid2, (float)flipbookCols2, (float)flipbookRows2);

                GLint locFrames2 = glGetUniformLocation(shader->getID(), "u_FrameCount2");
                if (locFrames2 != -1) glUniform1f(locFrames2, (float)flipbookFrames2);

                GLint locFps2 = glGetUniformLocation(shader->getID(), "u_Fps2");
                if (locFps2 != -1) glUniform1f(locFps2, flipbookFps2);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, flipbookTex2);

                // Restore active texture to 0 for safety.
                glActiveTexture(GL_TEXTURE0);
            }
        }
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(gpuBuffer.size() * sizeof(GPUParticle)),
                 gpuBuffer.data(),
                 GL_STREAM_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)gpuBuffer.size());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Restore GL state
    glDepthMask(prevDepthMask);

    if (!wasDepth) glDisable(GL_DEPTH_TEST);
    if (!wasBlend) glDisable(GL_BLEND);

    glBlendEquationSeparate(prevBlendEqRGB, prevBlendEqA);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA);

    if (!wasProgPoint) glDisable(GL_PROGRAM_POINT_SIZE);
}

bool ParticleSystem::buildRenderSnapshot(RenderSnapshot& out) const {
    out.particles = particles;
    out.renderSettings = renderSettings;
    out.pointScale = pointScale;
    out.timeSec = timeSec;
    out.shaderVertPath = shaderVertPath;
    out.shaderFragPath = shaderFragPath;
    out.useFlipbook = useFlipbook;
    out.flipbookPath = flipbookPath;
    out.flipbookCols = flipbookCols;
    out.flipbookRows = flipbookRows;
    out.flipbookFrames = flipbookFrames;
    out.flipbookFps = flipbookFps;
    out.useSecondaryFlipbook = useSecondaryFlipbook;
    out.flipbookPath2 = flipbookPath2;
    out.flipbookCols2 = flipbookCols2;
    out.flipbookRows2 = flipbookRows2;
    out.flipbookFrames2 = flipbookFrames2;
    out.flipbookFps2 = flipbookFps2;
    return !out.particles.empty();
}
