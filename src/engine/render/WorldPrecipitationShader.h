#pragma once
#include "WorldPrecipitationSource.h"
#include <string>
namespace engine::render {
inline std::string worldPrecipitationHlsl(unsigned stage){
    const char* entries[]{
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){volumeMaximumDepth(int2(id.xy));}",
        "[numthreads(4,4,4)]void main(uint3 id:SV_DispatchThreadID,uint3 group:SV_GroupID,uint lane:SV_GroupIndex){volumeLight(int3(id),int3(group),lane);}",
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){volumeScatter(int2(id.xy));}"};
    return std::string(R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define ivec3 int3
#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define floatBitsToUint asuint
#define uintBitsToFloat asfloat
#define WORLD_GI_PRIVATE static
#define WORLD_GI_LOOP [loop]
#define VP_SHARED groupshared
#define VP_LOOP [loop]
#define VP_BARRIER GroupMemoryBarrierWithGroupSync()
Texture2D<float> depthTexture:register(t0);
Texture2D<float4> historyTexture:register(t1),currentTexture:register(t2);
Texture2D<float> maximumDepth:register(t3),shadowTexture:register(t4),cloudShadowTexture:register(t7);
StructuredBuffer<float4> transport:register(t5),radiance:register(t6);
RWTexture2D<float4> outputTexture:register(u0);
RWTexture2D<float> depthOutput:register(u1);
SamplerState linearClamp:register(s0);
SamplerState nearestClamp:register(s1);
SamplerComparisonState sunSampler:register(s2);
Texture2D<float> sunShadowTexture:register(t8);
#define VP_SUN_COMPARE(uv,z) sunShadowTexture.SampleCmpLevelZero(sunSampler,uv,z)
cbuffer Parameters:register(b0){float4 parameters[672];}
#define VP(i) parameters[i]
#define WORLD_GI_DATA(i) transport[i]
#define WORLD_GI_RESULT(i) radiance[i]
#define VP_DEPTH(p) depthTexture.Load(int3(p,0))
#define VP_HISTORY(uv) historyTexture.SampleLevel(linearClamp,uv,0)
#define VP_CURRENT(p) currentTexture.Load(int3(p,0))
#define VP_MAX_DEPTH(p) maximumDepth.Load(int3(p,0))
#define VP_CLOUD_SHADOW(uv) cloudShadowTexture.SampleLevel(linearClamp,uv,0)
#define VP_SHADOW(uv) shadowTexture.SampleLevel(nearestClamp,uv,0)
#define VP_STORE(p,v) outputTexture[p]=v
#define VP_STORE_DEPTH(p,v) depthOutput[p]=v
)SHADER")+worldPrecipitationSource+entries[stage];
}
}
