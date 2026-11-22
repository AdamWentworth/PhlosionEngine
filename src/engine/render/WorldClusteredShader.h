#pragma once
#include "WorldClusteredSource.h"
#include <string>
namespace engine::render {
inline std::string worldClusteredHlsl(int pass) {
    return "#define CLUSTER_PASS "+std::to_string(pass)+"\n"+R"HLSL(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define uvec2 uint2
#define uvec3 uint3
#define mix lerp
#define inversesqrt rsqrt
#define uintBitsToFloat asfloat
#define floatBitsToUint asuint
cbuffer Parameters:register(b0){float4 rows[264];}
#define CP(i) rows[i]
Texture2D<float> depthTexture:register(t0);
Texture2D<float4> albedoTexture:register(t1);
Texture2D<float4> normalTexture:register(t2);
Texture2D<float4> materialTexture:register(t3);
Texture2D<float4> lutTexture:register(t4);
Texture2D<float> shadowTexture:register(t5);
Texture2D<float2> momentTexture:register(t6);
Texture2D<uint2> identityTexture:register(t7);
SamplerState linearClamp:register(s0);
SamplerState pointClamp:register(s1);
RWTexture2D<float4> diffuseImage:register(u0);
RWTexture2D<float4> specularImage:register(u1);
RWStructuredBuffer<uint> words:register(u2);
RWStructuredBuffer<uint> arguments:register(u3);
#define WORK(i) words[i]
#define ARGS(i) arguments[i]
#define DEPTH_LOAD(p) depthTexture.Load(int3(p,0))
#define ALBEDO_LOAD(p) albedoTexture.Load(int3(p,0))
#define NORMAL_LOAD(p) normalTexture.Load(int3(p,0))
#define MATERIAL_LOAD(p) materialTexture.Load(int3(p,0))
#define IDENTITY_LOAD(p) identityTexture.Load(int3(p,0)).r
#define LIGHT_CHANNELS_LOAD(p) (identityTexture.Load(int3(p,0)).g&255u)
#define ENV_SUN CP(11)
#define ENV_POINT(i) CP(12+i)
#define ENV_RECT(i) CP(32+i)
#define ENV_RECT_LUT(p) lutTexture.SampleLevel(linearClamp,p,0)
#define WORLD_LOCAL_SHADOW(i) CP(144+i)
#define WORLD_SHADOW_Y -1.0
#define WORLD_SHADOW_SAMPLE(p) shadowTexture.SampleLevel(pointClamp,p,0)
#define WORLD_MOMENT_SAMPLE(p) momentTexture.SampleLevel(pointClamp,p,0).rg
#define STORE_DIFFUSE(p,v) diffuseImage[p]=v
#define STORE_SPECULAR(p,v) specularImage[p]=v
#define CLUSTER_STATIC static
#define LOCAL_LIGHT_STORAGE static
#define CLUSTER_SHARED groupshared
#define CLUSTER_LOOP [loop]
#define CLUSTER_FIRST(v) uint(firstbitlow(v))
#define CLUSTER_BARRIER GroupMemoryBarrierWithGroupSync()
#define CLUSTER_CLASS_OR(v) InterlockedOr(classMask,v)
#define CLUSTER_APPEND(c,outSlot) InterlockedAdd(words[c],1u,outSlot)
)HLSL"+worldClusteredSource+R"HLSL(
#if CLUSTER_PASS < 2
[numthreads(8,8,8)]
#elif CLUSTER_PASS == 3
[numthreads(1,1,1)]
#else
[numthreads(8,8,1)]
#endif
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){clusteredMain(group,lane);}
)HLSL";
}
}
