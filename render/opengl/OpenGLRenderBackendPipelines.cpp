#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

#include <algorithm>
#include <glad/glad.h>

namespace {

constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

} // namespace
void OpenGLRenderBackend::ensureDebugPipeline() {
    if (debugProgram_ != 0 && debugVao_ != 0 && debugVbo_ != 0 && debugSurfaceSizeLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) {
        return;
    }

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec4 vColor;
        void main() {
            vec2 ndc;
            ndc.x = (aPos.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (aPos.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )GLSL";

    const unsigned int vs = opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    debugProgram_ = glCreateProgram();
    if (debugProgram_ == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    glAttachShader(debugProgram_, vs);
    glAttachShader(debugProgram_, fs);
    glLinkProgram(debugProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk = 0;
    glGetProgramiv(debugProgram_, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
        return;
    }

    debugSurfaceSizeLoc_ = glGetUniformLocation(debugProgram_, "uSurfaceSize");
    if (debugSurfaceSizeLoc_ < 0) {
        destroyDebugPipeline();
        return;
    }

    glGenVertexArrays(1, &debugVao_);
    glGenBuffers(1, &debugVbo_);
    if (debugVao_ == 0 || debugVbo_ == 0) {
        destroyDebugPipeline();
        return;
    }

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(float) * 6);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OpenGLRenderBackend::destroyDebugPipeline() {
    destroyCachedDebugGeometry();
    if (debugVbo_ != 0) {
        glDeleteBuffers(1, &debugVbo_);
        debugVbo_ = 0;
    }
    if (debugVao_ != 0) {
        glDeleteVertexArrays(1, &debugVao_);
        debugVao_ = 0;
    }
    if (debugProgram_ != 0) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
    }
    debugSurfaceSizeLoc_ = -1;
}

void OpenGLRenderBackend::destroyCachedDebugGeometry() {
    auto destroyCache = [](auto& cache) {
        for (auto& [_, geometry] : cache) {
            if (geometry.vertexBuffer != 0u) {
                glDeleteBuffers(1, &geometry.vertexBuffer);
                geometry.vertexBuffer = 0u;
            }
            if (geometry.vao != 0u) {
                glDeleteVertexArrays(1, &geometry.vao);
                geometry.vao = 0u;
            }
            geometry.valid = false;
        }
        cache.clear();
    };

    destroyCache(cachedDebugQuads_);
    destroyCache(cachedDebugLines_);
}

void OpenGLRenderBackend::ensureSpritePipeline() {
    if (spriteProgram_ != 0 && spriteVao_ != 0 && spriteVbo_ != 0 &&
        spriteSurfaceSizeLoc_ >= 0 && spriteSamplerLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec4 aRect;
        layout (location = 1) in vec4 aUvRect;
        layout (location = 2) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            const vec2 kCorners[6] = vec2[6](
                vec2(0.0, 0.0),
                vec2(1.0, 0.0),
                vec2(1.0, 1.0),
                vec2(0.0, 0.0),
                vec2(1.0, 1.0),
                vec2(0.0, 1.0)
            );
            vec2 corner = kCorners[gl_VertexID];
            vec2 posPx = aRect.xy + aRect.zw * corner;
            vec2 ndc;
            ndc.x = (posPx.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (posPx.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = mix(aUvRect.xy, aUvRect.zw, corner);
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        uniform sampler2D uTexture;
        out vec4 FragColor;
        void main() {
            FragColor = texture(uTexture, vUv) * vColor;
        }
    )GLSL";

    const unsigned int vs = opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }
    spriteProgram_ = opengl_backend_shader_utils::linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (spriteProgram_ == 0) return;

    spriteSurfaceSizeLoc_ = glGetUniformLocation(spriteProgram_, "uSurfaceSize");
    spriteSamplerLoc_ = glGetUniformLocation(spriteProgram_, "uTexture");
    if (spriteSurfaceSizeLoc_ < 0 || spriteSamplerLoc_ < 0) {
        destroySpritePipeline();
        return;
    }

    glGenVertexArrays(1, &spriteVao_);
    glGenBuffers(1, &spriteVbo_);
    if (spriteVao_ == 0 || spriteVbo_ == 0) {
        destroySpritePipeline();
        return;
    }

    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(2048u * sizeof(SpriteInstanceData)),
                 nullptr,
                 GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(SpriteInstanceData));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(0, 1);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 4));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 8));
    glVertexAttribDivisor(2, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (spriteFallbackTexture_ == 0) {
        static const unsigned char kFallbackRgba[16] = {
            72,  90, 108, 255,
            56,  70,  84, 255,
            56,  70,  84, 255,
            72,  90, 108, 255
        };
        glGenTextures(1, &spriteFallbackTexture_);
        if (spriteFallbackTexture_ != 0) {
            glBindTexture(GL_TEXTURE_2D, spriteFallbackTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         2,
                         2,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         kFallbackRgba);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        spriteTextures_[kFallbackSpriteTextureKey] = spriteFallbackTexture_;
    }
}

void OpenGLRenderBackend::destroySpritePipeline() {
    if (spriteVbo_ != 0) {
        glDeleteBuffers(1, &spriteVbo_);
        spriteVbo_ = 0;
    }
    if (spriteVao_ != 0) {
        glDeleteVertexArrays(1, &spriteVao_);
        spriteVao_ = 0;
    }
    if (spriteProgram_ != 0) {
        glDeleteProgram(spriteProgram_);
        spriteProgram_ = 0;
    }
    spriteSurfaceSizeLoc_ = -1;
    spriteSamplerLoc_ = -1;
}



