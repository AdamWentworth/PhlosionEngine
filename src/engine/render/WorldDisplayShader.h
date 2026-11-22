#pragma once
#include "WorldDisplaySource.h"
#include <string>
#include <regex>
namespace engine::render {
inline std::string worldDisplayHlsl(int stage) {
    std::string source="#define DISPLAY_STAGE "+std::to_string(stage)+"\n";
    source+=R"HLSL(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define SHARED groupshared
#define SYNC GroupMemoryBarrierWithGroupSync()
#define ATOMIC(v,n) InterlockedAdd(v,n)
#define ATAN2(y,x) atan2(y,x)
#define FLOAT_BITS(v) asuint(v)
#define BITS_FLOAT(v) asfloat(v)
cbuffer Params:register(b0){float4 p0,p1,p2,p3,p4,p5,p6,p7;}
Texture2D<float4> s0:register(t0),s1:register(t1),s2:register(t2),s3:register(t3),
 s4:register(t4),s5:register(t5),s6:register(t6),s7:register(t7);
SamplerState linearClamp:register(s0);
RWTexture2D<float4> outputColor:register(u0);
#define STORE(q,v) outputColor[q]=v
)HLSL";
    for(int i=0;i<8;++i) {
        const auto n=std::to_string(i);
        source+="#define LOAD"+n+"(q) s"+n+".Load(int3(q,0))\n";
        source+="#define SAMPLE"+n+"(q) s"+n+".SampleLevel(linearClamp,q,0)\n";
    }
    if(stage==6)source+=worldDisplayAcesSource;
    source+=worldDisplaySource;
    source=std::regex_replace(source,std::regex(R"((float|vec2)\[(6|9|10)\]\(([^;]+)\);)"),"{$3};");
    source+=stage==1?R"HLSL(
[numthreads(256,1,1)]void main(uint lane:SV_GroupIndex,uint3 group:SV_GroupID){displayHistogram(lane,group.x);}
)HLSL":R"HLSL(
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){displayStage(int2(id.xy));}
)HLSL";
    return source;
}
}
