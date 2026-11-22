#pragma once
#include "WorldTemporalReflectionSource.h"
#include <string>
namespace engine::render {
inline std::string worldTemporalReflectionHlsl(int quality,bool depth) {
    return "#define SSR_QUALITY "+std::to_string(quality)+"\n"+(depth?"#define SSR_DEPTH_PASS\n":"")+R"HLSL(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define SSR_LOOP [loop]
#define SSR_UNROLL [unroll]
#define REVERSE_BITS(i) reversebits(i)
cbuffer Params:register(b0){float4 rows[16];}
#define RP(i) rows[i]
SamplerState linearClamp:register(s0);
Texture2D<float4> t0:register(t0);
Texture2D<float4> t1:register(t1);
Texture2D<float4> t2:register(t2);
Texture2D<float4> t3:register(t3);
Texture2D<float4> t4:register(t4);
Texture2D<float4> t5:register(t5);
Texture2D<float4> t6:register(t6);
Texture2D<float4> t7:register(t7);
Texture2D<float4> t8:register(t8);
Texture2D<float4> t9:register(t9);
Texture2D<float4> t10:register(t10);
Texture2D<float4> t11:register(t11);
#define DEPTH_LOAD(p) t0.Load(int3(p,0)).r
#define NORMAL_LOAD(p) t1.Load(int3(p,0))
#define MATERIAL_LOAD(p) t11.Load(int3(p,0))
#define MOTION_SAMPLE(p) t2.SampleLevel(linearClamp,p,0)
#define INVALID_SAMPLE(p) t3.SampleLevel(linearClamp,p,0).z
#define TRACE_DEPTH(p) t4.Load(int3(p,0)).r
#define NOISE_LOAD(p) t10.Load(int3(p,0))
#define FEEDBACK0(p) t5.SampleLevel(linearClamp,p,0).rgb
#define FEEDBACK1(p) t6.SampleLevel(linearClamp,p,0).rgb
#define FEEDBACK2(p) t7.SampleLevel(linearClamp,p,0).rgb
#define FEEDBACK3(p) t8.SampleLevel(linearClamp,p,0).rgb
#define FEEDBACK4(p) t9.SampleLevel(linearClamp,p,0).rgb
RWTexture2D<float3> out0:register(u0);
#define STORE_RADIANCE(p,v) out0[p]=v
RWTexture2D<float> out1:register(u1);
#define STORE_CONFIDENCE(p,v) out1[p]=v
RWTexture2D<float> out2:register(u2);
#define STORE_DEPTH(p,v) out2[p]=v
)HLSL"+worldTemporalReflectionSource+R"HLSL(
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){temporalReflection(int2(id.xy));}
)HLSL";
}
}
