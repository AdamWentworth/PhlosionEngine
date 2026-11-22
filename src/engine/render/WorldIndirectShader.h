#pragma once
#include "WorldIndirectSource.h"
#include <string>
namespace engine::render {
inline std::string worldIndirectHlsl(unsigned stage) {
    return "#define GI_STAGE "+std::to_string(stage)+"\n"+R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define floatBitsToUint asuint
StructuredBuffer<float4> transport:register(t0);
StructuredBuffer<float4> previous:register(t1);
Texture2D<float4> environmentTexture:register(t2);
Texture2D<float> shadowTexture:register(t3),cloudShadowTexture:register(t4);
RWStructuredBuffer<float4> result:register(u0);
cbuffer Parameters:register(b0){float4 parameters[234];}
SamplerState linearClamp:register(s0);
SamplerState nearestClamp:register(s1);
SamplerComparisonState sunSampler:register(s2);
Texture2D<float> sunShadowTexture:register(t5);
#define GI_SUN_COMPARE(uv,z) sunShadowTexture.SampleCmpLevelZero(sunSampler,uv,z)
#define GI_DATA(i) transport[i]
#define GI_PREVIOUS(i) previous[i]
#define GI_OUTPUT(i) result[i]
#define GI_PARAM(i) parameters[i]
#define GI_ENVIRONMENT(uv) environmentTexture.SampleLevel(linearClamp,uv,0).rgb
#define GI_CLOUD_SHADOW(uv) cloudShadowTexture.SampleLevel(linearClamp,uv,0)
#define GI_SHADOW(uv) shadowTexture.SampleLevel(nearestClamp,uv,0)
#define GI_SHARED groupshared
#define GI_LOOP [loop]
#define GI_BARRIER GroupMemoryBarrierWithGroupSync()
)SHADER"+worldIndirectSource+R"SHADER(
[numthreads(64,1,1)]void main(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){indirectCompute(group.x,lane);}
)SHADER";
}
}
