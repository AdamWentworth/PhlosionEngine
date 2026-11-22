#pragma once
#include "WorldContactComputeSource.h"
#include <string>
namespace engine::render {
inline std::string worldContactComputeHlsl(int stage,int quality){
    const int dirs[]{5,10,15},steps[]{3,6,10};
    return "#define C_STAGE "+std::to_string(stage)+"\n#define C_DIRS "+std::to_string(dirs[quality])+
        "\n#define C_STEPS "+std::to_string(steps[quality])+R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define fract frac
#define CONTACT_LOOP [loop]
cbuffer Params:register(b0){float4 rows[32];}
#define CP(i) rows[i]
SamplerState linearBorder:register(s0);
Texture2D<float4> s0:register(t0),s1:register(t1),s2:register(t2);
Texture2D<uint2> s3:register(t3);
Texture2D<float4> s4:register(t4),s5:register(t5),s6:register(t6),s7:register(t7),
    s8:register(t8),s9:register(t9),s10:register(t10);
#define FULL_DEPTH(q) s0.Load(int3(q,0)).r
#define FULL_NORMAL(q) s1.Load(int3(q,0))
#define FULL_VELOCITY(q) s2.Load(int3(q,0))
#define FULL_FLAGS(q) (s3.Load(int3(q,0)).g>>16u)
#define HALF_DEPTH(q) s4.Load(int3(q,0)).r
#define HALF_NORMAL(q) s5.Load(int3(q,0))
#define HALF_VELOCITY(q) s6.Load(int3(q,0)).rg
#define CONE_LOAD(q) s7.Load(int3(q,0))
#define PREV_DEPTH(uv) s8.SampleLevel(linearBorder,uv,0).r
#define PREV_VELOCITY(uv) s9.SampleLevel(linearBorder,uv,0).rg
#define PREV_CONE(uv) s10.SampleLevel(linearBorder,uv,0)
RWTexture2D<float4> out0:register(u0);
RWTexture2D<float> out1:register(u1);
RWTexture2D<float2> out2:register(u2);
RWTexture2D<float4> out3:register(u3);
#define STORE_CONE(q,v) out0[q]=v
#define STORE_DEPTH(q,v) out1[q]=v
#define STORE_VELOCITY(q,v) out2[q]=v
#define STORE_NORMAL(q,v) out3[q]=v
)SHADER"+worldContactComputeSource+R"SHADER(
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){contactCompute(int2(id.xy));}
)SHADER";
}
}
