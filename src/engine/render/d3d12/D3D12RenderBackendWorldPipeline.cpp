#include "engine/render/PreparationBatch.h"
#include "engine/render/ShaderPreparation.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/core/Environment.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/WorldPbrShaderShared.h"
#include "engine/render/WorldMotionShader.h"
#include "engine/render/WorldWindShader.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
using namespace engine::render::d3d12_pipeline_compile;
#endif

#if defined(_WIN32)
namespace {
void temporalReactiveAlpha(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    // HDR alpha is reactive coverage, independent of which consumer reads it.
    auto& b=desc.BlendState.RenderTarget[0];
    if(!b.BlendEnable) {
        b.BlendEnable=TRUE;b.SrcBlend=D3D12_BLEND_ONE;b.DestBlend=D3D12_BLEND_ZERO;
        b.SrcBlendAlpha=b.DestBlendAlpha=D3D12_BLEND_ZERO;
    } else if(b.SrcBlend==D3D12_BLEND_SRC1_ALPHA) {
        b.SrcBlendAlpha=D3D12_BLEND_SRC1_ALPHA;b.DestBlendAlpha=D3D12_BLEND_INV_SRC1_ALPHA;
    }
}
}
#endif
void D3D12RenderBackend::createWorldPipeline(bool createBuffers) {
#if defined(_WIN32)
    worldShadowPipelineState_.Reset();
    worldPointLightPipelines_.clear();
    screenSourceSpecialization_ = !engine::env::flagEnabled("PHLOSION_DISABLE_SOURCE_SPECIALIZATION");
    worldPointLightPipelineFactory_ = {};
    static const std::string kVertexTemplate = engine::render::worldWindVertexHlsl()+R"HLSL(cbuffer VSConstants : register(b0) { float4x4 uViewProj; float4x4 uModel; float4 uSkinMeta; float4 uClipMeta; };cbuffer SharedVertexConstants : register(b4) { float4 uEnvironmentSunVs; float4 uEnvironmentRadianceVs; };cbuffer MaterialVsConstants : register(b1) { float _m0,_m1,_m2,_m3,_m4,_m5,_m6,_m7,_m8,_m9,_m10,uMaterialMode,uMaterialTimeSecVs,uMaterialFlagsVs,uMaterialAtlasWidthVs,uMaterialAtlasHeightVs; float4 uMaterialRect0Vs; float4 uMaterialRect1Vs; };StructuredBuffer<float4> gSkinMatrices : register(t7);
#ifdef PHLOSION_MOTION_VERTEX
StructuredBuffer<float4> gMotionWords:register(t8);
static bool motionPreviousEvaluation=false;
static uint motionPacket=0;
static uint motionPaletteWordBase=0;
static bool motionExternalPalette=false;
float4x4 motionLoadMatrix(uint offset) {
    uint k=motionPacket+offset;
    return transpose(float4x4(gMotionWords[k],gMotionWords[k+1],gMotionWords[k+2],gMotionWords[k+3]));
}
#endif
float4 vertexSkinWord(uint index) {
#ifdef PHLOSION_MOTION_VERTEX
    if(motionPreviousEvaluation)return motionExternalPalette?gSkinMatrices[motionPaletteWordBase+index]:gMotionWords[motionPacket+16+index];
#endif
    return gSkinMatrices[index];
}
Texture2D gVertexDisplacementMap : register(t1);Texture2D<float> gVertexEnvironmentMap : register(t5);SamplerState gVertexSampCC : register(s0);struct InstanceData { float4 model0; float4 model1; float4 model2; float4 model3; float4 color; uint4 skinMeta; };StructuredBuffer<InstanceData> gInstances : register(t6);struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 nrm : NORMAL; float4 jnts : BLENDINDICES; float4 wgts : BLENDWEIGHT; float4 tan : TANGENT; float2 sourceUv1 : TEXCOORD1; float2 sourceUv2 : TEXCOORD2; };struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; float3 generated : TEXCOORD4; float2 sourceUv1 : TEXCOORD5; float2 sourceUv2 : TEXCOORD6; };static const int kMaxSkinMatrices = 128;float4 resolveSkinMeta(InstanceData inst) {  if (inst.skinMeta.x != 0u) return float4(1.0f, (float)inst.skinMeta.y, (float)inst.skinMeta.z, (float)inst.skinMeta.w);  return uSkinMeta;}float4x4 loadPackedMatrix(uint baseFloat4) {  float4 c0 = vertexSkinWord(baseFloat4 + 0u);  float4 c1 = vertexSkinWord(baseFloat4 + 1u);  float4 c2 = vertexSkinWord(baseFloat4 + 2u);  float4 c3 = vertexSkinWord(baseFloat4 + 3u);  return float4x4(c0.x, c1.x, c2.x, c3.x, c0.y, c1.y, c2.y, c3.y, c0.z, c1.z, c2.z, c3.z, c0.w, c1.w, c2.w, c3.w);}float4x4 loadSkinMatrix(float4 skinMeta, int jointIndex, int c) {  const uint jointBase = (uint)skinMeta.w + (uint)(jointIndex * 4);  if (skinMeta.z > 0.5f) {    const uint inverseBindBase = (uint)skinMeta.w + (uint)(c * 4 + jointIndex * 4);    return mul(loadPackedMatrix(jointBase), loadPackedMatrix(inverseBindBase));  }  return loadPackedMatrix(jointBase);}float3 applySkinningPos(VSIn i, float3 localPos, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localPos;  float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j0, c), float4(localPos, 1.0f)) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j1, c), float4(localPos, 1.0f)) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j2, c), float4(localPos, 1.0f)) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul(loadSkinMatrix(skinMeta, j3, c), float4(localPos, 1.0f)) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localPos;  if (totalWeight < 0.999f) blended += float4(localPos, 1.0f) * (1.0f - totalWeight);  return blended.xyz;}float3 applySkinningNormal(VSIn i, float3 localNormal, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localNormal;  float3 blended = float3(0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), localNormal) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), localNormal) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), localNormal) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), localNormal) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localNormal;  if (totalWeight < 0.999f) blended += localNormal * (1.0f - totalWeight);  float len2 = dot(blended, blended);  return (len2 > 1e-8f) ? normalize(blended) : float3(0.0f, 1.0f, 0.0f);}float4 applySkinningTangent(VSIn i, float4 localTangent, float4 skinMeta) {  if (skinMeta.x < 0.5f) return localTangent;  float3 tangent = localTangent.xyz;  float3 blended = float3(0.0f, 0.0f, 0.0f);  float totalWeight = 0.0f;  int c = (int)skinMeta.y;  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j0, c), tangent) * w0; totalWeight += w0; }  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j1, c), tangent) * w1; totalWeight += w1; }  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j2, c), tangent) * w2; totalWeight += w2; }  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < kMaxSkinMatrices) { blended += mul((float3x3)loadSkinMatrix(skinMeta, j3, c), tangent) * w3; totalWeight += w3; }  if (totalWeight <= 0.00001f) return localTangent;  if (totalWeight < 0.999f) blended += tangent * (1.0f - totalWeight);  float len2 = dot(blended, blended);  if (len2 > 1e-8f) blended = normalize(blended);  else blended = tangent;  return float4(blended, localTangent.w);}float4 applyInstancePos(InstanceData inst, float3 localPos) {  return inst.model0 * localPos.x + inst.model1 * localPos.y + inst.model2 * localPos.z + inst.model3;}float3 applyInstanceLinear(InstanceData inst, float3 localDir) {  return inst.model0.xyz * localDir.x + inst.model1.xyz * localDir.y + inst.model2.xyz * localDir.z;}
__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
VSOut main(VSIn i, uint instanceId : SV_InstanceID) {  VSOut o;  InstanceData inst = gInstances[instanceId];  float4 skinMeta = resolveSkinMeta(inst);  float3 localPos = i.pos;  float3 localNormal = i.nrm;  float4 localTangent = i.tan;  float normalLengthSquared = dot(localNormal, localNormal);  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f && normalLengthSquared > 1e-10f) localPos += localNormal * rsqrt(normalLengthSquared) * 0.001f;
__PHLOSION_PROJECT_MATERIAL_EVALUATION__
  if (skinMeta.x > 0.5f) {    localPos = applySkinningPos(i, localPos, skinMeta);    localNormal = applySkinningNormal(i, localNormal, skinMeta);    localTangent = applySkinningTangent(i, localTangent, skinMeta);  }  float4 instanceWorld = applyInstancePos(inst, localPos);  float4 world = mul(uModel, instanceWorld);  float4 clip = mul(uViewProj, world);  clip.z = clip.z * 0.5f + clip.w * 0.5f;  clip.z -= uClipMeta.x * clip.w;  if (uEnvironmentSunVs.w > 0.5f && uEnvironmentRadianceVs.w > 0.5f) clip.z = clip.w;  o.pos = clip;  o.uv = i.uv;  o.sourceUv1 = i.sourceUv1;  o.sourceUv2 = i.sourceUv2;  o.col = i.col * inst.color;  float3 genDen = max(uMaterialRect1Vs.xyz - uMaterialRect0Vs.xyz, float3(1e-5f, 1e-5f, 1e-5f));  o.generated = saturate((i.pos - uMaterialRect0Vs.xyz) / genDen);  o.worldPos = world.xyz;  float3x3 normalM = (float3x3)uModel;  float3 instanceNormal = applyInstanceLinear(inst, localNormal);  float3 wn = mul(normalM, instanceNormal);  float wnLen2 = dot(wn, wn);  o.worldNormal = (wnLen2 > 1e-8f) ? normalize(wn) : float3(0.0f, 1.0f, 0.0f);  float3 instanceTangent = applyInstanceLinear(inst, localTangent.xyz);  float3 wt = mul(normalM, instanceTangent);  float wtLen2 = dot(wt, wt);  if (wtLen2 > 1e-8f) wt = normalize(wt);  o.worldTangent = float4(wt, localTangent.w);  return o;})HLSL";
    const std::string kVsSource = engine::render::injectWorldMaterialProfile(kVertexTemplate, worldMaterialProfile_.d3d12Vertex);
    static const std::string kPsSource = R"HLSL(
cbuffer EnvironmentConstants : register(b4) {
  float4 uEnvironmentSun;
  float4 uEnvironmentRadiance;
  float4 uEnvironmentAmbient;
  float4 uEnvironmentHaze;
  float4 uEnvironmentHeight;
  float4 uEnvironmentNight;
  float4 uEnvironmentMoon;
  float4 uEnvironmentMoonLight;
  float4 uEnvironmentCelestial;
  float4 uEnvironmentCloudParams;
  float4 uEnvironmentCloudOffsets;
  float4 uEnvironmentCloudTint;
  float4 uWeatherParams;
  float4 uWeatherShelters[4];
  float4 uWeatherShelterHeights;
  float4 uWeatherMaterial;
  float4 uEnvironmentCamera;
  float4 uWorldPointLights[20];
  float4x4 uWorldShadowMatrix;
  float4 uWorldShadowParams;
  float4 uWorldRectLights[112];
  float4 uShadowAtlasUv;
  float4 uLocalShadows[120];
  float4 uReflectionResponse;
  float4 uReflectionResponseParams;
  float4 uWorldPuddles[32];
  float4 uWorldSky[24];
};
cbuffer DiffuseProbeConstants : register(b2) { float4 uWorldDiffuseProbes[483]; float4 uWorldContact[24]; float4 uWorldScreenReflection[11]; float4 uWorldLocalReflection[68]; float4 uWorldClustered[5]; float4 uWorldFog[13]; float4 uWorldWet[11]; float4 uWorldClouds[12]; float4 uWorldSun[72]; float4 uSkyCloud[72]; };
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
  uint4 uLayeredMaterial;
};
#ifdef PHLOSION_PROBE_RELIGHT
Texture2D<uint4> gClusteredDiffuse : register(t18);
#else
Texture2D<float4> gClusteredDiffuse : register(t18);
#endif
Texture2D<float4> gClusteredSpecular : register(t19);
Texture2D<float4> gClusteredDepth : register(t20);
#define CLUSTER_VIEW(i) uWorldClustered[i]
#define CLUSTER_DIFFUSE(p) float4(gClusteredDiffuse.Load(int3(p,0)))
#define CLUSTER_SPECULAR(p) gClusteredSpecular.Load(int3(p,0))
#define CLUSTER_DEPTH(p) gClusteredDepth.Load(int3(p,0)).r
Texture2D<float4> gStoredAlbedo : register(t21);
Texture2D<float4> gStoredNormal : register(t22);
Texture2D<float4> gStoredMaterial : register(t23);
Texture2D<float4> gStoredEmission : register(t24);
#define STORED_ALBEDO(p) gStoredAlbedo.Load(int3(p,0))
#define STORED_NORMAL(p) gStoredNormal.Load(int3(p,0))
#define STORED_MATERIAL(p) gStoredMaterial.Load(int3(p,0))
#define STORED_EMISSION(p) gStoredEmission.Load(int3(p,0))

Texture2D gWorldWetDrops : register(t30),gWorldWetLeaks : register(t31),gWorldWetRipples : register(t32);
Texture2D gWorldWetFields : register(t33),gWorldWetRain : register(t34);
Texture2D gWorldSunNoise : register(t38);
#define WORLD_SUN_NOISE(p) gWorldSunNoise.Load(int3(p,0))
#define WORLD_SUN_PRIVATE static
Texture2D<float> gWorldSunDepth : register(t37);
SamplerComparisonState gWorldSunSampler : register(s12);
#define WORLD_SUN(i) uWorldSun[i]
#define WORLD_SUN_COMPARE(uv,z) gWorldSunDepth.SampleCmpLevelZero(gWorldSunSampler,uv,z)
Texture2D gWorldCloudShadow : register(t35),gWorldCloudResolved : register(t36);
#define SKY_CLOUD(i) uSkyCloud[i]
#define SKY_CLOUD_SHADOW(uv) gWorldCloudShadow.SampleLevel(gLinearCC,uv,0)
#define SKY_CLOUD_PRIVATE static
#define SKY_CLOUD_LOOP [loop]
#define WORLD_CLOUD(i) uWorldClouds[i]
#define WORLD_CLOUD_SHADOW(uv) gWorldCloudShadow.SampleLevel(gLinearCC,uv,0).r
#define WORLD_CLOUD_COLOR(uv) gWorldCloudResolved.SampleLevel(gLinearCC,uv,0)
#define WORLD_WET_LOOP [loop]
#define WORLD_WET_VIEW(i) uWorldWet[i]
#define WORLD_WET_DROPS(uv) gWorldWetDrops.SampleGrad(gLinearRR,uv,ddx(uv),ddy(uv))
#define WORLD_WET_LEAKS(uv) gWorldWetLeaks.SampleGrad(gLinearRR,uv,ddx(uv),ddy(uv))
#define WORLD_WET_RIPPLES(uv) gWorldWetRipples.SampleGrad(gLinearRR,uv,ddx(uv),ddy(uv))
#define WORLD_WET_FIELD(p) gWorldWetFields.Load(int3(p,0))
#define WORLD_WET_RAIN(p) gWorldWetRain.Load(int3(p,0)).r

Texture2D gWorldFogNear : register(t28),gWorldFogFar : register(t29);
Texture2D gWorldFogSky : register(t39);
#define WORLD_FOG_SKY_ATLAS(uv) gWorldFogSky.SampleLevel(gLinearCC,uv,0).rgb
#define WORLD_FOG_VIEW(i) uWorldFog[i]
#define WORLD_FOG_SAMPLE(uv,far) ((far) ? gWorldFogFar.SampleLevel(gSampCC,uv,0) : gWorldFogNear.SampleLevel(gSampCC,uv,0))
Texture2D gTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gMetallicRoughnessTex : register(t2);
Texture2D gOcclusionTex : register(t3);
Texture2D gEmissiveTex : register(t4);
Texture2D gEnvTex : register(t5);
Texture2D gLightProjectionTex : register(t6);
#define ENV_SKY_MODEL(uv) gLightProjectionTex.SampleLevel(gLinearCC,uv,0).rgb
Texture2D gProjectedShadowTex : register(t7);
Texture2D<float2> gLocalShadowMoments : register(t11);
#define WORLD_MOMENT_SAMPLE(uv) gLocalShadowMoments.SampleLevel(gWorldShadowSampler,uv,0)
Texture2D<float> gWorldShadowTex : register(t8);
Texture2D gRectLightLut : register(t9);
SamplerState gWorldShadowSampler : register(s9);
SamplerState gSampCC : register(s0);
SamplerState gSampRR : register(s1);
SamplerState gSampCR : register(s2);
SamplerState gSampRC : register(s3);
SamplerState gSampMR : register(s4);
SamplerState gSampRM : register(s5);
SamplerState gSampMM : register(s6);
SamplerState gSampCM : register(s7);
SamplerState gSampMC : register(s8);
SamplerState gLinearCC : register(s10);
SamplerState gLinearRR : register(s11);
struct PSIn { noperspective centroid float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; float3 generated : TEXCOORD4; float2 sourceUv1 : TEXCOORD5; float2 sourceUv2 : TEXCOORD6;
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
float4 motionCurrent:TEXCOORD7;float4 motionPrevious:TEXCOORD8;
nointerpolation uint motionId:TEXCOORD9;nointerpolation uint motionValid:TEXCOORD10;
#endif
};


#define WORLD_SCENE_RADIANCE (uSceneColorPostEnabled > 1.5)
#define ENV_ATLAS_RGBA(uv) gTex.SampleLevel(gLinearCC,uv,0)
#define ENV_SKY(i) uWorldSky[i]
#define ENV_SUN uEnvironmentSun
#define ENV_RADIANCE uEnvironmentRadiance
#define ENV_AMBIENT uEnvironmentAmbient
#define ENV_HAZE uEnvironmentHaze
#define ENV_HEIGHT uEnvironmentHeight
#define ENV_NIGHT uEnvironmentNight
#define ENV_MOON uEnvironmentMoon
#define ENV_MOON_LIGHT uEnvironmentMoonLight
#define ENV_CELESTIAL uEnvironmentCelestial
#define ENV_CLOUD_PARAMS uEnvironmentCloudParams
#define ENV_CLOUD_OFFSETS uEnvironmentCloudOffsets
#define ENV_CLOUD_TINT uEnvironmentCloudTint
#define ENV_WEATHER uWeatherParams
#define ENV_SHELTER(index) uWeatherShelters[index]
#define ENV_SHELTER_HEIGHT uWeatherShelterHeights
#define ENV_WEATHER_MATERIAL uWeatherMaterial
#define ENV_PUDDLE(index) uWorldPuddles[index]
#define ENV_REFLECTION_RESPONSE uReflectionResponse
#define ENV_REFLECTION_RESPONSE_PARAMS uReflectionResponseParams
#define WORLD_PROBE_CAPTURE (uWorldScreenReflection[10].w == -8.0)
#define ENV_LUT(uv) gTex.SampleLevel(gSampCC, uv, 0.0).rgb
__PHLOSION_ENVIRONMENT_SHADER__
cbuffer PlanarConstants : register(b3) { float4 uWorldPlanar[6]; };
Texture2D gWorldPlanarTexture : register(t10);
#define PLANAR(index) uWorldPlanar[index]
#define PLANAR_SAMPLE(uv) gWorldPlanarTexture.SampleLevel(gSampCC,uv,0.0)
__PHLOSION_WORLD_PLANAR_SHADER__
#define ENV_POINT(index) uWorldPointLights[index]
#define ENV_RECT(index) uWorldRectLights[index]
#define ENV_BOUNCE(index) uWorldDiffuseProbes[index]
#define ENV_RECT_LUT(uv) gRectLightLut.SampleLevel(gSampCC, uv, 0.0)

#define HAZE_DIMENSIONS float4(uMaterialRect0U,uMaterialRect0V,uMaterialRect0W,uMaterialRect0H)
#define HAZE_PRECISE precise
#define HAZE_BRANCH [branch]
#define HAZE_COORD(uv) float2((uv).x,1.0-(uv).y)
#define HAZE_DEPTH(p) gLightProjectionTex.Load(int3(int2(p),0)).r
#define HAZE_FETCH(p) gWorldPlanarTexture.Load(int3(int2(p),0))
#define HAZE_UNPROJECT(p) float4(dot(uProjectedShadowRowX,p),dot(uProjectedShadowRowY,p),dot(uProjectedShadowRowZ,p),dot(uLightProjectionUvRowU,p))
#define SCREEN_REFLECTION(index) uWorldScreenReflection[index]
#define SCREEN_REFLECTION_LOOP [loop]
#define LOCAL_REFLECTION(index) uWorldLocalReflection[index]
#define LOCAL_REFLECTION_LOOP [loop]
#define LOCAL_REFLECTION_SAMPLE(uv) gWorldLocalReflectionTexture.SampleLevel(gSampCC,uv,0.0)
Texture2D<float4> gWorldLocalReflectionTexture : register(t14);
StructuredBuffer<float4> gWorldIndirectTransport : register(t25);
StructuredBuffer<float4> gWorldIndirectRadiance : register(t26);
Texture2D<float4> gWorldIndirectReceiver : register(t27);
#define WORLD_GI_RECEIVER(p) gWorldIndirectReceiver.Load(int3(p,0))
#ifdef PHLOSION_WORLD_SURFACE_COMPOSITION
static float2 worldGiReceiverPixel;
#define WORLD_GI_PREPARED gWorldIndirectReceiver.Load(int3(int2(worldGiReceiverPixel),0))
#define WORLD_GI_PREPARED_REVERSE gWorldIndirectReceiver.Load(int3(int2(worldGiReceiverPixel)+int2(0,int(CLUSTER_VIEW(4).y)),0))
#define WORLD_GI_PREPARED_NORMAL (STORED_NORMAL(int2(worldGiReceiverPixel)).xyz*2.0-1.0)
#define WORLD_GI_PREPARED_TWO_SIDED (STORED_ALBEDO(int2(worldGiReceiverPixel)).a>.84)
#endif
#define WORLD_GI_PRIVATE static
#define WORLD_GI_FORWARD (uAlphaMode>1.5)
#define WORLD_GI_DATA(i) gWorldIndirectTransport[i]
#define WORLD_GI_RESULT(i) gWorldIndirectRadiance[i]
#define SCREEN_REFLECTION_DEPTH(p) gWorldContactDepth.Load(int3(int2(p),0)).r
#define SCREEN_REFLECTION_SAMPLE(uv) gWorldScreenReflectionTexture.SampleLevel(gSampCC,uv,0.0)
Texture2D<float4> gWorldScreenReflectionTexture : register(t13);
#define CONTACT(index) uWorldContact[index]
#define CONTACT_LOOP [loop]
Texture2D gWorldContactDepth : register(t12);
#define CONTACT_DEPTH(p) gWorldContactDepth.Load(int3(int2(p),0)).r
Texture2D<float4> gWorldContactBuffer : register(t15);
Texture2D<float4> gWorldContactHistory : register(t16);
Texture2D<float4> gWorldContactAux : register(t17);
#define CONTACT_BUFFER_FETCH(p) gWorldContactBuffer.Load(int3(int2(p),0))
#define CONTACT_PREPARED_FETCH(p) gWorldContactHistory.Load(int3(int2(p),0))
#define CONTACT_HISTORY_SAMPLE(uv) gWorldContactHistory.SampleLevel(gSampCC,uv,0.0)
#define SCREEN_REFLECTION_PREPARED(p) gWorldContactHistory.Load(int3(int2(p),0))
#define SCREEN_REFLECTION_CONFIDENCE(p) gWorldContactAux.Load(int3(int2(p),0)).r
#define SCREEN_REFLECTION_PREPARED_ELIGIBLE (uAlphaMode<1.5)
#define SCREEN_SURFACE_NORMAL(p) gWorldContactBuffer.Load(int3(int2(p),0))
#define SCREEN_SURFACE_DEPTH(p) gWorldContactHistory.Load(int3(int2(p),0)).r
#define SCREEN_SURFACE_MATERIAL(p) gWorldContactAux.Load(int3(int2(p),0))
#define CONTACT_AUX_FETCH(p) gWorldContactAux.Load(int3(int2(p),0))
__PHLOSION_WORLD_LIGHTING_SHADER__
#ifndef PHLOSION_LAYERED_DISABLED
ByteAddressBuffer gLayeredWords : register(t552);
Texture2D<float4> gLayeredTextures[512] : register(t40);
#define LAYER_CAMERA uEnvironmentCamera.xyz
#define LAYER_STORAGE static
#define LAYER_WORD(i) gLayeredWords.Load((i)*4u)
#define LAYER_SAMPLE(i,uv,dx,dy) gLayeredTextures[NonUniformResourceIndex(i)].SampleGrad(gSampRR,uv,dx,dy)
#define LAYER_LEVEL(i,uv) gLayeredTextures[NonUniformResourceIndex(i)].SampleLevel(gSampRR,uv,0.0)
#define LAYER_POPCOUNT(i) countbits(i)
__PHLOSION_WORLD_LAYERED_SHADER__
#endif
#define WORLD_SHADOW_PARAMS uWorldShadowParams
#define WORLD_SHADOW_CLIP(position) mul(uWorldShadowMatrix, float4(position, 1.0))
#define WORLD_SHADOW_SAMPLE(uv) gWorldShadowTex.SampleLevel(gWorldShadowSampler, uv, 0.0)
#define WORLD_SHADOW_ATLAS uShadowAtlasUv
#define WORLD_LOCAL_SHADOW(index) uLocalShadows[index]
#define WORLD_SHADOW_Y -1.0
__PHLOSION_WORLD_SHADOW_SHADER__
#define VOLUME_SAMPLE(uv) gTex.Sample(gSampCC, uv).a
__PHLOSION_WORLD_VOLUME_SHADER__
__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__
__PHLOSION_PROBE_RELIGHT_SHADER__
int legacyWrapIndex(int index, int size, float mode) {
  if (abs(mode - 33071.0f) < 0.5f) return clamp(index, 0, size - 1);
  if (abs(mode - 33648.0f) < 0.5f) {
    const int period = size * 2;
    const int wrapped = (index % period + period) % period;
    return wrapped < size ? wrapped : period - 1 - wrapped;
  }
  return (index % size + size) % size;
}
float4 evaluateWorldPixel(PSIn i, bool isFrontFace) {
  worldWetScreen=i.pos.xy;worldWetDepth=i.pos.z;
  localLightConsumer=uAlphaMode>1.5?128u:4u;
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
  worldReflectionClip(i.worldPos,uMaterialMode);
  isFrontFace = worldReflectionFrontFace(isFrontFace);
  if (uAlphaMode > 1.5 || (uMaterialMode > 246.5 && uMaterialMode != 254.0) ||
      (ENV_SUN.w > 0.5 && ENV_RADIANCE.w > 0.5)) discard;
#else
  if(worldScreenReflectionBuildingTiles())return worldScreenReflectionTileBounds(i.pos.xy);
  if(worldLocalReflectionFiltering())return float4(worldLocalReflectionDownsample(i.pos.xy),1.0);
#ifndef PHLOSION_WORLD_SURFACE_ONLY
  if (uMaterialMode == 239.0) return worldFogComposition(i.uv,uEnvironmentCamera.xyz);
  if (uMaterialMode == 246.0 || uMaterialMode == 247.0 || uMaterialMode == 248.0) return worldLocalHazePass(i.uv,uEnvironmentCamera.xyz,uMaterialMode);
#endif
  if((worldContactPrepass() || worldScreenReflectionCapture() || worldLocalReflectionCapture()) && (uAlphaMode>1.5 || (uMaterialMode>246.5 && uMaterialMode!=254.0)))discard;
  worldReflectionClip(i.worldPos,uMaterialMode);
  isFrontFace = worldReflectionFrontFace(isFrontFace);
  updateEnvironmentForwardDirectionalVisibility(i.worldPos, i.worldNormal, uMaterialMode);
#endif
#ifndef PHLOSION_WORLD_SURFACE_ONLY
  if (uMaterialMode == 249.0f) {
    // FXC cannot retain the fog value across a nested function that discards.
    float4 fogBeforeClip=worldFogAt(i.worldPos);
    float4 particleColor=worldVolumeColor(float3(i.sourceUv1,i.sourceUv2.x),i.sourceUv2.y,uMaterialTimeSec,
        i.col*float4(uVertexColorMulR,uVertexColorMulG,uVertexColorMulB,uVertexColorMulA),i.uv.x);
    return float4(fogBeforeClip.rgb*(uWorldFog[2].w>.5?0:1)+fogBeforeClip.a*particleColor.rgb,particleColor.a);
  }
#endif
  if (uMaterialMode >= 249.5f && uMaterialMode != 254.0f) {
    float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (uUseTexture > 0.5f) {
      uint width, height;
      gTex.GetDimensions(width, height);
      int2 size = int2(max(width, 1u), max(height, 1u));
      float2 pixel = i.uv * float2(size) - 0.5f;
      int2 base = int2(floor(pixel));
      float2 weight = frac(pixel);
      int2 next = base + 1;
      base.x = legacyWrapIndex(base.x, size.x, uWrapS);
      next.x = legacyWrapIndex(next.x, size.x, uWrapS);
      base.y = legacyWrapIndex(base.y, size.y, uWrapT);
      next.y = legacyWrapIndex(next.y, size.y, uWrapT);
      float4 top = lerp(gTex.Load(int3(base, 0)),
                        gTex.Load(int3(next.x, base.y, 0)), weight.x);
      float4 bottom = lerp(gTex.Load(int3(base.x, next.y, 0)),
                           gTex.Load(int3(next, 0)), weight.x);
      tex = lerp(top, bottom, weight.y);
    }
    float4 high = i.col * float4(uVertexColorMulR, uVertexColorMulG,
                                uVertexColorMulB, uVertexColorMulA);
    if (uMaterialMode > 254.5f) return worldFogTransparent(saturate(high * tex),i.worldPos);
    float4 low = float4(i.sourceUv1, i.sourceUv2.x, 0.0f);
    bool separate = uMaterialMode == 251.0f || uMaterialMode == 253.0f;
    float intensity = separate ? tex.r : tex.a;
    float4 factor = float4(intensity, intensity, intensity, tex.a);
    float4 color = saturate(lerp(low, high, factor));
    if (uMaterialMode >= 251.5f) {
      factor = floor(factor * 255.0f + 0.5f);
      low = floor(low * 255.0f + 0.5f);
      high = floor(high * 255.0f + 0.5f);
      float4 bytes = clamp(floor((low * 256.0f + (high - low) *
          (factor + floor(factor / 128.0f)) + 128.0f) / 256.0f), 0.0f, 255.0f);
      color = bytes / 255.0f;
      if (bytes.a == 1.0f) color.a = 0.0f;
    }
    return worldFogTransparent(color,i.worldPos);
  }
__PHLOSION_PROJECT_MATERIAL_EVALUATION__
}


#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
struct SurfacePSOut {
  float4 albedo : SV_TARGET0;
  float4 normalClass : SV_TARGET1;
  float4 material : SV_TARGET2;
  float4 emission : SV_TARGET3;
  float4 velocity : SV_TARGET4;
  uint2 identity : SV_TARGET5;
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
  float depth : SV_DepthGreaterEqual;
#endif
};
SurfacePSOut mainSurface(PSIn i, bool isFrontFace : SV_IsFrontFace) {
  worldWetScreen=i.pos.xy;worldWetDepth=i.pos.z;worldWetCategory=i.motionValid>>1u;
  evaluateWorldPixel(i, isFrontFace);
  if (!worldSurfaceValid && i.motionId==0) discard;
  SurfacePSOut result;
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
  result.depth=worldFoliageRasterDepth(i.pos.z);
#endif
  result.albedo = worldSurfaceValid?worldSurfaceOutput.albedo:float4(0,0,0,0);
  result.normalClass = worldSurfaceValid?worldSurfaceOutput.normalClass:
      float4(i.worldNormal*rsqrt(max(dot(i.worldNormal,i.worldNormal),1e-12))*.5+.5,1.0);
  if(worldSurfaceValid)result.normalClass.a=uAlphaMode>.5?2.0/3.0:1.0/3.0;
  result.material = worldSurfaceValid?worldSurfaceOutput.material:float4(0,0,0,0);
  result.emission = worldSurfaceValid?worldSurfaceOutput.emission:float4(0,0,0,0);
  uint contactFlags=(uint(uMaterialMode+.5f)==238u?1u:0u)|((uint(uMaterialFlags)&4096u)!=0u?2u:0u);
  result.identity=uint2(i.motionId,(uint(uWorldPointLights[1].w)>>16u)|((i.motionValid>>1u)<<8u)|(contactFlags<<16u)|(uint(round(saturate(worldWetReactive)*255.0))<<24u));result.velocity=0;
  float4 c=i.motionCurrent,p=i.motionPrevious;
  if((i.motionValid&1u)!=0 && c.w>1e-6 && p.w>1e-6 && abs(c.z)<=c.w && abs(p.z)<=p.w) {
      float3 a=c.xyz/c.w,b=p.xyz/p.w;
      float4 velocity=float4((b.xy-a.xy)*float2(.5,-.5),worldFoliageResolveDepth((b.z+1)*.5),1);
      if(all(isfinite(velocity)))result.velocity=velocity;
  }

  if(WORLD_PROBE_CAPTURE) {
      if(!worldSurfaceValid)discard;
      float3 right=normalize(float3(uWorldScreenReflection[4].x,uWorldScreenReflection[5].x,uWorldScreenReflection[6].x));
      float3 up=normalize(float3(uWorldScreenReflection[4].y,uWorldScreenReflection[5].y,uWorldScreenReflection[6].y));
      result.identity=packWorldProbeMaterial(worldSurfaceOutput,dot(uWorldScreenReflection[3],float4(i.worldPos,1.0)),right,up);
      result.velocity=float4(uWeatherMaterial.xy,uMaterialTimeSec,float(uint(uWorldPointLights[1].w)>>16u));
  }
  return result;
}
#endif
#ifdef PHLOSION_WORLD_SURFACE_COMPOSITION
float4 mainSurfaceComposition(PSIn i) : SV_TARGET {
    // Classify before evaluating either material; each program owns one response.
    bool leaf=STORED_ALBEDO(int2(i.pos.xy)).a>.84;
#ifdef PHLOSION_WORLD_FOLIAGE_COMPOSITION
    if(!leaf)discard;
#else
    if(leaf)discard;
#endif
    float3 emission,position;float2 foliage;
    WorldSurfaceMaterial surface=loadWorldCompositionSurface(i.pos.xy,emission,position,foliage);
    worldGiReceiverPixel=i.pos.xy;
    updateEnvironmentDirectionalVisibility(position,surface.normal,2.0);
    float3 color;
#ifdef PHLOSION_WORLD_FOLIAGE_COMPOSITION
    environmentWetMaterialApplied=1.0;
    color=worldLightFoliageSurface(surface.albedo,surface.normal,
        normalize(uEnvironmentCamera.xyz-position),foliage.x,surface.roughness,position,uEnvironmentCamera.xyz,foliage.y);
#else
    color=lightWorldSurface(surface,emission,position,surface.normal,uEnvironmentCamera.xyz,
        float3(uMaterialFlipbook0Cols,uMaterialFlipbook0Rows,uMaterialFlipbook0Frames),
        float3(uMaterialFlipbook0Fps,uMaterialFlipbook1Cols,uMaterialFlipbook1Rows));
#endif
    return float4(environmentHaze(color,position,uEnvironmentCamera.xyz),0.0);
}
#endif
float4 mainContactFilter(PSIn i) : SV_TARGET { return worldContactFilter(i.pos.xy); }
#ifdef SCREEN_REFLECTION
float4 mainSurfaceReflection(PSIn i) : SV_TARGET { return resolveWorldSurfaceReflection(i.pos.xy); }
float4 mainProbeRelight(PSIn i) : SV_TARGET { return resolveWorldProbeSurface(i.pos.xy,uEnvironmentCamera.xyz); }
#endif
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
float mainShadow(PSIn i, bool isFrontFace : SV_IsFrontFace) : SV_DepthGreaterEqual { evaluateWorldPixel(i,isFrontFace);return worldFoliageRasterDepth(i.pos.z); }
#else
void mainShadow(PSIn i, bool isFrontFace : SV_IsFrontFace) { evaluateWorldPixel(i,isFrontFace); }
#endif
struct WorldPSOut {
  float4 color : SV_TARGET;
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
  float depth : SV_DepthGreaterEqual;
#endif
};
WorldPSOut main(PSIn i, bool isFrontFace : SV_IsFrontFace) {
  float4 color = evaluateWorldPixel(i, isFrontFace);
  if(worldContactPrepass())color=float4(i.worldNormal*rsqrt(max(dot(i.worldNormal,i.worldNormal),1e-12))*.5+.5,1.0);
  [branch] if(worldLocalReflectionCapture() && !worldLocalReflectionFiltering())
    color.a=1.0+length(i.worldPos-uWorldLocalReflection[0].xyz);
  [branch] if(worldLocalReflectionCapture() && !worldLocalReflectionFiltering() && uWorldLocalReflection[0].w<-2.5)
    color.rgb=normalize(cross(ddx(i.worldPos),ddy(i.worldPos)))*.5+.5;
  WorldPSOut result;result.color=color;
#ifdef PHLOSION_WORLD_PIXEL_DEPTH
  result.depth=worldFoliageRasterDepth(i.pos.z);
#endif
  return result;
}

// Keep sky derivatives/uint hashing outside FXC's optimized surface shader.
// Combining them with the large source-material path corrupts opaque pixels.
float4 mainSky(PSIn i) : SV_TARGET {
  float3 ray = i.worldPos-uEnvironmentCamera.xyz;
  float3 color = resolveWorldSceneColor(tonemapACESFilmic(environmentSky(ray,uEnvironmentCamera.xyz), ENV_AMBIENT.w));
  return float4(environmentDitherSky(color,ray),1.0f);
}

struct DualSourcePSOut {
  float4 color : SV_TARGET0;
  float4 blendAlpha : SV_TARGET1;
};

DualSourcePSOut mainDualSource(PSIn i, bool isFrontFace : SV_IsFrontFace) {
  float4 evaluated = evaluateWorldPixel(i, isFrontFace);
  if(worldContactPrepass())evaluated=float4(i.worldNormal*rsqrt(max(dot(i.worldNormal,i.worldNormal),1e-12))*.5+.5,1.0);
  [branch] if(worldLocalReflectionCapture() && !worldLocalReflectionFiltering())
    evaluated.a=1.0+length(i.worldPos-uWorldLocalReflection[0].xyz);
  [branch] if(worldLocalReflectionCapture() && !worldLocalReflectionFiltering() && uWorldLocalReflection[0].w<-2.5)
    evaluated.rgb=normalize(cross(ddx(i.worldPos),ddy(i.worldPos)))*.5+.5;
  float blendAlpha = saturate(evaluated.a);
  DualSourcePSOut output;
  output.color = float4(
      evaluated.rgb,
      worldLocalReflectionCapture()?evaluated.a:floor(blendAlpha * 63.0f + 0.5f) / 63.0f);
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
  uint width, height, levels;
  tex.GetDimensions(0, width, height, levels);
  // A one-level authored texture requests bilinear filtering. Static D3D12
  // samplers remain anisotropic for mip chains; zero gradients select the
  // base-level magnification footprint for this texture only.
  if (levels == 1u) { uvDx = 0.0f.xx; uvDy = 0.0f.xx; }
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
  if (uUseTexture > 2.5f) return gTex.SampleGrad(gLinearRR, uv, uvDx, uvDy);
  if (uUseTexture > 1.5f) return gTex.SampleGrad(gLinearCC, uv, uvDx, uvDy);
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
    if (WORLD_SCENE_RADIANCE) return max(linearColor, float3(0.0,0.0,0.0));
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

WorldSurfaceMaterial prepareWorldMaterial(PSIn i,
                                           bool isFrontFace,
                                           float3 linearColor,
                                           float2 sampleUv,
                                           float2 uvDx,
                                           float2 uvDy,
                                           bool useNormalTexture,
                                           bool useMetallicRoughnessTexture,
                                           bool useSpecularStrengthTexture,
                                           bool useOcclusionTexture,
                                           float normalScale,
                                           float metallicFactor,
                                           float roughnessFactor,
                                           float specularIntensity,
                                           float occlusionStrength) {
#ifndef PHLOSION_LAYERED_DISABLED
  if(uLayeredMaterial.x!=0u)
      return worldLayeredSurface(uLayeredMaterial.x,sampleUv,i.worldPos,i.worldNormal,i.worldTangent,litTextureDetailLodBias());
#endif
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
  float roughness = environmentMaterialRoughness(orm.g * saturate(roughnessFactor));
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
  environmentWetMaterial(i.worldPos, n, albedo, roughness, dielectricSpecular, metallic, i.worldNormal);
  WorldSurfaceMaterial surface;
  surface.albedo = albedo;
  surface.normal = n;
  surface.orm = orm;
  surface.metallic = metallic;
  surface.roughness = roughness;
  surface.occlusion = ao;
  surface.dielectricSpecular = dielectricSpecular;
  return surface;
}

// Lighting consumes evaluated attributes independently of mesh interpolation/material sampling.
float3 lightWorldSurface(WorldSurfaceMaterial surface, float3 emissive,
                         float3 position, float3 sourceNormal,
                         float3 cameraPos, float3 cameraForwardPacked,
                         float3 cameraTarget) {
  // Evaluated PBR already contains wetness; haze must not dampen it again.
  environmentWetMaterialApplied = 1.0;
  float3 n = surface.normal;
  float4 orm = surface.orm;
  float roughness = surface.roughness;
  float metallic = surface.metallic;
  float ao = surface.occlusion;
  float3 albedo = worldWetEmissiveColor(surface.albedo,emissive);
  float dielectricSpecular = surface.dielectricSpecular;
  if(worldContactPrepass())return safeNormalize(sourceNormal,n)*.5+.5;
  float4 contact=worldContactVisibility(position,safeNormalize(sourceNormal,n),cameraPos);
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
  float3 v = safeNormalize(cameraPos - position, -camForward);
  float rawContact=min(ao,worldContactDiffuseVisibility(contact,n,v));
  float4 contactResponse=worldContactResponse(rawContact,worldContactSpecularVisibility(contact,n,v,roughness),0.0);
  float contactOcclusion=min(ao,contactResponse.x);
  float3 directContact=environmentDiffuseOcclusion(contactResponse.z,albedo);
  float3 directColor = ENV_SUN.w > 0.5 ? environmentLightRadiance() : float3(1.0f, 1.0f, 1.0f);
  float directIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  float3 ambientColor = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb : float3(1.0f, 1.0f, 1.0f);
  float ambientIntensity = ENV_SUN.w > 0.5 ? 1.0 : __PHLOSION_PBR_AMBIENT_INTENSITY__;

  float3 lightPos = cameraPos + camRight * 0.5f + camUp * 0.0f - camForward * 0.8660254f;
  float3 l0 = ENV_SUN.w > 0.5 ? environmentLightDirection() : safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  // Preserve the established direct-light lobe during this reflection pass.
  float directRoughness = max(roughness, .16);
  float3 direct = evalDirectPbr(n, v, l0, directColor * directIntensity, albedo, F0, directRoughness, metallic,directContact,contactResponse.w*environmentGlobalSpecularTint());
  direct += environmentStandardLocalPbr(position,n,v,albedo,F0,directRoughness,metallic,contactOcclusion,directContact,contactResponse.w);

  float NdotV = max(dot(n, v), 0.0f);
  float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

  float3 r = reflect(-v, n);
  float3 envIrradiance = ENV_SUN.w > 0.5 ? worldIndirectAmbient(position,n) * 3.14159265f : 3.14159265f * sampleNeutralEnvironment(n, 1.0f);
  float3 envRadiance = ENV_SUN.w > 0.5 ? ENV_AMBIENT.rgb * 0.5 : sampleNeutralEnvironment(r, roughness);
  float3 singleScattering = float3(0.0f, 0.0f, 0.0f);
  float3 multiScattering = float3(0.0f, 0.0f, 0.0f);
  computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
  environmentIndirectScattering(n, v, F0, roughness, singleScattering, multiScattering);
  envRadiance = worldReflectionRadiance(envRadiance, position, n, v, roughness);
  envRadiance *= contactResponse.y;
  float3 indirectOcclusion = environmentDiffuseOcclusion(contactOcclusion, albedo);
  float3 cosineWeightedIrradiance = envIrradiance * (1.0f / 3.14159265f);
  float3 totalScattering = singleScattering + multiScattering;
  float energyComp = 1.0f - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
  float3 diffuseIBL = diffuseColor * max(energyComp, 0.0f) * cosineWeightedIrradiance;
  float3 reflectionResponse = environmentReflectionResponse(n, v, F0, roughness, albedo, rawContact, singleScattering);
  float3 specularIBL = envRadiance * reflectionResponse + multiScattering * cosineWeightedIrradiance;
  diffuseIBL *= worldIndirectEnabled()?1.0:__PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= environmentSpecularScale(__PHLOSION_PBR_SPECULAR_IBL_SCALE__);
  diffuseIBL *= indirectOcclusion;
  float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
  specularIBL *= specularOcclusion;
  float3 ibl = diffuseIBL + specularIBL;

  float3 ambientLight = kD * albedo * ambientColor * ambientIntensity * (ENV_SUN.w > 0.5 ? indirectOcclusion : float3(1.0,1.0,1.0));
  if(worldIndirectEnabled())ambientLight=float3(0.0,0.0,0.0);
  float3 shaded = direct + ibl + ambientLight;



  return max(shaded + worldReflectionEmission(emissive), float3(0.0f, 0.0f, 0.0f));
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
  WorldSurfaceMaterial surface;
  float3 storedEmission;
  bool storedSurface=worldStoredSurface(i.worldPos,surface,storedEmission);
  if(!storedSurface)surface=prepareWorldMaterial(
      i, isFrontFace, linearColor, sampleUv, uvDx, uvDy, useNormalTexture,
      useMetallicRoughnessTexture, useSpecularStrengthTexture, useOcclusionTexture,
      normalScale, metallicFactor, roughnessFactor, specularIntensity, occlusionStrength);
#ifdef PHLOSION_WORLD_SURFACE_CAPTURE
  float3 surfaceEmission = (useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f)) * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
  storeWorldSurface(surface, surfaceEmission);
  return float3(0.0f, 0.0f, 0.0f);
#else
  float3 emissiveTex = useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f);
  float3 emissive = storedSurface ? storedEmission : emissiveTex * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
  return lightWorldSurface(surface, emissive, i.worldPos, i.worldNormal,
      cameraPos, cameraForwardPacked, cameraTarget);
#endif
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
  outLinear = environmentHaze(outLinear, i.worldPos, uEnvironmentCamera.xyz);
    float toneMappingExposure = ENV_SUN.w > 0.5 ? ENV_AMBIENT.w : __PHLOSION_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = resolveWorldSceneColor(mapped);
  return float4(outSrgb, outA);
)HLSL"};

    Microsoft::WRL::ComPtr<ID3DBlob> motionVsBlob;
    {
        auto motionSource=engine::render::worldMotionVertexSource(kVsSource);
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        if(!compileHlslWithCache(motionSource.data(),motionSource.size(),"main","vs_5_0",d3dCompileFlags(),0,motionVsBlob,errors))
            throw std::runtime_error("D3D motion VS: "+d3dCompileErrorMessage(errors.Get()));
    }
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> skyPsBlob;
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
    // These small passes run faster with FXC on the qualified worker. Keep a
    // matching vertex stage: DXBC and DXIL signatures cannot share a pipeline.
    Microsoft::WRL::ComPtr<ID3DBlob> legacyVsBlob;
    if(!compileHlslWithCache(kVsSource.data(),kVsSource.size(),"main","vs_5_0",
            d3dCompileFlags(),0,legacyVsBlob,errBlob,HlslCompiler::Legacy))
        throw std::runtime_error("World legacy VS: "+d3dCompileErrorMessage(errBlob.Get()));
    const std::string worldPsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            engine::render::injectWorldMaterialProfile(kPsSource, worldMaterialProfile_.empty() ? kDefaultMaterial : worldMaterialProfile_.d3d12),
            engine::render::world_pbr_shader_shared::ShaderLanguage::Hlsl);
    struct TransparentProgram {
        std::bitset<256> modes;
        std::string pixelSource;
        Microsoft::WRL::ComPtr<ID3DBlob> vertex, legacyVertex;
    };
    auto transparentPrograms = std::make_shared<std::vector<TransparentProgram>>();
    for (const auto& family : worldMaterialProfile_.transparentFamilies) {
        TransparentProgram program;
        program.modes = family.modes;
        program.pixelSource = engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            engine::render::injectWorldMaterialProfile(kPsSource, family.d3d12),
            engine::render::world_pbr_shader_shared::ShaderLanguage::Hlsl);
        const auto vertexSource = engine::render::injectWorldMaterialProfile(kVertexTemplate, family.d3d12Vertex);
        for (auto compiler : {HlslCompiler::Modern, HlslCompiler::Legacy}) {
            auto& output = compiler == HlslCompiler::Legacy ? program.legacyVertex : program.vertex;
            if (!compileHlslWithCache(vertexSource.data(), vertexSource.size(), "main", "vs_5_0",
                    d3dCompileFlags(), 0, output, errBlob, compiler) || !output)
                throw std::runtime_error("Transparent family VS " + family.id + ": " +
                    d3dCompileErrorMessage(errBlob.Get()));
        }
        transparentPrograms->push_back(std::move(program));
    }
    if (engine::env::flagEnabled("PHLOSION_VALIDATE_SURFACE_SHADER")) {
        // Surface passes use the same constant-material specialization as lighting.
        // Validate the two standard PBR modes without compiling every project effect.
        for (float mode : {2.0f, 254.0f}) {
            std::string surfaceSource = worldPsSource;
            const std::string declaration = "float uMaterialMode;";
            const auto offset = surfaceSource.find(declaration);
            if (offset == std::string::npos)
                throw std::runtime_error("Missing surface material-mode declaration.");
            surfaceSource.replace(offset, declaration.size(), "float _surfaceModeStorage;");
            surfaceSource.insert(0, "#define PHLOSION_WORLD_SURFACE_CAPTURE 1\n#define uMaterialMode " +
                std::to_string(mode) + "f\n");
            Microsoft::WRL::ComPtr<ID3DBlob> surfaceBlob;
            if (!compileHlslWithCache(surfaceSource.data(), surfaceSource.size(),
                "mainSurface", "ps_5_0", d3dCompileFlags(), 0, surfaceBlob, errBlob) || !surfaceBlob)
                throw std::runtime_error("D3DCompile failed for world surface PS: " + d3dCompileErrorMessage(errBlob.Get()));
        }
    }
    // Keep the established dispatcher for draws without local lighting. Its
    // constant zero slots fold away the additional PBR evaluations.
    // FXC corrupts some opaque source materials when the density path shares
    // this dispatcher. Volume draws use the existing constant-mode factory.
    std::string unlitPointSource = "#define PHLOSION_WORLD_SURFACE_ONLY\n" + worldPsSource;
    const std::string pointMacro = "#define ENV_POINT(index) uWorldPointLights[index]";
    const auto pointMacroOffset = unlitPointSource.find(pointMacro);
    if (pointMacroOffset == std::string::npos) throw std::runtime_error("Missing world point-light binding.");
    unlitPointSource.replace(pointMacroOffset, pointMacro.size(),
                            "#define ENV_POINT(index) float4(0.0f, 0.0f, 0.0f, 0.0f)");
    const std::string rectMacro = "#define ENV_RECT(index) uWorldRectLights[index]";
    const auto rectOffset = unlitPointSource.find(rectMacro);
    if (rectOffset == std::string::npos) throw std::runtime_error("Missing rectangle-light binding.");
    unlitPointSource.replace(rectOffset, rectMacro.size(), "#define ENV_RECT(index) float4(0.0f,0.0f,0.0f,0.0f)");
    const std::string bounceMacro = "#define ENV_BOUNCE(index) uWorldDiffuseProbes[index]";
    const auto bounceOffset = unlitPointSource.find(bounceMacro);
    if (bounceOffset == std::string::npos) throw std::runtime_error("Missing diffuse probe binding.");
    unlitPointSource.replace(bounceOffset, bounceMacro.size(), "#define ENV_BOUNCE(index) float4(0.0f,0.0f,0.0f,0.0f)");
    // Keep the horizon search out of the legacy material dispatcher too: FXC
    // otherwise corrupts character surfaces even with contact visibility off.
    // Active contact views use the constant-material factory below.
    const std::string contactMacro = "#define CONTACT(index) uWorldContact[index]";
    const auto contactOffset = unlitPointSource.find(contactMacro);
    if (contactOffset == std::string::npos) throw std::runtime_error("Missing contact visibility binding.");
    unlitPointSource.erase(contactOffset, contactMacro.size());
    const std::string screenMacro="#define SCREEN_REFLECTION(index) uWorldScreenReflection[index]";
    const auto screenOffset=unlitPointSource.find(screenMacro);
    if(screenOffset==std::string::npos)throw std::runtime_error("Missing screen reflection binding");
    unlitPointSource.erase(screenOffset,screenMacro.size());
    const std::string localMacro="#define LOCAL_REFLECTION(index) uWorldLocalReflection[index]";
    const auto localOffset=unlitPointSource.find(localMacro);
    if(localOffset==std::string::npos)throw std::runtime_error("Missing local reflection binding");
    unlitPointSource.erase(localOffset,localMacro.size());
    const std::string shadowMacro = "#define WORLD_SHADOW_PARAMS uWorldShadowParams";
    const auto shadowMacroOffset = unlitPointSource.find(shadowMacro);
    if (shadowMacroOffset == std::string::npos) throw std::runtime_error("Missing directional-shadow binding.");
    unlitPointSource.replace(shadowMacroOffset, shadowMacro.size(),
        "#define WORLD_SHADOW_PARAMS float4(0.0f, 0.0f, 0.0f, 0.0f)");
    Microsoft::WRL::ComPtr<ID3DBlob> shadowPsBlob;
    errBlob.Reset();
    const std::string shadowMaterialSource="#define PHLOSION_LAYERED_DISABLED 1\n"+unlitPointSource;
    if (!compileHlslWithCache(shadowMaterialSource.data(), shadowMaterialSource.size(),
        "mainShadow", "ps_5_0", d3dCompileFlags(), 0, shadowPsBlob, errBlob, HlslCompiler::Legacy) || !shadowPsBlob)
        throw std::runtime_error("D3DCompile failed for world shadow PS: " + d3dCompileErrorMessage(errBlob.Get()));
    errBlob.Reset();
    if (!compileHlslWithCache(unlitPointSource.c_str(),
                              unlitPointSource.size(),
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
    if (!compileHlslWithCache(unlitPointSource.c_str(),
                              unlitPointSource.size(),
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
    errBlob.Reset();
    if (!compileHlslWithCache(worldPsSource.c_str(), worldPsSource.size(),
                              "mainSky", "ps_5_0", d3dCompileFlags(), 0, skyPsBlob, errBlob) || !skyPsBlob) {
        throw std::runtime_error("D3DCompile failed for world sky PS: " + d3dCompileErrorMessage(errBlob.Get()));
    }

    D3D12_DESCRIPTOR_RANGE materialSrvRange{};
    Microsoft::WRL::ComPtr<ID3DBlob> contactFilterPsBlob;
    errBlob.Reset();
    if(!compileHlslWithCache(worldPsSource.c_str(),worldPsSource.size(),"mainContactFilter","ps_5_0",d3dCompileFlags(),0,contactFilterPsBlob,errBlob))
        throw std::runtime_error("D3DCompile failed for contact filter: "+d3dCompileErrorMessage(errBlob.Get()));
    materialSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialSrvRange.NumDescriptors = 10;
    materialSrvRange.BaseShaderRegister = 0;
    materialSrvRange.RegisterSpace = 0;
    materialSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE vertexDisplacementSrvRange{};
    vertexDisplacementSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    vertexDisplacementSrvRange.NumDescriptors = 5;
    vertexDisplacementSrvRange.BaseShaderRegister = 1;
    vertexDisplacementSrvRange.RegisterSpace = 0;
    vertexDisplacementSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE planarRange{};
    planarRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    planarRange.NumDescriptors=1; planarRange.BaseShaderRegister=10;
    planarRange.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE momentRange=planarRange; momentRange.BaseShaderRegister=11;
    D3D12_DESCRIPTOR_RANGE contactRange=planarRange; contactRange.BaseShaderRegister=12;
    D3D12_DESCRIPTOR_RANGE screenRange=planarRange; screenRange.BaseShaderRegister=13;
    D3D12_DESCRIPTOR_RANGE localRange=planarRange; localRange.BaseShaderRegister=14;
    D3D12_DESCRIPTOR_RANGE contactFilterRanges[3]{planarRange,planarRange,planarRange};
    D3D12_ROOT_PARAMETER rootParams[36]{};
    rootParams[32].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[32].Descriptor.ShaderRegister=9;rootParams[32].Descriptor.RegisterSpace=0;
    rootParams[33].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[33].Descriptor.ShaderRegister=552;rootParams[33].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE layeredRange{};
    layeredRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;layeredRange.NumDescriptors=engine::render::kWorldLayerTextureLimit;
    layeredRange.BaseShaderRegister=40;layeredRange.OffsetInDescriptorsFromTableStart=0;
    rootParams[34].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[34].DescriptorTable={1,&layeredRange};rootParams[34].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[32].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_DESCRIPTOR_RANGE fogSkyRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,39,0,0};
    rootParams[35].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[35].DescriptorTable={1,&fogSkyRange};rootParams[35].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE wetRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,8,30,0,0};
    rootParams[30].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[30].DescriptorTable={1,&wetRange};rootParams[30].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE sunNoiseRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,38,0,0};
    rootParams[31].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[31].DescriptorTable={1,&sunNoiseRange};rootParams[31].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE fogRanges[2]{};
    for(unsigned i=0;i<2;++i) {
        fogRanges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,28+i,0,0};
        rootParams[28+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[28+i].DescriptorTable={1,&fogRanges[i]};rootParams[28+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_DESCRIPTOR_RANGE indirectRanges[3]{};
    for(unsigned i=0;i<3;++i) {
        indirectRanges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,25+i,0,0};
        rootParams[25+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[25+i].DescriptorTable={1,&indirectRanges[i]};
        rootParams[25+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    rootParams[24].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[24].Descriptor.ShaderRegister = 4;
    rootParams[24].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE storedRanges[4]{planarRange,planarRange,planarRange,planarRange};
    for(unsigned i=0;i<4;++i) {
        storedRanges[i].BaseShaderRegister=21+i;
        rootParams[20+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[20+i].DescriptorTable={1,&storedRanges[i]};
        rootParams[20+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_DESCRIPTOR_RANGE clusterRanges[3]{planarRange,planarRange,planarRange};
    for(unsigned i=0;i<3;++i) {
        clusterRanges[i].BaseShaderRegister=18+i;
        rootParams[17+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[17+i].DescriptorTable={1,&clusterRanges[i]};
        rootParams[17+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    rootParams[16].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[16].Descriptor.ShaderRegister=8;
    rootParams[16].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    for(unsigned i=0;i<3;++i) {
        contactFilterRanges[i].BaseShaderRegister=15+i;
        rootParams[13+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[13+i].DescriptorTable={1,&contactFilterRanges[i]};
        rootParams[13+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    rootParams[12].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[12].DescriptorTable={1,&localRange};
    rootParams[12].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[11].DescriptorTable={1,&screenRange};
    rootParams[11].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[10].DescriptorTable={1,&contactRange};
    rootParams[10].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[9].DescriptorTable={1,&momentRange};
    rootParams[9].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[7].Descriptor.ShaderRegister=3;
    rootParams[7].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[8].DescriptorTable={1,&planarRange};
    rootParams[8].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[6].Descriptor.ShaderRegister = 2;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].Descriptor.RegisterSpace = 0;
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

    auto makeStaticWorldSampler = [this](UINT shaderRegister,
                                     D3D12_TEXTURE_ADDRESS_MODE addressU,
                                     D3D12_TEXTURE_ADDRESS_MODE addressV) {
        D3D12_STATIC_SAMPLER_DESC s{};
        // The default LOD keeps cross-backend texture detail consistent while retaining anisotropy.
        s.Filter = worldMaterialProfile_.maxAnisotropy>1?D3D12_FILTER_ANISOTROPIC:D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = addressU;
        s.AddressV = addressV;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = 0.0f;
        s.MaxAnisotropy = worldMaterialProfile_.maxAnisotropy;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        s.MinLOD = 0.0f;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = shaderRegister;
        s.RegisterSpace = 0;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        return s;
    };
    std::array<D3D12_STATIC_SAMPLER_DESC, 13> worldSamplers = {
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
    worldSamplers[9] = makeStaticWorldSampler(9, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    worldSamplers[9].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    for (UINT i = 10; i < 12; ++i) {
        const auto address = i == 10 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        worldSamplers[i] = makeStaticWorldSampler(i, address, address);
        worldSamplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        worldSamplers[i].MaxAnisotropy = 1;
    }
    worldSamplers[12]=makeStaticWorldSampler(12,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    worldSamplers[12].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    worldSamplers[12].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
    if (FAILED(engine::render::prepareRootSignature(device_.Get(), 0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(worldRootSignature_.ReleaseAndGetAddressOf()))) ||
        !worldRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 world pipeline.");
    }

    static const D3D12_INPUT_ELEMENT_DESC layout[] = {
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &pso,
                                                    IID_PPV_ARGS(worldPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world pipeline.");
    }
    auto shadowPso = pso;
    shadowPso.VS = {legacyVsBlob->GetBufferPointer(),legacyVsBlob->GetBufferSize()};
    auto contactPso=pso;contactPso.PS={contactFilterPsBlob->GetBufferPointer(),contactFilterPsBlob->GetBufferSize()};
    contactPso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;contactPso.DepthStencilState.DepthEnable=FALSE;
    if(FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &contactPso,IID_PPV_ARGS(worldContactFilterPipelineState_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("CreateGraphicsPipelineState failed for contact filter");
    shadowPso.PS = {shadowPsBlob->GetBufferPointer(), shadowPsBlob->GetBufferSize()};
    shadowPso.NumRenderTargets = 0;
    shadowPso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &shadowPso,
        IID_PPV_ARGS(worldShadowPipelineState_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("CreateGraphicsPipelineState failed for world shadow pipeline.");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPso = pso;
    skyPso.PS = {skyPsBlob->GetBufferPointer(), skyPsBlob->GetBufferSize()};
    skyPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &skyPso,
            IID_PPV_ARGS(worldSkyPipelineState_.ReleaseAndGetAddressOf()))) || !worldSkyPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world sky pipeline.");
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &premulBlendPso,
            IID_PPV_ARGS(worldPremultipliedBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPremultipliedBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world premultiplied blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthBlendPso = blendPso;
    noDepthBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &noDepthBlendPso,
            IID_PPV_ARGS(worldNoDepthBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world no-depth blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthAdditiveBlendPso = additiveBlendPso;
    noDepthAdditiveBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &noDepthAdditiveBlendPso,
            IID_PPV_ARGS(worldNoDepthAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthAdditiveBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world no-depth additive blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthPremulBlendPso = premulBlendPso;
    noDepthPremulBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &dualSourceBlendPso,
            IID_PPV_ARGS(worldDualSourceBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldDualSourceBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world dual-source blend pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC dualSourceAdditivePso = dualSourceBlendPso;
    dualSourceAdditivePso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &dualSourceAdditivePso,
            IID_PPV_ARGS(worldDualSourceAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldDualSourceAdditiveBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world dual-source additive pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthDualSourceBlendPso = dualSourceBlendPso;
    noDepthDualSourceBlendPso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &noDepthDualSourceBlendPso,
            IID_PPV_ARGS(worldNoDepthDualSourceBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthDualSourceBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world no-depth dual-source blend pipeline.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC noDepthDualSourceAdditivePso = dualSourceAdditivePso;
    noDepthDualSourceAdditivePso.DepthStencilState.DepthEnable = FALSE;
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &noDepthDualSourceAdditivePso,
            IID_PPV_ARGS(
                worldNoDepthDualSourceAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldNoDepthDualSourceAdditiveBlendPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for D3D12 world no-depth dual-source additive pipeline.");
    }

    // World composition uses floating radiance; direct display draws retain UNORM.
    const std::array sceneDescriptions{pso, blendPso, additiveBlendPso, premulBlendPso,
        noDepthBlendPso, noDepthAdditiveBlendPso, noDepthPremulBlendPso,
        dualSourceBlendPso, dualSourceAdditivePso, noDepthDualSourceBlendPso,
        noDepthDualSourceAdditivePso};
    for (std::size_t i = 0; i < sceneDescriptions.size(); ++i) {
        auto scene = sceneDescriptions[i];
        scene.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        temporalReactiveAlpha(scene);
        if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &scene,
                IID_PPV_ARGS(worldScenePipelines_[i].ReleaseAndGetAddressOf()))))
            throw std::runtime_error("Could not create floating world pipeline.");
    }
    skyPso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    temporalReactiveAlpha(skyPso);
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &skyPso,
            IID_PPV_ARGS(worldSceneSkyPipelineState_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("Could not create floating sky pipeline.");

    // A constant material mode avoids the rejected large composed dispatcher
    // while retaining level-3 optimization and all material/source factors.
    // Own shader blobs and use static input layout storage: these descriptions
    // remain valid after createWorldPipeline returns.
    auto premulAdditivePso=additiveBlendPso;
    premulAdditivePso.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_ONE;
    premulAdditivePso.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    auto noDepthPremulAdditivePso=premulAdditivePso;
    noDepthPremulAdditivePso.DepthStencilState.DepthEnable=FALSE;
    noDepthPremulAdditivePso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    worldPointLightPipelineFactory_ =
        [source = worldPsSource, transparentPrograms, vertex = vsBlob, legacyVertex = legacyVsBlob, motionVertex = motionVsBlob, root = worldRootSignature_,
         descriptions = std::array{pso, blendPso, additiveBlendPso, premulBlendPso,
             noDepthBlendPso, noDepthAdditiveBlendPso, noDepthPremulBlendPso,
             dualSourceBlendPso, dualSourceAdditivePso, noDepthDualSourceBlendPso,
             noDepthDualSourceAdditivePso,premulAdditivePso,noDepthPremulAdditivePso},
         shaders = std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3DBlob>>{},
         device = device_](float mode, unsigned variant) mutable {
            if(engine::env::flagEnabled("PHLOSION_SHADER_PREPARATION_TRACE"))
                std::osyncstream(std::clog)<<"[ShaderPreparation] world_mode="<<mode<<" variant="<<variant<<'\n';

            const bool hazeStencil = (variant & 2048u) != 0;
            const bool surfaceComposition = (variant & 1024u) != 0;
            const bool sceneColor = (variant & 16u) != 0;
            const bool sourceOnly = (variant & 32u) != 0;
            const bool surface = (variant & 64u) != 0;
            const bool surfaceReflection = (variant & 128u) != 0;
            const bool probeRelight = (variant & 512u) != 0;
            const bool clusteredComposition = (variant & 256u) != 0;
            const bool legacy = surfaceComposition ||
                (!surface && !surfaceReflection && !probeRelight && mode>=241.f && mode<=244.f);
            variant &= 15u;
            const bool dualSource = variant >= 7 && variant <= 10;
            const TransparentProgram* family = nullptr;
            if (mode >= 0.f && mode <= 255.f && mode == std::floor(mode))
                for (const auto& program : *transparentPrograms)
                    if (program.modes.test(static_cast<std::size_t>(mode))) { family = &program; break; }
            if (family && (variant == 0 || surface || surfaceComposition || surfaceReflection ||
                    probeRelight || clusteredComposition || hazeStencil || sourceOnly))
                throw std::runtime_error("Transparent material family requires a blended color pass.");
            const auto& selectedSource = family ? family->pixelSource : source;

            const auto shaderKey = std::uint64_t(std::bit_cast<std::uint32_t>(mode)) * 128 + (surfaceComposition ? 64u : 0u) + unsigned(dualSource) + (sourceOnly ? 2u : 0u) + (surface ? 4u : 0u) + (surfaceReflection ? 8u : 0u) + (clusteredComposition ? 16u : 0u) + (probeRelight ? 32u : 0u);
            auto shader = shaders.find(shaderKey);
            if (shader == shaders.end()) {
                std::string specialized = surface ? "#define PHLOSION_WORLD_SURFACE_CAPTURE 1\n" + selectedSource : selectedSource;
                if (clusteredComposition || surfaceComposition) specialized.insert(0,"#define CLUSTER_FORWARD_FALLBACK false\n");
                if(surfaceComposition)specialized.insert(0,"#define PHLOSION_WORLD_SURFACE_COMPOSITION 1\n");
                if(surfaceComposition && clusteredComposition)specialized.insert(0,"#define PHLOSION_WORLD_FOLIAGE_COMPOSITION 1\n");
                if(probeRelight)specialized.insert(0,"#define PHLOSION_PROBE_RELIGHT 1\n");
                if (sourceOnly) {
                    // This variant is selected only for an active source-colour capture.
                    // Its fixed capture flag lets the compiler remove the unused SSR trace.
                    const std::string binding = "#define SCREEN_REFLECTION(index) uWorldScreenReflection[index]";
                    const auto offset = specialized.find(binding);
                    if (offset == std::string::npos)
                        throw std::runtime_error("Missing screen source binding");
                    specialized.replace(offset, binding.size(),
                        "#define SCREEN_REFLECTION(index) ((index)==8 ? float4(uWorldScreenReflection[8].xyz,-1.0) : uWorldScreenReflection[index])");
                }
                const std::string declaration = "float uMaterialMode;";
                const auto declarationOffset = specialized.find(declaration);
                if (declarationOffset == std::string::npos)
                    throw std::runtime_error("Missing world material-mode declaration.");
                specialized.replace(declarationOffset, declaration.size(), "float _pointLightModeStorage;");
                const std::string identifier = "uMaterialMode";
                std::ostringstream modeText;
                modeText.imbue(std::locale::classic());
                modeText << std::showpoint << std::setprecision(std::numeric_limits<float>::max_digits10) << mode << 'f';
                const std::string literal = modeText.str();
                auto isIdentifier = [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                };
                for (std::size_t at = 0; (at = specialized.find(identifier, at)) != std::string::npos;) {
                    const auto end = at + identifier.size();
                    if ((at == 0 || !isIdentifier(specialized[at - 1])) &&
                        (end == specialized.size() || !isIdentifier(specialized[end]))) {
                        specialized.replace(at, identifier.size(), literal);
                        at += literal.size();
                    } else at = end;
                }
                Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
                if (!compileHlslWithCache(specialized.data(), specialized.size(),
                        surfaceComposition ? "mainSurfaceComposition" : probeRelight ? "mainProbeRelight" : surfaceReflection ? "mainSurfaceReflection" : surface ? "mainSurface" : dualSource ? "mainDualSource" : "main", "ps_5_0", d3dCompileFlags(), 0,
                        blob, errors, legacy?HlslCompiler::Legacy:HlslCompiler::Modern) || !blob) {
                    throw std::runtime_error("D3DCompile failed for local-lit material " +
                        literal + ": " + d3dCompileErrorMessage(errors.Get()));
                }
                shader = shaders.emplace(shaderKey, std::move(blob)).first;
            }
            auto desc = descriptions.at(variant);
            if (sceneColor) desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if(hazeStencil) {
                desc.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
                auto& stencil=desc.DepthStencilState;
                stencil.DepthEnable=FALSE;stencil.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
                stencil.StencilEnable=TRUE;stencil.StencilReadMask=0xff;
                stencil.StencilWriteMask=mode==248.f?0xff:0;
                stencil.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,
                    mode==248.f?D3D12_STENCIL_OP_REPLACE:D3D12_STENCIL_OP_KEEP,
                    mode==248.f?D3D12_COMPARISON_FUNC_ALWAYS:D3D12_COMPARISON_FUNC_EQUAL};
                stencil.BackFace=stencil.FrontFace;
            }
            if(sceneColor&&!surface&&!surfaceReflection&&!surfaceComposition) {
                temporalReactiveAlpha(desc);
                // Haze composition adds radiance with alpha=1 as a color
                // multiplier. That value is not transparent coverage.
                if(mode==239.f || mode==246.f || mode==248.f) {
                    desc.BlendState.RenderTarget[0].SrcBlendAlpha=D3D12_BLEND_ZERO;
                    desc.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_ONE;
                }
            }
            if (surface) {
                desc.NumRenderTargets=6;
                for(unsigned i=0;i<6;++i)desc.RTVFormats[i]=engine::render::d3d12_internal::kWorldSurfaceFormats[i];
                desc.BlendState.RenderTarget[0].BlendEnable=FALSE;
                desc.DepthStencilState.DepthEnable=TRUE;
                desc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
            }
            if(surfaceReflection || surfaceComposition) {
                desc.DepthStencilState.DepthEnable=FALSE;
                desc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
                desc.BlendState.RenderTarget[0].BlendEnable=FALSE;
            }
            desc.pRootSignature = root.Get();
            auto* selectedVertex = family ? (legacy ? family->legacyVertex.Get() : family->vertex.Get()) :
                surface ? motionVertex.Get() : legacy ? legacyVertex.Get() : vertex.Get();
            desc.VS = {selectedVertex->GetBufferPointer(),selectedVertex->GetBufferSize()};
            desc.PS = {shader->second->GetBufferPointer(), shader->second->GetBufferSize()};
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
            if (FAILED(engine::render::prepareGraphicsPipeline(device.Get(), &desc, IID_PPV_ARGS(pipeline.GetAddressOf()))))
                throw std::runtime_error("CreateGraphicsPipelineState failed for local-lit world material.");
            return pipeline;
        };

    {
        engine::render::ShaderPreparationScope trace("startup", "material-pipeline-prewarm");
        std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines(worldMaterialProfile_.d3d12PipelineWarmup.size());
        engine::render::PreparationBatch preparation;
        for(std::size_t i=0;i<pipelines.size();++i) {
            const auto request=worldMaterialProfile_.d3d12PipelineWarmup[i];
            preparation.add([factory=worldPointLightPipelineFactory_,request,&pipelines,i]() mutable {
                pipelines[i]=factory(static_cast<float>(request.mode),request.variant);
            });
        }
        preparation.run(4);
        for(std::size_t i=0;i<pipelines.size();++i) {
            const auto request=worldMaterialProfile_.d3d12PipelineWarmup[i];
            const auto key=std::uint64_t(std::bit_cast<std::uint32_t>(static_cast<float>(request.mode)))*4096+request.variant;
            worldPointLightPipelines_.insert_or_assign(key,std::move(pipelines[i]));
        }
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
    const std::size_t kWorldVsConstantsBytesPerDraw = 4096u; // object plus worst-case distinct shared view
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
    D3D12_RESOURCE_DESC probeDesc=bufferDesc;
    probeDesc.Width=kDiffuseProbePacketStride*kDiffuseProbePacketsPerFrame*kFrameCount;
    if (FAILED(device_->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&probeDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(worldDiffuseProbeBuffer_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("Cannot allocate diffuse probe upload buffer");
    void* probeMapped=nullptr;
    if (FAILED(worldDiffuseProbeBuffer_->Map(0,&worldVsReadRange,&probeMapped)) || !probeMapped)
        throw std::runtime_error("Cannot map diffuse probe upload buffer");
    worldDiffuseProbeMapped_=static_cast<std::uint8_t*>(probeMapped);
    worldDiffuseProbePacketCount_=0;

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
