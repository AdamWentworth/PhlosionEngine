#pragma once
#include "WorldCloudSource.h"
#include <string>
namespace engine::render {
inline std::string worldCloudHlsl(unsigned pass) {
    const char* entry[]{
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){cloudShadow(int2(id.xy));}",
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){cloudVisible(int2(id.xy));}",
        "[numthreads(8,8,1)]void main(uint3 group:SV_GroupID,uint3 local:SV_GroupThreadID){cloudIntegrate(group.x,int2(local.xy));}",
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){cloudResolve(int2(id.xy));}",
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){cloudSkyModel(int2(id.xy));}"};
    return "#define CL_STAGE "+std::to_string(pass)+"\n"+R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define fract frac
#define greaterThanEqual(a,b) ((a)>=(b))
#define equal(a,b) ((a)==(b))
#define CL_LOOP [loop]
Texture2D<float> depthTexture:register(t0),lowDepthTexture:register(t5);
Texture2D<float4> weatherTexture:register(t1),noiseTexture:register(t2),shadowTexture:register(t3),integratedTexture:register(t4);
StructuredBuffer<uint> inputTiles:register(t6);
Texture2D<float4> fogNearTexture:register(t7),fogFarTexture:register(t8),fogSkyTexture:register(t9);
RWTexture2D<float4> outputTexture:register(u0);
RWTexture2D<float> depthOutput:register(u1);
RWStructuredBuffer<uint> outputTiles:register(u2);
SamplerState linearWrap:register(s0),linearClamp:register(s1);
cbuffer Parameters:register(b0){float4 parameters[72];}
#define CP(i) parameters[i]
#define CL_FOG_SKY(uv) fogSkyTexture.SampleLevel(linearClamp,uv,0).rgb
#define CL_FOG(uv,distant) (distant?fogFarTexture.SampleLevel(linearClamp,uv,0):fogNearTexture.SampleLevel(linearClamp,uv,0))
#define CL_DEPTH(p) depthTexture.Load(int3(p,0))
#define CL_WEATHER(uv) weatherTexture.SampleLevel(linearWrap,uv,0)
#define CL_NOISE(uv) noiseTexture.SampleLevel(linearWrap,uv,0)
#define CL_SHADOW(uv) shadowTexture.SampleLevel(linearWrap,uv,0)
#define CL_INTEGRATED(p) integratedTexture.Load(int3(p,0))
#define CL_LOW_DEPTH(p) lowDepthTexture.Load(int3(p,0))
#define CL_STORE(p,v) outputTexture[p]=v
#define CL_DEPTH_STORE(p,v) depthOutput[p]=v
#define CL_TILE(i) inputTiles[i]
uint cloudAppend(){uint n;InterlockedAdd(outputTiles[0],1,n);return n;}
#define CL_APPEND() cloudAppend()
#define CL_TILE_STORE(i,v) outputTiles[i]=v
#define CL_ARGS() {outputTiles[1]=1;outputTiles[2]=1;}
)SHADER"+worldCloudSource+entry[pass];
}
}
