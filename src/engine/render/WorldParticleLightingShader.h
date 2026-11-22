#pragma once
#include "WorldParticleLightingSource.h"
#include <string>
namespace engine::render {
inline std::string worldParticleLightingHlsl(){
    return std::string(R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define floatBitsToUint asuint
#define uintBitsToFloat asfloat
#define WORLD_GI_PRIVATE static
#define WORLD_GI_LOOP [loop]
#define PL_SHARED groupshared
#define PL_LOOP [loop]
#define PL_BARRIER GroupMemoryBarrierWithGroupSync()
Texture2D<float> maximumDepth:register(t5);
#define PL_DEPTH(p) maximumDepth.Load(int3(p,0))
Texture2D<float> shadowTexture:register(t0);
Texture2D<float2> momentTexture:register(t1);
StructuredBuffer<float4> transport:register(t2),radiance:register(t3),quads:register(t4);
RWTexture2D<float4> outputTexture:register(u0);
SamplerState nearestClamp:register(s0);
cbuffer Parameters:register(b0){float4 parameters[320];}
#define PP(i) parameters[i]
#define PQ(i) quads[i]
#define WORLD_GI_DATA(i) transport[i]
#define WORLD_GI_RESULT(i) radiance[i]
#define PL_SHADOW(uv) shadowTexture.SampleLevel(nearestClamp,uv,0)
#define PL_MOMENT(uv) momentTexture.SampleLevel(nearestClamp,uv,0)
#define PL_STORE(p,v) outputTexture[p]=v
)SHADER")+worldParticleLightingSource+
    "[numthreads(64,1,1)]void main(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){particleLighting(group.x,lane);}";
}
}
