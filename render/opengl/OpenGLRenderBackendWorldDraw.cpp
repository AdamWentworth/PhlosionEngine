#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/WorldBlendPolicy.h"
#include "engine/core/Environment.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <string>
#include <vector>

#include <glad/glad.h>

namespace {

constexpr GLuint kWorldSkinBlockBinding = 0u;

float resolveVertexChannel(float channel, float fallback) {
    return (channel >= 0.0f) ? channel : fallback;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool supportsDualSourceBlendOpenGL() {
    static const bool supported = [] {
        if (GLAD_GL_VERSION_4_0) return true;
        if (glad_glGetStringi == nullptr) return false;
        GLint extensionCount = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
        for (GLint i = 0; i < extensionCount; ++i) {
            const GLubyte* ext = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (!ext) continue;
            if (std::string_view(reinterpret_cast<const char*>(ext)) ==
                "GL_ARB_blend_func_extended") {
                return true;
            }
        }
        return false;
    }();
    return supported;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

void maybeLogPbrBindingOpenGL(const IRenderBackend::WorldTextureData* texture,
                              bool hasBase,
                              bool hasNormal,
                              bool hasMetallicRoughness,
                              bool hasOcclusion,
                              bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][OpenGL] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

struct OpenGLWorldInstanceVertexData {
    float model[16];
    float color[4];
};

static_assert(sizeof(OpenGLWorldInstanceVertexData) == sizeof(float) * 20u);

void packWorldInstanceVertexData(const IRenderBackend::WorldMeshInstance& instance,
                                 OpenGLWorldInstanceVertexData& out) {
    for (std::size_t i = 0; i < 16u; ++i) {
        out.model[i] = instance.modelMatrix[i];
    }
    out.color[0] = instance.vertexColorMulR;
    out.color[1] = instance.vertexColorMulG;
    out.color[2] = instance.vertexColorMulB;
    out.color[3] = instance.vertexColorMulA;
}

OpenGLWorldInstanceVertexData makeIdentityInstanceData() {
    OpenGLWorldInstanceVertexData out{};
    out.model[0] = 1.0f;
    out.model[5] = 1.0f;
    out.model[10] = 1.0f;
    out.model[15] = 1.0f;
    out.color[0] = 1.0f;
    out.color[1] = 1.0f;
    out.color[2] = 1.0f;
    out.color[3] = 1.0f;
    return out;
}

} // namespace

void OpenGLRenderBackend::prewarmWorldIndexedMeshInstances(std::size_t instanceCount) {
    ensureWorldPipeline();
    if (worldInstanceVbo_ == 0u) return;

    const std::size_t safeInstanceCount = std::max<std::size_t>(instanceCount, 1u);
    const std::size_t requiredBytes = safeInstanceCount * sizeof(OpenGLWorldInstanceVertexData);
    if (requiredBytes <= worldInstanceBufferBytes_) return;

    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, worldInstanceVbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(requiredBytes),
        nullptr,
        GL_STREAM_DRAW);
    worldInstanceBufferBytes_ = requiredBytes;
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
}

void OpenGLRenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                             std::size_t triangleCount,
                                             const float* viewProjectionMatrix4x4,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    constexpr std::size_t kMaxWorldTriangles = 180000;
    const std::size_t safeCount = std::min(triangleCount, kMaxWorldTriangles);
    if (safeCount == 0) return;

    std::vector<WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(safeCount * 3u);
    indices.reserve(safeCount * 3u);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& tri = triangles[i];
        const WorldMeshVertex v0{
            tri.x1, tri.y1, tri.z1,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r1, tri.r),
            resolveVertexChannel(tri.g1, tri.g),
            resolveVertexChannel(tri.b1, tri.b),
            resolveVertexChannel(tri.a1, tri.a)};
        const WorldMeshVertex v1{
            tri.x2, tri.y2, tri.z2,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r2, tri.r),
            resolveVertexChannel(tri.g2, tri.g),
            resolveVertexChannel(tri.b2, tri.b),
            resolveVertexChannel(tri.a2, tri.a)};
        const WorldMeshVertex v2{
            tri.x3, tri.y3, tri.z3,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r3, tri.r),
            resolveVertexChannel(tri.g3, tri.g),
            resolveVertexChannel(tri.b3, tri.b),
            resolveVertexChannel(tri.a3, tri.a)};
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
    }
    if (vertices.empty() || indices.empty()) return;

    drawWorldIndexedMeshTextured(vertices.data(),
                                 vertices.size(),
                                 indices.data(),
                                 indices.size(),
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount,
                                               const float* viewProjectionMatrix4x4,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    drawWorldIndexedMeshTextured(vertices,
                                 vertexCount,
                                 indices,
                                 indexCount,
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                       std::size_t vertexCount,
                                                       const std::uint32_t* indices,
                                                       std::size_t indexCount,
                                                       const WorldTextureData* texture,
                                                       const float* viewProjectionMatrix4x4,
                                                       int surfaceWidth,
                                                       int surfaceHeight) {
    drawWorldIndexedMeshTexturedInternal(worldVao_,
                                         worldVbo_,
                                         worldIbo_,
                                         vertices,
                                         vertexCount,
                                         indices,
                                         indexCount,
                                         true,
                                         texture,
                                         nullptr,
                                         0u,
                                         viewProjectionMatrix4x4,
                                         surfaceWidth,
                                         surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedInternal(unsigned int vao,
                                                               unsigned int vertexBuffer,
                                                               unsigned int indexBuffer,
                                                               const WorldMeshVertex* vertices,
                                                               std::size_t vertexCount,
                                                               const std::uint32_t* indices,
                                                               std::size_t indexCount,
                                                               bool uploadGeometry,
                                                               const WorldTextureData* texture,
                                                               const WorldMeshInstance* instances,
                                                               std::size_t instanceCount,
                                                               const float* viewProjectionMatrix4x4,
                                                               int surfaceWidth,
                                                               int surfaceHeight) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureWorldPipeline();
    if (worldProgram_ == 0 || vao == 0u || vertexBuffer == 0u || indexBuffer == 0u || worldInstanceVbo_ == 0u ||
        worldSkinUbo_ == 0u ||
        worldViewProjLoc_ < 0 || worldModelLoc_ < 0 ||
        worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldVertexColorMulLoc_ < 0 ||
        worldDualSourceBlendEnabledLoc_ < 0 ||
        worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldAlphaWindowMinLoc_ < 0 || worldAlphaWindowMaxLoc_ < 0 ||
        worldCameraPosLoc_ < 0 || worldCameraForwardLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldSkinningEnabledLoc_ < 0 || worldSkinningModeLoc_ < 0 || worldSkinMatrixCountLoc_ < 0) {
        return;
    }

    constexpr std::size_t kMaxWorldVertices = 540000;
    constexpr std::size_t kMaxWorldIndices = 900000;
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount == 0 || safeIndexCount < 3) return;

    for (std::size_t i = 0; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return;
    }

    const std::uint8_t alphaMode =
        texture ? std::min<std::uint8_t>(2u, texture->alphaMode) : 0u;
    const auto blendState = engine::render::world_blend::resolve(
        alphaMode,
        texture ? texture->blendMode : 0u,
        !texture || texture->depthTestEnabled != 0u,
        texture && texture->dualSourceBlendEnabled != 0u,
        supportsDualSourceBlendOpenGL());
    const std::uint8_t blendMode = static_cast<std::uint8_t>(blendState.mode);
    const bool dualSourceBlendEnabled = blendState.dualSourceEnabled;
    const bool depthTestEnabled = blendState.depthTestEnabled;
    const float alphaCutoff = texture ? std::clamp(texture->alphaCutoff, 0.0f, 1.0f) : 0.5f;
    const std::uint8_t materialMode = texture ? texture->materialMode : 0u;
    const float vertexColorMulR = texture ? texture->vertexColorMulR : 1.0f;
    const float vertexColorMulG = texture ? texture->vertexColorMulG : 1.0f;
    const float vertexColorMulB = texture ? texture->vertexColorMulB : 1.0f;
    const float vertexColorMulA = texture ? texture->vertexColorMulA : 1.0f;
    const GLuint worldTexture = ensureWorldTexture(texture);
    const bool hasTexture = (worldTexture != 0u);
    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    if (worldFallbackTexture_ == 0u) {
        worldFallbackTexture_ = ensureWorldTextureRaw(
            "__world_fallback_white_srgb_1x1__",
            kFallbackWhiteRgba,
            1,
            1,
            33071,
            33071,
            /*srgb=*/true);
    }
    if (worldFallbackLinearTexture_ == 0u) {
        worldFallbackLinearTexture_ = ensureWorldTextureRaw(
            "__world_fallback_white_linear_1x1__",
            kFallbackWhiteRgba,
            1,
            1,
            33071,
            33071,
            /*srgb=*/false);
    }
    if (worldFallbackFlatNormalTexture_ == 0u) {
        worldFallbackFlatNormalTexture_ = ensureWorldTextureRaw(
            "__world_fallback_flat_normal_1x1__",
            kFallbackFlatNormalRgba,
            1,
            1,
            33071,
            33071,
            /*srgb=*/false);
    }
    const GLuint fallbackWhiteSrgbTexture = worldFallbackTexture_;
    const GLuint fallbackWhiteLinearTexture = worldFallbackLinearTexture_;
    const GLuint fallbackFlatNormalTexture = worldFallbackFlatNormalTexture_;
    const GLuint boundTexture = hasTexture ? worldTexture : fallbackWhiteSrgbTexture;
    const float useTexture = hasTexture ? 1.0f : 0.0f;
    const GLuint normalTexture = texture
        ? ensureWorldTextureRaw(
            texture->normalKey,
            texture->normalCacheKey,
            texture->normalRgba,
            texture->normalWidth,
            texture->normalHeight,
            texture->normalWrapS,
            texture->normalWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasNormalTexture = (normalTexture != 0u);
    const GLuint boundNormalTexture = hasNormalTexture ? normalTexture : fallbackFlatNormalTexture;
    const GLuint metallicRoughnessTexture = texture
        ? ensureWorldTextureRaw(
            texture->metallicRoughnessKey,
            texture->metallicRoughnessCacheKey,
            texture->metallicRoughnessRgba,
            texture->metallicRoughnessWidth,
            texture->metallicRoughnessHeight,
            texture->metallicRoughnessWrapS,
            texture->metallicRoughnessWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasMetallicRoughnessTexture = (metallicRoughnessTexture != 0u);
    const GLuint boundMetallicRoughnessTexture =
        hasMetallicRoughnessTexture ? metallicRoughnessTexture : fallbackWhiteLinearTexture;
    const GLuint occlusionTexture = texture
        ? ensureWorldTextureRaw(
            texture->occlusionKey,
            texture->occlusionCacheKey,
            texture->occlusionRgba,
            texture->occlusionWidth,
            texture->occlusionHeight,
            texture->occlusionWrapS,
            texture->occlusionWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasOcclusionTexture = (occlusionTexture != 0u);
    const GLuint boundOcclusionTexture = hasOcclusionTexture ? occlusionTexture : fallbackWhiteLinearTexture;
    const GLuint emissiveTexture = texture
        ? ensureWorldTextureRaw(
            texture->emissiveKey,
            texture->emissiveCacheKey,
            texture->emissiveRgba,
            texture->emissiveWidth,
            texture->emissiveHeight,
            texture->emissiveWrapS,
            texture->emissiveWrapT,
            /*srgb=*/true)
        : 0u;
    const bool hasEmissiveTexture = (emissiveTexture != 0u);
    const GLuint boundEmissiveTexture = hasEmissiveTexture ? emissiveTexture : fallbackWhiteSrgbTexture;
    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    if (worldNeutralPmremTexture_ == 0u) {
        worldNeutralPmremTexture_ = !neutralPmremAtlas.rgba16f.empty()
            ? ensureWorldTextureRawHalfFloat(
                "__neutral_room_pmrem_rgba16f_v2__",
                neutralPmremAtlas.rgba16f.data(),
                neutralPmremAtlas.width,
                neutralPmremAtlas.height,
                33071,
                33071)
            : ensureWorldTextureRaw(
                "__neutral_room_pmrem_rgbm_v2__",
                neutralPmremAtlas.rgba.data(),
                neutralPmremAtlas.width,
                neutralPmremAtlas.height,
                33071,
                33071,
                /*srgb=*/false);
    }
    const GLuint neutralPmremTexture = worldNeutralPmremTexture_;
    const GLuint boundEnvTexture = (neutralPmremTexture != 0u) ? neutralPmremTexture : fallbackWhiteLinearTexture;
    const GLfloat wrapS = static_cast<GLfloat>(texture ? texture->wrapS : 10497);
    const GLfloat wrapT = static_cast<GLfloat>(texture ? texture->wrapT : 10497);
    const bool blendAlpha = blendState.enabled;

    maybeLogPbrBindingOpenGL(
        texture,
        hasTexture,
        hasNormalTexture,
        hasMetallicRoughnessTexture,
        hasOcclusionTexture,
        hasEmissiveTexture);

    const bool preserveState = !worldIndexedBatchSubmissionState_.active;
    auto* batchState = preserveState ? nullptr : &worldIndexedBatchSubmissionState_;
    const auto bindProgram = [&](GLuint program) {
        if (!batchState || batchState->currentProgram != static_cast<int>(program)) {
            glUseProgram(program);
            if (batchState) {
                batchState->currentProgram = static_cast<int>(program);
                batchState->worldProgramStaticUniformsApplied = false;
            }
        }
    };
    const auto bindVertexArray = [&](GLuint vaoId) {
        if (!batchState || batchState->currentVao != static_cast<int>(vaoId)) {
            glBindVertexArray(vaoId);
            if (batchState) {
                batchState->currentVao = static_cast<int>(vaoId);
                batchState->currentElementArrayBuffer = -1;
            }
        }
    };
    const auto bindArrayBuffer = [&](GLuint bufferId) {
        if (!batchState || batchState->currentArrayBuffer != static_cast<int>(bufferId)) {
            glBindBuffer(GL_ARRAY_BUFFER, bufferId);
            if (batchState) {
                batchState->currentArrayBuffer = static_cast<int>(bufferId);
            }
        }
    };
    const auto bindElementArrayBuffer = [&](GLuint bufferId) {
        if (!batchState || batchState->currentElementArrayBuffer != static_cast<int>(bufferId)) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferId);
            if (batchState) {
                batchState->currentElementArrayBuffer = static_cast<int>(bufferId);
            }
        }
    };
    const auto setActiveTextureUnit = [&](int unit) {
        const GLint glUnit = static_cast<GLint>(GL_TEXTURE0 + unit);
        if (!batchState || batchState->currentActiveTexture != glUnit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            if (batchState) {
                batchState->currentActiveTexture = glUnit;
            }
        }
    };
    const auto bindTextureUnit2D = [&](int unit, GLuint textureId) {
        setActiveTextureUnit(unit);
        if (!batchState ||
            batchState->currentTexture2DOnUnit[static_cast<std::size_t>(unit)] != static_cast<int>(textureId)) {
            glBindTexture(GL_TEXTURE_2D, textureId);
            ++frameIndexedGlTextureBindCalls_;
            if (batchState) {
                batchState->currentTexture2DOnUnit[static_cast<std::size_t>(unit)] =
                    static_cast<int>(textureId);
            }
        }
    };
    const auto setDepthEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentDepthEnabled != enabled) {
            if (enabled) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            if (batchState) {
                batchState->currentDepthEnabled = enabled;
            }
        }
    };
    const auto setBlendEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentBlendEnabled != enabled) {
            if (enabled) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            if (batchState) {
                batchState->currentBlendEnabled = enabled;
            }
        }
    };
    const auto setCullEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentCullEnabled != enabled) {
            if (enabled) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            if (batchState) {
                batchState->currentCullEnabled = enabled;
            }
        }
    };
    const auto setFrontFace = [&](GLenum frontFace) {
        if (!batchState || batchState->currentFrontFace != static_cast<int>(frontFace)) {
            glFrontFace(frontFace);
            if (batchState) {
                batchState->currentFrontFace = static_cast<int>(frontFace);
            }
        }
    };
    const auto setDepthMask = [&](bool enabled) {
        if (!batchState || batchState->currentDepthMask != enabled) {
            glDepthMask(enabled ? GL_TRUE : GL_FALSE);
            if (batchState) {
                batchState->currentDepthMask = enabled;
            }
        }
    };
    const auto setDepthFunc = [&](GLenum func) {
        if (!batchState || batchState->currentDepthFunc != static_cast<int>(func)) {
            glDepthFunc(func);
            if (batchState) {
                batchState->currentDepthFunc = static_cast<int>(func);
            }
        }
    };
    const auto setBlendEquationSeparate = [&](GLenum rgb, GLenum alpha) {
        if (!batchState ||
            batchState->currentBlendEqRgb != static_cast<int>(rgb) ||
            batchState->currentBlendEqAlpha != static_cast<int>(alpha)) {
            glBlendEquationSeparate(rgb, alpha);
            if (batchState) {
                batchState->currentBlendEqRgb = static_cast<int>(rgb);
                batchState->currentBlendEqAlpha = static_cast<int>(alpha);
            }
        }
    };
    const auto setBlendFuncSeparate = [&](GLenum srcRgb,
                                          GLenum dstRgb,
                                          GLenum srcAlpha,
                                          GLenum dstAlpha) {
        if (!batchState ||
            batchState->currentBlendSrcRgb != static_cast<int>(srcRgb) ||
            batchState->currentBlendDstRgb != static_cast<int>(dstRgb) ||
            batchState->currentBlendSrcAlpha != static_cast<int>(srcAlpha) ||
            batchState->currentBlendDstAlpha != static_cast<int>(dstAlpha)) {
            glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
            if (batchState) {
                batchState->currentBlendSrcRgb = static_cast<int>(srcRgb);
                batchState->currentBlendDstRgb = static_cast<int>(dstRgb);
                batchState->currentBlendSrcAlpha = static_cast<int>(srcAlpha);
                batchState->currentBlendDstAlpha = static_cast<int>(dstAlpha);
            }
        }
    };
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTexture2DOnActive = 0;
    GLint prevTexture2DOnUnit[6] = {0, 0, 0, 0, 0, 0};
    GLint prevUniformBuffer = 0;
    GLint prevWorldSkinBufferBinding = 0;
    GLboolean depthEnabled = GL_FALSE;
    GLboolean blendEnabled = GL_FALSE;
    GLboolean cullEnabled = GL_FALSE;
    GLint prevFrontFace = GL_CCW;
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    GLint prevBlendSrcRgb = GL_SRC_ALPHA;
    GLint prevBlendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendSrcAlpha = GL_ONE;
    GLint prevBlendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendEqRgb = GL_FUNC_ADD;
    GLint prevBlendEqAlpha = GL_FUNC_ADD;
    if (preserveState) {
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnActive);
        for (int unit = 0; unit < 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnUnit[unit]);
        }
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &prevUniformBuffer);
        glGetIntegeri_v(
            GL_UNIFORM_BUFFER_BINDING,
            static_cast<GLuint>(kWorldSkinBlockBinding),
            &prevWorldSkinBufferBinding);

        depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        blendEnabled = glIsEnabled(GL_BLEND);
        cullEnabled = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);
    }

    glViewport(0, 0, std::max(1, surfaceWidth), std::max(1, surfaceHeight));
    setDepthEnabled(depthTestEnabled);
    setFrontFace(engine::render::parity_contract::kWorldFrontFaceClockwise ? GL_CW : GL_CCW);
    setDepthFunc(engine::render::parity_contract::kWorldDepthFuncLessEqual ? GL_LEQUAL : GL_LESS);
    setCullEnabled(engine::render::parity_contract::kWorldCullEnabled);
    if (blendAlpha) {
        setBlendEnabled(true);
        setBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        if (dualSourceBlendEnabled) {
            switch (blendMode) {
            case 1u:
                setBlendFuncSeparate(GL_SRC1_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
                break;
            case 0u:
            default:
                setBlendFuncSeparate(GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA, GL_ZERO, GL_ONE);
                break;
            }
        } else {
            switch (blendMode) {
            case 1u:
                setBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
                break;
            case 2u:
                setBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case 0u:
            default:
                setBlendFuncSeparate(
                    GL_SRC_ALPHA,
                    GL_ONE_MINUS_SRC_ALPHA,
                    GL_SRC_ALPHA,
                    GL_ONE_MINUS_SRC_ALPHA);
                break;
            }
        }
    } else {
        setBlendEnabled(false);
    }
    setDepthMask(!blendAlpha);

    bindProgram(worldProgram_);
    glBindBufferBase(GL_UNIFORM_BUFFER, kWorldSkinBlockBinding, worldSkinUbo_);
    glUniformMatrix4fv(worldViewProjLoc_, 1, GL_FALSE, viewProjectionMatrix4x4);
    static constexpr float kIdentityModel[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* modelMatrix = texture ? texture->modelMatrix.data() : kIdentityModel;
    glUniformMatrix4fv(worldModelLoc_, 1, GL_FALSE, modelMatrix);
    glUniform1f(
        worldClipSpaceDepthBiasLoc_,
        texture ? std::max(0.0f, texture->clipSpaceDepthBias) : 0.0f);
    const float cameraPosX = texture ? texture->cameraPosX : 0.0f;
    const float cameraPosY = texture ? texture->cameraPosY : 7.0f;
    const float cameraPosZ = texture ? texture->cameraPosZ : 9.0f;
    const float cameraForwardX = texture ? texture->cameraForwardX : 0.0f;
    const float cameraForwardY = texture ? texture->cameraForwardY : -0.6139406f;
    const float cameraForwardZ = texture ? texture->cameraForwardZ : -0.7893522f;
    const float cameraTargetX = texture ? texture->cameraTargetX : 0.0f;
    const float cameraTargetY = texture ? texture->cameraTargetY : -1.0f;
    const float cameraTargetZ = texture ? texture->cameraTargetZ : 0.0f;
    glUniform3f(worldCameraPosLoc_, cameraPosX, cameraPosY, cameraPosZ);
    glUniform3f(worldCameraForwardLoc_, cameraForwardX, cameraForwardY, cameraForwardZ);
    if (worldCameraTargetLoc_ >= 0) {
        glUniform3f(worldCameraTargetLoc_, cameraTargetX, cameraTargetY, cameraTargetZ);
    }
    glUniform1f(worldUseTextureLoc_, useTexture);
    glUniform4f(
        worldVertexColorMulLoc_,
        vertexColorMulR,
        vertexColorMulG,
        vertexColorMulB,
        vertexColorMulA);
    if (worldDualSourceBlendEnabledLoc_ >= 0) {
        glUniform1f(worldDualSourceBlendEnabledLoc_, dualSourceBlendEnabled ? 1.0f : 0.0f);
    }
    if (worldUseNormalTextureLoc_ >= 0) {
        glUniform1f(worldUseNormalTextureLoc_, hasNormalTexture ? 1.0f : 0.0f);
    }
    if (worldUseMetallicRoughnessTextureLoc_ >= 0) {
        glUniform1f(worldUseMetallicRoughnessTextureLoc_, hasMetallicRoughnessTexture ? 1.0f : 0.0f);
    }
    if (worldUseOcclusionTextureLoc_ >= 0) {
        glUniform1f(worldUseOcclusionTextureLoc_, hasOcclusionTexture ? 1.0f : 0.0f);
    }
    if (worldUseEmissiveTextureLoc_ >= 0) {
        glUniform1f(worldUseEmissiveTextureLoc_, hasEmissiveTexture ? 1.0f : 0.0f);
    }
    glUniform1f(worldWrapSLoc_, wrapS);
    glUniform1f(worldWrapTLoc_, wrapT);
    glUniform1f(worldAlphaModeLoc_, static_cast<GLfloat>(alphaMode));
    glUniform1f(worldAlphaCutoffLoc_, alphaCutoff);
    glUniform1f(worldAlphaWindowMinLoc_,
                texture ? std::clamp(texture->alphaWindowMin, 0.0f, 1.0f) : 0.0f);
    glUniform1f(worldAlphaWindowMaxLoc_,
                texture ? std::clamp(texture->alphaWindowMax, 0.0f, 1.0f) : 1.0f);
    if (worldNormalScaleLoc_ >= 0) {
        glUniform1f(worldNormalScaleLoc_, texture ? std::max(0.0f, texture->normalScale) : 1.0f);
    }
    if (worldMetallicFactorLoc_ >= 0) {
        glUniform1f(
            worldMetallicFactorLoc_,
            texture ? std::clamp(texture->metallicFactor, 0.0f, 1.0f) : 1.0f);
    }
    if (worldRoughnessFactorLoc_ >= 0) {
        glUniform1f(
            worldRoughnessFactorLoc_,
            texture ? std::clamp(texture->roughnessFactor, 0.0f, 1.0f) : 1.0f);
    }
    if (worldOcclusionStrengthLoc_ >= 0) {
        glUniform1f(
            worldOcclusionStrengthLoc_,
            texture ? std::clamp(texture->occlusionStrength, 0.0f, 1.0f) : 1.0f);
    }
    if (worldEmissiveFactorLoc_ >= 0) {
        glUniform3f(worldEmissiveFactorLoc_,
                    texture ? std::max(0.0f, texture->emissiveFactorR) : 0.0f,
                    texture ? std::max(0.0f, texture->emissiveFactorG) : 0.0f,
                    texture ? std::max(0.0f, texture->emissiveFactorB) : 0.0f);
    }
    if (worldCharacterInkingEnabledLoc_ >= 0) {
        glUniform1f(
            worldCharacterInkingEnabledLoc_,
            (texture && texture->characterInkingEnabled != 0u) ? 1.0f : 0.0f);
    }
    glUniform1f(worldMaterialModeLoc_, static_cast<GLfloat>(materialMode));
    glUniform1f(worldMaterialTimeLoc_, texture ? texture->materialTimeSec : 0.0f);
    glUniform1f(worldMaterialFlagsLoc_, texture ? texture->materialFlags : 0.0f);
    glUniform2f(worldMaterialAtlasSizeLoc_,
                texture ? texture->materialAtlasWidth : 0.0f,
                texture ? texture->materialAtlasHeight : 0.0f);
    glUniform4f(worldMaterialRect0Loc_,
                texture ? texture->materialRect0U : 0.0f,
                texture ? texture->materialRect0V : 0.0f,
                texture ? texture->materialRect0W : 1.0f,
                texture ? texture->materialRect0H : 1.0f);
    glUniform4f(worldMaterialRect1Loc_,
                texture ? texture->materialRect1U : 0.0f,
                texture ? texture->materialRect1V : 0.0f,
                texture ? texture->materialRect1W : 1.0f,
                texture ? texture->materialRect1H : 1.0f);
    glUniform4f(worldMaterialFlipbook0Loc_,
                texture ? texture->materialFlipbook0Cols : 1.0f,
                texture ? texture->materialFlipbook0Rows : 1.0f,
                texture ? texture->materialFlipbook0Frames : 1.0f,
                texture ? texture->materialFlipbook0Fps : 0.0f);
    glUniform4f(worldMaterialFlipbook1Loc_,
                texture ? texture->materialFlipbook1Cols : 1.0f,
                texture ? texture->materialFlipbook1Rows : 1.0f,
                texture ? texture->materialFlipbook1Frames : 1.0f,
                (texture && materialMode >= 2u)
                    ? static_cast<float>(pbrDebugViewMode())
                    : (texture ? texture->materialFlipbook1Fps : 0.0f));
    constexpr int kMaxGpuSkinMatrices = 128;
    const bool gpuSkinningEnabled =
        texture &&
        texture->gpuSkinning != 0u &&
        texture->skinMatrices != nullptr &&
        texture->skinMatrixCount > 0u;
    const std::uint8_t gpuSkinningMode = gpuSkinningEnabled
        ? static_cast<std::uint8_t>(std::min<std::uint32_t>(texture->gpuSkinningMode, 1u))
        : 0u;
    const int gpuSkinMatrixCount = gpuSkinningEnabled
        ? std::min<int>(static_cast<int>(texture->skinMatrixCount), kMaxGpuSkinMatrices)
        : 0;
    const float* skinMatrices =
        (gpuSkinMatrixCount > 0 && texture) ? texture->skinMatrices : nullptr;
    const bool canReuseSkinUniforms =
        batchState &&
        batchState->lastWorldSkinningEnabled == gpuSkinningEnabled &&
        batchState->lastWorldSkinningMode == gpuSkinningMode &&
        batchState->lastWorldSkinMatrixCount == static_cast<std::uint32_t>(gpuSkinMatrixCount) &&
        batchState->lastWorldSkinMatrices == skinMatrices;
    if (!canReuseSkinUniforms) {
        glUniform1f(worldSkinningEnabledLoc_, gpuSkinningEnabled ? 1.0f : 0.0f);
        glUniform1f(worldSkinningModeLoc_, gpuSkinningMode != 0u ? 1.0f : 0.0f);
        glUniform1i(worldSkinMatrixCountLoc_, gpuSkinMatrixCount);
        if (gpuSkinMatrixCount > 0) {
            const std::size_t skinFloatCount =
                static_cast<std::size_t>(gpuSkinMatrixCount) *
                (gpuSkinningMode != 0u ? 32u : 16u);
            glBindBuffer(GL_UNIFORM_BUFFER, worldSkinUbo_);
            glBufferSubData(
                GL_UNIFORM_BUFFER,
                0,
                static_cast<GLsizeiptr>(skinFloatCount * sizeof(float)),
                skinMatrices);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        if (batchState) {
            batchState->lastWorldSkinningEnabled = gpuSkinningEnabled;
            batchState->lastWorldSkinningMode = gpuSkinningMode;
            batchState->lastWorldSkinMatrixCount =
                static_cast<std::uint32_t>(gpuSkinMatrixCount);
            batchState->lastWorldSkinMatrices = skinMatrices;
        }
    }
    if (!batchState || !batchState->worldProgramStaticUniformsApplied) {
        glUniform1i(worldTextureSamplerLoc_, 0);
        if (worldNormalTextureSamplerLoc_ >= 0) glUniform1i(worldNormalTextureSamplerLoc_, 1);
        if (worldMetallicRoughnessTextureSamplerLoc_ >= 0) glUniform1i(worldMetallicRoughnessTextureSamplerLoc_, 2);
        if (worldOcclusionTextureSamplerLoc_ >= 0) glUniform1i(worldOcclusionTextureSamplerLoc_, 3);
        if (worldEmissiveTextureSamplerLoc_ >= 0) glUniform1i(worldEmissiveTextureSamplerLoc_, 4);
        if (worldEnvTextureSamplerLoc_ >= 0) glUniform1i(worldEnvTextureSamplerLoc_, 5);
        if (worldEnvTexelSizeLoc_ >= 0) {
            glUniform2f(
                worldEnvTexelSizeLoc_,
                neutralPmremAtlas.texelWidth,
                neutralPmremAtlas.texelHeight);
        }
        if (worldEnvMaxMipLoc_ >= 0) {
            glUniform1f(worldEnvMaxMipLoc_, neutralPmremAtlas.maxMip);
        }
        if (worldEnvRgbmRangeLoc_ >= 0) {
            glUniform1f(worldEnvRgbmRangeLoc_, neutralPmremAtlas.rgbmRange);
        }
        if (batchState) {
            batchState->worldProgramStaticUniformsApplied = true;
        }
    }

    bindTextureUnit2D(0, boundTexture);
    bindTextureUnit2D(1, boundNormalTexture);
    bindTextureUnit2D(2, boundMetallicRoughnessTexture);
    bindTextureUnit2D(3, boundOcclusionTexture);
    bindTextureUnit2D(4, boundEmissiveTexture);
    bindTextureUnit2D(5, boundEnvTexture);

    const std::size_t effectiveInstanceCount =
        (instances && instanceCount > 0u) ? instanceCount : 1u;
    if (effectiveInstanceCount > static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) {
        if (preserveState) {
            glBindVertexArray(static_cast<GLuint>(prevVao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
            glUseProgram(static_cast<GLuint>(prevProgram));
            for (int unit = 0; unit < 6; ++unit) {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit[unit]));
            }
            glActiveTexture(static_cast<GLenum>(prevActiveTexture));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));
            glDepthMask(previousDepthMask);
            glDepthFunc(static_cast<GLenum>(previousDepthFunc));
            glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
            glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                                static_cast<GLenum>(prevBlendDstRgb),
                                static_cast<GLenum>(prevBlendSrcAlpha),
                                static_cast<GLenum>(prevBlendDstAlpha));
            if (blendEnabled) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            glFrontFace(static_cast<GLenum>(prevFrontFace));
            if (cullEnabled) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            if (depthEnabled) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
        }
        return;
    }

    static thread_local std::vector<OpenGLWorldInstanceVertexData> instanceData;
    instanceData.resize(effectiveInstanceCount);
    if (instances && instanceCount > 0u) {
        for (std::size_t i = 0; i < effectiveInstanceCount; ++i) {
            packWorldInstanceVertexData(instances[i], instanceData[i]);
        }
    } else {
        instanceData[0] = makeIdentityInstanceData();
    }

    const std::size_t instanceBytes = instanceData.size() * sizeof(OpenGLWorldInstanceVertexData);
    bindArrayBuffer(worldInstanceVbo_);
    if (instanceBytes > worldInstanceBufferBytes_) {
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(instanceBytes),
            nullptr,
            GL_STREAM_DRAW);
        worldInstanceBufferBytes_ = instanceBytes;
    }
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(instanceBytes),
        instanceData.data());

    bindVertexArray(vao);
    if (uploadGeometry) {
        bindArrayBuffer(vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                     vertices,
                     GL_STREAM_DRAW);
        bindElementArrayBuffer(indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeIndexCount * sizeof(std::uint32_t)),
                     indices,
                     GL_STREAM_DRAW);
    }
    const bool drawCharacterOutline =
        texture &&
        texture->characterInkingEnabled != 0u &&
        materialMode >= 2u &&
        safeVertexCount > 0u;
    // Draw the inverted hull first. Alpha-blended character surfaces do not
    // necessarily write depth, so replaying the outline last can cover their
    // textured interiors with black.
    if (drawCharacterOutline) {
        glUniform1f(worldUseTextureLoc_, 0.0f);
        glUniform1f(worldMaterialModeLoc_, 3.0f);
        setDepthMask(false);
        bindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(safeIndexCount),
                                GL_UNSIGNED_INT,
                                nullptr,
                                static_cast<GLsizei>(effectiveInstanceCount));
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u) *
                           static_cast<std::uint64_t>(effectiveInstanceCount);
        glUniform1f(worldUseTextureLoc_, useTexture);
        glUniform1f(worldMaterialModeLoc_, static_cast<GLfloat>(materialMode));
        setDepthMask(!blendAlpha);
    }
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(safeIndexCount),
                            GL_UNSIGNED_INT,
                            nullptr,
                            static_cast<GLsizei>(effectiveInstanceCount));
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u) *
                       static_cast<std::uint64_t>(effectiveInstanceCount);

    if (preserveState) {
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
        glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(prevUniformBuffer));
        glBindBufferBase(
            GL_UNIFORM_BUFFER,
            kWorldSkinBlockBinding,
            static_cast<GLuint>(prevWorldSkinBufferBinding));
        glUseProgram(static_cast<GLuint>(prevProgram));

        for (int unit = 0; unit < 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit[unit]));
        }
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));

        glDepthMask(previousDepthMask);
        glDepthFunc(static_cast<GLenum>(previousDepthFunc));
        glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
        glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                            static_cast<GLenum>(prevBlendDstRgb),
                            static_cast<GLenum>(prevBlendSrcAlpha),
                            static_cast<GLenum>(prevBlendDstAlpha));
        if (blendEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glFrontFace(static_cast<GLenum>(prevFrontFace));
        if (cullEnabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        if (depthEnabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }
}
