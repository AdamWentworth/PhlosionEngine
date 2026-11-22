#pragma once
#include "WorldWindSource.h"
#include <string>
namespace engine::render {
inline std::string worldWindHlsl(unsigned pass){
    return "#define WF_HLSL\n#define WF_STAGE "+std::to_string(pass)+"\n"+R"SHADER(
#define vec2 float2
#define PHLOSION_FOLIAGE_HLSL 1
#define vec3 float3
#define vec4 float4
#define ivec3 int3
#define fract frac
#define mix lerp
#define greaterThanEqual(a,b) ((a)>=(b))
#define lessThan(a,b) ((a)<(b))
RWStructuredBuffer<float4> field:register(u0);
StructuredBuffer<float4> inputs:register(t0);
cbuffer Parameters:register(b0){float4 params[2];}
#define WF_READ(i) field[i]
#define WF_WRITE(i,v) field[i]=v
#define WF_INPUT(i) inputs[i]
#define WF_PARAM(i) params[i]
)SHADER"+worldWindFieldSource+"[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){windField(id.x);}";
}
inline std::string worldWindVertexHlsl(){
 return R"SHADER(
#define PHLOSION_FOLIAGE_HLSL 1
#define vec3 float3
#define vec4 float4
#define ivec3 int3
#define fract frac
#define mix lerp
#define lessThan(a,b) ((a)<(b))
#define greaterThan(a,b) ((a)>(b))
StructuredBuffer<float4> gWorldWind:register(t9);
#define WIND_WORD(i) gWorldWind[i]
)SHADER"+std::string(worldWindSampleSource)+std::string(worldFoliageGeometrySource)+R"SHADER(
#undef PHLOSION_FOLIAGE_HLSL
#undef vec3
#undef vec4
#undef ivec3
#undef fract
#undef mix
#undef lessThan
#undef greaterThan
#undef WIND_WORD
)SHADER";
}

}
