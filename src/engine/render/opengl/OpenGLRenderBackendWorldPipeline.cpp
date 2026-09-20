#include "engine/render/OpenGLRenderBackend.h"
#include "engine/core/Environment.h"
#include "engine/core/Paths.h"
#include "engine/render/WorldPbrShaderShared.h"
#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <glad/glad.h>

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kWorldProgramBinaryCacheMagic = 0x4f475042u; // OGPB
constexpr std::uint32_t kWorldProgramBinaryCacheVersion = 1u;

struct ProgramBinaryCacheHeader {
    std::uint32_t magic = 0u;
    std::uint32_t version = 0u;
    std::uint32_t format = 0u;
    std::uint32_t reserved = 0u;
    std::uint64_t binarySize = 0u;
};

template <typename T>
bool writePod(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return out.good();
}

template <typename T>
bool readPod(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

std::uint64_t fnv1a64(std::string_view payload) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : payload) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hexHash64(std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[value & 0x0full];
        value >>= 4u;
    }
    return out;
}

bool worldProgramBinaryCacheEnabled() {
    return !engine::env::flagEnabled("PHLOSION_DISABLE_OPENGL_WORLD_PROGRAM_CACHE");
}

bool worldProgramBinaryForceRebuild() {
    return engine::env::flagEnabled("PHLOSION_REBUILD_OPENGL_WORLD_PROGRAM_CACHE");
}

bool worldProgramBinarySupported() {
    if (!worldProgramBinaryCacheEnabled()) return false;
    if (glad_glGetProgramBinary == nullptr ||
        glad_glProgramBinary == nullptr ||
        glad_glProgramParameteri == nullptr) {
        return false;
    }
    GLint binaryFormatCount = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &binaryFormatCount);
    return binaryFormatCount > 0;
}

fs::path worldProgramBinaryCachePath(const char* vsSource, std::string_view fsSource) {
    std::string payload;
    payload.reserve(std::char_traits<char>::length(vsSource) + fsSource.size() + 256u);
    if (const GLubyte* vendor = glGetString(GL_VENDOR)) {
        payload.append(reinterpret_cast<const char*>(vendor));
    }
    payload.push_back('|');
    if (const GLubyte* renderer = glGetString(GL_RENDERER)) {
        payload.append(reinterpret_cast<const char*>(renderer));
    }
    payload.push_back('|');
    if (const GLubyte* version = glGetString(GL_VERSION)) {
        payload.append(reinterpret_cast<const char*>(version));
    }
    payload.push_back('|');
    payload.append(vsSource);
    payload.push_back('|');
    payload.append(fsSource.data(), fsSource.size());

    return fs::path(engine::paths::data("cache/shaders/opengl")) /
           ("world_pbr_" + hexHash64(fnv1a64(payload)) + ".glbin");
}

unsigned int tryLoadWorldProgramBinaryCache(const char* vsSource, std::string_view fsSource) {
    if (!worldProgramBinarySupported() || worldProgramBinaryForceRebuild()) return 0u;

    std::ifstream in(worldProgramBinaryCachePath(vsSource, fsSource), std::ios::binary);
    if (!in.is_open()) return 0u;

    ProgramBinaryCacheHeader header{};
    if (!readPod(in, header)) return 0u;
    if (header.magic != kWorldProgramBinaryCacheMagic ||
        header.version != kWorldProgramBinaryCacheVersion ||
        header.binarySize == 0u ||
        header.format == 0u) {
        return 0u;
    }

    std::vector<unsigned char> binary(static_cast<std::size_t>(header.binarySize), 0u);
    in.read(reinterpret_cast<char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    if (!in.good()) return 0u;

    const unsigned int program = glCreateProgram();
    if (program == 0u) return 0u;

    glProgramBinary(
        program,
        static_cast<GLenum>(header.format),
        binary.data(),
        static_cast<GLsizei>(binary.size()));
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0u;
    }
    return program;
}

void tryStoreWorldProgramBinaryCache(unsigned int program,
                                     const char* vsSource,
                                     std::string_view fsSource) {
    if (!worldProgramBinarySupported()) return;

    GLint binaryLength = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0) return;

    std::vector<unsigned char> binary(static_cast<std::size_t>(binaryLength), 0u);
    GLenum binaryFormat = 0u;
    GLsizei actualLength = 0;
    glGetProgramBinary(program,
                       binaryLength,
                       &actualLength,
                       &binaryFormat,
                       binary.data());
    if (actualLength <= 0 || binaryFormat == 0u) return;
    binary.resize(static_cast<std::size_t>(actualLength));

    const fs::path cachePath = worldProgramBinaryCachePath(vsSource, fsSource);
    std::error_code ec;
    fs::create_directories(cachePath.parent_path(), ec);
    if (ec) return;

    std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;

    ProgramBinaryCacheHeader header{};
    header.magic = kWorldProgramBinaryCacheMagic;
    header.version = kWorldProgramBinaryCacheVersion;
    header.format = static_cast<std::uint32_t>(binaryFormat);
    header.binarySize = static_cast<std::uint64_t>(binary.size());
    if (!writePod(out, header)) return;
    out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
}

unsigned int linkWorldProgramWithCache(unsigned int vs,
                                       unsigned int fs,
                                       const char* vsSource,
                                       std::string_view fsSource) {
    if (vs == 0u || fs == 0u) return 0u;

    const unsigned int program = glCreateProgram();
    if (program == 0u) return 0u;
    if (worldProgramBinarySupported()) {
        glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    if (glad_glBindFragDataLocationIndexed != nullptr &&
        fsSource.find("FragBlendAlpha") != std::string_view::npos) {
        glBindFragDataLocationIndexed(program, 0u, 0u, "FragColor");
        glBindFragDataLocationIndexed(program, 0u, 1u, "FragBlendAlpha");
    }
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0u;
    }

    tryStoreWorldProgramBinaryCache(program, vsSource, fsSource);
    return program;
}

} // namespace

void OpenGLRenderBackend::ensureWorldPipeline() {
    if (worldProgram_ != 0 && worldVao_ != 0 && worldVbo_ != 0 && worldIbo_ != 0 &&
        worldInstanceVbo_ != 0 && worldSkinUbo_ != 0 &&
        worldViewProjLoc_ >= 0 && worldModelLoc_ >= 0 && worldClipSpaceDepthBiasLoc_ >= 0 &&
        worldUseTextureLoc_ >= 0 && worldTextureSamplerLoc_ >= 0 &&
        worldWrapSLoc_ >= 0 && worldWrapTLoc_ >= 0 && worldVertexColorMulLoc_ >= 0 &&
        worldDualSourceBlendEnabledLoc_ >= 0 &&
        worldAlphaModeLoc_ >= 0 && worldAlphaCutoffLoc_ >= 0 &&
        worldCameraPosLoc_ >= 0 && worldCameraForwardLoc_ >= 0 &&
        worldMaterialModeLoc_ >= 0 && worldMaterialFlipbook1Loc_ >= 0 &&
        worldSceneColorPostEnabledLoc_ >= 0 &&
        worldSkinningEnabledLoc_ >= 0 && worldSkinningModeLoc_ >= 0 && worldSkinMatrixCountLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static const std::string kVertexTemplate = R"GLSL(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        layout (location = 3) in vec3 aNormal;
        layout (location = 4) in vec4 aJoints;
        layout (location = 5) in vec4 aWeights;
        layout (location = 6) in vec4 aTangent;
        layout (location = 7) in vec2 aSourceUv1;
        layout (location = 8) in vec2 aSourceUv2;
        layout (location = 9) in vec4 aInstanceModel0;
        layout (location = 10) in vec4 aInstanceModel1;
        layout (location = 11) in vec4 aInstanceModel2;
        layout (location = 12) in vec4 aInstanceModel3;
        layout (location = 13) in vec4 aInstanceColor;
        uniform mat4 uViewProj;
        uniform mat4 uModel;
        uniform float uClipSpaceDepthBias;
        uniform float uMaterialMode;
        uniform float uMaterialTimeSec;
        uniform float uMaterialFlags;
        uniform vec4 uMaterialRect0;
        uniform vec4 uMaterialRect1;
        uniform sampler2D uNormalTexture;
        uniform float uSkinningEnabled;
        uniform float uSkinningMode;
        uniform int uSkinMatrixCount;
        const int kMaxSkinMatrices = 128;
        layout (std140) uniform SkinMatricesBlock {
            mat4 uSkinMatrices[kMaxSkinMatrices * 2];
        };
        out vec2 vUv;
        out vec4 vColor;
        out vec3 vWorldPos;
        out vec3 vWorldNormal;
        out vec4 vWorldTangent;
        out vec3 vGenerated;
        out vec2 vSourceUv1;
        out vec2 vSourceUv2;
        mat4 loadSkinMatrix(int jointIndex) {
            if (uSkinningMode > 0.5) {
                return uSkinMatrices[jointIndex] * uSkinMatrices[jointIndex + uSkinMatrixCount];
            }
            return uSkinMatrices[jointIndex];
        }
        vec3 applySkinningPos(vec3 localPos) {
            vec4 blended = vec4(0.0);
            float totalWeight = 0.0;

            int j0 = int(aJoints.x + 0.5);
            int j1 = int(aJoints.y + 0.5);
            int j2 = int(aJoints.z + 0.5);
            int j3 = int(aJoints.w + 0.5);
            float w0 = aWeights.x;
            float w1 = aWeights.y;
            float w2 = aWeights.z;
            float w3 = aWeights.w;

            if (w0 > 0.00001 && j0 >= 0 && j0 < uSkinMatrixCount && j0 < kMaxSkinMatrices) {
                blended += (loadSkinMatrix(j0) * vec4(localPos, 1.0)) * w0;
                totalWeight += w0;
            }
            if (w1 > 0.00001 && j1 >= 0 && j1 < uSkinMatrixCount && j1 < kMaxSkinMatrices) {
                blended += (loadSkinMatrix(j1) * vec4(localPos, 1.0)) * w1;
                totalWeight += w1;
            }
            if (w2 > 0.00001 && j2 >= 0 && j2 < uSkinMatrixCount && j2 < kMaxSkinMatrices) {
                blended += (loadSkinMatrix(j2) * vec4(localPos, 1.0)) * w2;
                totalWeight += w2;
            }
            if (w3 > 0.00001 && j3 >= 0 && j3 < uSkinMatrixCount && j3 < kMaxSkinMatrices) {
                blended += (loadSkinMatrix(j3) * vec4(localPos, 1.0)) * w3;
                totalWeight += w3;
            }

            if (totalWeight <= 0.00001) return localPos;
            if (totalWeight < 0.999) {
                blended += vec4(localPos, 1.0) * (1.0 - totalWeight);
            }
            return blended.xyz;
        }
        vec3 applySkinningNormal(vec3 localNormal) {
            vec3 blended = vec3(0.0);
            float totalWeight = 0.0;

            int j0 = int(aJoints.x + 0.5);
            int j1 = int(aJoints.y + 0.5);
            int j2 = int(aJoints.z + 0.5);
            int j3 = int(aJoints.w + 0.5);
            float w0 = aWeights.x;
            float w1 = aWeights.y;
            float w2 = aWeights.z;
            float w3 = aWeights.w;

            if (w0 > 0.00001 && j0 >= 0 && j0 < uSkinMatrixCount && j0 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j0)) * localNormal) * w0;
                totalWeight += w0;
            }
            if (w1 > 0.00001 && j1 >= 0 && j1 < uSkinMatrixCount && j1 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j1)) * localNormal) * w1;
                totalWeight += w1;
            }
            if (w2 > 0.00001 && j2 >= 0 && j2 < uSkinMatrixCount && j2 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j2)) * localNormal) * w2;
                totalWeight += w2;
            }
            if (w3 > 0.00001 && j3 >= 0 && j3 < uSkinMatrixCount && j3 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j3)) * localNormal) * w3;
                totalWeight += w3;
            }

            if (totalWeight <= 0.00001) return localNormal;
            if (totalWeight < 0.999) {
                blended += localNormal * (1.0 - totalWeight);
            }
            return normalize(blended);
        }
        vec4 applySkinningTangent(vec4 localTangent) {
            vec3 tangent = localTangent.xyz;
            vec3 blended = vec3(0.0);
            float totalWeight = 0.0;

            int j0 = int(aJoints.x + 0.5);
            int j1 = int(aJoints.y + 0.5);
            int j2 = int(aJoints.z + 0.5);
            int j3 = int(aJoints.w + 0.5);
            float w0 = aWeights.x;
            float w1 = aWeights.y;
            float w2 = aWeights.z;
            float w3 = aWeights.w;

            if (w0 > 0.00001 && j0 >= 0 && j0 < uSkinMatrixCount && j0 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j0)) * tangent) * w0;
                totalWeight += w0;
            }
            if (w1 > 0.00001 && j1 >= 0 && j1 < uSkinMatrixCount && j1 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j1)) * tangent) * w1;
                totalWeight += w1;
            }
            if (w2 > 0.00001 && j2 >= 0 && j2 < uSkinMatrixCount && j2 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j2)) * tangent) * w2;
                totalWeight += w2;
            }
            if (w3 > 0.00001 && j3 >= 0 && j3 < uSkinMatrixCount && j3 < kMaxSkinMatrices) {
                blended += (mat3(loadSkinMatrix(j3)) * tangent) * w3;
                totalWeight += w3;
            }

            if (totalWeight <= 0.00001) return localTangent;
            if (totalWeight < 0.999) {
                blended += tangent * (1.0 - totalWeight);
            }
            return vec4(normalize(blended), localTangent.w);
        }

__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
        void main() {
            vec3 localPos = aPos;
            vec3 localNormal = aNormal;
            vec4 localTangent = aTangent;
            float normalLengthSquared = dot(localNormal, localNormal);
            if (uMaterialMode > 2.5 && uMaterialMode < 3.5 &&
                normalLengthSquared > 1e-10) {
                localPos += localNormal * inversesqrt(normalLengthSquared) * 0.001;
            }

__PHLOSION_PROJECT_MATERIAL_EVALUATION__

            if (uSkinningEnabled > 0.5) {
                localPos = applySkinningPos(localPos);
                localNormal = applySkinningNormal(localNormal);
                localTangent = applySkinningTangent(localTangent);
            }
            mat4 instanceModel = mat4(
                aInstanceModel0,
                aInstanceModel1,
                aInstanceModel2,
                aInstanceModel3);
            mat3 instanceLinear = mat3(
                aInstanceModel0.xyz,
                aInstanceModel1.xyz,
                aInstanceModel2.xyz);
            vec4 instanceWorld = instanceModel * vec4(localPos, 1.0);
            vec4 worldPos = uModel * instanceWorld;
            gl_Position = uViewProj * worldPos;
            gl_Position.z -= uClipSpaceDepthBias * gl_Position.w;
            vUv = aUv;
            vSourceUv1 = aSourceUv1;
            vSourceUv2 = aSourceUv2;
            vColor = aColor * aInstanceColor;
            vec3 genDen = max(uMaterialRect1.xyz - uMaterialRect0.xyz, vec3(1e-5));
            vGenerated = clamp((aPos - uMaterialRect0.xyz) / genDen, vec3(0.0), vec3(1.0));
            vWorldPos = worldPos.xyz;
            // Keep world normal/tangent transform behavior aligned with D3D12 path.
            // This improves cross-backend parity for authored tangent-space normal detail.
            mat3 normalM = mat3(uModel);
            vWorldNormal = normalize(normalM * (instanceLinear * localNormal));
            vec3 worldTangent = normalM * (instanceLinear * localTangent.xyz);
            float tangentLenSq = dot(worldTangent, worldTangent);
            if (tangentLenSq > 1e-10) worldTangent *= inversesqrt(tangentLenSq);
            vWorldTangent = vec4(worldTangent, localTangent.w);
        }
    )GLSL";
    const std::string kVs = engine::render::injectWorldMaterialProfile(kVertexTemplate, worldMaterialProfile_.openglVertex);
    static const std::string kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        in vec3 vWorldPos;
        in vec3 vWorldNormal;
        in vec4 vWorldTangent;
        in vec3 vGenerated;
        in vec2 vSourceUv1;
        in vec2 vSourceUv2;
        uniform float uUseTexture;
        uniform float uWrapS;
        uniform float uWrapT;
        uniform float uAlphaMode;
        uniform float uAlphaCutoff;
        uniform float uAlphaWindowMin;
        uniform float uAlphaWindowMax;
        uniform vec3 uCameraPos;
        uniform vec3 uCameraForward;
        uniform vec3 uCameraTarget;
        uniform float uMaterialMode;
        uniform float uMaterialTimeSec;
        uniform float uMaterialFlags;
        uniform vec2  uMaterialAtlasSize;
        uniform vec4  uMaterialRect0;
        uniform vec4  uMaterialRect1;
        uniform vec4  uMaterialFlipbook0;
        uniform vec4  uMaterialFlipbook1;
        uniform sampler2D uTexture;
        uniform sampler2D uNormalTexture;
        uniform sampler2D uMetallicRoughnessTexture;
        uniform sampler2D uOcclusionTexture;
        uniform sampler2D uEmissiveTexture;
        uniform sampler2D uEnvTexture;
        uniform sampler2D uLightProjectionTexture;
        uniform vec4 uLightProjectionUvRowU;
        uniform vec4 uLightProjectionUvRowV;
        uniform sampler2D uProjectedShadowTexture;
        uniform mat4 uProjectedShadowMatrix;
        uniform vec3 uProjectedShadowParams;
        uniform vec4 uVertexColorMul;
        uniform float uDualSourceBlendEnabled;
        uniform vec2 uEnvTexelSize;
        uniform float uEnvMaxMip;
        uniform float uEnvRgbmRange;
        uniform float uUseNormalTexture;
        uniform float uUseMetallicRoughnessTexture;
        uniform float uUseOcclusionTexture;
        uniform float uUseEmissiveTexture;
        uniform float uNormalScale;
        uniform float uMetallicFactor;
        uniform float uRoughnessFactor;
        uniform float uOcclusionStrength;
        uniform vec3 uEmissiveFactor;
        uniform float uCharacterInkingEnabled;
        uniform float uSceneColorPostEnabled;
        out vec4 FragColor;
        out vec4 FragBlendAlpha;


__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
        void main() {
__PHLOSION_PROJECT_MATERIAL_EVALUATION__
}

    )GLSL";
    static const engine::render::WorldMaterialShaderSource kDefaultMaterial{
        R"GLSL(        float applyWrap(float coord, float mode) {
            if (abs(mode - 33071.0) < 0.5) return clamp(coord, 0.0, 1.0);
            if (abs(mode - 33648.0) < 0.5) {
                float i = floor(coord);
                float f = fract(coord);
                float odd = mod(abs(i), 2.0);
                return (odd >= 1.0) ? (1.0 - f) : f;
            }
            return fract(coord);
        }

        vec2 clampWrappedUvToTexelCenter(vec2 uv) {
            vec2 texSize = max(vec2(textureSize(uTexture, 0)), vec2(1.0));
            vec2 halfTexel = vec2(0.5) / texSize;
            return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
        }

float litTextureDetailLodBias() { return clamp(uMaterialFlipbook1.z, -0.75, 1.25); }

        vec4 sampleTextureWithWrap(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy) {
            float lodScale = exp2(litTextureDetailLodBias());
            return textureGrad(tex, uv, uvDx * lodScale, uvDy * lodScale);
        }

        vec3 rgbToHsv(vec3 color) {
            vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
            vec4 p = mix(
                vec4(color.bg, k.wz),
                vec4(color.gb, k.xy),
                step(color.b, color.g));
            vec4 q = mix(
                vec4(p.xyw, color.r),
                vec4(color.r, p.yzx),
                step(p.x, color.r));
            float chroma = q.x - min(q.w, q.y);
            float epsilon = 1.0e-10;
            return vec3(
                abs(q.z + (q.w - q.y) / (6.0 * chroma + epsilon)),
                chroma / (q.x + epsilon),
                q.x);
        }

        vec3 hsvToRgb(vec3 hsv) {
            vec3 p = abs(
                fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
                    6.0 -
                3.0);
            return hsv.z *
                mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
        }

        vec3 srgbToLinear(vec3 c) {
            c = clamp(c, 0.0, 1.0);
            vec3 lo = c / 12.92;
            vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
            return mix(lo, hi, step(vec3(0.04045), c));
        }

        vec3 linearToSrgb(vec3 c) {
            c = max(c, vec3(0.0));
            vec3 lo = c * 12.92;
            vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
            return mix(lo, hi, step(vec3(0.0031308), c));
        }

        vec3 encodeWorldSurfaceColor(vec3 linearColor) {
            // Encode linear world color for an sRGB display.
            return linearToSrgb(clamp(linearColor, 0.0, 1.0));
        }

        vec3 resolveWorldSceneColor(vec3 linearColor) {
            vec3 clamped = clamp(linearColor, 0.0, 1.0);
            return (uSceneColorPostEnabled > 0.5)
                ? clamped
                : encodeWorldSurfaceColor(clamped);
        }

        vec3 safeNormalize(vec3 value, vec3 fallback) {
            float len2 = dot(value, value);
            if (len2 < 1e-8) return fallback;
            return value * inversesqrt(len2);
        }

        int decodeReviewLightingProfile(vec3 cameraForwardPacked) {
            return clamp(
                int(floor(length(cameraForwardPacked) + 0.5)) - 1,
                0,
                4);
        }

        vec3 applyReviewLightingProfile(
            vec3 composite,
            vec3 resolvedAlbedo,
            vec3 sourceNormal,
            vec3 cameraForwardPacked) {
            int profile = decodeReviewLightingProfile(cameraForwardPacked);
            if (profile == 0) return max(composite, vec3(0.0));
            if (profile == 4) return max(composite, vec3(0.0));
            vec3 albedo = max(resolvedAlbedo, vec3(0.0));
            if (profile == 1) {
                vec3 shadowFloor = albedo * 0.50;
                return max(
                    mix(composite, max(composite, shadowFloor), 0.56),
                    vec3(0.0));
            }
            if (profile == 2) {
                vec3 highlight = max(composite - albedo, vec3(0.0));
                return max(
                    mix(composite, albedo, 0.82) + highlight * 0.16,
                    vec3(0.0));
            }
            vec3 cameraForward = safeNormalize(
                cameraForwardPacked,
                vec3(0.0, -0.6139406, -0.7893522));
            vec3 cameraRight = safeNormalize(
                cross(cameraForward, vec3(0.0, 1.0, 0.0)),
                vec3(1.0, 0.0, 0.0));
            vec3 normal = safeNormalize(
                sourceNormal,
                vec3(0.0, 1.0, 0.0));
            float grazing = pow(abs(dot(normal, cameraRight)), 0.70);
            vec3 grazingSurface = albedo * (0.36 + 0.78 * grazing);
            vec3 highlight = max(composite - albedo, vec3(0.0));
            return max(
                mix(max(composite, albedo * 0.34), grazingSurface, 0.62) +
                    highlight * 0.20,
                vec3(0.0));
        }

__PHLOSION_SHARED_WORLD_PBR_SECTION__

        vec3 perturbNormal2Arb(vec3 eyePos, vec3 surfNorm, vec3 mapN, vec2 uv, float faceDirection) {
            // Mirrors three.js perturbNormal2Arb derivative basis construction.
            vec3 q0 = dFdx(eyePos.xyz);
            vec3 q1 = dFdy(eyePos.xyz);
            vec2 st0 = dFdx(uv);
            vec2 st1 = dFdy(uv);

            vec3 N = surfNorm;
            vec3 q1perp = cross(q1, N);
            vec3 q0perp = cross(N, q0);
            vec3 T = q1perp * st0.x + q0perp * st1.x;
            vec3 B = q1perp * st0.y + q0perp * st1.y;

            float det = max(dot(T, T), dot(B, B));
            float scale = (det <= 1e-10) ? 0.0 : faceDirection * inversesqrt(det);
            return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
        }

        vec3 computeMappedNormalFromTexture(
            sampler2D normalTexture,
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float sourceNormalScale) {
            // Keep OpenGL tangent-space face handling tied to native front-face
            // classification for stable normal-map response.
            bool isFrontFace = gl_FrontFacing;
            float faceDirection = isFrontFace ? 1.0 : -1.0;
            vec3 n = normalize(vWorldNormal);
            if (dot(n, n) < 1e-6) {
                vec3 dx = dFdx(vWorldPos);
                vec3 dy = dFdy(vWorldPos);
                n = normalize(cross(dx, dy));
            }
            n *= faceDirection;

            vec3 normalTexel = sampleTextureWithWrap(
                normalTexture,
                sampleUv,
                uvDx,
                uvDy).xyz;
            vec2 mapXY = normalTexel.xy * 2.0 - 1.0;
            mapXY *= max(sourceNormalScale, 0.0);
            // Support both standard tangent-space normals (RGB) and
            // two-channel packed XY normals. Decoded XY maps can use either
            // blue=0 or blue=255 as a sentinel; reconstruct Z in both cases.
            float authoredZ = normalTexel.z * 2.0 - 1.0;
            float reconZ = sqrt(max(1.0 - clamp(dot(mapXY, mapXY), 0.0, 1.0), 0.0));
            float useReconstructedZ =
                (normalTexel.z <= (1.5 / 255.0) ||
                 normalTexel.z >= (253.5 / 255.0))
                    ? 1.0
                    : 0.0;
            float mapZ = mix(authoredZ, reconZ, useReconstructedZ);
            vec3 mapN = normalize(vec3(mapXY, mapZ));

            vec3 mapped = vec3(0.0);
            vec3 tangent = vWorldTangent.xyz;
            float tangentLenSq = dot(tangent, tangent);
            bool hasAuthoredTangent = tangentLenSq > 1e-6 && abs(vWorldTangent.w) > 0.5;
            if (hasAuthoredTangent) {
                tangent *= inversesqrt(tangentLenSq);
                tangent = tangent - n * dot(n, tangent);
                float orthoLenSq = dot(tangent, tangent);
                if (orthoLenSq > 1e-10) {
                    tangent *= inversesqrt(orthoLenSq);
                    float tangentSign = (vWorldTangent.w < 0.0) ? -1.0 : 1.0;
                    vec3 bitangent = normalize(cross(n, tangent)) * tangentSign;
                    if (!isFrontFace) {
                        tangent = -tangent;
                        bitangent = -bitangent;
                    }
                    mapped = normalize(tangent * mapN.x + bitangent * mapN.y + n * mapN.z);
                } else {
                    hasAuthoredTangent = false;
                }
            }
            if (!hasAuthoredTangent) {
                mapped = perturbNormal2Arb(vWorldPos, n, mapN, sampleUv, faceDirection);
            }
            return mapped;
        }

        vec3 computeMappedNormal(
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float materialNormalMultiplier) {
            return computeMappedNormalFromTexture(
                uNormalTexture,
                sampleUv,
                uvDx,
                uvDy,
                max(uNormalScale, 0.0) * 1.25 *
                    max(materialNormalMultiplier, 0.0));
        }

        vec3 applyWorldLitModel(vec3 linearColor, vec3 n, vec2 sampleUv, vec2 uvDx, vec2 uvDy) {
            vec4 orm = sampleTextureWithWrap(uMetallicRoughnessTexture, sampleUv, uvDx, uvDy);
            float roughness = clamp(orm.g * clamp(uRoughnessFactor, 0.0, 1.0), 0.16, 1.0);
            float metallic = clamp(orm.b * clamp(uMetallicFactor, 0.0, 1.0), 0.0, 1.0);
            float occTex = sampleTextureWithWrap(
                uOcclusionTexture,
                sampleUv,
                uvDx,
                uvDy).r;
            float ao = mix(1.0, occTex, clamp(uOcclusionStrength, 0.0, 1.0));

            vec3 albedo = clamp(linearColor, 0.0, 1.0);
            float dielectricSpecular = 0.04;
            vec3 F0 = mix(vec3(dielectricSpecular), albedo, metallic);
            vec3 diffuseColor = albedo * (1.0 - metallic);
            const float specularF90 = 1.0;
            vec3 camForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 camRight = cross(camForward, vec3(0.0, 1.0, 0.0));
            if (dot(camRight, camRight) < 1e-6) {
                camRight = cross(camForward, vec3(0.0, 0.0, 1.0));
            }
            camRight = safeNormalize(camRight, vec3(1.0, 0.0, 0.0));
            vec3 camUp = safeNormalize(cross(camRight, camForward), vec3(0.0, 1.0, 0.0));
            vec3 v = safeNormalize(uCameraPos - vWorldPos, -camForward);
            const vec3 directColor = vec3(1.0);
            const float directIntensity = __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265;
            const vec3 ambientColor = vec3(1.0);
            const float ambientIntensity = __PHLOSION_PBR_AMBIENT_INTENSITY__;

            vec3 lightPos = uCameraPos + camRight * 0.5 + camUp * 0.0 - camForward * 0.8660254;
            vec3 l0 = safeNormalize(lightPos - uCameraTarget, vec3(0.45, 0.86, 0.24));
            vec3 direct = evalDirectPbr(
)GLSL"
        R"GLSL(                n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);

            float NdotV = max(dot(n, v), 0.0);
            vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            vec3 r = reflect(-v, n);
            vec3 envIrradiance = 3.14159265 * sampleNeutralEnvironment(n, 1.0);
            vec3 envRadiance = sampleNeutralEnvironment(r, roughness);
            vec3 singleScattering = vec3(0.0);
            vec3 multiScattering = vec3(0.0);
            computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
            vec3 cosineWeightedIrradiance = envIrradiance * (1.0 / 3.14159265);
            vec3 totalScattering = singleScattering + multiScattering;
            float energyComp = 1.0 - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
            vec3 diffuseIBL = diffuseColor * max(energyComp, 0.0) * cosineWeightedIrradiance;
            vec3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
            diffuseIBL *= __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
            specularIBL *= __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
            diffuseIBL *= ao;
            float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
            specularIBL *= specularOcclusion;
            vec3 ibl = diffuseIBL + specularIBL;

            vec3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
            vec3 shaded = direct + ibl + ambientLight;

            vec3 emissiveTex = clamp(
                sampleTextureWithWrap(
                    uEmissiveTexture,
                    sampleUv,
                    uvDx,
                    uvDy).rgb,
                0.0,
                1.0);
            vec3 emissive = emissiveTex * max(uEmissiveFactor, vec3(0.0));

            return max(shaded + emissive, vec3(0.0));
        }

        vec3 applyCharacterInking(vec3 linearColor, vec3 n) {
            if (uCharacterInkingEnabled < 0.5) return linearColor;

            vec3 camForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 v = safeNormalize(uCameraPos - vWorldPos, -camForward);
            vec3 nn = safeNormalize(n, vec3(0.0, 1.0, 0.0));
            float ndv = clamp(dot(nn, v), 0.0, 1.0);
            float edge = 1.0 - ndv;
            float fw = max(fwidth(edge), 1e-4);
            // Thin but clearly visible silhouette band (~1-2px at gameplay camera distance).
            float t0 = 0.84;
            float t1 = 0.985;
            float ringOuter = smoothstep(t0 - fw * 1.5, t0 + fw * 1.5, edge);
            float ringInner = smoothstep(t1 - fw * 1.5, t1 + fw * 1.5, edge);
            float outline = clamp(ringOuter - ringInner, 0.0, 1.0);

            const vec3 inkColor = vec3(0.0);
            const float inkStrength = 1.0;
            return mix(linearColor, inkColor, outline * inkStrength);
        }
)GLSL",
        R"GLSL(
            FragBlendAlpha = vec4(0.0);
            if (uMaterialMode > 2.5 && uMaterialMode < 3.5) {
                if (gl_FrontFacing) discard;
                FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            vec4 tex = vec4(1.0);
            vec3 outLinear = clamp(vColor.rgb * uVertexColorMul.rgb, 0.0, 1.0);
            vec2 rawUv = vUv;
            vec2 wrappedUv = vec2(applyWrap(rawUv.x, uWrapS), applyWrap(rawUv.y, uWrapT));
            bool clampS = abs(uWrapS - 33071.0) < 0.5;
            bool clampT = abs(uWrapT - 33071.0) < 0.5;
            if (clampS || clampT) {
                wrappedUv = clampWrappedUvToTexelCenter(wrappedUv);
            }
            // Keep derivative source aligned with D3D12 path for exact sampler parity.
            vec2 uvDx = dFdx(wrappedUv);
            vec2 uvDy = dFdy(wrappedUv);
            bool explicitMaterialDebug =
                uMaterialFlipbook1.w < -100.5;
            float pbrDebugView = explicitMaterialDebug
                ? -uMaterialFlipbook1.w - 100.0
                : uMaterialFlipbook1.w;
            if (uUseTexture > 0.5) {
                tex = sampleTextureWithWrap(uTexture, wrappedUv, uvDx, uvDy);
                outLinear = clamp(tex.rgb, 0.0, 1.0) * outLinear;
            }
            vec3 reviewAlbedo = outLinear;
            float outA = clamp(vColor.a * uVertexColorMul.a * tex.a, 0.0, 1.0);
            float alphaWindowMin = clamp(uAlphaWindowMin, 0.0, 1.0);
            float alphaWindowMax = clamp(uAlphaWindowMax, 0.0, 1.0);
            if (alphaWindowMax < 1.0 || alphaWindowMin > 0.0) {
                if (outA < alphaWindowMin || outA >= alphaWindowMax) discard;
            }
            if (uAlphaMode < 0.5) {
                outA = clamp(vColor.a * uVertexColorMul.a, 0.0, 1.0);
            } else if (uAlphaMode < 1.5) {
                if (outA < clamp(uAlphaCutoff, 0.0, 1.0)) discard;
                outA = clamp(vColor.a * uVertexColorMul.a, 0.0, 1.0);
            }
            if (uMaterialMode >= 1.5 && pbrDebugView > 0.5) {
                vec3 dbg = vec3(0.0);
                if (pbrDebugView < 1.5) {
                    // 1: Raw base-color texture sample.
                    dbg = clamp(tex.rgb, 0.0, 1.0);
                } else if (pbrDebugView < 2.5) {
                    // 2: Authored tint-resolved albedo without lighting.
                    dbg = clamp(outLinear, 0.0, 1.0);
                } else if (pbrDebugView < 3.5) {
                    // 3: Normal map sample.
                    dbg = uUseNormalTexture > 0.5
                        ? sampleTextureWithWrap(
                              uNormalTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.5, 0.5, 1.0);
                } else if (pbrDebugView < 4.5) {
                    // 4: Roughness channel.
                    float rgh = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).g
                        : 1.0;
                    dbg = vec3(rgh);
                } else if (pbrDebugView < 5.5) {
                    // 5: Metallic channel.
                    float met = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).b
                        : 0.0;
                    dbg = vec3(met);
                } else if (pbrDebugView < 6.5) {
                    // 6: AO channel.
                    float ao = uUseOcclusionTexture > 0.5
                        ? sampleTextureWithWrap(
                              uOcclusionTexture, wrappedUv, uvDx, uvDy).r
                        : 1.0;
                    dbg = vec3(ao);
                } else if (pbrDebugView < 7.5) {
                    // 7: Emissive sample.
                    dbg = uUseEmissiveTexture > 0.5
                        ? sampleTextureWithWrap(
                              uEmissiveTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.0);
                }
                FragColor = vec4(resolveWorldSceneColor(dbg), 1.0);
                return;
            }
            if (uMaterialMode >= 1.5) {
                vec3 n = computeMappedNormal(wrappedUv, uvDx, uvDy, 1.0);
                outLinear = applyWorldLitModel(outLinear, n, wrappedUv, uvDx, uvDy);
            }
            if (uMaterialMode >= 1.5) {
                outLinear = applyReviewLightingProfile(
                    outLinear,
                    reviewAlbedo,
                    vWorldNormal,
                    uCameraForward);
            }
            const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
            const float toneMappingMode = 1.0;
            vec3 mapped = applyViewerToneMapping(max(outLinear, vec3(0.0)), toneMappingMode, toneMappingExposure);
            vec3 outSrgb = resolveWorldSceneColor(mapped);
            float mainOutA = outA;
            if (uDualSourceBlendEnabled > 0.5) {
                mainOutA = floor(clamp(outA, 0.0, 1.0) * 63.0 + 0.5) / 63.0;
            }
            FragColor = vec4(outSrgb, mainOutA);
            FragBlendAlpha = (uDualSourceBlendEnabled > 0.5)
                ? vec4(0.0, 0.0, 0.0, clamp(outA, 0.0, 1.0))
                : vec4(0.0);
        )GLSL"};

    const std::string fsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            engine::render::injectWorldMaterialProfile(kFs, worldMaterialProfile_.empty() ? kDefaultMaterial : worldMaterialProfile_.opengl),
            engine::render::world_pbr_shader_shared::ShaderLanguage::Glsl);
    worldProgram_ = tryLoadWorldProgramBinaryCache(kVs.c_str(), fsSource);
    if (worldProgram_ == 0u) {
        const unsigned int vs = opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER, kVs.c_str());
        const unsigned int fs =
            opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER, fsSource.c_str());
        if (vs == 0 || fs == 0) {
            if (vs != 0) glDeleteShader(vs);
            if (fs != 0) glDeleteShader(fs);
            return;
        }

        worldProgram_ = linkWorldProgramWithCache(vs, fs, kVs.c_str(), fsSource);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (worldProgram_ == 0) return;
    }

    worldViewProjLoc_ = glGetUniformLocation(worldProgram_, "uViewProj");
    worldModelLoc_ = glGetUniformLocation(worldProgram_, "uModel");
    worldClipSpaceDepthBiasLoc_ = glGetUniformLocation(worldProgram_, "uClipSpaceDepthBias");
    worldUseTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseTexture");
    worldTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uTexture");
    worldUseNormalTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseNormalTexture");
    worldUseMetallicRoughnessTextureLoc_ =
        glGetUniformLocation(worldProgram_, "uUseMetallicRoughnessTexture");
    worldUseOcclusionTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseOcclusionTexture");
    worldUseEmissiveTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseEmissiveTexture");
    worldNormalTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uNormalTexture");
    worldMetallicRoughnessTextureSamplerLoc_ =
        glGetUniformLocation(worldProgram_, "uMetallicRoughnessTexture");
    worldOcclusionTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uOcclusionTexture");
    worldEmissiveTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uEmissiveTexture");
    worldEnvTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uEnvTexture");
    worldLightProjectionTextureSamplerLoc_ =
        glGetUniformLocation(worldProgram_, "uLightProjectionTexture");
    worldLightProjectionUvRowULoc_ =
        glGetUniformLocation(worldProgram_, "uLightProjectionUvRowU");
    worldLightProjectionUvRowVLoc_ =
        glGetUniformLocation(worldProgram_, "uLightProjectionUvRowV");
    worldProjectedShadowTextureSamplerLoc_ =
        glGetUniformLocation(worldProgram_, "uProjectedShadowTexture");
    worldProjectedShadowMatrixLoc_ =
        glGetUniformLocation(worldProgram_, "uProjectedShadowMatrix");
    worldProjectedShadowParamsLoc_ =
        glGetUniformLocation(worldProgram_, "uProjectedShadowParams");
    worldEnvTexelSizeLoc_ = glGetUniformLocation(worldProgram_, "uEnvTexelSize");
    worldEnvMaxMipLoc_ = glGetUniformLocation(worldProgram_, "uEnvMaxMip");
    worldEnvRgbmRangeLoc_ = glGetUniformLocation(worldProgram_, "uEnvRgbmRange");
    worldWrapSLoc_ = glGetUniformLocation(worldProgram_, "uWrapS");
    worldWrapTLoc_ = glGetUniformLocation(worldProgram_, "uWrapT");
    worldVertexColorMulLoc_ = glGetUniformLocation(worldProgram_, "uVertexColorMul");
    worldDualSourceBlendEnabledLoc_ = glGetUniformLocation(worldProgram_, "uDualSourceBlendEnabled");
    worldAlphaModeLoc_ = glGetUniformLocation(worldProgram_, "uAlphaMode");
    worldAlphaCutoffLoc_ = glGetUniformLocation(worldProgram_, "uAlphaCutoff");
    worldAlphaWindowMinLoc_ = glGetUniformLocation(worldProgram_, "uAlphaWindowMin");
    worldAlphaWindowMaxLoc_ = glGetUniformLocation(worldProgram_, "uAlphaWindowMax");
    worldCameraPosLoc_ = glGetUniformLocation(worldProgram_, "uCameraPos");
    worldCameraForwardLoc_ = glGetUniformLocation(worldProgram_, "uCameraForward");
    worldCameraTargetLoc_ = glGetUniformLocation(worldProgram_, "uCameraTarget");
    worldNormalScaleLoc_ = glGetUniformLocation(worldProgram_, "uNormalScale");
    worldMetallicFactorLoc_ = glGetUniformLocation(worldProgram_, "uMetallicFactor");
    worldRoughnessFactorLoc_ = glGetUniformLocation(worldProgram_, "uRoughnessFactor");
    worldOcclusionStrengthLoc_ = glGetUniformLocation(worldProgram_, "uOcclusionStrength");
    worldEmissiveFactorLoc_ = glGetUniformLocation(worldProgram_, "uEmissiveFactor");
    worldCharacterInkingEnabledLoc_ = glGetUniformLocation(worldProgram_, "uCharacterInkingEnabled");
    worldSceneColorPostEnabledLoc_ =
        glGetUniformLocation(worldProgram_, "uSceneColorPostEnabled");
    worldMaterialModeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialMode");
    worldMaterialTimeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialTimeSec");
    worldMaterialFlagsLoc_ = glGetUniformLocation(worldProgram_, "uMaterialFlags");
    worldMaterialAtlasSizeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialAtlasSize");
    worldMaterialRect0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect0");
    worldMaterialRect1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect1");
    worldMaterialFlipbook0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook0");
    worldMaterialFlipbook1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook1");
    worldSkinningEnabledLoc_ = glGetUniformLocation(worldProgram_, "uSkinningEnabled");
    worldSkinningModeLoc_ = glGetUniformLocation(worldProgram_, "uSkinningMode");
    worldSkinMatrixCountLoc_ = glGetUniformLocation(worldProgram_, "uSkinMatrixCount");
    const GLuint worldSkinBlockIndex = glGetUniformBlockIndex(worldProgram_, "SkinMatricesBlock");
    if (worldViewProjLoc_ < 0 || worldModelLoc_ < 0 || worldClipSpaceDepthBiasLoc_ < 0 ||
        worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldVertexColorMulLoc_ < 0 ||
        worldDualSourceBlendEnabledLoc_ < 0 ||
        worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldAlphaWindowMinLoc_ < 0 || worldAlphaWindowMaxLoc_ < 0 ||
        worldCameraPosLoc_ < 0 || worldCameraForwardLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldSceneColorPostEnabledLoc_ < 0 ||
        worldSkinningEnabledLoc_ < 0 || worldSkinningModeLoc_ < 0 || worldSkinMatrixCountLoc_ < 0 ||
        worldSkinBlockIndex == GL_INVALID_INDEX) {
        destroyWorldPipeline();
        return;
    }
    constexpr GLuint kWorldSkinBlockBinding = 0u;
    glUniformBlockBinding(worldProgram_, worldSkinBlockIndex, kWorldSkinBlockBinding);

    glGenVertexArrays(1, &worldVao_);
    glGenBuffers(1, &worldVbo_);
    glGenBuffers(1, &worldIbo_);
    glGenBuffers(1, &worldInstanceVbo_);
    glGenBuffers(1, &worldSkinUbo_);
    if (worldVao_ == 0 || worldVbo_ == 0 || worldIbo_ == 0 || worldInstanceVbo_ == 0 ||
        worldSkinUbo_ == 0) {
        destroyWorldPipeline();
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, worldInstanceVbo_);
    worldInstanceBufferBytes_ = sizeof(float) * 20u;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(worldInstanceBufferBytes_), nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, worldSkinUbo_);
    glBufferData(
        GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(sizeof(float) * 16u * 256u),
        nullptr,
        GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kWorldSkinBlockBinding, worldSkinUbo_);
    configureWorldMeshVertexLayout(worldVao_, worldVbo_, worldIbo_);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGLRenderBackend::configureWorldMeshVertexLayout(unsigned int vao,
                                                         unsigned int vertexBuffer,
                                                         unsigned int indexBuffer) {
    if (vao == 0u || vertexBuffer == 0u || indexBuffer == 0u || worldInstanceVbo_ == 0u) return;

    constexpr GLsizei vertexStride = static_cast<GLsizei>(sizeof(WorldMeshVertex));
    constexpr GLsizei instanceStride = static_cast<GLsizei>(sizeof(float) * 20u);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, nx)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, joint0)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, weight0)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, tx)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, sourceUv1U)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, vertexStride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, sourceUv2U)));

    glBindBuffer(GL_ARRAY_BUFFER, worldInstanceVbo_);
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, instanceStride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, instanceStride, reinterpret_cast<void*>(sizeof(float) * 4));
    glVertexAttribDivisor(10, 1);
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, instanceStride, reinterpret_cast<void*>(sizeof(float) * 8));
    glVertexAttribDivisor(11, 1);
    glEnableVertexAttribArray(12);
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, instanceStride, reinterpret_cast<void*>(sizeof(float) * 12));
    glVertexAttribDivisor(12, 1);
    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, instanceStride, reinterpret_cast<void*>(sizeof(float) * 16));
    glVertexAttribDivisor(13, 1);
}

void OpenGLRenderBackend::destroyWorldPipeline() {
    destroyCachedWorldMeshes();
    if (worldSkinUbo_ != 0) {
        glDeleteBuffers(1, &worldSkinUbo_);
        worldSkinUbo_ = 0;
    }
    if (worldInstanceVbo_ != 0) {
        glDeleteBuffers(1, &worldInstanceVbo_);
        worldInstanceVbo_ = 0;
    }
    worldInstanceBufferBytes_ = 0u;
    if (worldIbo_ != 0) {
        glDeleteBuffers(1, &worldIbo_);
        worldIbo_ = 0;
    }
    if (worldVbo_ != 0) {
        glDeleteBuffers(1, &worldVbo_);
        worldVbo_ = 0;
    }
    if (worldVao_ != 0) {
        glDeleteVertexArrays(1, &worldVao_);
        worldVao_ = 0;
    }
    if (worldProgram_ != 0) {
        glDeleteProgram(worldProgram_);
        worldProgram_ = 0;
    }
    worldViewProjLoc_ = -1;
    worldModelLoc_ = -1;
    worldClipSpaceDepthBiasLoc_ = -1;
    worldUseTextureLoc_ = -1;
    worldTextureSamplerLoc_ = -1;
    worldUseNormalTextureLoc_ = -1;
    worldUseMetallicRoughnessTextureLoc_ = -1;
    worldUseOcclusionTextureLoc_ = -1;
    worldUseEmissiveTextureLoc_ = -1;
    worldNormalTextureSamplerLoc_ = -1;
    worldMetallicRoughnessTextureSamplerLoc_ = -1;
    worldOcclusionTextureSamplerLoc_ = -1;
    worldEmissiveTextureSamplerLoc_ = -1;
    worldEnvTextureSamplerLoc_ = -1;
    worldLightProjectionTextureSamplerLoc_ = -1;
    worldLightProjectionUvRowULoc_ = -1;
    worldLightProjectionUvRowVLoc_ = -1;
    worldProjectedShadowTextureSamplerLoc_ = -1;
    worldProjectedShadowMatrixLoc_ = -1;
    worldProjectedShadowParamsLoc_ = -1;
    worldEnvTexelSizeLoc_ = -1;
    worldEnvMaxMipLoc_ = -1;
    worldEnvRgbmRangeLoc_ = -1;
    worldWrapSLoc_ = -1;
    worldWrapTLoc_ = -1;
    worldVertexColorMulLoc_ = -1;
    worldDualSourceBlendEnabledLoc_ = -1;
    worldAlphaModeLoc_ = -1;
    worldAlphaCutoffLoc_ = -1;
    worldAlphaWindowMinLoc_ = -1;
    worldAlphaWindowMaxLoc_ = -1;
    worldCameraPosLoc_ = -1;
    worldCameraForwardLoc_ = -1;
    worldCameraTargetLoc_ = -1;
    worldNormalScaleLoc_ = -1;
    worldMetallicFactorLoc_ = -1;
    worldRoughnessFactorLoc_ = -1;
    worldOcclusionStrengthLoc_ = -1;
    worldEmissiveFactorLoc_ = -1;
    worldCharacterInkingEnabledLoc_ = -1;
    worldSceneColorPostEnabledLoc_ = -1;
    worldMaterialModeLoc_ = -1;
    worldMaterialTimeLoc_ = -1;
    worldMaterialFlagsLoc_ = -1;
    worldMaterialAtlasSizeLoc_ = -1;
    worldMaterialRect0Loc_ = -1;
    worldMaterialRect1Loc_ = -1;
    worldMaterialFlipbook0Loc_ = -1;
    worldMaterialFlipbook1Loc_ = -1;
    worldSkinningEnabledLoc_ = -1;
    worldSkinningModeLoc_ = -1;
    worldSkinMatrixCountLoc_ = -1;
}

void OpenGLRenderBackend::destroyCachedWorldMeshes() {
    for (auto& [_, mesh] : cachedWorldMeshes_) {
        if (mesh.indexBuffer != 0u) {
            glDeleteBuffers(1, &mesh.indexBuffer);
            mesh.indexBuffer = 0u;
        }
        if (mesh.vertexBuffer != 0u) {
            glDeleteBuffers(1, &mesh.vertexBuffer);
            mesh.vertexBuffer = 0u;
        }
        if (mesh.vao != 0u) {
            glDeleteVertexArrays(1, &mesh.vao);
            mesh.vao = 0u;
        }
        mesh.valid = false;
    }
    cachedWorldMeshes_.clear();
}
