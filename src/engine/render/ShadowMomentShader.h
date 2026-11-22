#pragma once
#include "ShadowMomentSource.h"
#include "engine/render/IWorldLocalShadow.h"
#include <string>

namespace engine::render {
inline std::string shadowMomentShader(bool hlsl) {
    std::string source = hlsl ? R"(
Texture2D<float4> momentInput : register(t0);
Texture2D<float4> momentCache : register(t1);
cbuffer MomentPush : register(b0) { float4 momentProjection; float4 momentOffsets; float4 momentPass; };
#define MOMENT_LOAD(p) momentInput.Load(int3(int2(p),0))
#define MOMENT_CACHE_LOAD(p) momentCache.Load(int3(int2(p),0)).x
#define vec2 float2
#define vec4 float4
#define mod fmod
)" : R"(#version 330 core
uniform sampler2D momentInput;
uniform vec4 momentProjection, momentOffsets, momentPass;
out vec2 result;
#define MOMENT_LOAD(p) texelFetch(momentInput,ivec2(p),0)
)";
    source += shadowMomentFilterSource;
    source += hlsl ? "\nfloat2 main(float4 p:SV_POSITION):SV_TARGET { return filterShadowMoment(p.xy); }\n"
                   : "\nvoid main() { result=filterShadowMoment(gl_FragCoord.xy); }\n";
    return source;
}
inline std::array<float,12> shadowMomentConstants(const WorldDirectionalShadowView& view,
                                                 const WorldLocalShadowAtlas& atlas, bool vertical) {
    const auto xy=atlas.offset();
    const auto& m=view.distanceMoments;
    return {m[0],m[1],m[2],m[3],vertical?0.f:float(xy[0]),vertical?0.f:float(xy[1]),
        vertical?float(xy[0]-2048):0.f,vertical?float(xy[1]):0.f,
        vertical?1.f:0.f,float(view.resolution),float(view.momentKernel),view.momentCentreRay?2.f:0.f};
}
inline std::array<float,12> shadowMomentCacheConstants(const WorldDirectionalShadowView& view,
        const WorldLocalShadowAtlas& atlas, unsigned pass, bool cached) {
    auto data=shadowMomentConstants(view,atlas,pass==1);
    data[8]=float(pass); data[11]=(cached?1.f:0.f)+(view.momentCentreRay?2.f:0.f);
    if(pass==2) { data[6]=data[4]-2048.f; data[7]=data[5]; }
    return data;
}
}
