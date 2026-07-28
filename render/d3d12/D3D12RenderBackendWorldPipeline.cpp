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

void D3D12RenderBackend::createWorldPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float4x4 uViewProj; float4x4 uModel; float4 uSkinMeta; float4 uClipMeta; };"
        "cbuffer MaterialVsConstants : register(b1) { float _m0,_m1,_m2,_m3,_m4,_m5,_m6,_m7,_m8,_m9,_m10,uMaterialMode,_m12,_m13; float4 uGeneratedBoundsMin; float4 uGeneratedBoundsMax; };"
        "StructuredBuffer<float4> gSkinMatrices : register(t7);"
        "struct InstanceData { float4 model0; float4 model1; float4 model2; float4 model3; float4 color; uint4 skinMeta; };"
        "StructuredBuffer<InstanceData> gInstances : register(t6);"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 nrm : NORMAL; float4 jnts : BLENDINDICES; float4 wgts : BLENDWEIGHT; float4 tan : TANGENT; float2 sourceUv1 : TEXCOORD1; float2 sourceUv2 : TEXCOORD2; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; float3 generated : TEXCOORD4; float2 sourceUv1 : TEXCOORD5; float2 sourceUv2 : TEXCOORD6; };"
        "static const int kMaxSkinMatrices = 128;"
        "float4 resolveSkinMeta(InstanceData inst) {"
        "  if (inst.skinMeta.x != 0u) return float4(1.0f, (float)inst.skinMeta.y, (float)inst.skinMeta.z, (float)inst.skinMeta.w);"
        "  return uSkinMeta;"
        "}"
        "float4x4 loadPackedMatrix(uint baseFloat4) {"
        "  float4 c0 = gSkinMatrices[baseFloat4 + 0u];"
        "  float4 c1 = gSkinMatrices[baseFloat4 + 1u];"
        "  float4 c2 = gSkinMatrices[baseFloat4 + 2u];"
        "  float4 c3 = gSkinMatrices[baseFloat4 + 3u];"
        "  return float4x4(c0.x, c1.x, c2.x, c3.x, c0.y, c1.y, c2.y, c3.y, c0.z, c1.z, c2.z, c3.z, c0.w, c1.w, c2.w, c3.w);"
        "}"
        "float4x4 loadSkinMatrix(float4 skinMeta, int jointIndex, int c) {"
        "  const uint jointBase = (uint)skinMeta.w + (uint)(jointIndex * 4);"
        "  if (skinMeta.z > 0.5f) {"
        "    const uint inverseBindBase = (uint)skinMeta.w + (uint)(c * 4 + jointIndex * 4);"
        "    return mul(loadPackedMatrix(jointBase), loadPackedMatrix(inverseBindBase));"
        "  }"
        "  return loadPackedMatrix(jointBase);"
        "}"
        "float3 applySkinningPos(VSIn i, float3 localPos, float4 skinMeta) {"
        "  if (skinMeta.x < 0.5f) return localPos;"
        "  float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);"
        "  float totalWeight = 0.0f;"
        "  int c = (int)skinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j0, c), float4(localPos, 1.0f)) * w0; totalWeight += w0; }"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j1, c), float4(localPos, 1.0f)) * w1; totalWeight += w1; }"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j2, c), float4(localPos, 1.0f)) * w2; totalWeight += w2; }"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j3, c), float4(localPos, 1.0f)) * w3; totalWeight += w3; }"
        "  if (totalWeight <= 0.00001f) return localPos;"
        "  if (totalWeight < 0.999f) blended += float4(localPos, 1.0f) * (1.0f - totalWeight);"
        "  return blended.xyz;"
        "}"
        "float3 applySkinningNormal(VSIn i, float3 localNormal, float4 skinMeta) {"
        "  if (skinMeta.x < 0.5f) return localNormal;"
        "  float3 blended = float3(0.0f, 0.0f, 0.0f);"
        "  float totalWeight = 0.0f;"
        "  int c = (int)skinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), localNormal) * w0; totalWeight += w0; }"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), localNormal) * w1; totalWeight += w1; }"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), localNormal) * w2; totalWeight += w2; }"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), localNormal) * w3; totalWeight += w3; }"
        "  if (totalWeight <= 0.00001f) return localNormal;"
        "  if (totalWeight < 0.999f) blended += localNormal * (1.0f - totalWeight);"
        "  float len2 = dot(blended, blended);"
        "  return (len2 > 1e-8f) ? normalize(blended) : float3(0.0f, 1.0f, 0.0f);"
        "}"
        "float4 applySkinningTangent(VSIn i, float4 localTangent, float4 skinMeta) {"
        "  if (skinMeta.x < 0.5f) return localTangent;"
        "  float3 tangent = localTangent.xyz;"
        "  float3 blended = float3(0.0f, 0.0f, 0.0f);"
        "  float totalWeight = 0.0f;"
        "  int c = (int)skinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), tangent) * w0; totalWeight += w0; }"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), tangent) * w1; totalWeight += w1; }"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), tangent) * w2; totalWeight += w2; }"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), tangent) * w3; totalWeight += w3; }"
        "  if (totalWeight <= 0.00001f) return localTangent;"
        "  if (totalWeight < 0.999f) blended += tangent * (1.0f - totalWeight);"
        "  float len2 = dot(blended, blended);"
        "  if (len2 > 1e-8f) blended = normalize(blended);"
        "  else blended = tangent;"
        "  return float4(blended, localTangent.w);"
        "}"
        "float4 applyInstancePos(InstanceData inst, float3 localPos) {"
        "  return inst.model0 * localPos.x + inst.model1 * localPos.y + inst.model2 * localPos.z + inst.model3;"
        "}"
        "float3 applyInstanceLinear(InstanceData inst, float3 localDir) {"
        "  return inst.model0.xyz * localDir.x + inst.model1.xyz * localDir.y + inst.model2.xyz * localDir.z;"
        "}"
        "VSOut main(VSIn i, uint instanceId : SV_InstanceID) {"
        "  VSOut o;"
        "  InstanceData inst = gInstances[instanceId];"
        "  float4 skinMeta = resolveSkinMeta(inst);"
        "  float3 localPos = i.pos;"
        "  float3 localNormal = i.nrm;"
        "  float4 localTangent = i.tan;"
        "  float normalLengthSquared = dot(localNormal, localNormal);"
        "  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f && normalLengthSquared > 1e-10f) localPos += localNormal * rsqrt(normalLengthSquared) * 0.001f;"
        "  if (skinMeta.x > 0.5f) {"
        "    localPos = applySkinningPos(i, localPos, skinMeta);"
        "    localNormal = applySkinningNormal(i, localNormal, skinMeta);"
        "    localTangent = applySkinningTangent(i, localTangent, skinMeta);"
        "  }"
        "  float4 instanceWorld = applyInstancePos(inst, localPos);"
        "  float4 world = mul(uModel, instanceWorld);"
        "  float4 clip = mul(uViewProj, world);"
        "  clip.z = clip.z * 0.5f + clip.w * 0.5f;"
        "  clip.z -= uClipMeta.x * clip.w;"
        "  o.pos = clip;"
        "  o.uv = i.uv;"
        "  o.sourceUv1 = i.sourceUv1;"
        "  o.sourceUv2 = i.sourceUv2;"
        "  o.col = i.col * inst.color;"
        "  float3 genDen = max(uGeneratedBoundsMax.xyz - uGeneratedBoundsMin.xyz, float3(1e-5f, 1e-5f, 1e-5f));"
        "  o.generated = saturate((i.pos - uGeneratedBoundsMin.xyz) / genDen);"
        "  o.worldPos = world.xyz;"
        "  float3x3 normalM = (float3x3)uModel;"
        "  float3 instanceNormal = applyInstanceLinear(inst, localNormal);"
        "  float3 wn = mul(normalM, instanceNormal);"
        "  float wnLen2 = dot(wn, wn);"
        "  o.worldNormal = (wnLen2 > 1e-8f) ? normalize(wn) : float3(0.0f, 1.0f, 0.0f);"
        "  float3 instanceTangent = applyInstanceLinear(inst, localTangent.xyz);"
        "  float3 wt = mul(normalM, instanceTangent);"
        "  float wtLen2 = dot(wt, wt);"
        "  if (wtLen2 > 1e-8f) wt = normalize(wt);"
        "  o.worldTangent = float4(wt, localTangent.w);"
        "  return o;"
        "}";
    static constexpr char kPsSource[] = R"HLSL(
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
};
Texture2D gTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gMetallicRoughnessTex : register(t2);
Texture2D gOcclusionTex : register(t3);
Texture2D gEmissiveTex : register(t4);
Texture2D gEnvTex : register(t5);
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

float applyWrap(float coord, float mode) {
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
float litTextureDetailLodBias() {
  if (uMaterialMode < 1.5f || uMaterialMode >= 2.5f) return 0.0f;
  return clamp(uMaterialFlipbook1Frames, -0.75f, 1.25f);
}
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
float4 sampleLgpeGroundTexture(Texture2D tex, float2 uv) {
  // The shared world sampler contributes -0.35; this restores the source
  // FieldGroundShader01 effective -2.0 LOD bias.
  return tex.SampleBias(gSampRR, uv, -1.65f);
}
float4 sampleLgpeFieldTreeTexture(Texture2D tex, float2 uv) {
  // The shared sampler contributes -0.35; the source material requests zero.
  return tex.SampleBias(gSampRR, uv, 0.35f);
}
float4 sampleLgpeFieldGrassRepeat(
    Texture2D tex,
    float2 uv,
    float sourceMipBias) {
  // The shared sampler contributes -0.35; preserve the source material bias.
  return tex.SampleBias(gSampRR, uv, sourceMipBias + 0.35f);
}
float4 sampleLgpeFieldGrassClamp(
    Texture2D tex,
    float2 uv,
    float sourceMipBias) {
  return tex.SampleBias(gSampCC, uv, sourceMipBias + 0.35f);
}
float3 evaluateLgpeFieldGroundSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv = float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float2 uv2 = float2(i.sourceUv2.x, 1.0f - i.sourceUv2.y);
  float4 ground01 = sampleLgpeGroundTexture(gTex, uv0);
  float4 ground02 = sampleLgpeGroundTexture(gNormalTex, uv0);
  float4 grass02 = sampleLgpeGroundTexture(gMetallicRoughnessTex, uv0);
  float4 grass01 = sampleLgpeGroundTexture(gOcclusionTex, uv0);
  float blend = saturate(sampleLgpeGroundTexture(gEmissiveTex, blendUv).r);
  float4 grassBlend = sampleLgpeGroundTexture(gEnvTex, uv2);
  float3 ground = lerp(ground01.rgb, ground02.rgb, blend);
  float3 grass = lerp(grass02.rgb, grass01.rgb, blend);
  float3 surface = lerp(ground, grass, saturate(grassBlend.a));
  float4 authoredVertexColor =
      i.col * float4(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB,
          uVertexColorMulA);
  const float3 alphaLight =
      max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  return grassBlend.rgb * authoredVertexColor.rgb * surface +
         alphaLight * (1.0f - saturate(authoredVertexColor.a));
}
float3 evaluateLgpeFieldCliffSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv = float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 uv2 = float2(i.sourceUv2.x, 1.0f - i.sourceUv2.y);
  float4 cliffTex = sampleLgpeGroundTexture(gTex, uv1);
  float4 ground02 = sampleLgpeGroundTexture(gNormalTex, uv0);
  float4 ground01 = sampleLgpeGroundTexture(gMetallicRoughnessTex, uv0);
  float blend =
      saturate(sampleLgpeGroundTexture(gOcclusionTex, blendUv).r);
  float4 borderTex = sampleLgpeGroundTexture(gEmissiveTex, uv2);
  float3 normal = normalize(i.worldNormal);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialAtlasWidth;
  float rimMax = uMaterialAtlasHeight;
  float rimStrength = uMaterialRect0U;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 rimColor =
      max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  float3 cliff = cliffTex.rgb + rimColor * rim * cliffTex.a;
  float3 grass = lerp(ground02.rgb, ground01.rgb, blend);
  float3 surface = lerp(cliff, grass, saturate(borderTex.a));
  float4 authoredVertexColor =
      i.col * float4(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB,
          uVertexColorMulA);
  return borderTex.rgb * authoredVertexColor.rgb * surface;
}
float4 evaluateLgpeFieldTree05Surface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float4 texture01 = sampleLgpeFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float3 texture02 =
      sampleLgpeFieldTreeTexture(gNormalTex, uv1).rgb;
  float texture03 =
      sampleLgpeFieldTreeTexture(gMetallicRoughnessTex, uv0).r;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      gOcclusionTex.SampleBias(
          gSampCC,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.35f).r);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float lightGate =
      1.0f -
      saturate((1.0f - normalDotLight) * 12.7408008575f);
  float3 secondaryDirection =
      lerp(
          viewDirection,
          -sourceSunRay,
          uMaterialFlipbook0Fps);
  float secondaryMin = uMaterialFlipbook1Frames;
  float secondaryMax = uMaterialFlipbook1Fps;
  float secondarySpan =
      max(secondaryMax, secondaryMin) - secondaryMin;
  float secondaryCoordinate =
      saturate(1.0f - dot(normal, secondaryDirection));
  float secondary = secondarySpan > 0.0f
      ? saturate(
            (secondaryCoordinate - secondaryMin) /
            secondarySpan)
      : 0.0f;
  float3 shadowColor =
      max(float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor =
      max(float3(
              uMaterialAtlasHeight,
              uMaterialRect0U,
              uMaterialRect0V),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor02 =
      max(float3(
              uMaterialRect0W,
              uMaterialRect0H,
              uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  float3 surface =
      texture01.rgb +
      texture02 * rim * rimColor +
      float3(0.110647157f, 0.3070065f, 0.0411512256f) *
          (1.0f - secondary) +
      texture03 * lightGate * rimColor02;
  return float4(lerp(shadowColor, 1.0f.xxx, toon) * surface,
                texture01.a);
}

float4 evaluateLgpeFieldTree02Surface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float4 texture01 = sampleLgpeFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float3 texture02 =
      sampleLgpeFieldTreeTexture(gNormalTex, uv0).rgb;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float2 toonUv =
      float2(toonCoordinate, 1.0f - toonCoordinate);
  float toon = saturate(
      gOcclusionTex.SampleBias(gSampCC, toonUv, 0.35f).r);
  float lightToon = saturate(
      gEmissiveTex.SampleBias(gSampCC, toonUv, 0.35f).r);
  float3 cameraPos =
      float3(
          uMaterialFlipbook1Cols,
          uMaterialFlipbook1Rows,
          uMaterialFlipbook1Frames);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float edge = saturate(1.0f - dot(normal, viewDirection));
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate((edge - rimMin) / rimSpan) * rimStrength
      : 0.0f;
  float directional = saturate(edge * (5.0f / 3.0f));
  float3 greenColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 rimColor =
      float3(
          uMaterialAtlasHeight,
          uMaterialRect0U,
          uMaterialRect0V);
  float3 directionalLightColor =
      float3(
          uMaterialRect1V,
          uMaterialRect1W,
          uMaterialRect1H);
  float3 rimColor02 =
      float3(
          uMaterialFlipbook0Cols,
          uMaterialFlipbook0Rows,
          uMaterialFlipbook0Frames);
  float3 secondary =
      rim * rimColor +
      (1.0f - directional) * rimColor02 +
      lightToon * directionalLightColor;
  float3 surface = texture01.rgb + texture02 * secondary;
  float3 authored = surface * i.col.rgb;
  float3 tinted = lerp(greenColor, authored, saturate(i.col.a));
  // The shared projected-depth PCF is intentionally held at one until its
  // source matrix/depth state is represented by the world renderer.
  float3 lighting = lerp(shadowColor, 1.0f.xxx, toon);
  return float4(lighting * tinted, texture01.a);
}

float4 evaluateLgpeFieldGrassSurface(PSIn i, bool withRim) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float4 textureMap01 =
      sampleLgpeFieldGrassRepeat(gTex, uv1, sourceMipBias);
  if (textureMap01.a <= saturate(uAlphaCutoff)) discard;
  float3 textureMap02 =
      sampleLgpeFieldGrassRepeat(
          gNormalTex,
          uv0,
          sourceMipBias).rgb;
  float3 greenHikari =
      sampleLgpeFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv0,
          sourceMipBias).rgb;
  float greenBlend = saturate(
      sampleLgpeFieldGrassRepeat(
          gOcclusionTex,
          blendUv,
          sourceMipBias).r);
  float highlight = saturate(
      sampleLgpeFieldGrassRepeat(
          gEmissiveTex,
          uv1,
          sourceMipBias).r);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleLgpeFieldGrassClamp(
          gEnvTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 sourceColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 decoration =
      lerp(greenHikari, textureMap02, greenBlend);
  float3 surface =
      textureMap01.rgb * i.col.rgb *
      (decoration + sourceColor * (1.0f - highlight));
  if (withRim) {
    float3 cameraPos =
        float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
    float3 viewDirection = normalize(cameraPos - i.worldPos);
    float edge = saturate(1.0f - dot(normal, viewDirection));
    float rimMin = uMaterialTimeSec;
    float rimMax = uMaterialFlags;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0f
        ? saturate((edge - rimMin) / rimSpan) *
              uMaterialAtlasWidth
        : 0.0f;
    float3 rimColor =
        float3(
            uMaterialAtlasHeight,
            uMaterialRect0U,
            uMaterialRect0V);
    surface += rimColor * rim;
  }
  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 result = lerp(shadowColor, 1.0f.xxx, toon) * surface;
  float alpha = textureMap01.a;
  if (!withRim) {
    float3 onGameColor =
        float3(
            uMaterialTimeSec,
            uMaterialFlags,
            uMaterialAtlasWidth);
    float onGameValue = saturate(uMaterialAtlasHeight);
    result *= lerp(1.0f.xxx, onGameColor, onGameValue);
    alpha *= saturate(uMaterialRect0U);
  }
  return float4(result, alpha);
}

float4 evaluateLgpeFieldGrassShader04Surface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float texture03 =
      saturate(sampleLgpeFieldGrassRepeat(gTex, uv0, 0.0f).r);
  float4 texture02 =
      sampleLgpeFieldGrassRepeat(gNormalTex, uv1, 0.0f);
  float4 texture01 =
      sampleLgpeFieldGrassRepeat(gMetallicRoughnessTex, uv1, 0.0f);
  float4 base = lerp(texture02, texture01, texture03);
  float alpha =
      base.a * i.col.a * saturate(uMaterialTimeSec) *
      saturate(uMaterialAtlasWidth);
  if (alpha <= saturate(uAlphaCutoff)) discard;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleLgpeFieldGrassClamp(
          gOcclusionTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.0f).r);
  float3 shadowColor =
      float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB);
  float3 onGameColor =
      float3(uMaterialAtlasHeight, uMaterialRect0U, uMaterialRect0V);
  float3 surface =
      base.rgb * i.col.rgb *
      lerp(1.0f.xxx, onGameColor, saturate(uMaterialFlags));
  return float4(lerp(shadowColor, 1.0f.xxx, toon) * surface, alpha);
}

float4 evaluateLgpeFieldGrassShader05Surface(PSIn i) {
  float2 maskUv =
      float2(
          i.uv.x + uMaterialRect0V,
          1.0f - (i.uv.y + uMaterialRect0W));
  float2 uv1Primary =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 uv1Secondary =
      float2(i.sourceUv1.x + 1.0f, 0.5f - i.sourceUv1.y);
  float lightLine =
      saturate(
          sampleLgpeFieldGrassRepeat(gNormalTex, maskUv, 0.0f).r);
  float4 alpha01Primary =
      sampleLgpeFieldGrassRepeat(gTex, uv1Primary, 0.0f);
  float4 alpha01Secondary =
      sampleLgpeFieldGrassRepeat(gTex, uv1Secondary, 0.0f);
  float4 base =
      lerp(alpha01Primary, alpha01Secondary, lightLine);
  float alpha = base.a * i.col.a * saturate(uMaterialFlags);
  if (alpha <= saturate(uAlphaCutoff)) discard;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float greenBlend = saturate(
      sampleLgpeFieldGrassRepeat(gEmissiveTex, blendUv, 0.0f).r);
  float3 textureMap01 =
      sampleLgpeFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv0,
          0.0f).rgb;
  float3 textureMap02 =
      sampleLgpeFieldGrassRepeat(gOcclusionTex, uv0, 0.0f).rgb;
  float3 decoration = lerp(textureMap02, textureMap01, greenBlend);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleLgpeFieldGrassClamp(
          gEnvTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.0f).r);
  float3 shadowColor =
      float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB);
  float3 onGameColor =
      float3(
          uMaterialAtlasWidth,
          uMaterialAtlasHeight,
          uMaterialRect0U);
  float3 surface =
      (base.rgb + decoration) * i.col.rgb *
      lerp(1.0f.xxx, onGameColor, saturate(uMaterialTimeSec));
  return float4(lerp(shadowColor, 1.0f.xxx, toon) * surface, alpha);
}

float4 evaluateLgpeFieldObjectTreeMikiSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float4 texture01 = sampleLgpeFieldTreeTexture(gTex, uv0);
  float highlightAlpha =
      sampleLgpeFieldTreeTexture(gNormalTex, uv1).a;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      gOcclusionTex.SampleBias(
          gSampCC,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.35f).r);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            (saturate(1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 shadowColor =
      max(float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor =
      max(float3(
              uMaterialAtlasHeight,
              uMaterialRect0U,
              uMaterialRect0V),
          float3(0.0f, 0.0f, 0.0f));
  float3 lighting = lerp(shadowColor, 1.0f.xxx, toon);
  float3 surface =
      texture01.rgb + rimColor * rim * highlightAlpha;
  return float4(
      lighting * surface * i.col.rgb,
      texture01.a * i.col.a);
}

float hash11(float x) { return frac(sin(x * 12.9898f) * 43758.5453f); }
float hash21(float2 p) {
  float n = dot(p, float2(127.1f, 311.7f));
  return frac(sin(n) * 43758.5453f);
}
float valueNoise2D(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0f - 2.0f * f);
  float a = hash21(i);
  float b = hash21(i + float2(1.0f, 0.0f));
  float c = hash21(i + float2(0.0f, 1.0f));
  float d = hash21(i + float2(1.0f, 1.0f));
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float smoothFlicker(float t, float seed) {
  float x = t * 9.0f + seed * 97.0f;
  float i = floor(x);
  float f = frac(x);
  f = f * f * (3.0f - 2.0f * f);
  return lerp(hash11(i), hash11(i + 1.0f), f);
}
float fbm2D(float2 p) {
  float v = 0.0f;
  float a = 0.5f;
  [unroll]
  for (int k = 0; k < 5; ++k) {
    v += a * valueNoise2D(p);
    p *= 2.02f;
    a *= 0.5f;
  }
  return v;
}
float2 fbmGrad(float2 p) {
  float e = 0.03f;
  float nx = fbm2D(p + float2(e, 0.0f)) - fbm2D(p - float2(e, 0.0f));
  float ny = fbm2D(p + float2(0.0f, e)) - fbm2D(p - float2(0.0f, e));
  return float2(nx, ny) / (2.0f * e);
}
float2 curl2D(float2 p) {
  float2 g = fbmGrad(p);
  return float2(g.y, -g.x);
}
float2 advect2D(float2 p, float flowY, float amount) {
  float2 c1 = curl2D(p * 1.30f + float2(0.0f, -flowY * 0.10f));
  float2 c2 = curl2D(p * 2.70f + float2(3.1f, -flowY * 0.18f));
  return p + (c1 * 0.65f + c2 * 0.35f) * amount;
}
float3 tonemapSoftLocal(float3 c) { return c / (1.0f + c); }

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

float2 clampUvToRegionPixels(float2 localUV01, float4 rectUv) {
  float2 atlasSize = max(float2(uMaterialAtlasWidth, uMaterialAtlasHeight), float2(1.0f, 1.0f));
  float2 rectPx = max(rectUv.zw * atlasSize, float2(1.0f, 1.0f));
  float2 minPx = float2(0.5f, 0.5f) / atlasSize;
  float2 maxPx = (rectPx - float2(0.5f, 0.5f)) / atlasSize;
  float2 uv = saturate(localUV01);
  float2 regionUv = rectUv.xy + uv * rectUv.zw;
  return rectUv.xy + clamp(regionUv - rectUv.xy, minPx, maxPx);
}

float4 sampleAtlasCombined(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float seed, float t, bool coherent) {
  float speed = coherent ? 1.0f : lerp(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
  float phase = coherent ? 0.0f : (seed * frames);
  float f = floor(t * fps * speed + phase);
  float frame = fmod(f, max(1.0f, frames));
  if (frame < 0.0f) frame += max(1.0f, frames);
  float cols = max(1.0f, grid.x);
  float rows = max(1.0f, grid.y);
  float col = fmod(frame, cols);
  float rowFromTop = floor(frame / cols);
  float row = (rows - 1.0f) - rowFromTop;
  float2 cellUVLocal = (float2(col, row) + localUV01) / float2(cols, rows);
  float2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
  return gTex.Sample(gSampCC, cellUv);
}

float4 sampleFireDirect0(float2 uvLocal, float seed, float t) {
  return sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                             float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                             uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, uvLocal, seed, t, true);
}

float4 sampleAtlasCombinedTopLeft(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float t) {
  float f = floor(t * fps);
  float frame = fmod(f, max(1.0f, frames));
  if (frame < 0.0f) frame += max(1.0f, frames);
  float cols = max(1.0f, grid.x);
  float rows = max(1.0f, grid.y);
  float col = fmod(frame, cols);
  float row = floor(frame / cols);
  float2 cellUVLocal = (float2(col, row) + localUV01) / float2(cols, rows);
  float2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
  return gTex.Sample(gSampCC, cellUv);
}
)HLSL"
R"HLSL(

float hash41(float4 p) {
  return frac(sin(dot(p, float4(127.1f, 311.7f, 74.7f, 269.5f))) * 43758.5453123f);
}

float valueNoise4D(float4 p) {
  float4 i = floor(p);
  float4 f = frac(p);
  float4 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
  float accum = 0.0f;
  [unroll]
  for (int dw = 0; dw < 2; ++dw) {
    [unroll]
    for (int dz = 0; dz < 2; ++dz) {
      [unroll]
      for (int dy = 0; dy < 2; ++dy) {
        [unroll]
        for (int dx = 0; dx < 2; ++dx) {
          float4 corner = float4((float)dx, (float)dy, (float)dz, (float)dw);
          float wx = lerp(1.0f - u.x, u.x, corner.x);
          float wy = lerp(1.0f - u.y, u.y, corner.y);
          float wz = lerp(1.0f - u.z, u.z, corner.z);
          float ww = lerp(1.0f - u.w, u.w, corner.w);
          accum += hash41(i + corner) * wx * wy * wz * ww;
        }
      }
    }
  }
  return accum;
}

float authoredFireNoise(float4 p) {
  float value = 0.0f;
  float amplitude = 1.0f;
  float amplitudeSum = 0.0f;
  [unroll]
  for (int octave = 0; octave < 2; ++octave) {
    value += amplitude * valueNoise4D(p);
    amplitudeSum += amplitude;
    p *= 2.0f;
    amplitude *= 0.5f;
  }
  return value / max(amplitudeSum, 1e-5f);
}

float4 evalAuthoredFireMesh(PSIn i) {
  float2 uv = saturate(i.uv + float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows));
  float4 baked = sampleAtlasCombinedTopLeft(
      float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
      float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
      uMaterialFlipbook0Frames,
      uMaterialFlipbook0Fps,
      uv,
      uMaterialTimeSec);
  float rgbCoverage = smoothstep(
      0.03f,
      0.20f,
      max(baked.r, max(baked.g, baked.b)));
  baked.a = max(baked.a, rgbCoverage);
  float baseEngulf = 1.0f - smoothstep(0.0f, 0.28f, saturate(i.generated.y));
  float2 centerXZ = i.generated.xz - float2(0.5f, 0.5f);
  float centerDist = length(centerXZ * float2(1.2f, 1.0f));
  float coreMask = 1.0f - smoothstep(0.0f, 0.23f, centerDist);
  float tipHideMask = baseEngulf * coreMask;
  float warmMask =
      smoothstep(0.68f, 0.98f, baked.r) *
      smoothstep(0.56f, 0.90f, baked.g) *
      (1.0f - smoothstep(0.22f, 0.58f, baked.b));
  baked.rgb = lerp(baked.rgb, float3(1.0f, 0.68f, 0.16f), warmMask * 0.44f);
  baked.rgb = lerp(baked.rgb, float3(1.0f, 0.82f, 0.30f), tipHideMask * 0.55f);
  baked.a = max(baked.a, baseEngulf * 0.95f);
  baked.a = max(baked.a, tipHideMask);
  if (baked.a <= 0.08f) discard;
  baked.a = 1.0f;
  return baked;
}

float lickBlobs(float x, float y, float2 advP, float flowY, float seed) {
  float k = y * 6.6f + flowY * 0.55f;
  float seg = floor(k);
  float f = frac(k);
  float cx1 = (hash11(seg + seed * 31.0f) - 0.5f) * 0.95f * (1.0f - y);
  float cx2 = (hash11(seg + seed * 73.0f) - 0.5f) * 0.95f * (1.0f - y);
  float w = lerp(0.34f, 0.085f, y);
  float2 q1 = float2((x - cx1) / w,        (f - 0.30f) / 0.70f);
  float2 q2 = float2((x - cx2) / (w*0.85f),(f - 0.45f) / 0.65f);
  float m1 = 1.0f - smoothstep(0.60f, 1.00f, length(q1 * float2(1.0f, 1.45f)));
  float m2 = 1.0f - smoothstep(0.60f, 1.00f, length(q2 * float2(1.0f, 1.60f)));
  float br = fbm2D(advP * float2(7.0f, 12.0f) + seed * 17.0f);
  float broken = smoothstep(0.25f, 0.88f, br);
  float gate = smoothstep(0.05f, 0.22f, y) * (1.0f - smoothstep(0.86f, 1.0f, y));
  float m = (m1 + 0.85f * m2) * broken * gate;
  return saturate(m);
}

float4 evalFireTailExact(PSIn i) {
  float age = saturate(i.col.r);
  float vSeed = saturate(i.col.g);
  float t = uMaterialTimeSec;
  // Legacy fire_tail.frag flips gl_PointCoord.y; shared quads already provide the legacy-facing orientation.
  float2 uv = i.uv;
  float2 cc = (uv - 0.5f) * 2.0f;
  float x = cc.x;
  float y = saturate(uv.y);
  float bottomFade = smoothstep(0.00f, 0.11f, y);

  float baseT = smoothstep(0.00f, 0.22f, y);
  float xScaleBase = lerp(2.55f, 1.90f, baseT);
  float yScaleBase = lerp(1.05f, 0.75f, baseT);
  float reBase = length(float2(cc.x * xScaleBase, cc.y * yScaleBase));
  float radialMaskBase = 1.0f - smoothstep(0.98f, 1.10f, reBase);
  float tightMask = 1.0f - smoothstep(0.62f, 0.88f, reBase);
  float reLoose = length(cc * float2(0.55f, 0.85f));
  float radialMaskLoose = 1.0f - smoothstep(0.98f, 1.20f, reLoose);

  float fade = (1.0f - age);
  fade = pow(lerp(fade, 1.0f, 0.25f), 0.75f);

  float2 wobble = float2(
    smoothFlicker(t * 0.9f, vSeed + 0.17f),
    smoothFlicker(t * 1.1f, vSeed + 0.73f)
  ) - 0.5f;
  float4 fb1 = float4(1,1,1,1);
  float4 fb2 = float4(1,1,1,1);
  int fireFlags = (int)(uMaterialFlags + 0.5f);
  bool has1 = (fireFlags & 1) != 0;
  bool has2 = (fireFlags & 2) != 0;
  bool authoredFireMesh = (fireFlags & 8) != 0;
  if (authoredFireMesh) {
    return evalAuthoredFireMesh(i);
  }
  float wobbleScale1 = has2 ? 0.010f : 0.0009f;
  float wobbleScale2 = has2 ? 0.002f : 0.0002f;
  float2 local1 = uv + wobble * wobbleScale1;
  float2 local2 = uv + wobble * wobbleScale2;
  if (has1) {
    fb1 = sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                              float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                              uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, local1, vSeed, t, !has2);
    if (has2) {
      fb2 = sampleAtlasCombined(float4(uMaterialRect1U, uMaterialRect1V, uMaterialRect1W, uMaterialRect1H),
                                float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows),
                                uMaterialFlipbook1Frames, uMaterialFlipbook1Fps, local2, vSeed, t, false);
    } else {
      fb2 = fb1;
    }
  }

  if (has1 && !has2) {
    float2 directUv = float2(uv.x, 1.0f - uv.y);
    float4 fbDirect = sampleFireDirect0(directUv, vSeed, t);
    float alpha = saturate(fbDirect.a);
    float3 rgb = saturate(fbDirect.rgb * 1.15f);
    alpha *= bottomFade;
    alpha *= fade;
    alpha = clamp(alpha, 0.0f, 0.985f);
    if (alpha < 0.003f) discard;
    rgb *= alpha;
    return float4(rgb, alpha);
  }

  float fb1A = saturate(fb1.a);
  float fb1Lum = saturate(dot(fb1.rgb, float3(0.3333f, 0.3333f, 0.3333f)));
  float speed = has2 ? lerp(0.95f, 1.10f, hash11(vSeed * 19.31f)) : 1.0f;
  float flow = t * 1.55f * speed;
  float flowY = flow * lerp(0.75f, 1.55f, y * y);
  float width = lerp(0.30f, 0.055f, pow(y, 2.35f));
  float widthHybrid = width * 2.80f;
  float yy = (y * 2.0f - 1.0f);
  yy = yy * 1.45f + 0.38f;
  yy /= 1.12f;
  float2 p = float2(x / widthHybrid, yy) * 1.22f;
  float sway = fbm2D(float2(x * 1.7f, y * 3.8f) + float2(0.0f, -flowY * 0.65f) + vSeed * 7.0f);
  p.x += (sway - 0.5f) * (has2 ? 0.015f : 0.004f) * (1.0f - y);
  float d0 = length(p);
  float2 advP = advect2D(p * float2(1.20f, 1.0f) + vSeed * 6.0f, flowY, 0.25f);
  float n = fbm2D(advP * float2(2.7f, 4.5f) + vSeed * 11.0f);
  float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);
  float core = saturate(1.0f - smoothstep(0.00f, 0.88f, d));
  float outer = saturate(1.0f - smoothstep(0.30f, 1.05f, d));
  float blobs = lickBlobs(x, y, advP, flowY, vSeed);
  float body = saturate(smoothstep(0.92f, 0.12f, d));
  float procAlpha = body * (0.60f + 0.55f * blobs);
  float calmFlicker = smoothFlicker(t * 1.2f, vSeed);
  procAlpha *= has2 ? (0.92f + 0.15f * calmFlicker) : (0.985f + 0.03f * calmFlicker);
  procAlpha *= bottomFade;
  procAlpha *= fade;
  procAlpha = 1.0f - exp(-procAlpha * 1.85f);
  procAlpha = clamp(procAlpha, 0.0f, 0.96f);

  float3 yellow = float3(1.70f, 1.20f, 0.28f);
  float3 red = float3(1.45f, 0.18f, 0.06f);
  float3 orange = float3(1.60f, 0.55f, 0.12f);
  float wave = 0.5f + 0.5f * sin((x * 1.8f + y * 8.5f - flowY * 4.9f) + vSeed * 7.0f);
  float kk = y * 6.0f - flowY * 0.55f;
  float seg = floor(kk);
  float segRand = hash11(seg + vSeed * 71.3f);
  float segRand2 = hash11(seg + vSeed * 19.7f + 5.0f);
  float tri1 = abs(frac((x * 0.85f + y * 1.05f - flowY * 0.18f) * 2.8f + vSeed * 7.0f) - 0.5f) * 2.0f;
  float tri2 = abs(frac((x * 1.10f - y * 0.60f - flowY * 0.14f) * 3.8f + vSeed * 3.0f) - 0.5f) * 2.0f;
  float zig = lerp(tri1, tri2, 0.50f + 0.50f * (segRand - 0.5f));
  zig = smoothstep(0.15f, 0.85f, zig);
  float warp = fbm2D(advect2D(float2(x * 0.85f, y * 1.2f) + vSeed * 6.0f, flowY, 0.22f) * float2(4.5f, 7.5f)) - 0.5f;
  float jag = 0.0f;
  jag += (segRand - 0.5f) * 0.10f;
  jag += (segRand2 - 0.5f) * 0.05f;
  jag += (zig - 0.5f) * 0.14f;
  jag += warp * 0.06f;
  jag *= (1.0f - 0.55f * smoothstep(0.65f, 1.0f, y));
  float boundary = clamp(0.34f + jag, 0.14f, 0.62f);
  float redMask = smoothstep(boundary, boundary + 0.11f, y);
  float3 procRgb = lerp(yellow, red, redMask);
  float band = smoothstep(boundary - 0.02f, boundary + 0.02f, y) *
               (1.0f - smoothstep(boundary + 0.02f, boundary + 0.10f, y));
  procRgb = lerp(procRgb, orange, 0.55f * band);
  float climb = core * (1.0f - smoothstep(0.55f, 0.95f, y)) * (0.35f + 0.65f * wave);
  procRgb = lerp(procRgb, yellow, 0.18f * climb);
  procRgb *= (1.18f + 0.35f * outer);

  float3 hybridRgb = procRgb;
  float hybridAlpha = procAlpha;
  if (has1) {
    hybridAlpha = clamp(hybridAlpha * lerp(0.55f, 1.65f, fb1A), 0.0f, 0.96f);
    hybridRgb *= lerp(0.85f, 1.25f, fb1Lum);
    hybridRgb *= lerp(float3(1.0f,1.0f,1.0f), fb1.rgb * 1.35f, 0.30f);
  }

  float3 fb2Rgb = fb2.rgb;
  float fb2Alpha = pow(saturate(fb2.a), 0.66f);
  float hot = smoothstep(0.10f, 0.55f, 1.0f - y);
  float3 tint = lerp(red, yellow, hot);
  fb2Rgb *= tint * 1.30f;
  fb2Alpha *= tightMask;
  fb2Alpha *= bottomFade;

  float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
  float fb2MaskedA = fb2Alpha * radialMaskBase;
  float mixW = 0.50f;
  float3 rgb = lerp(hybridRgb, fb2Rgb, mixW);
  float alpha = lerp(hybridMaskedA, fb2MaskedA, mixW);
  alpha *= fade;
  alpha = clamp(alpha + 0.10f * outer * fade, 0.0f, 0.985f);
  rgb *= 2.60f;
  float emissive = (0.85f * outer + 0.45f * core) * fade;
  rgb *= (1.0f + 2.10f * emissive);
  rgb = tonemapSoftLocal(rgb);
  if (alpha < 0.003f) discard;
  rgb *= alpha;
  return float4(rgb, alpha);
}
)HLSL"
R"HLSL(

float3 safeNormalize(float3 value, float3 fallback) {
  float len2 = dot(value, value);
  if (len2 < 1e-8f) return fallback;
  return value * rsqrt(len2);
}

__PAC_SHARED_WORLD_PBR_SECTION__

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
float3 computeMappedNormal(PSIn i,
                           bool isFrontFace,
                           float2 sampleUv,
                           float2 uvDx,
                           float2 uvDy,
                           bool useNormalTexture,
                           float normalScale) {
  float faceDirection = isFrontFace ? 1.0f : -1.0f;
  float3 n = normalize(i.worldNormal);
  if (dot(n, n) < 1e-6f) {
    float3 dx = ddx(i.worldPos);
    float3 dy = ddy(i.worldPos);
    n = normalize(cross(dx, dy));
  }
  n *= faceDirection;
  if (!useNormalTexture) return n;

  float3 normalTexel = sampleTextureWithWrap(gNormalTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).xyz;
  float2 mapXY = normalTexel.xy * 2.0f - 1.0f;
  mapXY *= max(normalScale, 0.0f) * 1.25f;
  // Support standard RGB tangent-space normals and packed-XY normals (blue=0).
  float authoredZ = normalTexel.z * 2.0f - 1.0f;
  float reconZ = sqrt(max(1.0f - saturate(dot(mapXY, mapXY)), 0.0f));
  float useReconstructedZ = (normalTexel.z <= (1.5f / 255.0f)) ? 1.0f : 0.0f;
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

float3 applyWorldLitModel(PSIn i,
                          bool isFrontFace,
                          float3 linearColor,
                          float2 sampleUv,
                          float2 uvDx,
                          float2 uvDy,
                          bool useNormalTexture,
                          bool useMetallicRoughnessTexture,
                          bool useOcclusionTexture,
                          bool useEmissiveTexture,
                          float normalScale,
                          float metallicFactor,
                          float roughnessFactor,
                          float occlusionStrength,
                          float3 emissiveFactor,
                          float3 cameraPos,
                          float3 cameraForwardPacked,
                          float3 cameraTarget) {
  float3 n = computeMappedNormal(i, isFrontFace, sampleUv, uvDx, uvDy, useNormalTexture, normalScale);
  float3 orm = float3(1.0f, 1.0f, 1.0f);
  if (useMetallicRoughnessTexture) {
    orm = sampleTextureWithWrap(
              gMetallicRoughnessTex,
              sampleUv,
              uvDx,
              uvDy,
              uWrapS,
              uWrapT).rgb;
  }
  float roughness = clamp(orm.g * saturate(roughnessFactor), 0.16f, 1.0f);
  float metallic = clamp(orm.b * saturate(metallicFactor), 0.0f, 1.0f);
  float ao = 1.0f;
  if (useOcclusionTexture) {
    float occTex = sampleTextureWithWrap(gOcclusionTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).r;
    ao = lerp(1.0f, occTex, saturate(occlusionStrength));
  }

  float3 albedo = saturate(linearColor);
  float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
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
  const float directIntensity = __PAC_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  const float3 ambientColor = float3(1.0f, 1.0f, 1.0f);
  const float ambientIntensity = __PAC_PBR_AMBIENT_INTENSITY__;

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
  diffuseIBL *= __PAC_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= __PAC_PBR_SPECULAR_IBL_SCALE__;
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
)HLSL"
R"HLSL(

float4 evaluateWorldPixel(PSIn i, bool isFrontFace) {
  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f) {
    if (!isFrontFace) discard;
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  if (uMaterialMode > 0.5f && uMaterialMode < 1.5f) {
    return evalFireTailExact(i);
  }
  if (uMaterialMode > 3.5f && uMaterialMode < 4.5f) {
    const float groundExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float3 groundLinear = evaluateLgpeFieldGroundSurface(i);
    float3 groundMapped = applyViewerToneMapping(
        max(groundLinear, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        groundExposure);
    return float4(linearToSrgb(groundMapped), 1.0f);
  }
  if (uMaterialMode > 4.5f && uMaterialMode < 5.5f) {
    const float cliffExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float3 cliffLinear = evaluateLgpeFieldCliffSurface(i);
    float3 cliffMapped = applyViewerToneMapping(
        max(cliffLinear, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        cliffExposure);
    return float4(linearToSrgb(cliffMapped), 1.0f);
  }
  if (uMaterialMode > 5.5f && uMaterialMode < 6.5f) {
    const float treeExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 treeSurface = evaluateLgpeFieldTree05Surface(i);
    float3 treeMapped = applyViewerToneMapping(
        max(treeSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        treeExposure);
    return float4(linearToSrgb(treeMapped), treeSurface.a);
  }
  if (uMaterialMode > 6.5f && uMaterialMode < 7.5f) {
    const float trunkExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 trunkSurface =
        evaluateLgpeFieldObjectTreeMikiSurface(i);
    float3 trunkMapped = applyViewerToneMapping(
        max(trunkSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        trunkExposure);
    return float4(linearToSrgb(trunkMapped), trunkSurface.a);
  }
  if (uMaterialMode > 7.5f && uMaterialMode < 8.5f) {
    const float treeExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 treeSurface = evaluateLgpeFieldTree02Surface(i);
    float3 treeMapped = applyViewerToneMapping(
        max(treeSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        treeExposure);
    return float4(linearToSrgb(treeMapped), treeSurface.a);
  }
  if (uMaterialMode > 8.5f && uMaterialMode < 9.5f) {
    const float grassExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 grassSurface =
        evaluateLgpeFieldGrassSurface(i, false);
    float3 grassMapped = applyViewerToneMapping(
        max(grassSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        grassExposure);
    return float4(linearToSrgb(grassMapped), grassSurface.a);
  }
  if (uMaterialMode > 9.5f && uMaterialMode < 10.5f) {
    const float grassExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 grassSurface =
        evaluateLgpeFieldGrassSurface(i, true);
    float3 grassMapped = applyViewerToneMapping(
        max(grassSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        grassExposure);
    return float4(linearToSrgb(grassMapped), grassSurface.a);
  }
  if (uMaterialMode > 10.5f && uMaterialMode < 11.5f) {
    const float grassExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 grassSurface =
        evaluateLgpeFieldGrassShader04Surface(i);
    float3 grassMapped = applyViewerToneMapping(
        max(grassSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        grassExposure);
    return float4(linearToSrgb(grassMapped), grassSurface.a);
  }
  if (uMaterialMode > 11.5f && uMaterialMode < 12.5f) {
    const float grassExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
    float4 grassSurface =
        evaluateLgpeFieldGrassShader05Surface(i);
    float3 grassMapped = applyViewerToneMapping(
        max(grassSurface.rgb, float3(0.0f, 0.0f, 0.0f)),
        1.0f,
        grassExposure);
    return float4(linearToSrgb(grassMapped), grassSurface.a);
  }
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outLinear = saturate(i.col.rgb * float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB));
  float2 wrappedUv = float2(applyWrap(i.uv.x, uWrapS), applyWrap(i.uv.y, uWrapT));
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
  const float pbrDebugView = uMaterialFlipbook1Fps;
  if (uMaterialMode >= 1.5f && pbrDebugView > 0.5f) {
    float3 dbg = float3(0.0f, 0.0f, 0.0f);
    if (pbrDebugView < 1.5f) {
      // 1: Base/albedo sample.
      dbg = saturate(tex.rgb);
    } else if (pbrDebugView < 2.5f) {
      // 2: Normal map sample.
      dbg = sampleTextureWithWrap(gNormalTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb;
    } else if (pbrDebugView < 3.5f) {
      // 3: Roughness channel.
      const float rgh =
          sampleTextureWithWrap(gMetallicRoughnessTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).g;
      dbg = float3(rgh, rgh, rgh);
    } else if (pbrDebugView < 4.5f) {
      // 4: Metallic channel.
      const float met =
          sampleTextureWithWrap(gMetallicRoughnessTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).b;
      dbg = float3(met, met, met);
    } else if (pbrDebugView < 5.5f) {
      // 5: AO channel.
      const float ao =
          sampleTextureWithWrap(gOcclusionTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).r;
      dbg = float3(ao, ao, ao);
    }
    return float4(linearToSrgb(saturate(dbg)), 1.0f);
  }
  if (uMaterialMode >= 1.5f) {
    const int pbrFlags = (int)(uMaterialFlags + 0.5f);
    const bool useNormalTexture = (pbrFlags & (1 << 0)) != 0;
    const bool useMetallicRoughnessTexture = (pbrFlags & (1 << 1)) != 0;
    const bool useOcclusionTexture = (pbrFlags & (1 << 2)) != 0;
    const bool useEmissiveTexture = (pbrFlags & (1 << 3)) != 0;
    const float normalScale = max(uMaterialAtlasWidth, 0.0f);
    const float metallicFactor = saturate(uMaterialAtlasHeight);
    const float roughnessFactor = saturate(uMaterialRect0U);
    const float occlusionStrength = saturate(uMaterialRect0V);
    const float3 emissiveFactor =
        max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U), float3(0.0f, 0.0f, 0.0f));
    const float3 cameraPos = float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
    const float3 cameraForward = float3(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows, uMaterialFlipbook0Frames);
    const float3 cameraTarget = float3(uMaterialFlipbook0Fps, uMaterialFlipbook1Cols, uMaterialFlipbook1Rows);
    outLinear = applyWorldLitModel(i,
                                   isFrontFace,
                                   outLinear,
                                   wrappedUv,
                                   uvDx,
                                   uvDy,
                                   useNormalTexture,
                                   useMetallicRoughnessTexture,
                                   useOcclusionTexture,
                                   useEmissiveTexture,
                                   normalScale,
                                   metallicFactor,
                                   roughnessFactor,
                                   occlusionStrength,
                                   emissiveFactor,
                                   cameraPos,
                                   cameraForward,
                                   cameraTarget);
  }
  const float toneMappingExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = linearToSrgb(mapped);
  return float4(outSrgb, outA);
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

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> dualSourcePsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (!compileHlslWithCache(kVsSource,
                              sizeof(kVsSource) - 1,
                              "main",
                              "vs_5_0",
                              d3dCompileFlags(),
                              0,
                              vsBlob,
                              errBlob) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    const std::string worldPsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            kPsSource, engine::render::world_pbr_shader_shared::ShaderLanguage::Hlsl);
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
    materialSrvRange.NumDescriptors = 6;
    materialSrvRange.BaseShaderRegister = 0;
    materialSrvRange.RegisterSpace = 0;
    materialSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[5]{};
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
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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

