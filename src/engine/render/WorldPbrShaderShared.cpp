#include "engine/render/WorldPbrShaderShared.h"

#include <iomanip>
#include <sstream>

namespace engine::render::world_pbr_shader_shared {
namespace {

void replaceAll(std::string& source, std::string_view needle, std::string_view replacement) {
    if (needle.empty()) return;
    std::size_t pos = 0;
    while ((pos = source.find(needle, pos)) != std::string::npos) {
        source.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

std::string toLiteral(float value, bool hlslLiterals) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(6) << value;
    if (hlslLiterals) oss << "f";
    return oss.str();
}

std::string_view sharedWorldPbrSectionGlsl() {
    return R"GLSL(
        vec3 rrtAndOdtFit(vec3 v) {
            vec3 a = v * (v + 0.0245786) - 0.000090537;
            vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
            return a / b;
        }

        vec3 linearToneMapping(vec3 color, float toneMappingExposure) {
            return clamp(toneMappingExposure * color, 0.0, 1.0);
        }

        vec3 tonemapACESFilmic(vec3 color, float toneMappingExposure) {
            const mat3 ACESInputMat = mat3(
                vec3(0.59719, 0.07600, 0.02840),
                vec3(0.35458, 0.90834, 0.13383),
                vec3(0.04823, 0.01566, 0.83777)
            );
            const mat3 ACESOutputMat = mat3(
                vec3( 1.60475, -0.10208, -0.00327),
                vec3(-0.53108,  1.10813, -0.07276),
                vec3(-0.07367, -0.00605,  1.07602)
            );
            color *= toneMappingExposure / 0.6;
            color = ACESInputMat * color;
            color = rrtAndOdtFit(color);
            color = ACESOutputMat * color;
            return clamp(color, 0.0, 1.0);
        }

        vec3 applyViewerToneMapping(vec3 color, float toneMappingMode, float toneMappingExposure) {
            if (toneMappingMode < 0.5) {
                return linearToneMapping(color, toneMappingExposure);
            }
            return tonemapACESFilmic(color, toneMappingExposure);
        }

        float distributionGGX(float NdotH, float roughness) {
            float a = roughness * roughness;
            float a2 = a * a;
            float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
            return a2 / max(3.14159265 * d * d, 1e-5);
        }

        float geometrySchlickGGX(float NdotV, float roughness) {
            float r = roughness + 1.0;
            float k = (r * r) * 0.125;
            return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
        }

        float geometrySmith(float NdotV, float NdotL, float roughness) {
            return geometrySchlickGGX(NdotV, roughness) *
                   geometrySchlickGGX(NdotL, roughness);
        }

        vec3 fresnelSchlick(float cosTheta, vec3 F0) {
            float m = clamp(1.0 - cosTheta, 0.0, 1.0);
            float m2 = m * m;
            float m5 = m2 * m2 * m;
            return F0 + (vec3(1.0) - F0) * m5;
        }

        vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
            float m = clamp(1.0 - cosTheta, 0.0, 1.0);
            float m2 = m * m;
            float m5 = m2 * m2 * m;
            vec3 F90 = max(vec3(1.0 - roughness), F0);
            return F0 + (F90 - F0) * m5;
        }

        vec2 DFGApprox(const in vec3 normal, const in vec3 viewDir, const in float roughness) {
            float dotNV = clamp(dot(normal, viewDir), 0.0, 1.0);
            const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
            const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
            vec4 r = roughness * c0 + c1;
            float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;
            vec2 fab = vec2(-1.04, 1.04) * a004 + r.zw;
            return fab;
        }

        void computeMultiscattering(const in vec3 normal,
                                    const in vec3 viewDir,
                                    const in vec3 specularColor,
                                    const in float specularF90,
                                    const in float roughness,
                                    inout vec3 singleScatter,
                                    inout vec3 multiScatter) {
            vec2 fab = DFGApprox(normal, viewDir, roughness);
            vec3 FssEss = specularColor * fab.x + specularF90 * fab.y;
            float Ess = fab.x + fab.y;
            float Ems = 1.0 - Ess;
            vec3 Favg = specularColor + (1.0 - specularColor) * 0.047619;
            vec3 Fms = FssEss * Favg / max(1.0 - Ems * Favg, vec3(1e-5));
            singleScatter += FssEss;
            multiScatter += Fms * Ems;
        }

        float computeSpecularOcclusion(const in float dotNV, const in float ambientOcclusion, const in float roughness) {
            return clamp(pow(dotNV + ambientOcclusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + ambientOcclusion, 0.0, 1.0);
        }

        float roughnessToNeutralMip(float roughness) {
            const float r0 = 1.0;
            const float m0 = -2.0;
            const float r1 = 0.8;
            const float m1 = -1.0;
            const float r4 = 0.4;
            const float m4 = 2.0;
            const float r5 = 0.305;
            const float m5 = 3.0;
            const float r6 = 0.21;
            const float m6 = 4.0;
            float r = clamp(roughness, 0.0, 1.0);
            if (r >= r1) return (r0 - r) * (m1 - m0) / (r0 - r1) + m0;
            if (r >= r4) return (r1 - r) * (m4 - m1) / (r1 - r4) + m1;
            if (r >= r5) return (r4 - r) * (m5 - m4) / (r4 - r5) + m4;
            if (r >= r6) return (r5 - r) * (m6 - m5) / (r5 - r6) + m5;
            return -2.0 * log2(max(1.16 * r, 1e-4));
        }

        float getFace(vec3 direction) {
            vec3 absDirection = abs(direction);
            float face = -1.0;
            if (absDirection.x > absDirection.z) {
                if (absDirection.x > absDirection.y) {
                    face = direction.x > 0.0 ? 0.0 : 3.0;
                } else {
                    face = direction.y > 0.0 ? 1.0 : 4.0;
                }
            } else {
                if (absDirection.z > absDirection.y) {
                    face = direction.z > 0.0 ? 2.0 : 5.0;
                } else {
                    face = direction.y > 0.0 ? 1.0 : 4.0;
                }
            }
            return face;
        }

        vec2 getUV(vec3 direction, float face) {
            vec2 uv;
            if (face == 0.0) {
                uv = vec2(direction.z, direction.y) / abs(direction.x);
            } else if (face == 1.0) {
                uv = vec2(-direction.x, -direction.z) / abs(direction.y);
            } else if (face == 2.0) {
                uv = vec2(-direction.x, direction.y) / abs(direction.z);
            } else if (face == 3.0) {
                uv = vec2(-direction.z, direction.y) / abs(direction.x);
            } else if (face == 4.0) {
                uv = vec2(-direction.x, direction.z) / abs(direction.y);
            } else {
                uv = vec2(direction.x, direction.y) / abs(direction.z);
            }
            return 0.5 * (uv + 1.0);
        }

        vec3 bilinearCubeUV(sampler2D envMap, vec3 direction, float mipInt) {
            const float cubeUV_minMipLevel = 4.0;
            const float cubeUV_minTileSize = 16.0;
            vec2 envTexelSize = 1.0 / max(vec2(textureSize(envMap, 0)), vec2(1.0));
            float faceSizeMax = max(float(textureSize(envMap, 0).x) / 3.0, 16.0);
            float envMaxMip = max(log2(faceSizeMax), 4.0);
            float face = getFace(direction);
            float filterInt = max(cubeUV_minMipLevel - mipInt, 0.0);
            mipInt = max(mipInt, cubeUV_minMipLevel);
            float faceSize = exp2(mipInt);
            vec2 uv = getUV(direction, face) * (faceSize - 2.0) + 1.0;
            if (face > 2.0) {
                uv.y += faceSize;
                face -= 3.0;
            }
            uv.x += face * faceSize;
            uv.x += filterInt * 3.0 * cubeUV_minTileSize;
            uv.y += 4.0 * (exp2(envMaxMip) - faceSize);
            uv *= envTexelSize;
            return textureLod(envMap, uv, 0.0).rgb;
        }

        vec3 textureCubeUV(sampler2D envMap, vec3 sampleDir, float roughness) {
            float faceSizeMax = max(float(textureSize(envMap, 0).x) / 3.0, 16.0);
            float envMaxMip = max(log2(faceSizeMax), 4.0);
            float mip = clamp(roughnessToNeutralMip(roughness), -2.0, envMaxMip);
            float mipF = fract(mip);
            float mipI = floor(mip);
            vec3 color0 = bilinearCubeUV(envMap, sampleDir, mipI);
            if (mipF == 0.0) return color0;
            vec3 color1 = bilinearCubeUV(envMap, sampleDir, mipI + 1.0);
            return mix(color0, color1, mipF);
        }

        vec3 sampleNeutralEnvironment(vec3 dir, float roughness) {
            vec3 d = safeNormalize(dir, vec3(0.0, 1.0, 0.0));
            return textureCubeUV(uEnvTexture, d, clamp(roughness, 0.0, 1.0));
        }

        vec3 evalDirectPbr(vec3 n,
                           vec3 v,
                           vec3 l,
                           vec3 radiance,
                           vec3 albedo,
                           vec3 F0,
                           float roughness,
                           float metallic) {
            float NdotL = max(dot(n, l), 0.0);
            float NdotV = max(dot(n, v), 0.0);
            if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
            vec3 h = normalize(v + l);
            float NdotH = max(dot(n, h), 0.0);
            float VdotH = max(dot(v, h), 0.0);

            float D = distributionGGX(NdotH, roughness);
            float G = geometrySmith(NdotV, NdotL, roughness);
            vec3 F = fresnelSchlick(VdotH, F0);
            vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
            return (kD * albedo / 3.14159265 + spec) * radiance * NdotL;
        }
    )GLSL";
}

std::string_view sharedWorldPbrSectionHlsl() {
    return R"HLSL(
float3 rrtAndOdtFit(float3 v) {
  float3 a = v * (v + 0.0245786f) - 0.000090537f;
  float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
  return a / b;
}

float3 linearToneMapping(float3 color, float toneMappingExposure) {
  return clamp(
      toneMappingExposure * color,
      float3(0.0f, 0.0f, 0.0f),
      float3(1.0f, 1.0f, 1.0f));
}

float3 acesInputMul(float3 c) {
  return float3(
      0.59719f * c.x + 0.35458f * c.y + 0.04823f * c.z,
      0.07600f * c.x + 0.90834f * c.y + 0.01566f * c.z,
      0.02840f * c.x + 0.13383f * c.y + 0.83777f * c.z);
}

float3 acesOutputMul(float3 c) {
  return float3(
      1.60475f * c.x + -0.53108f * c.y + -0.07367f * c.z,
     -0.10208f * c.x +  1.10813f * c.y + -0.00605f * c.z,
     -0.00327f * c.x + -0.07276f * c.y +  1.07602f * c.z);
}

float3 tonemapACESFilmic(float3 color, float toneMappingExposure) {
  color *= toneMappingExposure / 0.6f;
  color = acesInputMul(color);
  color = rrtAndOdtFit(color);
  color = acesOutputMul(color);
  return clamp(color, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
}

float3 applyViewerToneMapping(float3 color, float toneMappingMode, float toneMappingExposure) {
  if (toneMappingMode < 0.5f) {
    return linearToneMapping(color, toneMappingExposure);
  }
  return tonemapACESFilmic(color, toneMappingExposure);
}

float distributionGGX(float NdotH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
  return a2 / max(3.14159265f * d * d, 1e-5f);
}

float geometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0f;
  float k = (r * r) * 0.125f;
  return NdotV / max(NdotV * (1.0f - k) + k, 1e-5f);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
  return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
  float m = saturate(1.0f - cosTheta);
  float m2 = m * m;
  float m5 = m2 * m2 * m;
  return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * m5;
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
  float m = saturate(1.0f - cosTheta);
  float m2 = m * m;
  float m5 = m2 * m2 * m;
  float3 F90 = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
  return F0 + (F90 - F0) * m5;
}

float2 DFGApprox(const in float3 normal, const in float3 viewDir, const in float roughness) {
  float dotNV = saturate(dot(normal, viewDir));
  const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
  const float4 c1 = float4( 1.0f,  0.0425f,  1.04f, -0.04f);
  float4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28f * dotNV)) * r.x + r.y;
  float2 fab = float2(-1.04f, 1.04f) * a004 + r.zw;
  return fab;
}

void computeMultiscattering(const in float3 normal,
                            const in float3 viewDir,
                            const in float3 specularColor,
                            const in float specularF90,
                            const in float roughness,
                            inout float3 singleScatter,
                            inout float3 multiScatter) {
  float2 fab = DFGApprox(normal, viewDir, roughness);
  float3 FssEss = specularColor * fab.x + specularF90 * fab.y;
  float Ess = fab.x + fab.y;
  float Ems = 1.0f - Ess;
  float3 Favg = specularColor + (float3(1.0f, 1.0f, 1.0f) - specularColor) * 0.047619f;
  float3 Fms = FssEss * Favg / max(float3(1.0f, 1.0f, 1.0f) - Ems * Favg, 1e-5f);
  singleScatter += FssEss;
  multiScatter += Fms * Ems;
}

float computeSpecularOcclusion(const in float dotNV, const in float ambientOcclusion, const in float roughness) {
  return saturate(pow(dotNV + ambientOcclusion, exp2(-16.0f * roughness - 1.0f)) - 1.0f + ambientOcclusion);
}

float roughnessToNeutralMip(float roughness) {
  const float r0 = 1.0f;
  const float m0 = -2.0f;
  const float r1 = 0.8f;
  const float m1 = -1.0f;
  const float r4 = 0.4f;
  const float m4 = 2.0f;
  const float r5 = 0.305f;
  const float m5 = 3.0f;
  const float r6 = 0.21f;
  const float m6 = 4.0f;
  float r = saturate(roughness);
  if (r >= r1) return (r0 - r) * (m1 - m0) / (r0 - r1) + m0;
  if (r >= r4) return (r1 - r) * (m4 - m1) / (r1 - r4) + m1;
  if (r >= r5) return (r4 - r) * (m5 - m4) / (r4 - r5) + m4;
  if (r >= r6) return (r5 - r) * (m6 - m5) / (r5 - r6) + m5;
  return -2.0f * log2(max(1.16f * r, 1e-4f));
}

float getFace(float3 direction) {
  float3 absDirection = abs(direction);
  float face = -1.0f;
  if (absDirection.x > absDirection.z) {
    if (absDirection.x > absDirection.y) {
      face = (direction.x > 0.0f) ? 0.0f : 3.0f;
    } else {
      face = (direction.y > 0.0f) ? 1.0f : 4.0f;
    }
  } else {
    if (absDirection.z > absDirection.y) {
      face = (direction.z > 0.0f) ? 2.0f : 5.0f;
    } else {
      face = (direction.y > 0.0f) ? 1.0f : 4.0f;
    }
  }
  return face;
}

float2 getUV(float3 direction, float face) {
  float2 uv;
  if (face == 0.0f) {
    uv = float2(direction.z, direction.y) / abs(direction.x);
  } else if (face == 1.0f) {
    uv = float2(-direction.x, -direction.z) / abs(direction.y);
  } else if (face == 2.0f) {
    uv = float2(-direction.x, direction.y) / abs(direction.z);
  } else if (face == 3.0f) {
    uv = float2(-direction.z, direction.y) / abs(direction.x);
  } else if (face == 4.0f) {
    uv = float2(-direction.x, direction.z) / abs(direction.y);
  } else {
    uv = float2(direction.x, direction.y) / abs(direction.z);
  }
  return 0.5f * (uv + 1.0f);
}

float2 getEnvTexelSize() {
  uint w = 1, h = 1;
  gEnvTex.GetDimensions(w, h);
  float fw = max((float)w, 1.0f);
  float fh = max((float)h, 1.0f);
  return float2(1.0f / fw, 1.0f / fh);
}

float getEnvMaxMip() {
  uint w = 1, h = 1;
  gEnvTex.GetDimensions(w, h);
  float faceSizeMax = max((float)w / 3.0f, 16.0f);
  return max(log2(faceSizeMax), 4.0f);
}

float3 bilinearCubeUV(Texture2D envMap, float3 direction, float mipInt) {
  const float cubeUV_minMipLevel = 4.0f;
  const float cubeUV_minTileSize = 16.0f;
  const float envMaxMip = getEnvMaxMip();
  const float2 envTexelSize = getEnvTexelSize();
  float face = getFace(direction);
  float filterInt = max(cubeUV_minMipLevel - mipInt, 0.0f);
  mipInt = max(mipInt, cubeUV_minMipLevel);
  float faceSize = exp2(mipInt);
  float2 uv = getUV(direction, face) * (faceSize - 2.0f) + 1.0f;
  if (face > 2.0f) {
    uv.y += faceSize;
    face -= 3.0f;
  }
  uv.x += face * faceSize;
  uv.x += filterInt * 3.0f * cubeUV_minTileSize;
  uv.y += 4.0f * (exp2(envMaxMip) - faceSize);
  uv *= envTexelSize;
  return envMap.SampleLevel(gSampCC, uv, 0.0f).rgb;
}

float3 textureCubeUV(Texture2D envMap, float3 sampleDir, float roughness) {
  const float envMaxMip = getEnvMaxMip();
  float mip = clamp(roughnessToNeutralMip(roughness), -2.0f, envMaxMip);
  float mipF = frac(mip);
  float mipI = floor(mip);
  float3 color0 = bilinearCubeUV(envMap, sampleDir, mipI);
  if (mipF == 0.0f) return color0;
  float3 color1 = bilinearCubeUV(envMap, sampleDir, mipI + 1.0f);
  return lerp(color0, color1, mipF);
}

float3 sampleNeutralEnvironment(float3 dir, float roughness) {
  float3 d = safeNormalize(dir, float3(0.0f, 1.0f, 0.0f));
  return textureCubeUV(gEnvTex, d, saturate(roughness));
}

float3 evalDirectPbr(float3 n,
                     float3 v,
                     float3 l,
                     float3 radiance,
                     float3 albedo,
                     float3 F0,
                     float roughness,
                     float metallic) {
  float NdotL = max(dot(n, l), 0.0f);
  float NdotV = max(dot(n, v), 0.0f);
  if (NdotL <= 0.0f || NdotV <= 0.0f) return float3(0.0f, 0.0f, 0.0f);
  float3 h = normalize(v + l);
  float NdotH = max(dot(n, h), 0.0f);
  float VdotH = max(dot(v, h), 0.0f);

  float D = distributionGGX(NdotH, roughness);
  float G = geometrySmith(NdotV, NdotL, roughness);
  float3 F = fresnelSchlick(VdotH, F0);
  float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-4f);

  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);
  return (kD * albedo / 3.14159265f + spec) * radiance * NdotL;
}
    )HLSL";
}

} // namespace

const Tunables& getTunables() {
    static const Tunables tunables{};
    return tunables;
}

std::string injectTunables(std::string_view source, bool hlslLiterals) {
    const Tunables& t = getTunables();
    std::string out(source);
    replaceAll(out, "__PAC_PBR_DIRECT_INTENSITY__", toLiteral(t.directIntensity, hlslLiterals));
    replaceAll(out, "__PAC_PBR_AMBIENT_INTENSITY__", toLiteral(t.ambientIntensity, hlslLiterals));
    replaceAll(out, "__PAC_PBR_DIFFUSE_IBL_SCALE__", toLiteral(t.diffuseIblScale, hlslLiterals));
    replaceAll(out, "__PAC_PBR_SPECULAR_IBL_SCALE__", toLiteral(t.specularIblScale, hlslLiterals));
    replaceAll(out, "__PAC_PBR_TONEMAP_EXPOSURE__", toLiteral(t.toneMappingExposure, hlslLiterals));
    return out;
}

std::string injectSharedWorldPbr(std::string_view source, ShaderLanguage language) {
    const bool hlslLiterals = (language == ShaderLanguage::Hlsl);
    std::string out = injectTunables(source, hlslLiterals);
    const std::string_view section =
        (language == ShaderLanguage::Hlsl) ? sharedWorldPbrSectionHlsl() : sharedWorldPbrSectionGlsl();
    replaceAll(out, "__PAC_SHARED_WORLD_PBR_SECTION__", section);
    return out;
}

} // namespace engine::render::world_pbr_shader_shared
