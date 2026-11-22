#pragma once
#include "WorldIndirectReceiverSource.h"
#include <string>
namespace engine::render {
inline std::string worldIndirectReceiverHlsl() {
    return std::string(R"SHADER(
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define mix lerp
#define inversesqrt rsqrt
StructuredBuffer<float4> transport:register(t8);
StructuredBuffer<float4> radiance:register(t9);
Texture2D<float> depthTexture:register(t0);
Texture2D<float4> albedoTexture:register(t1);
Texture2D<float4> normalTexture:register(t2);
RWTexture2D<float4> indirectImage:register(u4);
cbuffer Parameters:register(b0){float4 rows[269];}
#define CP(i) rows[i]
#define ENV_SUN CP(11)
#define ENV_BOUNCE(i) CP(264+i)
#define WORLD_GI_NEAR_ONLY
#define WORLD_GI_PRIVATE static
#define WORLD_GI_LOOP [loop]
#define WORLD_GI_DATA(i) transport[i]
#define WORLD_GI_RESULT(i) radiance[i]
#define DEPTH_LOAD(p) depthTexture.Load(int3(p,0))
#define ALBEDO_LOAD(p) albedoTexture.Load(int3(p,0))
#define NORMAL_LOAD(p) normalTexture.Load(int3(p,0))
#define STORE_INDIRECT(p,v) indirectImage[p]=v
)SHADER")+worldIndirectReceiverSource+R"SHADER(
[numthreads(8,8,1)]void main(uint3 pixel:SV_DispatchThreadID){indirectReceiver(int2(pixel.xy));}
)SHADER";
}
}
