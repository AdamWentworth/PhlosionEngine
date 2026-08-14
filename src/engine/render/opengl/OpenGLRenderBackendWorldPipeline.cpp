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
        worldMaterialModeLoc_ >= 0 && worldMaterialTimeLoc_ >= 0 && worldMaterialFlagsLoc_ >= 0 &&
        worldMaterialAtlasSizeLoc_ >= 0 && worldMaterialRect0Loc_ >= 0 && worldMaterialRect1Loc_ >= 0 &&
        worldMaterialFlipbook0Loc_ >= 0 && worldMaterialFlipbook1Loc_ >= 0 &&
        worldSceneColorPostEnabledLoc_ >= 0 &&
        worldSkinningEnabledLoc_ >= 0 && worldSkinningModeLoc_ >= 0 && worldSkinMatrixCountLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
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
        void main() {
            vec3 localPos = aPos;
            vec3 localNormal = aNormal;
            vec4 localTangent = aTangent;
            float normalLengthSquared = dot(localNormal, localNormal);
            if (uMaterialMode > 2.5 && uMaterialMode < 3.5 &&
                normalLengthSquared > 1e-10) {
                localPos += localNormal * inversesqrt(normalLengthSquared) * 0.001;
            }
            // Scarlet's always-on loop01 channel animates UVScaleOffset3 for
            // the displacement sample independently of the selected skeleton
            // clip. Preserve that source material clock instead of freezing
            // the noise on the authored flame mesh.
            if (uMaterialMode > 26.5 && uMaterialMode < 27.5 &&
                dot(localNormal, localNormal) > 1e-10) {
                bool exactSourceTrack = uMaterialFlags > 1.5;
                float displacementScrollHz = max(uMaterialRect0.w, 0.0);
                float displacementScroll =
                    !exactSourceTrack && displacementScrollHz > 0.0
                        ? fract(uMaterialTimeSec * displacementScrollHz)
                        : 0.0;
                vec2 displacementOffset = exactSourceTrack
                    ? uMaterialRect1.zw
                    : uMaterialRect1.zw + vec2(displacementScroll);
                vec2 displacementUv = vec2(
                    (aUv.x - displacementOffset.x) * uMaterialRect1.x,
                    1.0 - ((1.0 - aUv.y) - displacementOffset.y) * uMaterialRect1.y);
                // The animated Scarlet fire maps are periodic on the axes
                // driven by UVScaleOffset3.  The extracted sampler metadata
                // reports clamp, but clamping turns each sawtooth reset into
                // a visible hitch.  Wrap the authored coordinates explicitly
                // so the nearly identical texture borders meet at the loop.
                displacementUv = fract(displacementUv);
                float displacement = sin(
                    textureLod(uNormalTexture, displacementUv, 0.0).r);
                localPos += normalize(localNormal) *
                    clamp(aColor.r, 0.0, 1.0) *
                    max(uMaterialRect0.x, 0.0) * displacement;
            }
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

    const std::string kFs = std::string(R"GLSL(
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

        float applyWrap(float coord, float mode) {
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
        float litTextureDetailLodBias() {
            bool qualityControlled =
                (uMaterialMode > 1.5 && uMaterialMode < 2.5) ||
                (uMaterialMode > 28.5 && uMaterialMode < 29.5) ||
                (uMaterialMode > 31.5 && uMaterialMode < 33.5);
            if (!qualityControlled) return 0.0;
            return clamp(uMaterialFlipbook1.z, -0.75, 1.25);
        }
        vec4 sampleTextureWithWrap(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy) {
            float lodScale = exp2(litTextureDetailLodBias());
            return textureGrad(tex, uv, uvDx * lodScale, uvDy * lodScale);
        }
        vec4 sampleLgpeGroundTexture(sampler2D tex, vec2 uv) {
            return texture(tex, uv, -2.0);
        }
        vec2 lgpeRoute1CloudTextureUv(vec3 worldPosition);
        float evaluateLgpeRoute1ProjectedCloud();
        float evaluateLgpeRoute1ProjectedShadow();
        float evaluateLgpeRoute1ProjectedLighting(float toon);
        vec3 applyLgpeGroundCliffSharedLighting(vec3 surface) {
            const vec3 shadowColor = vec3(0.235, 0.361, 0.391);
            float light =
                evaluateLgpeRoute1ProjectedLighting(1.0);
            return mix(shadowColor, vec3(1.0), light) * surface;
        }
        vec3 evaluateLgpeFieldGroundSurface() {
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 blendUv = vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            vec2 uv2 = vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
            vec4 ground01 = sampleLgpeGroundTexture(uTexture, uv0);
            vec4 ground02 = sampleLgpeGroundTexture(uNormalTexture, uv0);
            vec4 grass02 =
                sampleLgpeGroundTexture(uMetallicRoughnessTexture, uv0);
            vec4 grass01 = sampleLgpeGroundTexture(uOcclusionTexture, uv0);
            float blend =
                clamp(sampleLgpeGroundTexture(uEnvTexture, blendUv).r,
                      0.0,
                      1.0);
            vec4 grassMask =
                sampleLgpeGroundTexture(uEmissiveTexture, uv2);
            vec3 ground = mix(ground01.rgb, ground02.rgb, blend);
            vec3 grass = mix(grass02.rgb, grass01.rgb, blend);
            vec3 surface =
                mix(ground, grass, clamp(grassMask.a, 0.0, 1.0));
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            vec3 sourceSurface =
                grassMask.rgb * authoredVertexColor.rgb * surface +
                max(uEmissiveFactor, vec3(0.0)) *
                    (1.0 - clamp(authoredVertexColor.a, 0.0, 1.0));
            return applyLgpeGroundCliffSharedLighting(sourceSurface);
        }
        vec3 evaluateLgpeFieldCliffSurface() {
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 blendUv = vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec2 uv2 = vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
            vec4 cliffTex = sampleLgpeGroundTexture(uTexture, uv1);
            vec4 ground02 = sampleLgpeGroundTexture(uNormalTexture, uv0);
            vec4 ground01 =
                sampleLgpeGroundTexture(uMetallicRoughnessTexture, uv0);
            float blend =
                clamp(sampleLgpeGroundTexture(uOcclusionTexture, blendUv).r,
                      0.0,
                      1.0);
            vec4 borderTex = sampleLgpeGroundTexture(uEmissiveTexture, uv2);
            vec3 normal = normalize(vWorldNormal);
            vec3 viewDirection = normalize(uCameraPos - vWorldPos);
            float rimSpan =
                max(uMetallicFactor, uNormalScale) - uNormalScale;
            float rim = rimSpan > 0.0
                ? clamp(
                      ((1.0 - dot(normal, viewDirection)) - uNormalScale) /
                          rimSpan,
                      0.0,
                      1.0) *
                      uRoughnessFactor
                : 0.0;
            vec3 cliff =
                cliffTex.rgb +
                max(uEmissiveFactor, vec3(0.0)) * rim * cliffTex.a;
            vec3 grass = mix(ground02.rgb, ground01.rgb, blend);
            vec3 surface =
                mix(cliff, grass, clamp(borderTex.a, 0.0, 1.0));
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            vec3 sourceSurface =
                borderTex.rgb * authoredVertexColor.rgb * surface;
            return applyLgpeGroundCliffSharedLighting(sourceSurface);
        }
        vec3 lgpeFoliageRgbToHsv(vec3 color) {
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
        vec3 lgpeFoliageHsvToRgb(vec3 hsv) {
            vec3 p = abs(
                fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
                    6.0 -
                3.0);
            return hsv.z *
                mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
        }
        vec3 lgpeFoliageColorBalance(
            vec3 color,
            float hueShift,
            float saturation,
            float value) {
            vec3 hsv = lgpeFoliageRgbToHsv(max(color, vec3(0.0)));
            hsv.x = fract(hsv.x + hueShift);
            hsv.y = clamp(hsv.y * saturation, 0.0, 1.0);
            hsv.z *= value;
            return lgpeFoliageHsvToRgb(hsv);
        }
        float lgpeFoliageAcceptedLightCoordinate(vec3 normal) {
            const vec3 acceptedLightDirection =
                vec3(0.32, -0.42, 0.85);
            float sourceDot =
                dot(normalize(normal), acceptedLightDirection);
            return mix(
                0.12,
                0.96,
                clamp((sourceDot + 0.15) / 1.0, 0.0, 1.0));
        }
        vec3 lgpeFoliageProjectionCompensation(vec3 shadowColor) {
            float cloud = evaluateLgpeRoute1ProjectedLighting(1.0);
            vec3 projectedLighting =
                mix(max(shadowColor, vec3(0.0)), vec3(1.0), cloud);
            return mix(vec3(1.0), projectedLighting, 0.25);
        }
        vec3 lgpeFoliageColorize(
            vec3 baseColor,
            vec3 paletteColor,
            float factor) {
            const vec3 luminanceWeights =
                vec3(0.2126, 0.7152, 0.0722);
            float baseLuminance =
                dot(max(baseColor, vec3(0.0)), luminanceWeights);
            float paletteLuminance =
                max(dot(max(paletteColor, vec3(0.0)), luminanceWeights),
                    0.0001);
            vec3 paletteAtBaseLuminance =
                paletteColor * (baseLuminance / paletteLuminance);
            return mix(
                baseColor,
                paletteAtBaseLuminance,
                clamp(factor, 0.0, 1.0));
        }
        vec3 lgpeFoliageAcceptedDisplayTransform(vec3 color) {
            // The accepted Route 1 Blender checkpoint was judged through its
            // AgX high-contrast display transform.  A restrained filmic
            // shoulder here translates that material calibration into the
            // engine's otherwise direct-sRGB LGPE path without changing the
            // route's already-qualified ground, cliff, grass, or flower
            // presentation.
            vec3 exposed = max(color, vec3(0.0)) * 0.72;
            vec3 mapped = clamp(
                (exposed * (2.51 * exposed + 0.03)) /
                    (exposed * (2.43 * exposed + 0.59) + 0.14),
                0.0,
                1.0);
            float luminance =
                dot(mapped, vec3(0.2126, 0.7152, 0.0722));
            float highlight =
                smoothstep(0.35, 0.85, luminance);
            float saturationRetention =
                mix(0.9, 0.55, highlight);
            return mix(
                vec3(luminance),
                mapped,
                saturationRetention);
        }
        vec4 evaluateLgpeReviewedFieldTree05Surface(
            float hueShift,
            float saturation,
            float value) {
            // Canonical cache rows are uploaded in source top-down order.
            // As with the reviewed BuildModel flowers and sign, raw source
            // UV0 is the presentation-space coordinate; applying the source
            // program's vertical convention again selects the opposite atlas
            // rows.
            vec2 uv0 = vUv;
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }
            float lightCoordinate =
                lgpeFoliageAcceptedLightCoordinate(vWorldNormal);
            vec3 texture02 =
                texture(
                    uNormalTexture,
                    vec2(0.5, 1.0 - lightCoordinate),
                    0.0).rgb;
            float texture03 =
                texture(uMetallicRoughnessTexture, uv0, 0.0).r;
            vec3 acceptedLocalSurface =
                clamp(texture01.rgb + texture02 * texture03, 0.0, 1.0);
            vec3 acceptedColor = lgpeFoliageColorBalance(
                acceptedLocalSurface,
                hueShift,
                saturation,
                value);
            vec3 shadowColor =
                max(
                    vec3(
                        uNormalScale,
                        uMetallicFactor,
                        uRoughnessFactor),
                    vec3(0.0));
            vec3 finalColor =
                acceptedColor *
                lgpeFoliageProjectionCompensation(shadowColor);
            return vec4(
                lgpeFoliageAcceptedDisplayTransform(finalColor),
                texture01.a);
        }
        vec4 evaluateLgpeReviewedFieldTree02Surface(
            float hueShift,
            float saturation,
            float value,
            bool darkConifer) {
            vec2 uv0 = vUv;
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }
            vec3 texture02 = texture(uNormalTexture, uv0, 0.0).rgb;
            float lightCoordinate =
                lgpeFoliageAcceptedLightCoordinate(vWorldNormal);
            float lightToon =
                texture(
                    uEmissiveTexture,
                    vec2(lightCoordinate, 0.5),
                    0.0).r;
            vec3 directionalLightColor =
                vec3(
                    uMaterialRect0.z,
                    uMaterialRect0.w,
                    uMaterialRect1.x);
            vec3 acceptedLocalSurface = clamp(
                texture01.rgb +
                    texture02 * directionalLightColor *
                        clamp(lightToon, 0.0, 1.0),
                0.0,
                1.0);
            if (darkConifer) {
                vec3 greenColor =
                    vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
                float inverseShade =
                    1.0 -
                    dot(
                        clamp(texture02, 0.0, 1.0),
                        vec3(0.2126, 0.7152, 0.0722));
                acceptedLocalSurface = lgpeFoliageColorize(
                    acceptedLocalSurface,
                    greenColor,
                    inverseShade);
            }
            vec3 acceptedColor = lgpeFoliageColorBalance(
                acceptedLocalSurface,
                hueShift,
                saturation,
                value);
            vec3 finalColor =
                acceptedColor *
                lgpeFoliageProjectionCompensation(uEmissiveFactor);
            vec3 displayedColor =
                lgpeFoliageAcceptedDisplayTransform(finalColor);
            return vec4(
                displayedColor,
                texture01.a);
        }
        vec4 evaluateLgpeFieldTree05Surface() {
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
            vec3 texture02 = texture(uNormalTexture, uv1, 0.0).rgb;
            float texture03 =
                texture(uMetallicRoughnessTexture, uv0, 0.0).r;
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    0.0).r,
                0.0,
                1.0);
            vec3 viewDirection = normalize(uCameraPos - vWorldPos);
            float rimMin = uMaterialTimeSec;
            float rimMax = uMaterialFlags;
            float rimStrength = uMaterialAtlasSize.x;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp(
                      ((1.0 - dot(normal, viewDirection)) - rimMin) /
                          rimSpan,
                      0.0,
                      1.0) *
                      rimStrength
                : 0.0;
            float lightGate =
                1.0 -
                clamp(
                    (1.0 - normalDotLight) * 12.7408008575,
                    0.0,
                    1.0);
            vec3 secondaryDirection =
                mix(
                    viewDirection,
                    -sourceSunRay,
                    uMaterialFlipbook0.w);
            float secondaryMin = uMaterialFlipbook1.z;
            float secondaryMax = uMaterialFlipbook1.w;
            float secondarySpan =
                max(secondaryMax, secondaryMin) - secondaryMin;
            float secondaryCoordinate =
                clamp(
                    1.0 - dot(normal, secondaryDirection),
                    0.0,
                    1.0);
            float secondary = secondarySpan > 0.0
                ? clamp(
                      (secondaryCoordinate - secondaryMin) /
                          secondarySpan,
                      0.0,
                      1.0)
                : 0.0;
            vec3 shadowColor =
                max(vec3(uNormalScale, uMetallicFactor, uRoughnessFactor),
                    vec3(0.0));
            vec3 rimColor =
                max(
                    vec3(
                        uMaterialAtlasSize.y,
                        uMaterialRect0.x,
                        uMaterialRect0.y),
                    vec3(0.0));
            vec3 rimColor02 =
                max(
                    vec3(
                        uMaterialRect0.z,
                        uMaterialRect0.w,
                        uMaterialRect1.x),
                    vec3(0.0));
            vec3 surface =
                texture01.rgb +
                texture02 * rim * rimColor +
                max(uEmissiveFactor, vec3(0.0)) *
                    (1.0 - secondary) +
                texture03 * lightGate * rimColor02;
            return vec4(
                mix(
                    shadowColor,
                    vec3(1.0),
                    evaluateLgpeRoute1ProjectedLighting(toon)) *
                    surface,
                texture01.a);
        }
        vec4 evaluateLgpeFieldTree02Surface(
            bool useProjectedCloud,
            bool useCanonicalTextureUv) {
            // Exact modes retain the decoded 1-V operation. Reviewed
            // BuildModel grass02 uses canonical presentation UVs, matching
            // its exact Blender checkpoint's direct model-texcoord links.
            vec2 uv0 = useCanonicalTextureUv
                ? vUv
                : vec2(vUv.x, 1.0 - vUv.y);
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
            vec3 texture02 = texture(uNormalTexture, uv0, 0.0).rgb;
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            vec2 toonUv =
                vec2(toonCoordinate, 1.0 - toonCoordinate);
            float toon = clamp(
                texture(uOcclusionTexture, toonUv, 0.0).r,
                0.0,
                1.0);
            float lightToon = clamp(
                texture(uEmissiveTexture, toonUv, 0.0).r,
                0.0,
                1.0);
            vec3 viewDirection = normalize(uCameraPos - vWorldPos);
            float edge =
                clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
            float rimMin = uMaterialTimeSec;
            float rimMax = uMaterialFlags;
            float rimStrength = uMaterialAtlasSize.x;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                      rimStrength
                : 0.0;
            float directional = clamp(edge * (5.0 / 3.0), 0.0, 1.0);
            vec3 greenColor =
                vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
            vec3 shadowColor = uEmissiveFactor;
            vec3 rimColor =
                vec3(
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x,
                    uMaterialRect0.y);
            vec3 directionalLightColor =
                vec3(
                    uMaterialRect0.z,
                    uMaterialRect0.w,
                    uMaterialRect1.x);
            vec3 rimColor02 = uMaterialRect1.yzw;
            vec3 secondary =
                rim * rimColor +
                (1.0 - directional) * rimColor02 +
                lightToon * directionalLightColor;
            vec3 surface = texture01.rgb + texture02 * secondary;
            vec3 authored = surface * vColor.rgb;
            vec3 tinted =
                mix(greenColor, authored, clamp(vColor.a, 0.0, 1.0));
            // The source uses a shared ten-tap projected-depth PCF. Until its
            float light = useProjectedCloud
                ? evaluateLgpeRoute1ProjectedLighting(toon)
                : toon * evaluateLgpeRoute1ProjectedShadow();
            vec3 lighting =
                mix(shadowColor, vec3(1.0), light);
            return vec4(lighting * tinted, texture01.a);
        }
    )GLSL") + R"GLSL(
        vec4 evaluateLgpeFieldGrassSurface(bool withRim) {
            float sourceMipBias = uMaterialFlipbook0.w;
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec2 blendUv =
                vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            bool floorFoliageCard =
                !withRim && vSourceUv2.x < -2048.0;
            vec3 textureMap01 =
                texture(
                    uTexture,
                    uv0,
                    sourceMipBias).rgb;
            vec3 textureMap02 =
                texture(uNormalTexture, uv0, sourceMipBias).rgb;
            vec4 greenHikari =
                texture(
                    uMetallicRoughnessTexture,
                    floorFoliageCard
                        ? vSourceUv1
                        : uv1,
                    sourceMipBias);
            if (floorFoliageCard) {
                if (greenHikari.r >= clamp(uAlphaCutoff, 0.0, 1.0)) {
                    discard;
                }
            } else if (
                greenHikari.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }
            float greenBlend = clamp(
                texture(
                    uOcclusionTexture,
                    blendUv,
                    sourceMipBias).r,
                0.0,
                1.0);
            float highlight = clamp(
                texture(
                    uEmissiveTexture,
                    uv1,
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uEnvTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 sourceColor =
                vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
            vec3 decoration =
                mix(textureMap02, textureMap01, greenBlend) +
                sourceColor * (1.0 - highlight);
            vec3 authoredColor = vColor.rgb;
            if (!withRim && sourceMipBias > -1.0 &&
                normalize(vWorldNormal).y > 0.9) {
                // grass01_com_001 floor carriers use branch-coded Color0
                // values. The accepted source-geometry audit resolves their
                // horizontal lawn branch to this continuous raised-lawn tint.
                authoredColor =
                    vec3(0.180392161, 0.482352942, 0.431372553);
            }
            vec3 surface =
                decoration * greenHikari.rgb * authoredColor;
            if (withRim) {
                vec3 viewDirection = normalize(uCameraPos - vWorldPos);
                float edge =
                    clamp(
                        1.0 - dot(normal, viewDirection),
                        0.0,
                        1.0);
                float rimMin = uMaterialTimeSec;
                float rimMax = uMaterialFlags;
                float rimSpan = max(rimMax, rimMin) - rimMin;
                float rim = rimSpan > 0.0
                    ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                          uMaterialAtlasSize.x
                    : 0.0;
                vec3 rimColor =
                    vec3(
                        uMaterialAtlasSize.y,
                        uMaterialRect0.x,
                        uMaterialRect0.y);
                surface += rimColor * rim;
            }
            // The depth PCF remains held lit; the captured stationary
            // LightProjMap projection is represented separately.
            vec3 lighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(toon));
            vec3 result = lighting * surface;
            float alpha = greenHikari.a;
            if (!withRim) {
                vec3 onGameColor =
                    vec3(
                        uMaterialTimeSec,
                        uMaterialFlags,
                        uMaterialAtlasSize.x);
                float onGameValue =
                    clamp(uMaterialAtlasSize.y, 0.0, 1.0);
                result *=
                    mix(vec3(1.0), onGameColor, onGameValue);
                alpha *= clamp(uMaterialRect0.x, 0.0, 1.0);
            }
            return vec4(result, alpha);
        }
        vec2 lgpeRoute1CloudTextureUv(vec3 worldPosition) {
            vec4 position = vec4(worldPosition, 1.0);
            float sourceU = dot(position, uLightProjectionUvRowU);
            float sourceV = dot(position, uLightProjectionUvRowV);
            return vec2(sourceU, 1.0 - sourceV);
        }
        float evaluateLgpeRoute1ProjectedCloud() {
            return clamp(
                texture(
                    uLightProjectionTexture,
                    lgpeRoute1CloudTextureUv(vWorldPos),
                    0.0).r,
                0.0,
                1.0);
        }
        float evaluateLgpeRoute1ProjectedShadow() {
            if (uProjectedShadowParams.x < 0.5) return 1.0;
            vec4 shadowClip =
                uProjectedShadowMatrix * vec4(vWorldPos, 1.0);
            if (abs(shadowClip.w) <= 1e-8) return 1.0;
            vec3 shadowNdc = shadowClip.xyz / shadowClip.w;
            vec2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
            if (any(lessThan(shadowUv, vec2(0.0))) ||
                any(greaterThan(shadowUv, vec2(1.0)))) {
                return 1.0;
            }
            float reference =
                shadowNdc.z * 0.5 + 0.5 -
                uProjectedShadowParams.z / shadowClip.w;
            const vec2 poisson[10] = vec2[10](
                vec2(-0.8405, -0.0740),
                vec2(-0.3262, -0.4058),
                vec2(-0.2034,  0.4573),
                vec2(-0.6985,  0.6206),
                vec2( 0.9635, -0.1944),
                vec2( 0.4734, -0.4800),
                vec2( 0.5195,  0.7670),
                vec2( 0.1855, -0.8945),
                vec2( 0.5074,  0.0650),
                vec2(-0.3219,  0.5954));
            ivec2 textureExtent =
                max(textureSize(uProjectedShadowTexture, 0), ivec2(1));
            float projectedShadow = 0.0;
            for (int tap = 0; tap < 10; ++tap) {
                vec2 tapUv =
                    shadowUv + poisson[tap] *
                    (0.0004 * uProjectedShadowParams.y);
                vec2 texelPosition =
                    tapUv * vec2(textureExtent) - vec2(0.5);
                ivec2 baseTexel = ivec2(floor(texelPosition));
                vec2 blend = fract(texelPosition);
                float comparisons[4];
                for (int corner = 0; corner < 4; ++corner) {
                    ivec2 cornerOffset =
                        ivec2(corner & 1, (corner >> 1) & 1);
                    ivec2 texel = baseTexel + cornerOffset;
                    float storedDepth = 1.0;
                    if (all(greaterThanEqual(texel, ivec2(0))) &&
                        all(lessThan(texel, textureExtent))) {
                        vec3 packedDepth =
                            texelFetch(
                                uProjectedShadowTexture,
                                texel,
                                0).rgb * 255.0;
                        storedDepth =
                            dot(
                                packedDepth,
                                vec3(65536.0, 256.0, 1.0)) /
                            16777215.0;
                    }
                    comparisons[corner] =
                        reference <= storedDepth ? 1.0 : 0.0;
                }
                float row0 = mix(comparisons[0], comparisons[1], blend.x);
                float row1 = mix(comparisons[2], comparisons[3], blend.x);
                projectedShadow += mix(row0, row1, blend.y) * 0.1;
            }
            return projectedShadow;
        }
        float evaluateLgpeRoute1ProjectedLighting(float toon) {
            return min(
                clamp(toon, 0.0, 1.0) *
                    evaluateLgpeRoute1ProjectedShadow(),
                evaluateLgpeRoute1ProjectedCloud());
        }
        vec4 evaluateLgpeFieldGrassShader04Surface() {
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            vec4 texture02 = texture(uNormalTexture, uv1, 0.0);
            float texture03 = clamp(
                texture(uMetallicRoughnessTexture, uv1, 0.0).r,
                0.0,
                1.0);
            vec4 base = mix(texture01, texture02, texture03);
            float alpha =
                base.a * vColor.a *
                clamp(uMaterialTimeSec, 0.0, 1.0) *
                clamp(uMaterialAtlasSize.x, 0.0, 1.0);
            if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    0.0).r,
                0.0,
                1.0);
            float projectedLight =
                evaluateLgpeRoute1ProjectedLighting(toon);
            vec3 onGameColor =
                vec3(
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x,
                    uMaterialRect0.y);
            vec3 surface =
                base.rgb * vColor.rgb *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialFlags, 0.0, 1.0));
            vec3 lighting =
                mix(
                    uEmissiveFactor,
                    vec3(1.0),
                    projectedLight);
            return vec4(lighting * surface, alpha);
        }
        vec4 evaluateLgpeFieldGrassShader05Surface() {
            float scrollU = uMaterialRect0.y;
            float scrollV = uMaterialRect0.z;
            vec2 maskUv =
                vec2(vUv.x + scrollU, 1.0 - (vUv.y + scrollV));
            vec2 uv1Primary =
                vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec2 uv1Secondary =
                vec2(vSourceUv1.x + 1.0, 0.5 - vSourceUv1.y);
            float lightLine =
                clamp(texture(uNormalTexture, maskUv, 0.0).r, 0.0, 1.0);
            vec4 alpha01Primary =
                texture(uTexture, uv1Primary, 0.0);
            vec4 alpha01Secondary =
                texture(uTexture, uv1Secondary, 0.0);
            vec4 base = mix(
                alpha01Primary,
                alpha01Secondary,
                lightLine);
            float alpha =
                base.a * vColor.a *
                clamp(uMaterialFlags, 0.0, 1.0);
            if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 blendUv =
                vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            float greenBlend =
                clamp(texture(uEmissiveTexture, blendUv, 0.0).r, 0.0, 1.0);
            vec3 textureMap01 =
                texture(uMetallicRoughnessTexture, uv0, 0.0).rgb;
            vec3 textureMap02 =
                texture(uOcclusionTexture, uv0, 0.0).rgb;
            vec3 decoration =
                mix(textureMap02, textureMap01, greenBlend);
            float projectedLight =
                evaluateLgpeRoute1ProjectedLighting(1.0);
            vec3 onGameColor =
                vec3(
                    uMaterialAtlasSize.x,
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x);
            vec3 surface =
                (base.rgb + decoration) * vColor.rgb *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialTimeSec, 0.0, 1.0));
            vec3 lighting =
                mix(uEmissiveFactor, vec3(1.0), projectedLight);
            return vec4(lighting * surface, alpha);
        }
    )GLSL" + R"GLSL(
        vec4 evaluateLgpeRoadstoneOverlaySurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 onGameColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            float alpha =
                texture01.a * vColor.a *
                clamp(uMaterialRect0.y, 0.0, 1.0) *
                clamp(uMaterialRect0.x, 0.0, 1.0);
            vec3 surface =
                texture01.rgb * vColor.rgb *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialAtlasSize.y, 0.0, 1.0));
            vec3 lighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(toon));
            return vec4(lighting * surface * alpha, alpha);
        }
        vec4 evaluateLgpeRockMaskOverlaySurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec2 blendUv =
                vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            vec3 textureMap01 =
                texture(uTexture, uv0, sourceMipBias).rgb;
            vec3 textureMap02 =
                texture(uNormalTexture, uv0, sourceMipBias).rgb;
            vec4 greenHikari =
                texture(
                    uMetallicRoughnessTexture,
                    uv1,
                    sourceMipBias);
            float greenBlend = clamp(
                texture(
                    uOcclusionTexture,
                    blendUv,
                    sourceMipBias).r,
                0.0,
                1.0);
            float highlight = clamp(
                texture(
                    uEmissiveTexture,
                    uv1,
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uEnvTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 sourceColor =
                vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
            vec3 decoration =
                mix(textureMap02, textureMap01, greenBlend) +
                sourceColor * (1.0 - highlight);
            vec3 onGameColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            float alpha =
                greenHikari.a *
                clamp(uMaterialRect0.x, 0.0, 1.0);
            vec3 surface =
                decoration * greenHikari.rgb * vColor.rgb *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialAtlasSize.y, 0.0, 1.0));
            vec3 lighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(toon));
            return vec4(lighting * surface * alpha, alpha);
        }
    )GLSL"
    R"GLSL(
        float evaluateLgpeFieldRockLightToon(float toonCoordinate) {
            const float sourceValues[54] = float[54](
                1.0, 3.0, 5.0, 7.0, 10.0, 13.0, 16.0, 19.0,
                22.0, 26.0, 30.0, 34.0, 38.0, 43.0, 47.0, 53.0,
                58.0, 63.0, 68.0, 73.0, 80.0, 85.0, 91.0, 97.0,
                103.0, 110.0, 116.0, 122.0, 129.0, 135.0, 142.0,
                148.0, 155.0, 161.0, 168.0, 174.0, 181.0, 188.0,
                193.0, 200.0, 207.0, 211.0, 218.0, 223.0, 227.0,
                232.0, 236.0, 239.0, 243.0, 247.0, 249.0, 253.0,
                255.0, 255.0);
            float sourceTexel =
                clamp(toonCoordinate, 0.0, 1.0) * 512.0 - 0.5;
            int lower = int(floor(sourceTexel));
            int upper = lower + 1;
            float lowerValue = lower < 458
                ? 0.0
                : (lower >= 512
                    ? 255.0
                    : sourceValues[lower - 458]);
            float upperValue = upper < 458
                ? 0.0
                : (upper >= 512
                    ? 255.0
                    : sourceValues[upper - 458]);
            return mix(
                       lowerValue,
                       upperValue,
                       fract(sourceTexel)) /
                   255.0;
        }
        float lgpeFlowerCoverage(float alpha) {
            float coverageT =
                clamp((alpha - 0.55) / (0.85 - 0.55), 0.0, 1.0);
            return coverageT * coverageT * (3.0 - 2.0 * coverageT);
        }
        float lgpeFlowerDitherThreshold(vec2 fragmentPosition) {
            ivec2 pixel = ivec2(mod(floor(fragmentPosition), 4.0));
            int index = pixel.x + pixel.y * 4;
            const float bayer[16] = float[16](
                 0.5,  8.5,  2.5, 10.5,
                12.5,  4.5, 14.5,  6.5,
                 3.5, 11.5,  1.5,  9.5,
                15.5,  7.5, 13.5,  5.5);
            return bayer[index] / 16.0;
        }
        vec3 lgpeFlowerFieldHighlight(vec3 sourceColor) {
            float maximum = max(max(sourceColor.r, sourceColor.g), sourceColor.b);
            float minimum = min(min(sourceColor.r, sourceColor.g), sourceColor.b);
            float chroma = maximum - minimum;
            float sourceSaturation =
                maximum > 0.000001 ? chroma / maximum : 0.0;
            float saturation = clamp(sourceSaturation * 1.14, 0.0, 1.0);
            float value = maximum * 1.12;
            float adjustedMinimum = value * (1.0 - saturation);
            float adjustedChroma = value * saturation;
            vec3 hueComponent =
                chroma > 0.000001
                    ? (sourceColor - vec3(minimum)) / chroma
                    : vec3(0.0);
            return vec3(adjustedMinimum) + hueComponent * adjustedChroma;
        }
        vec4 evaluateLgpeFieldFlowerSurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            bool buildmodelReview =
                uMaterialMode > 19.5 && uMaterialMode < 20.5;
            // Canonical BuildModel texture rows remain in source top-down
            // order when uploaded. Their raw source UV0 therefore samples
            // directly here; the road-scene texture retains the older
            // bottom-origin upload contract.
            vec2 uv0 =
                buildmodelReview
                    ? vUv
                    : vec2(vUv.x, 1.0 - vUv.y);
            vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            float sourceAlpha =
                texture01.a * authoredVertexColor.a *
                clamp(uMaterialRect0.y, 0.0, 1.0) *
                clamp(uMaterialRect0.x, 0.0, 1.0);
            float alpha = sourceAlpha;
            if (buildmodelReview) {
                alpha = lgpeFlowerCoverage(sourceAlpha);
                if (alpha <= lgpeFlowerDitherThreshold(gl_FragCoord.xy)) {
                    discard;
                }
            } else if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }

            if (buildmodelReview) {
                float projectedLight =
                    evaluateLgpeRoute1ProjectedLighting(1.0);
                vec3 normal = normalize(vWorldNormal);
                const vec3 sourceSunRay =
                    vec3(0.5533391237, 0.2078260481, -0.8066127300);
                float normalDotLight = dot(normal, -sourceSunRay);
                float toonCoordinate = normalDotLight * 0.5 + 0.5;
                float toon = clamp(
                    texture(
                        uOcclusionTexture,
                        vec2(toonCoordinate, 1.0 - toonCoordinate),
                        sourceMipBias).r,
                    0.0,
                    1.0);
                vec3 projectedLighting = mix(
                    uEmissiveFactor,
                    vec3(1.0),
                    projectedLight);
                vec3 projectionCompensation = mix(
                    vec3(1.0),
                    projectedLighting,
                    0.25);
                vec3 accepted = lgpeFlowerFieldHighlight(texture01.rgb);
                vec3 exact =
                    texture01.rgb * authoredVertexColor.rgb *
                    projectionCompensation;
                vec3 restrained = mix(accepted, exact, 0.35);
                vec3 fieldLighting = mix(
                    uEmissiveFactor,
                    vec3(1.0),
                    evaluateLgpeRoute1ProjectedLighting(toon));
                return vec4(
                    restrained * (fieldLighting + vec3(0.12)),
                    alpha);
            }

            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 onGameColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            vec3 surface =
                texture01.rgb * authoredVertexColor.rgb *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialAtlasSize.y, 0.0, 1.0));
            vec3 lighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(toon));
            return vec4(lighting * surface, alpha);
        }
        vec4 evaluateLgpeFieldRockSurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 =
                vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec2 uv2 =
                vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
            vec2 blendUv =
                vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
            vec4 rockTexture =
                texture(uTexture, uv1, sourceMipBias);
            vec3 groundTexture02 =
                texture(uNormalTexture, uv0, sourceMipBias).rgb;
            vec3 groundTexture01 =
                texture(
                    uMetallicRoughnessTexture,
                    uv0,
                    sourceMipBias).rgb;
            float blend = clamp(
                texture(
                    uOcclusionTexture,
                    blendUv,
                    sourceMipBias).r,
                0.0,
                1.0);
            vec4 borderTexture =
                texture(uEmissiveTexture, uv2, sourceMipBias);

            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float shadowToon = clamp(
                texture(
                    uEnvTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            float lightToon =
                evaluateLgpeFieldRockLightToon(toonCoordinate);
            vec3 viewDirection =
                normalize(uCameraPos - vWorldPos);
            float rimMin = uOcclusionStrength;
            float rimMax = uMaterialTimeSec;
            float rimStrength = uMaterialFlags;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp(
                      ((1.0 - dot(normal, viewDirection)) - rimMin) /
                          rimSpan,
                      0.0,
                      1.0) *
                      rimStrength
                : 0.0;
            vec3 rock =
                rockTexture.rgb +
                uEmissiveFactor * lightToon +
                vec3(
                    uNormalScale,
                    uMetallicFactor,
                    uRoughnessFactor) *
                    rim * rockTexture.a;
            vec3 ground =
                mix(groundTexture02, groundTexture01, blend);
            vec3 surface =
                mix(rock, ground, clamp(borderTexture.a, 0.0, 1.0));
            vec3 shadowColor =
                vec3(
                    uMaterialAtlasSize.x,
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x);
            vec3 onGameColor =
                vec3(
                    uMaterialRect0.y,
                    uMaterialRect0.z,
                    uMaterialRect0.w);
            vec3 lighting = mix(
                shadowColor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(shadowToon));
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            return vec4(
                lighting * borderTexture.rgb *
                    authoredVertexColor.rgb * surface *
                    mix(
                        vec3(1.0),
                        onGameColor,
                        clamp(uMaterialRect1.x, 0.0, 1.0)),
                clamp(uMaterialRect1.y, 0.0, 1.0));
        }
        vec4 evaluateLgpeFieldSignSurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            // Canonical BNTX RGBA rows are top-down. The source program's
            // 1-V convention is therefore already represented by the decode.
            vec2 uv0 = vUv;
            vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            vec2 toonUv =
                vec2(toonCoordinate, 1.0 - toonCoordinate);
            float shadowToon = clamp(
                texture(
                    uOcclusionTexture,
                    toonUv,
                    sourceMipBias).r,
                0.0,
                1.0);
            float lightToon =
                evaluateLgpeFieldRockLightToon(toonCoordinate);
            vec3 viewDirection =
                normalize(uCameraPos - vWorldPos);
            float rimMin = uMaterialAtlasSize.y;
            float rimMax = uMaterialRect0.x;
            float rimStrength = uMaterialRect0.y;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp(
                      ((1.0 - dot(normal, viewDirection)) - rimMin) /
                          rimSpan,
                      0.0,
                      1.0) *
                      rimStrength
                : 0.0;
            vec3 rimColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            // Source ShadowColor and OnGameColor are exactly white, so the
            // recovered AutoShadow and OnGame mixes are neutral for this
            // Route 1 sign material.
            vec3 surface =
                texture01.rgb +
                uEmissiveFactor * lightToon +
                rimColor * rim;
            vec3 shadowColor =
                vec3(
                    uNormalScale,
                    uMetallicFactor,
                    uRoughnessFactor);
            vec3 lighting = mix(
                shadowColor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(shadowToon));
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            return vec4(
                lighting * surface * authoredVertexColor.rgb,
                texture01.a * authoredVertexColor.a);
        }
        vec4 evaluateLgpeFieldEncounterGrassSurface() {
            float sourceMipBias = uMaterialFlipbook0.w;
            // The canonical top-down decode already accounts for the
            // Switch-to-runtime texture-origin convention.
            vec2 uv0 = vUv;
            vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
            if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }
            float rimMask = clamp(
                texture(uNormalTexture, uv0, sourceMipBias).r,
                0.0,
                1.0);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float toonCoordinate =
                dot(normal, -sourceSunRay) * 0.5 + 0.5;
            float shadowToon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 viewDirection =
                normalize(uCameraPos - vWorldPos);
            float rimMin = uMaterialAtlasSize.y;
            float rimMax = uMaterialRect0.x;
            float rimStrength = uMaterialRect0.y;
            float rim = smoothstep(
                            rimMin,
                            max(rimMax, rimMin + 1e-5),
                            clamp(
                                1.0 - abs(dot(normal, viewDirection)),
                                0.0,
                                1.0)) *
                        rimStrength * rimMask;
            vec3 shadowColor =
                vec3(
                    uNormalScale,
                    uMetallicFactor,
                    uRoughnessFactor);
            vec3 rimColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            vec3 base =
                texture01.rgb * authoredVertexColor.rgb;
            vec3 lighting = mix(
                shadowColor,
                vec3(1.0),
                evaluateLgpeRoute1ProjectedLighting(shadowToon));
            return vec4(
                (base + rimColor * rim) * lighting,
                texture01.a * authoredVertexColor.a);
        }
        vec4 evaluateLgpeFieldObjectTreeMikiSurface() {
            vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
            vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
            vec4 texture01 = texture(uTexture, uv0, 0.0);
            float highlightAlpha =
                texture(uNormalTexture, uv1, 0.0).a;
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    0.0).r,
                0.0,
                1.0);
            vec3 viewDirection = normalize(uCameraPos - vWorldPos);
            float rimMin = uMaterialTimeSec;
            float rimMax = uMaterialFlags;
            float rimStrength = uMaterialAtlasSize.x;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp(
                      (clamp(
                           1.0 - dot(normal, viewDirection),
                           0.0,
                           1.0) -
                       rimMin) /
                          rimSpan,
                      0.0,
                      1.0) *
                      rimStrength
                : 0.0;
            vec3 shadowColor =
                max(vec3(uNormalScale, uMetallicFactor, uRoughnessFactor),
                    vec3(0.0));
            vec3 rimColor =
                max(
                    vec3(
                        uMaterialAtlasSize.y,
                        uMaterialRect0.x,
                        uMaterialRect0.y),
                    vec3(0.0));
            vec3 lighting = mix(shadowColor, vec3(1.0), toon);
            vec3 surface =
                texture01.rgb + rimColor * rim * highlightAlpha;
            vec4 authoredVertexColor = vColor * uVertexColorMul;
            return vec4(
                lighting * surface * authoredVertexColor.rgb,
                texture01.a * authoredVertexColor.a);
        }

        float hash11(float x) { return fract(sin(x * 12.9898) * 43758.5453); }
        float hash21(vec2 p) {
            float n = dot(p, vec2(127.1, 311.7));
            return fract(sin(n) * 43758.5453);
        }
        float valueNoise2D(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            float a = hash21(i);
            float b = hash21(i + vec2(1.0, 0.0));
            float c = hash21(i + vec2(0.0, 1.0));
            float d = hash21(i + vec2(1.0, 1.0));
            return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
        }
        float smoothFlicker(float t, float seed) {
            float x = t * 9.0 + seed * 97.0;
            float i = floor(x);
            float f = fract(x);
            f = f * f * (3.0 - 2.0 * f);
            return mix(hash11(i), hash11(i + 1.0), f);
        }
        float fbm2D(vec2 p) {
            float v = 0.0;
            float a = 0.5;
            for (int k = 0; k < 5; ++k) {
                v += a * valueNoise2D(p);
                p *= 2.02;
                a *= 0.5;
            }
            return v;
        }
        vec2 fbmGrad(vec2 p) {
            float e = 0.03;
            float nx = fbm2D(p + vec2(e, 0.0)) - fbm2D(p - vec2(e, 0.0));
            float ny = fbm2D(p + vec2(0.0, e)) - fbm2D(p - vec2(0.0, e));
            return vec2(nx, ny) / (2.0 * e);
        }
        vec2 curl2D(vec2 p) {
            vec2 g = fbmGrad(p);
            return vec2(g.y, -g.x);
        }
        vec2 advect(vec2 p, float flowY, float amount) {
            vec2 c1 = curl2D(p * 1.30 + vec2(0.0, -flowY * 0.10));
            vec2 c2 = curl2D(p * 2.70 + vec2(3.1, -flowY * 0.18));
            return p + (c1 * 0.65 + c2 * 0.35) * amount;
        }
        vec3 tonemapSoftLocal(vec3 c) {
            return c / (vec3(1.0) + c);
        }
        vec2 clampUvToRegionPixels(vec2 localUV01, vec4 rectUv) {
            vec2 atlasSize = max(uMaterialAtlasSize, vec2(1.0));
            vec2 rectPx = max(rectUv.zw * atlasSize, vec2(1.0));
            vec2 minPx = vec2(0.5) / atlasSize;
            vec2 maxPx = (rectPx - vec2(0.5)) / atlasSize;
            vec2 uv = clamp(localUV01, vec2(0.0), vec2(1.0));
            vec2 regionUv = rectUv.xy + uv * rectUv.zw;
            return rectUv.xy + clamp(regionUv - rectUv.xy, minPx, maxPx);
        }
        vec4 sampleAtlasCombined(vec4 rectUv, vec2 grid, float frames, float fps, vec2 localUV01, float seed, float t, bool coherent) {
            float speed = coherent ? 1.0 : mix(0.85, 1.10, hash11(seed * 31.7 + 2.3));
            float phase = coherent ? 0.0 : (seed * frames);
            float f = floor(t * fps * speed + phase);
            float frame = mod(f, max(1.0, frames));
            float cols = max(1.0, grid.x);
            float rows = max(1.0, grid.y);
            float col = mod(frame, cols);
            float rowFromTop = floor(frame / cols);
            float row = (rows - 1.0) - rowFromTop;
            vec2 cellUVLocal = (vec2(col, row) + localUV01) / vec2(cols, rows);
            vec2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
            return texture(uTexture, cellUv);
        }
        vec4 sampleFireDirect0(vec2 uvLocal, float seed, float t) {
            return sampleAtlasCombined(
                uMaterialRect0,
                uMaterialFlipbook0.xy,
                uMaterialFlipbook0.z,
                uMaterialFlipbook0.w,
                uvLocal,
                seed,
                t,
                true);
        }
        vec4 sampleAtlasCombinedTopLeft(vec4 rectUv, vec2 grid, float frames, float fps, vec2 localUV01, float t) {
            float f = floor(t * fps);
            float frame = mod(f, max(1.0, frames));
            float cols = max(1.0, grid.x);
            float rows = max(1.0, grid.y);
            float col = mod(frame, cols);
            float row = floor(frame / cols);
            vec2 cellUVLocal = (vec2(col, row) + localUV01) / vec2(cols, rows);
            vec2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
            return texture(uTexture, cellUv);
        }
    )GLSL"
    R"GLSL(
        float hash41(vec4 p) {
            return fract(sin(dot(p, vec4(127.1, 311.7, 74.7, 269.5))) * 43758.5453123);
        }
        float valueNoise4D(vec4 p) {
            vec4 i = floor(p);
            vec4 f = fract(p);
            vec4 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
            float accum = 0.0;
            for (int dw = 0; dw < 2; ++dw) {
                for (int dz = 0; dz < 2; ++dz) {
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            vec4 corner = vec4(float(dx), float(dy), float(dz), float(dw));
                            float wx = mix(1.0 - u.x, u.x, corner.x);
                            float wy = mix(1.0 - u.y, u.y, corner.y);
                            float wz = mix(1.0 - u.z, u.z, corner.z);
                            float ww = mix(1.0 - u.w, u.w, corner.w);
                            accum += hash41(i + corner) * wx * wy * wz * ww;
                        }
                    }
                }
            }
            return accum;
        }
        float authoredFireNoise(vec4 p) {
            float value = 0.0;
            float amplitude = 1.0;
            float amplitudeSum = 0.0;
            for (int octave = 0; octave < 2; ++octave) {
                value += amplitude * valueNoise4D(p);
                amplitudeSum += amplitude;
                p *= 2.0;
                amplitude *= 0.5;
            }
            return value / max(amplitudeSum, 1e-5);
        }
        vec4 evalNativeLayeredUnlitDisplaced() {
            float emissionIntensity = max(uMaterialRect0.y, 0.0);
            bool exactSourceTrack = uMaterialFlags > 1.5;
            float baseScrollHz = max(uMaterialRect0.z, 0.0);
            vec2 baseOffset = exactSourceTrack
                ? uMaterialRect0.zw
                : vec2(
                    baseScrollHz > 0.0
                        ? 1.0 - fract(uMaterialTimeSec * baseScrollHz)
                        : 0.0,
                    0.0);
            vec2 baseScale = exactSourceTrack
                ? vec2(uMaterialFlipbook0.w, uMaterialFlipbook1.w)
                : vec2(1.0);
            vec2 baseUv = vec2(
                (vUv.x - baseOffset.x) * baseScale.x,
                1.0 - ((1.0 - vUv.y) - baseOffset.y) * baseScale.y);
            // UVScaleOffset animates U across a horizontally seamless mask.
            // Explicit wrapping avoids a clamp-to-edge jump at each reset.
            baseUv.x = fract(baseUv.x);
            vec4 base = texture(uTexture, baseUv);
            vec4 weights = clamp(
                texture(uMetallicRoughnessTexture, baseUv),
                vec4(0.0),
                vec4(1.0));
            float coverage = clamp(1.0 - dot(weights, vec4(1.0)), 0.0, 1.0);
            vec3 color = base.rgb * coverage;
            vec3 layer1 = uMaterialFlipbook0.xyz;
            vec3 layer2 = uMaterialFlipbook1.xyz;
            // Scarlet Unlit variation 48 multiplies every layer color by the
            // shared base map before successive alpha-over compositing.
            color = mix(color, base.rgb * layer1, weights.r);
            coverage += weights.r * (1.0 - coverage);
            color = mix(color, base.rgb * layer2, weights.g);
            coverage += weights.g * (1.0 - coverage);
            color = mix(color, base.rgb, weights.b);
            coverage += weights.b * (1.0 - coverage);
            color = mix(color, base.rgb, weights.a);
            coverage += weights.a * (1.0 - coverage);
            vec4 surface = vec4(
                color / max(coverage, 1e-6) * emissionIntensity,
                1.0);
            // SSSEffect subtype 3 uses the complete dynamic alpha. Cached
            // world-scene draws carry it in vColor; direct indexed draws
            // carry it in uVertexColorMul. Ignoring either path leaves hidden
            // smoke puffs visible on one of the submission routes.
            surface.a = uMaterialFlags > 2.5 && uMaterialFlags < 3.125
                ? clamp(vColor.a * uVertexColorMul.a, 0.0, 1.0)
                : 1.0;
            return surface;
        }

        vec3 applyNativeGastlySmokeLighting(vec3 color) {
            vec3 normal = normalize(vWorldNormal);
            vec3 toCamera = uCameraPos - vWorldPos;
            float toCameraLengthSquared = dot(toCamera, toCamera);
            vec3 viewDirection = toCameraLengthSquared > 1e-10
                ? toCamera * inversesqrt(toCameraLengthSquared)
                : vec3(0.0, 0.0, 1.0);
            float facing = clamp(dot(normal, viewDirection), -1.0, 1.0);
            // Z-A IkCharacter: HalfLambertBias=.1, ShadowStrength=.7,
            // RimLightOffset=.2, RimLightContrast=2,
            // RimLightIntensity=.8, BackRimLightIntensity=.01.
            float halfLambert = clamp(facing * 0.5 + 0.6, 0.0, 1.0);
            float diffuse = mix(1.0, halfLambert, 0.7);
            float edge = clamp(1.0 - max(facing, 0.0), 0.0, 1.0);
            float rimDomain = clamp((edge - 0.2) / 0.8, 0.0, 1.0);
            float rim = rimDomain * rimDomain * 0.8;
            float backRim = clamp(-facing, 0.0, 1.0) * 0.01;
            return max(color * (diffuse + rim + backRim), vec3(0.0));
        }

        vec4 evalAuthoredFireMesh() {
            vec2 uv = clamp(
                vUv + vec2(uMaterialFlipbook1.x, uMaterialFlipbook1.y),
                vec2(0.0),
                vec2(1.0));
            vec4 baked = sampleAtlasCombinedTopLeft(
                uMaterialRect0,
                uMaterialFlipbook0.xy,
                uMaterialFlipbook0.z,
                uMaterialFlipbook0.w,
                uv,
                uMaterialTimeSec);
            float rgbCoverage = smoothstep(
                0.03,
                0.20,
                max(baked.r, max(baked.g, baked.b)));
            baked.a = max(baked.a, rgbCoverage);
            float baseEngulf = 1.0 - smoothstep(0.0, 0.28, clamp(vGenerated.y, 0.0, 1.0));
            vec2 centerXZ = vGenerated.xz - vec2(0.5, 0.5);
            float centerDist = length(centerXZ * vec2(1.2, 1.0));
            float coreMask = 1.0 - smoothstep(0.0, 0.23, centerDist);
            float tipHideMask = baseEngulf * coreMask;
            float warmMask =
                smoothstep(0.68, 0.98, baked.r) *
                smoothstep(0.56, 0.90, baked.g) *
                (1.0 - smoothstep(0.22, 0.58, baked.b));
            baked.rgb = mix(baked.rgb, vec3(1.0, 0.68, 0.16), warmMask * 0.44);
            baked.rgb = mix(baked.rgb, vec3(1.0, 0.82, 0.30), tipHideMask * 0.55);
            baked.a = max(baked.a, baseEngulf * 0.95);
            baked.a = max(baked.a, tipHideMask);
            if (baked.a <= 0.08) discard;
            baked.a = 1.0;
            return baked;
        }
        float lickBlobs(float x, float y, vec2 advP, float flowY, float seed) {
            float k = y * 6.6 + flowY * 0.55;
            float seg = floor(k);
            float f = fract(k);
            float cx1 = (hash11(seg + seed * 31.0) - 0.5) * 0.95 * (1.0 - y);
            float cx2 = (hash11(seg + seed * 73.0) - 0.5) * 0.95 * (1.0 - y);
            float w = mix(0.34, 0.085, y);
            vec2 q1 = vec2((x - cx1) / w,        (f - 0.30) / 0.70);
            vec2 q2 = vec2((x - cx2) / (w*0.85), (f - 0.45) / 0.65);
            float m1 = 1.0 - smoothstep(0.60, 1.00, length(q1 * vec2(1.0, 1.45)));
            float m2 = 1.0 - smoothstep(0.60, 1.00, length(q2 * vec2(1.0, 1.60)));
            float br = fbm2D(advP * vec2(7.0, 12.0) + seed * 17.0);
            float broken = smoothstep(0.25, 0.88, br);
            float gate = smoothstep(0.05, 0.22, y) * (1.0 - smoothstep(0.86, 1.0, y));
            float m = (m1 + 0.85 * m2) * broken * gate;
            return clamp(m, 0.0, 1.0);
        }

        vec4 evalFireTailExact() {
            float age = clamp(vColor.r, 0.0, 1.0);
            float vSeed = clamp(vColor.g, 0.0, 1.0);
            float t = uMaterialTimeSec;

            // Legacy fire_tail.frag flips gl_PointCoord.y; shared quads already provide the legacy-facing orientation.
            vec2 uv = vUv;

            vec2 cc = (uv - 0.5) * 2.0;
            float x = cc.x;
            float y = clamp(uv.y, 0.0, 1.0);
            float bottomFade = smoothstep(0.00, 0.11, y);

            float baseT = smoothstep(0.00, 0.22, y);
            float xScaleBase = mix(2.55, 1.90, baseT);
            float yScaleBase = mix(1.05, 0.75, baseT);
            float reBase = length(vec2(cc.x * xScaleBase, cc.y * yScaleBase));
            float radialMaskBase = 1.0 - smoothstep(0.98, 1.10, reBase);
            float tightMask      = 1.0 - smoothstep(0.62, 0.88, reBase);

            float reLoose = length(cc * vec2(0.55, 0.85));
            float radialMaskLoose = 1.0 - smoothstep(0.98, 1.20, reLoose);

            float fade = (1.0 - age);
            fade = pow(mix(fade, 1.0, 0.25), 0.75);

            vec2 wobble = vec2(
                smoothFlicker(t * 0.9, vSeed + 0.17),
                smoothFlicker(t * 1.1, vSeed + 0.73)
            ) - 0.5;
            vec4 fb1 = vec4(1.0);
            vec4 fb2 = vec4(1.0);
            int fireFlags = int(uMaterialFlags + 0.5);
            int has1 = ((fireFlags & 1) != 0) ? 1 : 0;
            int has2 = ((fireFlags & 2) != 0) ? 1 : 0;
            int authoredFireMesh = ((fireFlags & 8) != 0) ? 1 : 0;
            if (authoredFireMesh == 1) {
                return evalAuthoredFireMesh();
            }
            float wobbleScale1 = (has2 == 1) ? 0.010 : 0.0009;
            float wobbleScale2 = (has2 == 1) ? 0.002 : 0.0002;
            vec2 local1 = uv + wobble * wobbleScale1;
            vec2 local2 = uv + wobble * wobbleScale2;
            if (has1 == 1) {
                fb1 = sampleAtlasCombined(uMaterialRect0, uMaterialFlipbook0.xy, uMaterialFlipbook0.z, uMaterialFlipbook0.w, local1, vSeed, t, has2 != 1);
                if (has2 == 1) {
                    fb2 = sampleAtlasCombined(uMaterialRect1, uMaterialFlipbook1.xy, uMaterialFlipbook1.z, uMaterialFlipbook1.w, local2, vSeed, t, false);
                } else {
                    fb2 = fb1;
                }
            }

            if (has1 == 1 && has2 == 0) {
                vec2 directUv = vec2(uv.x, 1.0 - uv.y);
                vec4 fbDirect = sampleFireDirect0(directUv, vSeed, t);
                float alpha = clamp(fbDirect.a, 0.0, 1.0);
                vec3 rgb = clamp(fbDirect.rgb * 1.15, 0.0, 1.0);
                alpha *= bottomFade;
                alpha *= fade;
                alpha = clamp(alpha, 0.0, 0.985);
                if (alpha < 0.003) discard;
                rgb *= alpha;
                return vec4(rgb, alpha);
            }

            float fb1A   = clamp(fb1.a, 0.0, 1.0);
            float fb1Lum = clamp(dot(fb1.rgb, vec3(0.3333)), 0.0, 1.0);

            float speed = (has2 == 1) ? mix(0.95, 1.10, hash11(vSeed * 19.31)) : 1.0;
            float flow  = t * 1.55 * speed;
            float flowY = flow * mix(0.75, 1.55, y * y);
            float width = mix(0.30, 0.055, pow(y, 2.35));
            float fb1Thicken = 2.80;
            float widthHybrid = width * fb1Thicken;
            float yy = (y * 2.0 - 1.0);
            yy = yy * 1.45 + 0.38;
            yy /= 1.12;
            vec2 p = vec2(x / widthHybrid, yy);
            p *= 1.22;
            float sway = fbm2D(vec2(x * 1.7, y * 3.8) + vec2(0.0, -flowY * 0.65) + vSeed * 7.0);
            p.x += (sway - 0.5) * ((has2 == 1) ? 0.015 : 0.004) * (1.0 - y);
            float d0 = length(p);
            vec2 advP = advect(p * vec2(1.20, 1.0) + vSeed * 6.0, flowY, 0.25);
            float n = fbm2D(advP * vec2(2.7, 4.5) + vSeed * 11.0);
            float d = d0 + (n - 0.5) * 0.18 * (1.0 - y);
            float core  = clamp(1.0 - smoothstep(0.00, 0.88, d), 0.0, 1.0);
            float outer = clamp(1.0 - smoothstep(0.30, 1.05, d), 0.0, 1.0);
            float blobs = lickBlobs(x, y, advP, flowY, vSeed);
            float body  = clamp(smoothstep(0.92, 0.12, d), 0.0, 1.0);

            float procAlpha = body * (0.60 + 0.55 * blobs);
            float calmFlicker = smoothFlicker(t * 1.2, vSeed);
            procAlpha *= (has2 == 1) ? (0.92 + 0.15 * calmFlicker) : (0.985 + 0.03 * calmFlicker);
            procAlpha *= bottomFade;
            procAlpha *= fade;
            procAlpha = 1.0 - exp(-procAlpha * 1.85);
            procAlpha = clamp(procAlpha, 0.0, 0.96);

            vec3 yellow = vec3(1.70, 1.20, 0.28);
            vec3 red    = vec3(1.45, 0.18, 0.06);
            vec3 orange = vec3(1.60, 0.55, 0.12);
            float wave = 0.5 + 0.5 * sin((x * 1.8 + y * 8.5 - flowY * 4.9) + vSeed * 7.0);
            float baseBoundary = 0.34;
            float segCount = 6.0;
            float kk = y * segCount - flowY * 0.55;
            float seg = floor(kk);
            float segRand  = hash11(seg + vSeed * 71.3);
            float segRand2 = hash11(seg + vSeed * 19.7 + 5.0);
            float tri1 = abs(fract((x * 0.85 + y * 1.05 - flowY * 0.18) * 2.8 + vSeed * 7.0) - 0.5) * 2.0;
            float tri2 = abs(fract((x * 1.10 - y * 0.60 - flowY * 0.14) * 3.8 + vSeed * 3.0) - 0.5) * 2.0;
            float zig = mix(tri1, tri2, 0.50 + 0.50 * (segRand - 0.5));
            zig = smoothstep(0.15, 0.85, zig);
            float warp = fbm2D(advect(vec2(x * 0.85, y * 1.2) + vSeed * 6.0, flowY, 0.22) * vec2(4.5, 7.5)) - 0.5;
            float jag = 0.0;
            jag += (segRand  - 0.5) * 0.10;
            jag += (segRand2 - 0.5) * 0.05;
            jag += (zig      - 0.5) * 0.14;
            jag += warp * 0.06;
            jag *= (1.0 - 0.55 * smoothstep(0.65, 1.0, y));
            float boundary = clamp(baseBoundary + jag, 0.14, 0.62);
            float splitWidth = 0.11;
            float redMask = smoothstep(boundary, boundary + splitWidth, y);
            vec3 procRgb = mix(yellow, red, redMask);
            float band = smoothstep(boundary - 0.02, boundary + 0.02, y) *
                         (1.0 - smoothstep(boundary + 0.02, boundary + 0.10, y));
            procRgb = mix(procRgb, orange, 0.55 * band);
            float climb = core * (1.0 - smoothstep(0.55, 0.95, y)) * (0.35 + 0.65 * wave);
            procRgb = mix(procRgb, yellow, 0.18 * climb);
            procRgb *= (1.18 + 0.35 * outer);

            vec3 hybridRgb = procRgb;
            float hybridAlpha = procAlpha;
            if (has1 == 1) {
                float aMod = mix(0.55, 1.65, fb1A);
                float lMod = mix(0.85, 1.25, fb1Lum);
                hybridAlpha = clamp(hybridAlpha * aMod, 0.0, 0.96);
                hybridRgb *= lMod;
                hybridRgb *= mix(vec3(1.0), fb1.rgb * 1.35, 0.30);
            }

            vec3 fb2Rgb = fb2.rgb;
            float fb2Alpha = pow(clamp(fb2.a, 0.0, 1.0), 0.66);
            float hot = smoothstep(0.10, 0.55, 1.0 - y);
            vec3 tint = mix(red, yellow, hot);
            fb2Rgb *= tint * 1.30;
            fb2Alpha *= tightMask;
            fb2Alpha *= bottomFade;

            float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
            float fb2MaskedA    = fb2Alpha    * radialMaskBase;
            float mixW = 0.50;
            vec3 rgb = mix(hybridRgb, fb2Rgb, mixW);
            float alpha = mix(hybridMaskedA, fb2MaskedA, mixW);
            alpha *= fade;
            alpha = clamp(alpha + 0.10 * outer * fade, 0.0, 0.985);
            float exposure = 2.60;
            rgb *= exposure;
            float emissive = (0.85 * outer + 0.45 * core) * fade;
            rgb *= (1.0 + 2.10 * emissive);
            rgb = tonemapSoftLocal(rgb);
            if (alpha < 0.003) discard;
            rgb *= alpha;
            return vec4(rgb, alpha);
        }
    )GLSL"
    R"GLSL(
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

        vec3 encodeLgpeFinalColor(vec3 linearColor) {
            // The source writes linear color to UNORM before its dedicated
            // gamma_correction shader applies the standard sRGB transfer.
            return linearToSrgb(clamp(linearColor, 0.0, 1.0));
        }

        vec3 resolveWorldSceneColor(vec3 linearColor) {
            vec3 clamped = clamp(linearColor, 0.0, 1.0);
            return (uSceneColorPostEnabled > 0.5)
                ? clamped
                : encodeLgpeFinalColor(clamped);
        }

        vec3 safeNormalize(vec3 value, vec3 fallback) {
            float len2 = dot(value, value);
            if (len2 < 1e-8) return fallback;
            return value * inversesqrt(len2);
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
    )GLSL"
    R"GLSL(
        vec3 computeMappedNormal(
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float materialNormalMultiplier) {
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
                uNormalTexture,
                sampleUv,
                uvDx,
                uvDy).xyz;
            vec2 mapXY = normalTexel.xy * 2.0 - 1.0;
            mapXY *= max(uNormalScale, 0.0) * 1.25 *
                max(materialNormalMultiplier, 0.0);
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

        vec3 applyWorldLitModel(vec3 linearColor, vec3 n, vec2 sampleUv, vec2 uvDx, vec2 uvDy) {
            vec4 orm = sampleTextureWithWrap(
                uMetallicRoughnessTexture,
                sampleUv,
                uvDx,
                uvDy);
            float roughness = clamp(orm.g * clamp(uRoughnessFactor, 0.0, 1.0), 0.16, 1.0);
            float metallic = clamp(orm.b * clamp(uMetallicFactor, 0.0, 1.0), 0.0, 1.0);
            float occTex = sampleTextureWithWrap(
                uOcclusionTexture,
                sampleUv,
                uvDx,
                uvDy).r;
            float ao = mix(1.0, occTex, clamp(uOcclusionStrength, 0.0, 1.0));

            vec3 albedo = clamp(linearColor, 0.0, 1.0);
            bool useSpecularStrengthTexture =
                uMaterialMode > 1.5 && uMaterialMode < 2.5 &&
                uMaterialFlags > 4.5 && uMaterialFlags < 5.5;
            float dielectricSpecular = useSpecularStrengthTexture
                ? clamp(uMaterialRect0.x, 0.0, 1.0) * clamp(orm.a, 0.0, 1.0)
                : 0.04;
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
                n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);

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
            // Plain Game Freak Eye materials carry a negative coat-coverage
            // marker. Their authored glint is an explicit mask plus direct
            // response, so the neutral-room reflection would double that
            // response and make the eye look glassy. EyeClearCoat keeps its
            // non-negative source coverage and the ordinary PBR IBL path.
            if (uMaterialMode > 27.5 && uMaterialMode < 28.5 &&
                uMaterialRect1.w < -0.5) {
                specularIBL = vec3(0.0);
            }
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
            // PLA's plain Eye shader can carry a sparse layer-5 catchlight in
            // the emissive texture while the rest of the eye still needs its
            // softer native diffuse response. A material-wide emissive guard
            // incorrectly disabled that response for Geodude's entire eye.
            // Gate the fill per pixel instead: authored emissive regions stay
            // exact, while non-emissive sclera/iris pixels retain their baked
            // layer color. The proportional blend still preserves pupils.
            if (uMaterialMode > 27.5 && uMaterialMode < 28.5 &&
                uMaterialRect1.w < -0.5) {
                float emissiveCoverage = clamp(
                    max(emissive.r, max(emissive.g, emissive.b)),
                    0.0,
                    1.0);
                shaded = mix(
                    shaded,
                    albedo,
                    0.25 * (1.0 - emissiveCoverage));
            }
            return max(shaded + emissive, vec3(0.0));
        }

        vec3 applyNativeIkCharacter(
            vec3 linearColor,
            vec3 n,
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy) {
            vec3 cameraForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
            if (dot(cameraRight, cameraRight) < 1e-6) {
                cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
            }
            cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
            vec3 viewDirection = safeNormalize(
                uCameraPos - vWorldPos,
                -cameraForward);
            vec3 lightPosition =
                uCameraPos + cameraRight * 0.5 - cameraForward * 0.8660254;
            vec3 lightDirection = safeNormalize(
                lightPosition - uCameraTarget,
                vec3(0.45, 0.86, 0.24));
            vec4 shadowSpec = uUseMetallicRoughnessTexture > 0.5
                ? sampleTextureWithWrap(
                      uMetallicRoughnessTexture,
                      sampleUv,
                      uvDx,
                      uvDy)
                : vec4(1.0, 1.0, 1.0, 0.0);
            vec4 surfaceControl = uUseOcclusionTexture > 0.5
                ? sampleTextureWithWrap(
                      uOcclusionTexture,
                      sampleUv,
                      uvDx,
                      uvDy)
                : vec4(1.0, 0.0, 1.0 / 3.0, 0.0);
            // Z-A resolves AO into its ambient/base blend before authored
            // shadow color; multiplying the final albedo created dark bands.
            float aoBaseWeight = clamp(
                surfaceControl.r * max(uOcclusionStrength, 0.0),
                0.0,
                1.0);
            float metallic = clamp(surfaceControl.g, 0.0, 1.0);
            float specularOffset = surfaceControl.b * 1.5 - 0.5;
            float specularContrast = surfaceControl.a * 5.0;
            float reflectionBlur = max(uMaterialRect0.x, 0.0);
            float diffusionLevels = clamp(uMaterialRect0.y, 0.0, 1.0);
            float surfaceProfile = uMaterialRect0.z;
            float shadowingGiGain = clamp(uMaterialRect0.w, 0.0, 1.0);
            float faceDirection = gl_FrontFacing ? 1.0 : -1.0;
            vec3 geometricNormal = safeNormalize(
                vWorldNormal,
                vec3(0.0, 1.0, 0.0)) * faceDirection;
            float normalDotLightSigned = dot(n, lightDirection);
            float lambert = max(normalDotLightSigned, 0.0);
            float wrappedLambert = clamp(
                normalDotLightSigned * 0.5 + 0.5,
                0.0,
                1.0);
            // HalfLambertBias selects between ordinary and wrapped Lambert;
            // it is not an additive term that can saturate the normal map.
            float halfLambert = mix(
                lambert,
                wrappedLambert,
                clamp(uMetallicFactor, 0.0, 1.0));
            halfLambert = mix(
                halfLambert,
                sqrt(max(halfLambert, 0.0)),
                diffusionLevels);
            float geometricLambert = max(
                dot(geometricNormal, lightDirection),
                0.0);
            float geometricWrappedLambert = clamp(
                dot(geometricNormal, lightDirection) * 0.5 + 0.5,
                0.0,
                1.0);
            float geometricHalfLambert = mix(
                geometricLambert,
                geometricWrappedLambert,
                clamp(uMetallicFactor, 0.0, 1.0));
            geometricHalfLambert = mix(
                geometricHalfLambert,
                sqrt(max(geometricHalfLambert, 0.0)),
                diffusionLevels);
            // Older cooked assets have no source color-process block. The
            // authored HueShiftBias is nonzero, making rect1.w a safe payload
            // marker while preserving the legacy response for those assets.
            bool hasAuthoredColorProcess = uMaterialRect1.w > 0.05;
            float authoredShadowBias = hasAuthoredColorProcess
                ? uMaterialRect1.x
                : 1.0;
            float authoredShadowShift = hasAuthoredColorProcess
                ? uMaterialRect1.y
                : -0.5;
            float authoredShadowContrast = hasAuthoredColorProcess
                ? uMaterialRect1.z
                : 0.0;
            float authoredShadowDomain = clamp(
                (1.0 - halfLambert) +
                    (authoredShadowShift + 0.5) * 0.24,
                0.0,
                1.0);
            authoredShadowDomain = clamp(
                (authoredShadowDomain - 0.5) *
                        (1.0 + max(authoredShadowContrast, 0.0) * 1.5) +
                    0.5,
                0.0,
                1.0);
            authoredShadowDomain = pow(
                authoredShadowDomain,
                clamp(authoredShadowBias, 0.25, 2.0));
            float shadowAmount = authoredShadowDomain *
                clamp(uRoughnessFactor, 0.0, 1.0);
            float qualityDetail = clamp(
                (0.90 - litTextureDetailLodBias()) / 1.30,
                0.0,
                1.0);
            vec3 albedo = clamp(linearColor, 0.0, 1.0);
            float aoShadowAmount =
                (1.0 - aoBaseWeight) * shadowingGiGain;
            float combinedShadowAmount = 1.0 -
                (1.0 - shadowAmount) * (1.0 - aoShadowAmount);
            vec3 shadowTint = mix(
                vec3(1.0),
                shadowSpec.rgb,
                combinedShadowAmount);
            vec3 shaded = albedo * shadowTint;
            float midArea = 4.0 * combinedShadowAmount *
                (1.0 - combinedShadowAmount);
            midArea = clamp(
                (midArea - 0.5) *
                        (1.0 + max(uMaterialFlipbook0.y, 0.0)) +
                    0.5 + uMaterialFlipbook0.x,
                0.0,
                1.0);
            float darkArea = clamp(
                (combinedShadowAmount - 0.5) *
                        (1.0 + max(uMaterialFlipbook1.x, 0.0)) +
                    0.5 + uMaterialFlipbook0.w,
                0.0,
                1.0);
            float hueStrength = hasAuthoredColorProcess
                ? clamp(uMaterialRect1.w, 0.0, 1.0)
                : 0.0;
            vec3 midHsv = lgpeFoliageRgbToHsv(max(shaded, vec3(0.0)));
            midHsv.x = fract(midHsv.x + uMaterialFlipbook0.z);
            vec3 darkHsv = lgpeFoliageRgbToHsv(max(shaded, vec3(0.0)));
            darkHsv.x = fract(darkHsv.x + uMaterialFlipbook1.y);
            shaded = mix(
                shaded,
                lgpeFoliageHsvToRgb(midHsv),
                midArea * hueStrength * 0.20);
            shaded = mix(
                shaded,
                lgpeFoliageHsvToRgb(darkHsv),
                darkArea * hueStrength * 0.32);
            // Recover local diffuse relief authored in Z-A's normal map as a
            // bounded delta from the geometric-normal response. Broad light
            // stays stable and sharp atlas features cannot recreate dark
            // facial or whole-body bands.
            float normalDetailDelta = clamp(
                halfLambert - geometricHalfLambert,
                -0.22,
                0.22);
            shaded *= 1.0 + normalDetailDelta * qualityDetail * 0.62;
            bool fibreSurface =
                abs(surfaceProfile - 1.0) < 0.25 &&
                uUseEmissiveTexture > 0.5;
            bool featherSurface =
                abs(surfaceProfile - 2.0) < 0.25 &&
                uUseNormalTexture > 0.5;
            vec4 rimResponse = uUseEmissiveTexture > 0.5
                ? sampleTextureWithWrap(
                      uEmissiveTexture,
                      sampleUv,
                      uvDx,
                      uvDy)
                : vec4(0.0, 0.0, 0.0, 1.0);
            float facing = dot(n, viewDirection);
            float edge = clamp(1.0 - max(facing, 0.0), 0.0, 1.0);
            float rimOffset = clamp(uEmissiveFactor.r, 0.0, 0.99);
            float rimDomain = clamp(
                (edge - rimOffset) / max(1.0 - rimOffset, 1e-4),
                0.0,
                1.0);
            float rim = pow(
                rimDomain,
                max(uEmissiveFactor.g, 1.0)) * rimResponse.r;
            float backRim = clamp(-facing, 0.0, 1.0) * rimResponse.g;
            float specularStrength = clamp(shadowSpec.a, 0.0, 1.0);
            // Use a sharper surface-carrier sample than base color so the
            // 1024px directional strokes survive the Inspector thumbnail.
            float fineFibre = fibreSurface
                ? sampleTextureWithWrap(
                      uEmissiveTexture,
                      sampleUv,
                      uvDx * exp2(-1.25),
                      uvDy * exp2(-1.25)).a
                : 1.0;
            float coarseFibre = fibreSurface
                ? sampleTextureWithWrap(
                      uEmissiveTexture,
                      sampleUv,
                      uvDx * exp2(1.25),
                      uvDy * exp2(1.25)).a
                : 1.0;
            float fibreRelief = clamp(
                abs(coarseFibre - fineFibre) * 10.0,
                0.0,
                1.0);
            float fibreSignal = clamp(1.0 - fineFibre, 0.0, 1.0);
            float velvet = pow(edge, 2.5);
            float surfaceDetailLight = 0.35 + 0.65 * halfLambert;
            // Source-authored strand lift is additive-only: no whole-body
            // dirt tint and no dark eye seam. Missing payloads stay neutral.
            float fibreSheen = qualityDetail * surfaceDetailLight *
                (1.0 - metallic) *
                (fibreSignal * (0.90 + 0.20 * velvet) +
                 fibreRelief * (0.30 + 0.15 * velvet));

            vec2 fineFeatherNormal = featherSurface
                ? sampleTextureWithWrap(
                      uNormalTexture,
                      sampleUv,
                      uvDx * exp2(-1.0),
                      uvDy * exp2(-1.0)).xy * 2.0 - 1.0
                : vec2(0.0);
            vec2 coarseFeatherNormal = featherSurface
                ? sampleTextureWithWrap(
                      uNormalTexture,
                      sampleUv,
                      uvDx * exp2(1.25),
                      uvDy * exp2(1.25)).xy * 2.0 - 1.0
                : fineFeatherNormal;
            float featherRelief = clamp(max(
                length(fineFeatherNormal - coarseFeatherNormal) * 10.0,
                length(fineFeatherNormal) * 0.50),
                0.0,
                1.0);
            float featherSheen = featherSurface
                ? qualityDetail * surfaceDetailLight *
                    (1.0 - metallic) *
                    featherRelief *
                    (0.32 + pow(edge, 2.0) * 0.05)
                : 0.0;
            vec3 featherTint = mix(albedo, vec3(1.0), 0.22);
            vec3 nativeBase = shaded +
                albedo * (rim + backRim + fibreSheen) +
                featherTint * featherSheen;

            // The decompiled Z-A IkCharacter body program carries no generic
            // roughness/PBR coat. Preserve its layer-resolved specular shape,
            // metal response, reflection blur and diffusion controls.
            vec3 halfDirection = safeNormalize(
                lightDirection + viewDirection,
                n);
            float normalDotHalf = max(dot(n, halfDirection), 0.0);
            float normalDotView = max(dot(n, viewDirection), 0.0);
            float normalDotLight = lambert;
            float specularDomain = clamp(
                normalDotHalf + specularOffset,
                0.0,
                1.0);
            float specularExponent = mix(
                8.0,
                64.0,
                clamp(specularContrast / 5.0, 0.0, 1.0));
            float specularLobe = pow(specularDomain, specularExponent);
            float dielectricSpecular = specularStrength * specularStrength;
            float surfaceSpecular = max(dielectricSpecular, metallic);
            vec3 specularColor = mix(vec3(1.0), albedo, metallic);
            vec3 directSpecular = specularColor * surfaceSpecular * specularLobe *
                normalDotLight * 0.72;
            vec3 reflection = reflect(-viewDirection, n);
            float reflectionRoughness = clamp(
                reflectionBlur * 0.16,
                0.04,
                0.92);
            vec3 environmentRadiance = sampleNeutralEnvironment(
                reflection,
                reflectionRoughness);
            float grazingResponse = mix(
                1.0,
                1.35,
                pow(1.0 - normalDotView, 5.0));
            vec3 environmentSpecular = environmentRadiance *
                specularColor * surfaceSpecular * grazingResponse *
                computeSpecularOcclusion(
                    normalDotView,
                    aoBaseWeight,
                    reflectionRoughness) *
                __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
            vec3 diffuse = nativeBase * (1.0 - metallic * 0.85);
            return max(
                diffuse + directSpecular + environmentSpecular,
                vec3(0.0));
        }

        vec3 applyNativeSssSurface(
            vec3 linearColor,
            vec3 n,
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy,
            float surfaceProfile) {
            vec3 cameraForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
            if (dot(cameraRight, cameraRight) < 1e-6) {
                cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
            }
            cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
            vec3 cameraUp = safeNormalize(
                cross(cameraRight, cameraForward),
                vec3(0.0, 1.0, 0.0));
            vec3 viewDirection = safeNormalize(
                uCameraPos - vWorldPos,
                -cameraForward);
            vec3 lightDirection = safeNormalize(
                cameraRight * 0.45 + cameraUp * 0.86 - cameraForward * 0.24,
                vec3(0.45, 0.86, 0.24));
            vec3 halfDirection = safeNormalize(
                lightDirection + viewDirection,
                n);

            float roughness = uUseMetallicRoughnessTexture > 0.5
                ? clamp(
                      sampleTextureWithWrap(
                          uMetallicRoughnessTexture,
                          sampleUv,
                          uvDx,
                          uvDy).g * clamp(uRoughnessFactor, 0.0, 1.0),
                      0.04,
                      1.0)
                : 1.0;
            bool fibreSurface = abs(surfaceProfile - 1.0) < 0.25;
            float coarseRoughness =
                fibreSurface && uUseMetallicRoughnessTexture > 0.5
                ? clamp(
                      sampleTextureWithWrap(
                          uMetallicRoughnessTexture,
                          sampleUv,
                          uvDx * 4.0,
                          uvDy * 4.0).g * clamp(uRoughnessFactor, 0.0, 1.0),
                      0.04,
                      1.0)
                : roughness;
            float ao = uUseOcclusionTexture > 0.5
                ? mix(
                      1.0,
                      sampleTextureWithWrap(
                          uOcclusionTexture,
                          sampleUv,
                          uvDx,
                          uvDy).r,
                      clamp(uOcclusionStrength, 0.0, 1.0))
                : 1.0;
            float sssMask = uUseEmissiveTexture > 0.5
                ? sampleTextureWithWrap(
                      uEmissiveTexture,
                      sampleUv,
                      uvDx,
                      uvDy).r
                : 0.0;
            vec3 albedo = clamp(linearColor, 0.0, 1.0);
            vec3 subsurfaceTint = mix(
                albedo,
                max(uEmissiveFactor, vec3(0.0)),
                0.35);
            vec3 diffuse = albedo;
            float wrappedNdotL = clamp(
                (dot(n, lightDirection) + 0.5) / 1.5,
                0.0,
                1.0);
            float subsurfaceFill = clamp(sssMask, 0.0, 1.0) *
                (1.0 - max(dot(n, lightDirection), 0.0)) * 0.08;
            float specularPower = mix(16.0, 96.0, 1.0 - roughness);
            float sourceSpecular = pow(
                max(dot(n, halfDirection), 0.0),
                specularPower) * 0.04 * 0.45;
            // The optional fibre profile is a Phlosion reconstruction over
            // source-proven scalar roughness. Smooth SSS surfaces skip it.
            float qualityDetail = clamp(
                (0.90 - litTextureDetailLodBias()) / 1.30,
                0.0,
                1.0);
            float fibreRelief = clamp(
                (coarseRoughness - roughness) * 3.25,
                0.0,
                1.0);
            float nDotV = clamp(dot(n, viewDirection), 0.0, 1.0);
            float velvet = pow(1.0 - nDotV, 2.5);
            float fibreSheen = fibreSurface
                ? qualityDetail * wrappedNdotL *
                      (fibreRelief * (0.22 + 0.20 * velvet) + velvet * 0.08)
                : 0.0;
            return max(
                diffuse * (0.34 + 0.78 * wrappedNdotL) * ao +
                    subsurfaceTint * subsurfaceFill +
                    vec3(sourceSpecular) + albedo * fibreSheen,
                vec3(0.0));
        }

        vec3 applyNativeGastlyFace(
            vec3 albedo,
            vec3 normal,
            vec2 sampleUv,
            vec2 uvDx,
            vec2 uvDy) {
            vec3 cameraForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 cameraRight = cross(cameraForward, vec3(0.0, 1.0, 0.0));
            if (dot(cameraRight, cameraRight) < 1e-6) {
                cameraRight = cross(cameraForward, vec3(0.0, 0.0, 1.0));
            }
            cameraRight = safeNormalize(cameraRight, vec3(1.0, 0.0, 0.0));
            vec3 cameraUp = safeNormalize(
                cross(cameraRight, cameraForward),
                vec3(0.0, 1.0, 0.0));
            vec3 view = safeNormalize(uCameraPos - vWorldPos, -cameraForward);
            vec3 light = safeNormalize(
                cameraRight * 0.45 + cameraUp * 0.86 - cameraForward * 0.24,
                vec3(0.45, 0.86, 0.24));
            vec4 shadowSpec = uUseMetallicRoughnessTexture > 0.5
                ? sampleTextureWithWrap(
                      uMetallicRoughnessTexture,
                      sampleUv,
                      uvDx,
                      uvDy)
                : vec4(0.0);
            float tongueMask = smoothstep(0.48, 0.52, shadowSpec.a);
            if (uMaterialRect0.w > 0.5 && tongueMask > 0.5) {
                discard;
            }
            float occlusion = uUseOcclusionTexture > 0.5
                ? mix(
                      1.0,
                      sampleTextureWithWrap(
                          uOcclusionTexture,
                          sampleUv,
                          uvDx,
                          uvDy).r,
                      clamp(uOcclusionStrength, 0.0, 1.0))
                : 1.0;
            float halfLambert = clamp(
                dot(normal, light) * 0.5 + 0.6,
                0.0,
                1.0);
            float shadowAmount = (1.0 - halfLambert) * 0.7;
            vec3 shaded = mix(albedo, shadowSpec.rgb, shadowAmount) * occlusion;
            vec3 halfVector = safeNormalize(view + light, normal);
            float sourceSpecularMask = max(
                shadowSpec.a - 0.5 * tongueMask,
                0.0);
            float specular = pow(
                max(dot(normal, halfVector), 0.0),
                32.0) * sourceSpecularMask;
            float tongueDiffuse = mix(
                0.82,
                1.06,
                smoothstep(0.0, 1.0, halfLambert));
            vec3 tongueShaded = albedo * tongueDiffuse *
                mix(1.0, occlusion, 0.25);
            float ndh = max(dot(normal, halfVector), 0.0);
            float tongueSpecular = pow(ndh, 12.0) * 0.105 +
                pow(ndh, 48.0) * 0.04;
            shaded = mix(shaded, tongueShaded, tongueMask);
            specular = mix(specular, tongueSpecular, tongueMask);
            float edge = clamp(
                1.0 - max(dot(normal, view), 0.0),
                0.0,
                1.0);
            float rimDomain = clamp((edge - 0.4) / 0.6, 0.0, 1.0);
            float rimMask = uUseEmissiveTexture > 0.5
                ? sampleTextureWithWrap(
                      uEmissiveTexture,
                      sampleUv,
                      uvDx,
                      uvDy).r
                : 1.0;
            float rim = pow(rimDomain, 5.0) * 0.8 * rimMask;
            float backRim = clamp(-dot(normal, view), 0.0, 1.0) *
                0.08 * rimMask;
            rim *= mix(1.0, 0.18, tongueMask);
            backRim *= mix(1.0, 0.18, tongueMask);
            return max(
                shaded + vec3(specular) + albedo * (rim + backRim),
                vec3(0.0));
        }

        vec3 applyNativeEyeClearCoat(vec3 linearColor, vec3 n) {
            vec3 camForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 camRight = cross(camForward, vec3(0.0, 1.0, 0.0));
            if (dot(camRight, camRight) < 1e-6) {
                camRight = cross(camForward, vec3(0.0, 0.0, 1.0));
            }
            camRight = safeNormalize(camRight, vec3(1.0, 0.0, 0.0));
            vec3 v = safeNormalize(uCameraPos - vWorldPos, -camForward);
            vec3 lightPos = uCameraPos + camRight * 0.5 - camForward * 0.8660254;
            vec3 l = safeNormalize(lightPos - uCameraTarget, vec3(0.45, 0.86, 0.24));
            vec3 h = safeNormalize(v + l, n);
            float ndv = max(dot(n, v), 0.0);
            float ndl = max(dot(n, l), 0.0);
            float ndh = max(dot(n, h), 0.0);
            float vdh = max(dot(v, h), 0.0);
            // Native EyeClearCoat params0.x stores RoughnessClearCoat.
            // Reading the unused params3.z forced 0 -> 0.04 and produced an
            // excessively silver grazing-angle reflection.
            float roughness = clamp(uMaterialRect0.x, 0.04, 1.0);
            float clearCoatCoverage = clamp(uMaterialRect1.w, 0.0, 1.0);
            float distribution = distributionGGX(ndh, roughness);
            float geometry = geometrySchlickGGX(ndv, roughness) *
                geometrySchlickGGX(ndl, roughness);
            vec3 fresnel = fresnelSchlick(vdh, vec3(0.04));
            vec3 direct = distribution * geometry * fresnel /
                max(4.0 * ndv * ndl, 1e-4) *
                (__PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265) * ndl;
            vec3 reflection = reflect(-v, n);
            vec3 environment = sampleNeutralEnvironment(reflection, roughness) *
                fresnelSchlickRoughness(ndv, vec3(0.04), roughness) *
                __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
            return max(
                linearColor *
                    (vec3(1.0) - fresnel * (0.18 * clearCoatCoverage)) +
                    (direct + environment) * clearCoatCoverage,
                vec3(0.0));
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
    )GLSL"
    R"GLSL(
        void main() {
            FragBlendAlpha = vec4(0.0);
            if (uMaterialMode > 2.5 && uMaterialMode < 3.5) {
                if (gl_FrontFacing) discard;
                FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            if (uMaterialMode > 0.5 && uMaterialMode < 1.5) {
                FragColor = evalFireTailExact();
                return;
            }
            if (uMaterialMode > 26.5 && uMaterialMode < 27.5) {
                vec4 surface = evalNativeLayeredUnlitDisplaced();
                if (uMaterialFlags > 2.5 && uMaterialFlags < 3.5) {
                    surface.rgb = applyNativeGastlySmokeLighting(surface.rgb);
                }
                const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
                // Preserve the hue separation in Scarlet's authored HDR
                // flame layers; the generic viewer ACES fit washes both into
                // nearly the same pale yellow.
                vec3 mapped = linearToneMapping(
                    max(surface.rgb, vec3(0.0)),
                    toneMappingExposure);
                FragColor = vec4(resolveWorldSceneColor(mapped), surface.a);
                return;
            }
            if (uMaterialMode > 3.5 && uMaterialMode < 4.5) {
                vec3 groundLinear = evaluateLgpeFieldGroundSurface();
                FragColor = vec4(resolveWorldSceneColor(groundLinear), 1.0);
                return;
            }
            if (uMaterialMode > 4.5 && uMaterialMode < 5.5) {
                vec3 cliffLinear = evaluateLgpeFieldCliffSurface();
                FragColor = vec4(resolveWorldSceneColor(cliffLinear), 1.0);
                return;
            }
            if (uMaterialMode > 5.5 && uMaterialMode < 6.5) {
                vec4 treeSurface = evaluateLgpeFieldTree05Surface();
                FragColor =
                    vec4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
                return;
            }
            if (uMaterialMode > 6.5 && uMaterialMode < 7.5) {
                vec4 trunkSurface =
                    evaluateLgpeFieldObjectTreeMikiSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(trunkSurface.rgb), trunkSurface.a);
                return;
            }
            if (uMaterialMode > 7.5 && uMaterialMode < 8.5) {
                vec4 treeSurface =
                    evaluateLgpeFieldTree02Surface(false, false);
                FragColor =
                    vec4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
                return;
            }
            if (uMaterialMode > 8.5 && uMaterialMode < 9.5) {
                vec4 grassSurface =
                    evaluateLgpeFieldGrassSurface(false);
                FragColor =
                    vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
                return;
            }
            if (uMaterialMode > 9.5 && uMaterialMode < 10.5) {
                vec4 grassSurface =
                    evaluateLgpeFieldGrassSurface(true);
                FragColor =
                    vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
                return;
            }
            if (uMaterialMode > 10.5 && uMaterialMode < 11.5) {
                vec4 grassSurface =
                    evaluateLgpeFieldGrassShader04Surface();
                FragColor =
                    vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
                return;
            }
            if (uMaterialMode > 11.5 && uMaterialMode < 12.5) {
                vec4 grassSurface =
                    evaluateLgpeFieldGrassShader05Surface();
                FragColor =
                    vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
                return;
            }
            if (uMaterialMode > 12.5 && uMaterialMode < 13.5) {
                vec4 overlaySurface =
                    evaluateLgpeRoadstoneOverlaySurface();
                FragColor =
                    vec4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
                return;
            }
            if (uMaterialMode > 13.5 && uMaterialMode < 14.5) {
                vec4 overlaySurface =
                    evaluateLgpeRockMaskOverlaySurface();
                FragColor =
                    vec4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
                return;
            }
            if (uMaterialMode > 14.5 && uMaterialMode < 15.5) {
                vec4 flowerSurface =
                    evaluateLgpeFieldFlowerSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
                return;
            }
            if (uMaterialMode > 15.5 && uMaterialMode < 16.5) {
                vec4 rockSurface =
                    evaluateLgpeFieldRockSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(rockSurface.rgb), rockSurface.a);
                return;
            }
            if (uMaterialMode > 16.5 && uMaterialMode < 17.5) {
                vec4 signSurface =
                    evaluateLgpeFieldSignSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(signSurface.rgb), signSurface.a);
                return;
            }
            if (uMaterialMode > 17.5 && uMaterialMode < 18.5) {
                vec4 grassSurface =
                    evaluateLgpeFieldEncounterGrassSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
                return;
            }
            if (uMaterialMode > 18.5 && uMaterialMode < 19.5) {
                vec4 shrubSurface =
                    evaluateLgpeFieldTree02Surface(true, false);
                FragColor =
                    vec4(resolveWorldSceneColor(shrubSurface.rgb), shrubSurface.a);
                return;
            }
            if (uMaterialMode > 19.5 && uMaterialMode < 20.5) {
                vec4 flowerSurface =
                    evaluateLgpeFieldFlowerSurface();
                FragColor =
                    vec4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
                return;
            }
            if (uMaterialMode > 20.5 && uMaterialMode < 21.5) {
                vec4 foliageSurface =
                    evaluateLgpeReviewedFieldTree05Surface(
                        0.02072325, 1.08, 0.8807060431);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            if (uMaterialMode > 21.5 && uMaterialMode < 22.5) {
                vec4 foliageSurface =
                    evaluateLgpeReviewedFieldTree05Surface(
                        0.00049965, 1.0371891204, 0.9015603440);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            if (uMaterialMode > 22.5 && uMaterialMode < 23.5) {
                vec4 foliageSurface =
                    evaluateLgpeReviewedFieldTree02Surface(
                        0.02271645, 1.0476480571, 0.82, false);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            if (uMaterialMode > 23.5 && uMaterialMode < 24.5) {
                vec4 foliageSurface =
                    evaluateLgpeReviewedFieldTree02Surface(
                        0.00425595, 1.0114461323, 1.0689001800, true);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            if (uMaterialMode > 24.5 && uMaterialMode < 25.5) {
                vec4 foliageSurface =
                    evaluateLgpeReviewedFieldTree05Surface(
                        0.02981715, 0.9248157036, 0.9181899276);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            if (uMaterialMode > 25.5 && uMaterialMode < 26.5) {
                vec4 sourceSurface =
                    evaluateLgpeFieldTree02Surface(true, true);
                vec4 foliageSurface = vec4(
                    lgpeFoliageAcceptedDisplayTransform(sourceSurface.rgb),
                    sourceSurface.a);
                FragColor =
                    vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
                return;
            }
            vec4 tex = vec4(1.0);
            vec3 outLinear = clamp(vColor.rgb * uVertexColorMul.rgb, 0.0, 1.0);
            bool animatedEyeMaterial =
                (uMaterialMode > 28.5 && uMaterialMode < 30.5);
            vec2 rawUv = animatedEyeMaterial
                ? vec2(
                      vUv.x * uLightProjectionUvRowU.x +
                          uLightProjectionUvRowU.z,
                      vUv.y * uLightProjectionUvRowU.y +
                          uLightProjectionUvRowU.w)
                : vUv;
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
                : (animatedEyeMaterial ? 0.0 : uMaterialFlipbook1.w);
            if (uUseTexture > 0.5) {
                tex = sampleTextureWithWrap(uTexture, wrappedUv, uvDx, uvDy);
                outLinear = clamp(tex.rgb, 0.0, 1.0) * outLinear;
            }
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
                    // 1: Base/albedo sample.
                    dbg = clamp(tex.rgb, 0.0, 1.0);
                } else if (pbrDebugView < 2.5) {
                    // 2: Normal map sample.
                    dbg = uUseNormalTexture > 0.5
                        ? sampleTextureWithWrap(
                              uNormalTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.5, 0.5, 1.0);
                } else if (pbrDebugView < 3.5) {
                    // 3: Roughness channel.
                    float rgh = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).g
                        : 1.0;
                    dbg = vec3(rgh);
                } else if (pbrDebugView < 4.5) {
                    // 4: Metallic channel.
                    float met = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).b
                        : 0.0;
                    dbg = vec3(met);
                } else if (pbrDebugView < 5.5) {
                    // 5: AO channel.
                    float ao = uUseOcclusionTexture > 0.5
                        ? sampleTextureWithWrap(
                              uOcclusionTexture, wrappedUv, uvDx, uvDy).r
                        : 1.0;
                    dbg = vec3(ao);
                } else if (pbrDebugView < 6.5) {
                    // 6: Emissive sample.
                    dbg = uUseEmissiveTexture > 0.5
                        ? sampleTextureWithWrap(
                              uEmissiveTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.0);
                }
                FragColor = vec4(resolveWorldSceneColor(dbg), 1.0);
                return;
            }
            if (uMaterialMode >= 1.5) {
                bool nativeGastlyFace =
                    uMaterialMode > 30.5 &&
                    uMaterialFlags > 3.5 &&
                    uMaterialFlags < 4.5;
                bool nativeIkCharacter =
                    uMaterialMode > 31.5 && uMaterialMode < 32.5;
                bool nativeSss =
                    uMaterialMode > 32.5 && uMaterialMode < 33.5;
                // The generic PBR path deliberately boosts normal XY by
                // 1.25. Z-A IkCharacter's NormalHeight is already authored
                // at final strength, so cancel only that path's boost.
                vec3 n = computeMappedNormal(
                    wrappedUv,
                    uvDx,
                    uvDy,
                    nativeIkCharacter ? 0.8 : 1.0);
                if (nativeSss) {
                    outLinear = applyNativeSssSurface(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy,
                        uMaterialFlags);
                } else if (nativeIkCharacter) {
                    outLinear = applyNativeIkCharacter(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                } else if (nativeGastlyFace) {
                    outLinear = applyNativeGastlyFace(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                } else {
                    outLinear = applyWorldLitModel(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                }
                if ((uMaterialMode > 27.5 && uMaterialMode < 28.5) ||
                    (uMaterialMode > 29.5 && uMaterialMode < 30.5)) {
                    outLinear = applyNativeEyeClearCoat(outLinear, n);
                }
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
        }
    )GLSL";

    const std::string fsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            kFs, engine::render::world_pbr_shader_shared::ShaderLanguage::Glsl);
    worldProgram_ = tryLoadWorldProgramBinaryCache(kVs, fsSource);
    if (worldProgram_ == 0u) {
        const unsigned int vs = opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER, kVs);
        const unsigned int fs =
            opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER, fsSource.c_str());
        if (vs == 0 || fs == 0) {
            if (vs != 0) glDeleteShader(vs);
            if (fs != 0) glDeleteShader(fs);
            return;
        }

        worldProgram_ = linkWorldProgramWithCache(vs, fs, kVs, fsSource);
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
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldLightProjectionUvRowULoc_ < 0 ||
        worldLightProjectionUvRowVLoc_ < 0 ||
        worldProjectedShadowTextureSamplerLoc_ < 0 ||
        worldProjectedShadowMatrixLoc_ < 0 ||
        worldProjectedShadowParamsLoc_ < 0 ||
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



