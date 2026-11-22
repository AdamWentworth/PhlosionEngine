#pragma once
#include "WorldTemporalSource.h"
#include <string>
namespace engine::render {
inline std::string worldTemporalHlsl() {
    return std::string(R"HLSL(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
cbuffer Params:register(b0){float4 p0,p1,p2,p3,p4,p5,p6,p7;}
Texture2D<float4> s0:register(t0),s1:register(t1),s2:register(t2),s3:register(t3),
 s4:register(t4),s5:register(t5),s6:register(t6),s7:register(t7);
Texture2D<uint2> s8:register(t8);
Texture2D<uint2> s9:register(t9);
SamplerState linearClamp:register(s0);
RWTexture2D<float4> outputColor:register(u0);
RWTexture2D<uint2> outputIdentity:register(u1);
#define LOAD0(q) s0.Load(int3(q,0))
#define LOAD1(q) s1.Load(int3(q,0))
#define LOAD2(q) s2.Load(int3(q,0))
#define LOAD4(q) s4.Load(int3(q,0))
#define LOAD5(q) s5.Load(int3(q,0))
#define LOAD6(q) s6.Load(int3(q,0))
#define ID8(q) s8.Load(int3(q,0)).r
#define ID9(q) s9.Load(int3(q,0)).r
#define REACTIVE8(q) (float(s8.Load(int3(q,0)).g>>24u)/255.0)
#define CLASS8(q) ((s8.Load(int3(q,0)).g>>8u)&255u)
#define CLASS9(q) ((s9.Load(int3(q,0)).g>>8u)&255u)
#define SAMPLE0(q) s0.SampleLevel(linearClamp,q,0)
#define SAMPLE3(q) s3.SampleLevel(linearClamp,q,0)
#define SAMPLE6(q) s6.SampleLevel(linearClamp,q,0)
#define STORE(q,v) outputColor[q]=v
#define STORE_ID(q,v,c) outputIdentity[q]=uint2(v,c<<8u)
)HLSL")+worldTemporalSource+R"HLSL(
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){temporalScene(int2(id.xy));}
)HLSL";
}
}
