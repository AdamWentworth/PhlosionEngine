#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/WorldPbrShaderShared.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
using namespace engine::render::d3d12_pipeline_compile;
#endif

void D3D12RenderBackend::createWorldPipeline(bool createBuffers) {
#if defined(_WIN32)
    static const std::string kVertexTemplate = R"HLSL(cbuffer VSConstants : register(b0) { float4x4 uViewProj; float4x4 uModel; float4 uSkinMeta; float4 uClipMeta; };cbuffer MaterialVsConstants : register(b1) { float _m0,_m1,_m2,_m3,_m4,_m5,_m6,_m7,_m8,_m9,_m10,uMaterialMode,uMaterialTimeSecVs,uMaterialFlagsVs,uMaterialAtlasWidthVs,uMaterialAtlasHeightVs; float4 uMaterialRect0Vs; float4 uMaterialRect1Vs; };StructuredBuffer<float4> gSkinMatrices : register(t7);Texture2D gVertexDisplacementMap : register(t1);SamplerState gVertexSampCC : register(s0);struct InstanceData { float4 model0; float4 model1; float4 model2; float4 model3; float4 color; uint4 skinMeta; };StructuredBuffer<InstanceData> gInstances : register(t6);struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 nrm : NORMAL; float4 jnts : BLENDINDICES; float4 wgts : BLENDWEIGHT; float4 tan : TANGENT; float2 sourceUv1 : TEXCOORD1; float2 sourceUv2 : TEXCOORD2; };struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; float3 generated : TEXCOORD4; float2 sourceUv1 : TEXCOORD5; float2 sourceUv2 : TEXCOORD6; };static const int kMaxSkinMatrices = 128;float4 resolveSkinMeta(InstanceData inst) {  if (inst.skinMeta.x != 0u) return float4(1.0f, (float)inst.skinMeta.y, (float)inst.skinMeta.z, (float)inst.skinMeta.w);  return uSkinMeta;}float4x4 loadPackedMatrix(uint baseFloat4) {  float4 c0 = gSkinMatrices[baseFloat4 + 0u];  float4 c1 = gSkinMatrices[baseFloat4 + 1u];  float4 c2 = gSkinMatrices[baseFloat4 + 2u];  float4 c3 = gSkinMatrices[baseFloat4 + 3u];  return float4x4(c0.x, c1.x, c2.x, c3.x, c0.y, c1.y, c2.y, c3.y, c0.z, c1.z, c2.z, c3.z, c0.w, c1.w, c2.w, c3.w);}float4x4 loadSkinMatrix(float4 skinMeta, int jointIndex, int c) {  const uint jointBase = (uint)skinMeta.w + (uint)(jointIndex * 4);  if (skinMeta.z > 0.5f) {    const uint inverseBindBase = (uint)skinMeta.w + (uint)(c * 4 + jointIndex * 4);    return mul(loadPackedMatrix(jointBase), loadPackedMatrix(inverseBindBase));  }  return loadPackedMatrix(jointBase);}float3 applySkinningPos(VSIn i, float3 localPos, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localPos;  float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j0, c), float4(localPos, 1.0f)) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j1, c), float4(localPos, 1.0f)) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j2, c), float4(localPos, 1.0f)) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j3, c), float4(localPos, 1.0f)) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localPos;  if (totalWeight < 0.999f) blended += float4(localPos, 1.0f) * (1.0f - totalWeight);  return blended.xyz;}float3 applySkinningNormal(VSIn i, float3 localNormal, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localNormal;  float3 blended = float3(0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), localNormal) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), localNormal) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), localNormal) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), localNormal) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localNormal;  if (totalWeight < 0.999f) blended += localNormal * (1.0f - totalWeight);  float len2 = dot(blended, blended);  return (len2 > 1e-8f) ? normalize(blended) : float3(0.0f, 1.0f, 0.0f);}float4 applySkinningTangent(VSIn i, float4 localTangent, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localTangent;  float3 tangent = localTangent.xyz;  float3 blended = float3(0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), tangent) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), tangent) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), tangent) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), tangent) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localTangent;  if (totalWeight < 0.999f) blended += tangent * (1.0f - totalWeight);  float len2 = dot(blended, blended);  if (len2 > 1e-8f) blended = normalize(blended);  else blended = tangent;  return float4(blended, localTangent.w);}float4 applyInstancePos(InstanceData inst, float3 localPos) {  return inst.model0 * localPos.x + inst.model1 * localPos.y + inst.model2 * localPos.z + inst.model3;}float3 applyInstanceLinear(InstanceData inst, float3 localDir) {  return inst.model0.xyz * localDir.x + inst.model1.xyz * localDir.y + inst.model2.xyz * localDir.z;}
__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
VSOut main(VSIn i, uint instanceId : SV_InstanceID) {  VSOut o;  InstanceData inst = gInstances[instanceId];  float4 skinMeta = resolveSkinMeta(inst);  float3 localPos = i.pos;  float3 localNormal = i.nrm;  float4 localTangent = i.tan;  float normalLengthSquared = dot(localNormal, localNormal);  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f && normalLengthSquared > 1e-10f) localPos += localNormal * rsqrt(normalLengthSquared) * 0.001f;
__PHLOSION_PROJECT_MATERIAL_EVALUATION__
  if (skinMeta.x > 0.5f) {    localPos = applySkinningPos(i, localPos, skinMeta);    localNormal = applySkinningNormal(i, localNormal, skinMeta);    localTangent = applySkinningTangent(i, localTangent, skinMeta);  }  float4 instanceWorld = applyInstancePos(inst, localPos);  float4 world = mul(uModel, instanceWorld);  float4 clip = mul(uViewProj, world);  clip.z = clip.z * 0.5f + clip.w * 0.5f;  clip.z -= uClipMeta.x * clip.w;  o.pos = clip;  o.uv = i.uv;  o.sourceUv1 = i.sourceUv1;  o.sourceUv2 = i.sourceUv2;  o.col = i.col * inst.color;  float3 genDen = max(uMaterialRect1Vs.xyz - uMaterialRect0Vs.xyz, float3(1e-5f, 1e-5f, 1e-5f));  o.generated = saturate((i.pos - uMaterialRect0Vs.xyz) / genDen);  o.worldPos = world.xyz;  float3x3 normalM = (float3x3)uModel;  float3 instanceNormal = applyInstanceLinear(inst, localNormal);  float3 wn = mul(normalM, instanceNormal);  float wnLen2 = dot(wn, wn);  o.worldNormal = (wnLen2 > 1e-8f) ? normalize(wn) : float3(0.0f, 1.0f, 0.0f);  float3 instanceTangent = applyInstanceLinear(inst, localTangent.xyz);  float3 wt = mul(normalM, instanceTangent);  float wtLen2 = dot(wt, wt);  if (wtLen2 > 1e-8f) wt = normalize(wt);  o.worldTangent = float4(wt, localTangent.w);  return o;})HLSL";
    const std::string kVsSource = engine::render::injectWorldMaterialProfile(kVertexTemplate, worldMaterialProfile_.d3d12Vertex);
    static const std::string kPsSource = R"HLSL(
cbuffer PSConstants : register(b1) {
  float uUseTexture;
  float uWrapS;
  float uWrapT;
  float uAlphaMode;
  float uAlphaCutoff;
  float uAlphaWindowMin;
  float uAlphaWindowMax;
  float uVertexColorMulR;
  float uVertexColorMulG;
  float uVertexColorMulB;
  float uVertexColorMulA;
  float uMaterialMode;
  float uMaterialTimeSec;
  float uMaterialFlags;
  float uMaterialAtlasWidth;
  float uMaterialAtlasHeight;
  float uMaterialRect0U;
  float uMaterialRect0V;
  float uMaterialRect0W;
  float uMaterialRect0H;
  float uMaterialRect1U;
  float uMaterialRect1V;
  float uMaterialRect1W;
  float uMaterialRect1H;
  float uMaterialFlipbook0Cols;
  float uMaterialFlipbook0Rows;
  float uMaterialFlipbook0Frames;
  float uMaterialFlipbook0Fps;
  float uMaterialFlipbook1Cols;
  float uMaterialFlipbook1Rows;
  float uMaterialFlipbook1Frames;
  float uMaterialFlipbook1Fps;
  float uSceneColorPostEnabled;
  float uProjectedShadowEnabled;
  float uProjectedShadowSamplingScale;
  float uProjectedShadowBias;
  float4 uProjectedShadowRowX;
  float4 uProjectedShadowRowY;
  float4 uProjectedShadowRowZ;
  float4 uLightProjectionUvRowU;
  float4 uLightProjectionUvRowV;
};
Texture2D gTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gMetallicRoughnessTex : register(t2);
Texture2D gOcclusionTex : register(t3);
Texture2D gEmissiveTex : register(t4);
Texture2D gEnvTex : register(t5);
Texture2D gLightProjectionTex : register(t6);
Texture2D gProjectedShadowTex : register(t7);
SamplerState gSampCC : register(s0);
SamplerState gSampRR : register(s1);
SamplerState gSampCR : register(s2);
SamplerState gSampRC : register(s3);
SamplerState gSampMR : register(s4);
SamplerState gSampRM : register(s5);
SamplerState gSampMM : register(s6);
SamplerState gSampCM : register(s7);
SamplerState gSampMC : register(s8);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; float3 generated : TEXCOORD4; float2 sourceUv1 : TEXCOORD5; float2 sourceUv2 : TEXCOORD6; };


__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
float4 evaluateWorldPixel(PSIn i, bool isFrontFace) {
__PHLOSION_PROJECT_MATERIAL_EVALUATION__
}


float4 main(PSIn i, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  return evaluateWorldPixel(i, isFrontFace);
}

struct DualSourcePSOut {
  float4 color : SV_TARGET0;
  float4 blendAlpha : SV_TARGET1;
};

DualSourcePSOut mainDualSource(PSIn i, bool isFrontFace : SV_IsFrontFace) {
  float4 evaluated = evaluateWorldPixel(i, isFrontFace);
  float blendAlpha = saturate(evaluated.a);
  DualSourcePSOut output;
  output.color = float4(
      evaluated.rgb,
      floor(blendAlpha * 63.0f + 0.5f) / 63.0f);
  output.blendAlpha = float4(0.0f, 0.0f, 0.0f, blendAlpha);
  return output;
}
)HLSL";
    static const engine::render::WorldMaterialShaderSource kDefaultMaterial{
        R"HLSL(float applyWrap(float coord, float mode) {
  if (abs(mode - 33071.0f) < 0.5f) return saturate(coord);
  if (abs(mode - 33648.0f) < 0.5f) {
    float i = floor(coord);
    float f = frac(coord);
    float odd = fmod(abs(i), 2.0f);
    return (odd >= 1.0f) ? (1.0f - f) : f;
  }
  return frac(coord);
}

float2 clampWrappedUvToTexelCenter(float2 uv) {
  uint w = 1, h = 1;
  gTex.GetDimensions(w, h);
  float2 texSize = max(float2((float)w, (float)h), float2(1.0f, 1.0f));
  float2 halfTexel = 0.5f / texSize;
  return clamp(uv, halfTexel, 1.0f.xx - halfTexel);
}

bool isClampWrap(float mode) { return abs(mode - 33071.0f) < 0.5f; }

bool isMirrorWrap(float mode) { return abs(mode - 33648.0f) < 0.5f; }

float litTextureDetailLodBias() { return clamp(uMaterialFlipbook1Frames, -0.75, 1.25); }

float4 sampleTextureWithWrap(Texture2D tex,
                             float2 uv,
                             float2 uvDx,
                             float2 uvDy,
                             float wrapS,
                             float wrapT) {
  const float lodScale = exp2(litTextureDetailLodBias());
  uvDx *= lodScale;
  uvDy *= lodScale;
  bool sClamp = isClampWrap(wrapS);
  bool tClamp = isClampWrap(wrapT);
  bool sMirror = isMirrorWrap(wrapS);
  bool tMirror = isMirrorWrap(wrapT);

  if (sClamp && tClamp) return tex.SampleGrad(gSampCC, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
  if (sClamp && !tClamp && !tMirror) return tex.SampleGrad(gSampCR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tClamp) return tex.SampleGrad(gSampRC, uv, uvDx, uvDy);
  if (sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampMR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tMirror) return tex.SampleGrad(gSampRM, uv, uvDx, uvDy);
  if (sMirror && tMirror) return tex.SampleGrad(gSampMM, uv, uvDx, uvDy);
  if (sClamp && tMirror) return tex.SampleGrad(gSampCM, uv, uvDx, uvDy);
  if (sMirror && tClamp) return tex.SampleGrad(gSampMC, uv, uvDx, uvDy);
  return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
}

float4 sampleWorldTextureWithWrap(float2 uv, float2 uvDx, float2 uvDy) {
  return sampleTextureWithWrap(gTex, uv, uvDx, uvDy, uWrapS, uWrapT);
}

float3 rgbToHsv(float3 color) {
  float4 k = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = lerp(
      float4(color.bg, k.wz),
      float4(color.gb, k.xy),
      step(color.b, color.g));
  float4 q = lerp(
      float4(p.xyw, color.r),
      float4(color.r, p.yzx),
      step(p.x, color.r));
  float chroma = q.x - min(q.w, q.y);
  const float epsilon = 1.0e-10f;
  return float3(
      abs(q.z + (q.w - q.y) / (6.0f * chroma + epsilon)),
      chroma / (q.x + epsilon),
      q.x);
}

float3 hsvToRgb(float3 hsv) {
  float3 p = abs(
      frac(hsv.xxx + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) *
          6.0f -
      3.0f);
  return hsv.z *
      lerp(1.0f.xxx, saturate(p - 1.0f), hsv.y);
}

float3 srgbToLinear(float3 c) {
  c = saturate(c);
  float3 lo = c / 12.92f;
  float3 hi = pow((c + 0.055f) / 1.055f, 2.4f);
  return lerp(lo, hi, step(float3(0.04045f, 0.04045f, 0.04045f), c));
}

float3 linearToSrgb(float3 c) {
  c = max(c, float3(0.0f, 0.0f, 0.0f));
  float3 lo = c * 12.92f;
  float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
  return lerp(lo, hi, step(float3(0.0031308f, 0.0031308f, 0.0031308f), c));
}

float3 encodeWorldSurfaceColor(float3 linearColor) {
  // The source writes linear color to UNORM before its dedicated
  // gamma_correction shader applies the standard sRGB transfer.
  return linearToSrgb(saturate(linearColor));
}

float3 resolveWorldSceneColor(float3 linearColor) {
  float3 clamped = saturate(linearColor);
  return (uSceneColorPostEnabled > 0.5f)
      ? clamped
      : encodeWorldSurfaceColor(clamped);
}

float3 safeNormalize(float3 value, float3 fallback) {
  float len2 = dot(value, value);
  if (len2 < 1e-8f) return fallback;
  return value * rsqrt(len2);
}

int decodeReviewLightingProfile(float3 cameraForwardPacked) {
  return clamp(
      (int)floor(length(cameraForwardPacked) + 0.5f) - 1,
      0,
      4);
}

float3 applyReviewLightingProfile(float3 composite,
                                  float3 resolvedAlbedo,
                                  float3 sourceNormal,
                                  float3 cameraForwardPacked) {
  const int profile = decodeReviewLightingProfile(cameraForwardPacked);
  if (profile == 0) return max(composite, float3(0.0f, 0.0f, 0.0f));
  if (profile == 4) return max(composite, float3(0.0f, 0.0f, 0.0f));
  const float3 albedo = max(resolvedAlbedo, float3(0.0f, 0.0f, 0.0f));
  if (profile == 1) {
    const float3 shadowFloor = albedo * 0.50f;
    return max(
        lerp(composite, max(composite, shadowFloor), 0.56f),
        float3(0.0f, 0.0f, 0.0f));
  }
  if (profile == 2) {
    const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
    return max(
        lerp(composite, albedo, 0.82f) + highlight * 0.16f,
        float3(0.0f, 0.0f, 0.0f));
  }
  const float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      float3(0.0f, -0.6139406f, -0.7893522f));
  const float3 cameraRight = safeNormalize(
      cross(cameraForward, float3(0.0f, 1.0f, 0.0f)),
      float3(1.0f, 0.0f, 0.0f));
  const float3 normal = safeNormalize(
      sourceNormal,
      float3(0.0f, 1.0f, 0.0f));
  const float grazing = pow(abs(dot(normal, cameraRight)), 0.70f);
  const float3 grazingSurface = albedo * (0.36f + 0.78f * grazing);
  const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
  return max(
      lerp(max(composite, albedo * 0.34f), grazingSurface, 0.62f) +
          highlight * 0.20f,
      float3(0.0f, 0.0f, 0.0f));
}

__PHLOSION_SHARED_WORLD_PBR_SECTION__

float3 perturbNormal2Arb(float3 eyePos, float3 surfNorm, float3 mapN, float2 uv, float faceDirection) {
  float3 q0 = ddx(eyePos.xyz);
  float3 q1 = ddy(eyePos.xyz);
  float2 st0 = ddx(uv);
  float2 st1 = ddy(uv);

  float3 N = surfNorm;
  float3 q1perp = cross(q1, N);
  float3 q0perp = cross(N, q0);
  float3 T = q1perp * st0.x + q0perp * st1.x;
  float3 B = q1perp * st0.y + q0perp * st1.y;

  float det = max(dot(T, T), dot(B, B));
  float scale = (det <= 1e-10f) ? 0.0f : faceDirection * rsqrt(det);
  return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
}

float3 computeMappedNormalFromTexel(PSIn i,
                                    bool isFrontFace,
                                    float2 sampleUv,
                                    float3 normalTexel,
                                    float sourceNormalScale) {
  float faceDirection = isFrontFace ? 1.0f : -1.0f;
  float3 n = normalize(i.worldNormal);
  if (dot(n, n) < 1e-6f) {
    float3 dx = ddx(i.worldPos);
    float3 dy = ddy(i.worldPos);
    n = normalize(cross(dx, dy));
  }
  n *= faceDirection;
  float2 mapXY = normalTexel.xy * 2.0f - 1.0f;
  mapXY *= max(sourceNormalScale, 0.0f);
  // Support standard RGB tangent-space normals and two-channel packed XY
  // normals. Decoded XY maps can use blue=0 or blue=255 as a sentinel, so
  // reconstruct Z for both encodings.
  float authoredZ = normalTexel.z * 2.0f - 1.0f;
  float reconZ = sqrt(max(1.0f - saturate(dot(mapXY, mapXY)), 0.0f));
  float useReconstructedZ =
      (normalTexel.z <= (1.5f / 255.0f) ||
       normalTexel.z >= (253.5f / 255.0f))
          ? 1.0f
          : 0.0f;
  float mapZ = lerp(authoredZ, reconZ, useReconstructedZ);
  float3 mapN = normalize(float3(mapXY, mapZ));
  float3 mapped = float3(0.0f, 0.0f, 0.0f);
  float3 tangent = i.worldTangent.xyz;
  float tangentLen2 = dot(tangent, tangent);
  bool hasAuthoredTangent = tangentLen2 > 1e-6f && abs(i.worldTangent.w) > 0.5f;
  if (hasAuthoredTangent) {
    tangent *= rsqrt(tangentLen2);
    tangent = tangent - n * dot(n, tangent);
    float orthoLen2 = dot(tangent, tangent);
    if (orthoLen2 > 1e-10f) {
      tangent *= rsqrt(orthoLen2);
      float tangentSign = (i.worldTangent.w < 0.0f) ? -1.0f : 1.0f;
      float3 bitangent = normalize(cross(n, tangent)) * tangentSign;
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
    mapped = perturbNormal2Arb(i.worldPos, n, mapN, sampleUv, faceDirection);
  }
  return mapped;
}

float3 computeMappedNormal(PSIn i,
                           bool isFrontFace,
                           float2 sampleUv,
                           float2 uvDx,
                           float2 uvDy,
                           bool useNormalTexture,
                           float normalScale) {
  float3 normalTexel = useNormalTexture
      ? sampleTextureWithWrap(
            gNormalTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).xyz
      : float3(0.5f, 0.5f, 1.0f);
  return computeMappedNormalFromTexel(
      i,
      isFrontFace,
      sampleUv,
      normalTexel,
      useNormalTexture ? max(normalScale, 0.0f) * 1.25f : 0.0f);
}

float3 applyWorldLitModel(PSIn i,
                          bool isFrontFace,
                          float3 linearColor,
                          float2 sampleUv,
                          float2 uvDx,
                          float2 uvDy,
                          bool useNormalTexture,
                          bool useMetallicRoughnessTexture,
                          bool useSpecularStrengthTexture,
                          bool useOcclusionTexture,
                          bool useEmissiveTexture,
                          float normalScale,
                          float metallicFactor,
                          float roughnessFactor,
                          float specularIntensity,
                          float occlusionStrength,
                          float3 emissiveFactor,
                          float3 cameraPos,
                          float3 cameraForwardPacked,
                          float3 cameraTarget) {
  float3 n = computeMappedNormal(i, isFrontFace, sampleUv, uvDx, uvDy, useNormalTexture, normalScale);
  float4 orm = float4(1.0f, 1.0f, 1.0f, 1.0f);
  if (useMetallicRoughnessTexture) {
    orm = sampleTextureWithWrap(
              gMetallicRoughnessTex,
              sampleUv,
              uvDx,
              uvDy,
              uWrapS,
              uWrapT);
  }
  float roughness = clamp(orm.g * saturate(roughnessFactor), 0.16f, 1.0f);
  float metallic = clamp(orm.b * saturate(metallicFactor), 0.0f, 1.0f);
  float ao = 1.0f;
  if (useOcclusionTexture) {
    float occTex = sampleTextureWithWrap(gOcclusionTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).r;
    ao = lerp(1.0f, occTex, saturate(occlusionStrength));
  }

  float3 albedo = saturate(linearColor);
  float dielectricSpecular = useSpecularStrengthTexture
      ? saturate(specularIntensity) * saturate(orm.a)
      : 0.04f;
  float3 F0 = lerp(
)HLSL"
        R"HLSL(      float3(dielectricSpecular, dielectricSpecular, dielectricSpecular),
      albedo,
      metallic);
  float3 diffuseColor = albedo * (1.0f - metallic);
  const float specularF90 = 1.0f;
  float3 camForward = safeNormalize(cameraForwardPacked, normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 camRight = cross(camForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(camRight, camRight) < 1e-6f) {
    camRight = cross(camForward, float3(0.0f, 0.0f, 1.0f));
  }
  camRight = safeNormalize(camRight, float3(1.0f, 0.0f, 0.0f));
  float3 camUp = safeNormalize(cross(camRight, camForward), float3(0.0f, 1.0f, 0.0f));
  float3 v = safeNormalize(cameraPos - i.worldPos, -camForward);
  const float3 directColor = float3(1.0f, 1.0f, 1.0f);
  const float directIntensity = __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  const float3 ambientColor = float3(1.0f, 1.0f, 1.0f);
  const float ambientIntensity = __PHLOSION_PBR_AMBIENT_INTENSITY__;

  float3 lightPos = cameraPos + camRight * 0.5f + camUp * 0.0f - camForward * 0.8660254f;
  float3 l0 = safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  float3 direct = evalDirectPbr(n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);

  float NdotV = max(dot(n, v), 0.0f);
  float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

  float3 r = reflect(-v, n);
  float3 envIrradiance = 3.14159265f * sampleNeutralEnvironment(n, 1.0f);
  float3 envRadiance = sampleNeutralEnvironment(r, roughness);
  float3 singleScattering = float3(0.0f, 0.0f, 0.0f);
  float3 multiScattering = float3(0.0f, 0.0f, 0.0f);
  computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
  float3 cosineWeightedIrradiance = envIrradiance * (1.0f / 3.14159265f);
  float3 totalScattering = singleScattering + multiScattering;
  float energyComp = 1.0f - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
  float3 diffuseIBL = diffuseColor * max(energyComp, 0.0f) * cosineWeightedIrradiance;
  float3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
  diffuseIBL *= __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  diffuseIBL *= ao;
  float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
  specularIBL *= specularOcclusion;
  float3 ibl = diffuseIBL + specularIBL;

  float3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
  float3 shaded = direct + ibl + ambientLight;

  float3 emissiveTex = useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f);
  float3 emissive = emissiveTex * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));

  return max(shaded + emissive, float3(0.0f, 0.0f, 0.0f));
}

float3 applyCharacterInking(PSIn i, float3 linearColor, float3 n, float3 cameraPos, float3 cameraForwardPacked) {
  return linearColor;
}
)HLSL",
        R"HLSL(
  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f) {
    // Match the OpenGL/Vulkan inverted-hull outline contract: discard the
    // expanded mesh's front faces and retain only its back-facing silhouette.
    if (isFrontFace) discard;
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outLinear = saturate(i.col.rgb * float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB));
  const float2 materialUv = i.uv;
  float2 wrappedUv = float2(
      applyWrap(materialUv.x, uWrapS),
      applyWrap(materialUv.y, uWrapT));
  bool clampS = isClampWrap(uWrapS);
  bool clampT = isClampWrap(uWrapT);
  if (clampS || clampT) {
    wrappedUv = clampWrappedUvToTexelCenter(wrappedUv);
  }
  float2 uvDx = ddx(wrappedUv);
  float2 uvDy = ddy(wrappedUv);
  if (uUseTexture > 0.5f) {
    tex = sampleWorldTextureWithWrap(wrappedUv, uvDx, uvDy);
    outLinear = saturate(tex.rgb) * outLinear;
  }
  float3 reviewAlbedo = outLinear;
  const float3 reviewCameraForward = float3(
      uMaterialFlipbook0Cols,
      uMaterialFlipbook0Rows,
      uMaterialFlipbook0Frames);
  float outA = saturate(i.col.a * uVertexColorMulA * tex.a);
  float alphaWindowMin = saturate(uAlphaWindowMin);
  float alphaWindowMax = saturate(uAlphaWindowMax);
  if (alphaWindowMax < 1.0f || alphaWindowMin > 0.0f) {
    if (outA < alphaWindowMin || outA >= alphaWindowMax) discard;
  }
  if (uAlphaMode < 0.5f) {
    outA = saturate(i.col.a * uVertexColorMulA);
  } else if (uAlphaMode < 1.5f) {
    if (outA < saturate(uAlphaCutoff)) discard;
    outA = saturate(i.col.a * uVertexColorMulA);
  }
  const bool explicitMaterialDebug =
      uMaterialFlipbook1Fps < -100.5f;
  const int materialDebugFlags = (int)(uMaterialFlags + 0.5f);
  const float pbrDebugView = explicitMaterialDebug
      ? -uMaterialFlipbook1Fps - 100.0f
      : uMaterialFlipbook1Fps;
  if (uMaterialMode >= 1.5f && pbrDebugView > 0.5f) {
    float3 dbg = float3(0.0f, 0.0f, 0.0f);
    if (pbrDebugView < 1.5f) {
      // 1: Raw base-color texture sample.
      dbg = saturate(tex.rgb);
    } else if (pbrDebugView < 2.5f) {
      // 2: Authored tint-resolved albedo without lighting.
      dbg = saturate(outLinear);
    } else if (pbrDebugView < 3.5f) {
      // 3: Normal map sample.
      dbg = (materialDebugFlags & (1 << 0)) != 0
          ? sampleTextureWithWrap(
                gNormalTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb
          : float3(0.5f, 0.5f, 1.0f);
    } else if (pbrDebugView < 4.5f) {
      // 4: Roughness channel.
      const float rgh = (materialDebugFlags & (1 << 1)) != 0
          ? sampleTextureWithWrap(
                gMetallicRoughnessTex,
                wrappedUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).g
          : 1.0f;
      dbg = float3(rgh, rgh, rgh);
    } else if (pbrDebugView < 5.5f) {
      // 5: Metallic channel.
      const float met = (materialDebugFlags & (1 << 1)) != 0
          ? sampleTextureWithWrap(
                gMetallicRoughnessTex,
                wrappedUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).b
          : 0.0f;
      dbg = float3(met, met, met);
    } else if (pbrDebugView < 6.5f) {
      // 6: AO channel.
      const float ao = (materialDebugFlags & (1 << 2)) != 0
          ? sampleTextureWithWrap(
                gOcclusionTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).r
          : 1.0f;
      dbg = float3(ao, ao, ao);
    } else if (pbrDebugView < 7.5f) {
      // 7: Emissive sample.
      dbg = (materialDebugFlags & (1 << 3)) != 0
          ? sampleTextureWithWrap(
                gEmissiveTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb
          : float3(0.0f, 0.0f, 0.0f);
    }
    return float4(resolveWorldSceneColor(dbg), 1.0f);
  }
  if (uMaterialMode >= 1.5f) {
    const int flags = (int)(uMaterialFlags + 0.5f);
    outLinear = applyWorldLitModel(i, isFrontFace, outLinear, wrappedUv, uvDx, uvDy,
        (flags & 1) != 0, (flags & 2) != 0, false, (flags & 4) != 0, (flags & 8) != 0,
        uMaterialAtlasWidth, uMaterialAtlasHeight, uMaterialRect0U, 0.04f, uMaterialRect0V,
        float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U),
        float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H), reviewCameraForward,
        float3(uMaterialFlipbook0Fps, uMaterialFlipbook1Cols, uMaterialFlipbook1Rows));
  }
  if (uMaterialMode >= 1.5f) {
    outLinear = applyReviewLightingProfile(
        outLinear,
        reviewAlbedo,
        i.worldNormal,
        reviewCameraForward);
  }
  const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = resolveWorldSceneColor(mapped);
  return float4(outSrgb, outA);
)HLSL"};

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> dualSourcePsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (!compileHlslWithCache(kVsSource.c_str(),
                              kVsSource.size(),
                              "main",
                              "vs_5_0",
                              d3dCompileFlags(),
                              0,
                              vsBlob,
                              errBlob) ||
        !vsBlob) {
        const std::string details = d3dCompileErrorMessage(errBlob.Get());
        if (!details.empty()) {
            throw std::runtime_error(
                std::string("D3DCompile failed for world VS: ") +
                details);
        }
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    const std::string worldPsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            engine::render::injectWorldMaterialProfile(kPsSource, worldMaterialProfile_.empty() ? kDefaultMaterial : worldMaterialProfile_.d3d12),
            engine::render::world_pbr_shader_shared::ShaderLanguage::Hlsl);
    errBlob.Reset();
    if (!compileHlslWithCache(worldPsSource.c_str(),
                              worldPsSource.size(),
                              "main",
                              "ps_5_0",
                              d3dCompileFlags(),
                              0,
                              psBlob,
                              errBlob) ||
        !psBlob) {
        const std::string details = d3dCompileErrorMessage(errBlob.Get());
        if (!details.empty()) {
            throw std::runtime_error(std::string("D3DCompile failed for world PS: ") + details);
        }
        throw std::runtime_error("D3DCompile failed for world PS.");
    }
    errBlob.Reset();
    if (!compileHlslWithCache(worldPsSource.c_str(),
                              worldPsSource.size(),
                              "mainDualSource",
                              "ps_5_0",
                              d3dCompileFlags(),
                              0,
                              dualSourcePsBlob,
                              errBlob) ||
        !dualSourcePsBlob) {
        const std::string details = d3dCompileErrorMessage(errBlob.Get());
        if (!details.empty()) {
            throw std::runtime_error(
                std::string("D3DCompile failed for world dual-source PS: ") + details);
        }
        throw std::runtime_error("D3DCompile failed for world dual-source PS.");
    }

    D3D12_DESCRIPTOR_RANGE materialSrvRange{};
    materialSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialSrvRange.NumDescriptors = 8;
    materialSrvRange.BaseShaderRegister = 0;
    materialSrvRange.RegisterSpace = 0;
    materialSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE vertexDisplacementSrvRange{};
    vertexDisplacementSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    vertexDisplacementSrvRange.NumDescriptors = 1;
    vertexDisplacementSrvRange.BaseShaderRegister = 1;
    vertexDisplacementSrvRange.RegisterSpace = 0;
    vertexDisplacementSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[6]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.Num32BitValues = static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float));
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[2].Descriptor.ShaderRegister = 7;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &materialSrvRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[4].Descriptor.ShaderRegister = 6;
    rootParams[4].Descriptor.RegisterSpace = 0;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[5].DescriptorTable.pDescriptorRanges =
        &vertexDisplacementSrvRange;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    auto makeStaticWorldSampler = [](UINT shaderRegister,
                                     D3D12_TEXTURE_ADDRESS_MODE addressU,
                                     D3D12_TEXTURE_ADDRESS_MODE addressV) {
        D3D12_STATIC_SAMPLER_DESC s{};
        // Match OpenGL world texture quality closer: anisotropic + slight negative LOD bias.
        s.Filter = D3D12_FILTER_ANISOTROPIC;
        s.AddressU = addressU;
        s.AddressV = addressV;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = -0.35f;
        s.MaxAnisotropy = 16;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        s.MinLOD = 0.0f;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = shaderRegister;
        s.RegisterSpace = 0;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        return s;
    };
    std::array<D3D12_STATIC_SAMPLER_DESC, 9> worldSamplers = {
        makeStaticWorldSampler(0, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // CC
        makeStaticWorldSampler(1, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // RR
        makeStaticWorldSampler(2, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // CR
        makeStaticWorldSampler(3, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // RC
        makeStaticWorldSampler(4, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // MR
        makeStaticWorldSampler(5, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // RM
        makeStaticWorldSampler(6, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // MM
        makeStaticWorldSampler(7, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // CM
        makeStaticWorldSampler(8, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // MC
    };

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = static_cast<UINT>(worldSamplers.size());
    rsDesc.pStaticSamplers = worldSamplers.data();
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for world pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(worldRootSignature_.ReleaseAndGetAddressOf()))) ||
        !worldRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 world pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 96, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = worldRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blendOpaque{};
    blendOpaque.AlphaToCoverageEnable = FALSE;
    blendOpaque.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtOpaque{};
    rtOpaque.BlendEnable = engine::render::parity_contract::kWorldOpaqueBlendEnabled ? TRUE : FALSE;
    rtOpaque.LogicOpEnable = FALSE;
    rtOpaque.SrcBlend = D3D12_BLEND_ONE;
    rtOpaque.DestBlend = D3D12_BLEND_ZERO;
    rtOpaque.BlendOp = D3D12_BLEND_OP_ADD;
    rtOpaque.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtOpaque.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtOpaque.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtOpaque.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtOpaque.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendOpaque.RenderTarget[0] = rtOpaque;
    pso.BlendState = blendOpaque;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = TRUE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencil.DepthFunc = engine::render::parity_contract::kWorldDepthFuncLessEqual
        ? D3D12_COMPARISON_FUNC_LESS_EQUAL
        : D3D12_COMPARISON_FUNC_LESS;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(worldPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC blendPso = pso;
    blendPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    blendPso.BlendState.RenderTarget[0].BlendEnable =
        engine::render::parity_contract::kWorldBlendPipelineEnabled ? TRUE : FALSE;
    blendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &blendPso,
            IID_PPV_ARGS(worldBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC additiveBlendPso = blendPso;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &additiveBlendPso,
            IID_PPV_ARGS(worldAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldAdditiveBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world additive blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC premulBlendPso = blendPso;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &premulBlendPso,
            IID_PPV_ARGS(worldPremultipliedBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPremultipliedBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world premultiplied blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthBlendPso = blendPso;
    noDepthBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &noDepthBlendPso,
            IID_PPV_ARGS(worldNoDepthBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world no-depth blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthAdditiveBlendPso = additiveBlendPso;
    noDepthAdditiveBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &noDepthAdditiveBlendPso,
            IID_PPV_ARGS(worldNoDepthAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthAdditiveBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world no-depth additive blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthPremulBlendPso = premulBlendPso;
    noDepthPremulBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &noDepthPremulBlendPso,
            IID_PPV_ARGS(worldNoDepthPremultipliedBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthPremultipliedBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world no-depth premultiplied blend pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC dualSourceBlendPso = blendPso;
    dualSourceBlendPso.PS = {
        dualSourcePsBlob->GetBufferPointer(), dualSourcePsBlob->GetBufferSize()};
    dualSourceBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC1_ALPHA;
    dualSourceBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC1_ALPHA;
    dualSourceBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    dualSourceBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &dualSourceBlendPso,
            IID_PPV_ARGS(worldDualSourceBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldDualSourceBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world dual-source blend pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC dualSourceAdditivePso = dualSourceBlendPso;
    dualSourceAdditivePso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &dualSourceAdditivePso,
            IID_PPV_ARGS(worldDualSourceAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldDualSourceAdditiveBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world dual-source additive pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthDualSourceBlendPso = dualSourceBlendPso;
    noDepthDualSourceBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &noDepthDualSourceBlendPso,
            IID_PPV_ARGS(worldNoDepthDualSourceBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthDualSourceBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world no-depth dual-source blend pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthDualSourceAdditivePso = dualSourceAdditivePso;
    noDepthDualSourceAdditivePso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &noDepthDualSourceAdditivePso,
            IID_PPV_ARGS(
                worldNoDepthDualSourceAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthDualSourceAdditiveBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world no-depth dual-source additive pipeline.");
    }

    if (!createBuffers) return;

    constexpr std::size_t kWorldVertexBufferBytesPerFrame =
        kMaxWorldVertices * sizeof(WorldVertex);
    constexpr std::size_t kBufferBytes =
        kWorldVertexBufferBytesPerFrame * kFrameCount;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world vertex buffer.");
    }

    worldVertexBufferGpuAddress_ = worldVertexBuffer_->GetGPUVirtualAddress();
    worldVertexStride_ = sizeof(WorldVertex);
    worldVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    worldVertexBufferBytesPerFrame_ = static_cast<UINT>(kWorldVertexBufferBytesPerFrame);
    worldVertexMappedData_ = nullptr;
    void* worldVertexMapped = nullptr;
    D3D12_RANGE worldVertexReadRange{0, 0};
    if (FAILED(worldVertexBuffer_->Map(0, &worldVertexReadRange, &worldVertexMapped)) || !worldVertexMapped) {
        throw std::runtime_error("Map failed for D3D12 world vertex buffer.");
    }
    worldVertexMappedData_ = static_cast<std::uint8_t*>(worldVertexMapped);

    const std::size_t indexBufferBytesPerFrame =
        kMaxWorldIndices * sizeof(std::uint32_t);
    const std::size_t indexBufferBytes = indexBufferBytesPerFrame * kFrameCount;
    D3D12_RESOURCE_DESC indexBufferDesc = bufferDesc;
    indexBufferDesc.Width = indexBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &indexBufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldIndexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldIndexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world index buffer.");
    }
    worldIndexBufferGpuAddress_ = worldIndexBuffer_->GetGPUVirtualAddress();
    worldIndexBufferSize_ = static_cast<UINT>(indexBufferBytes);
    worldIndexBufferBytesPerFrame_ = static_cast<UINT>(indexBufferBytesPerFrame);
    worldIndexMappedData_ = nullptr;
    void* worldIndexMapped = nullptr;
    D3D12_RANGE worldIndexReadRange{0, 0};
    if (FAILED(worldIndexBuffer_->Map(0, &worldIndexReadRange, &worldIndexMapped)) || !worldIndexMapped) {
        throw std::runtime_error("Map failed for D3D12 world index buffer.");
    }
    worldIndexMappedData_ = static_cast<std::uint8_t*>(worldIndexMapped);

    // Per-draw VS constant upload ring buffer (view-proj + model + skin meta).
    const std::size_t kWorldVsConstantsBytesPerDraw = alignUp(40u * sizeof(float), 256u);
    const std::size_t kMaxWorldDrawsPerFrame = 4096u;
    const std::size_t kWorldVsConstantsBufferBytesPerFrame =
        kWorldVsConstantsBytesPerDraw * kMaxWorldDrawsPerFrame;
    const std::size_t kWorldVsConstantsBufferBytes =
        kWorldVsConstantsBufferBytesPerFrame * kFrameCount;
    D3D12_RESOURCE_DESC worldVsConstantsDesc = bufferDesc;
    worldVsConstantsDesc.Width = kWorldVsConstantsBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &worldVsConstantsDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldVsConstantBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldVsConstantBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world VS constants buffer.");
    }
    worldVsConstantBufferGpuAddress_ = worldVsConstantBuffer_->GetGPUVirtualAddress();
    worldVsConstantBufferSize_ = static_cast<UINT>(kWorldVsConstantsBufferBytes);
    worldVsConstantBufferBytesPerFrame_ =
        static_cast<UINT>(kWorldVsConstantsBufferBytesPerFrame);
    worldVsConstantMappedData_ = nullptr;
    void* worldVsMapped = nullptr;
    D3D12_RANGE worldVsReadRange{0, 0};
    if (FAILED(worldVsConstantBuffer_->Map(0, &worldVsReadRange, &worldVsMapped)) || !worldVsMapped) {
        throw std::runtime_error("Map failed for D3D12 world VS constants buffer.");
    }
    worldVsConstantMappedData_ = static_cast<std::uint8_t*>(worldVsMapped);
    std::memset(worldVsConstantMappedData_, 0, worldVsConstantBufferSize_);
    worldVsConstantFrameOffset_ = 0u;

    // Per-draw GPU clip-skinning matrix upload ring buffer.
    const std::size_t kMaxGpuSkinMatrices = 128u;
    const std::size_t kSkinMatrixBytesPerDraw = alignUp(
        kMaxGpuSkinMatrices * 2u * 16u * sizeof(float), 256u);
    const std::size_t kSkinMatrixBufferBytesPerFrame =
        kSkinMatrixBytesPerDraw * kMaxWorldDrawsPerFrame;
    const std::size_t kSkinMatrixBufferBytes =
        kSkinMatrixBufferBytesPerFrame * kFrameCount;
    D3D12_RESOURCE_DESC skinMatrixBufferDesc = bufferDesc;
    skinMatrixBufferDesc.Width = kSkinMatrixBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &skinMatrixBufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldSkinMatrixBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldSkinMatrixBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world skin-matrix buffer.");
    }
    worldSkinMatrixBufferGpuAddress_ = worldSkinMatrixBuffer_->GetGPUVirtualAddress();
    worldSkinMatrixBufferSize_ = static_cast<UINT>(kSkinMatrixBufferBytes);
    worldSkinMatrixBufferBytesPerFrame_ =
        static_cast<UINT>(kSkinMatrixBufferBytesPerFrame);
    worldSkinMatrixMappedData_ = nullptr;
    void* worldSkinMapped = nullptr;
    D3D12_RANGE worldSkinReadRange{0, 0};
    if (FAILED(worldSkinMatrixBuffer_->Map(0, &worldSkinReadRange, &worldSkinMapped)) || !worldSkinMapped) {
        throw std::runtime_error("Map failed for D3D12 world skin-matrix buffer.");
    }
    worldSkinMatrixMappedData_ = static_cast<std::uint8_t*>(worldSkinMapped);
    std::memset(worldSkinMatrixMappedData_, 0, worldSkinMatrixBufferSize_);
    // Reserve identity at offset 0 for non-skinned draws.
    constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    for (std::uint32_t frame = 0; frame < kFrameCount; ++frame) {
        const std::size_t frameOffset =
            static_cast<std::size_t>(frame) * kSkinMatrixBufferBytesPerFrame;
        std::memcpy(worldSkinMatrixMappedData_ + frameOffset, kIdentity, sizeof(kIdentity));
    }
    worldSkinMatrixFrameOffset_ = 256u;

    constexpr std::size_t kMaxWorldInstancesPerFrame = 65536u;
    const std::size_t kWorldInstanceBufferBytesPerFrame =
        kMaxWorldInstancesPerFrame * sizeof(WorldInstanceVertexData);
    const std::size_t kWorldInstanceBufferBytes =
        kWorldInstanceBufferBytesPerFrame * kFrameCount;
    D3D12_RESOURCE_DESC worldInstanceDesc = bufferDesc;
    worldInstanceDesc.Width = kWorldInstanceBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &worldInstanceDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldInstanceBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldInstanceBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world instance buffer.");
    }
    worldInstanceBufferGpuAddress_ = worldInstanceBuffer_->GetGPUVirtualAddress();
    worldInstanceBufferSize_ = static_cast<UINT>(kWorldInstanceBufferBytes);
    worldInstanceBufferBytesPerFrame_ =
        static_cast<UINT>(kWorldInstanceBufferBytesPerFrame);
    worldInstanceMappedData_ = nullptr;
    void* worldInstanceMapped = nullptr;
    D3D12_RANGE worldInstanceReadRange{0, 0};
    if (FAILED(worldInstanceBuffer_->Map(0, &worldInstanceReadRange, &worldInstanceMapped)) || !worldInstanceMapped) {
        throw std::runtime_error("Map failed for D3D12 world instance buffer.");
    }
    worldInstanceMappedData_ = static_cast<std::uint8_t*>(worldInstanceMapped);
    std::memset(worldInstanceMappedData_, 0, worldInstanceBufferSize_);
    for (std::uint32_t frame = 0; frame < kFrameCount; ++frame) {
        auto* identityInstance = reinterpret_cast<WorldInstanceVertexData*>(
            worldInstanceMappedData_ +
            static_cast<std::size_t>(frame) * kWorldInstanceBufferBytesPerFrame);
        identityInstance->model0x = 1.0f;
        identityInstance->model1y = 1.0f;
        identityInstance->model2z = 1.0f;
        identityInstance->model3w = 1.0f;
        identityInstance->colorR = 1.0f;
        identityInstance->colorG = 1.0f;
        identityInstance->colorB = 1.0f;
        identityInstance->colorA = 1.0f;
    }
    worldInstanceFrameOffset_ = static_cast<UINT>(sizeof(WorldInstanceVertexData));
#endif
}
