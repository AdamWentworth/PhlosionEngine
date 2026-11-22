#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace engine::render {
inline constexpr std::size_t worldPuddleCapacity=16;
// Shallow water patches belong to the receiving material, not a second lit mesh.
// Extent precedes rotation, matching the authored footprint and impact query.
struct WorldPuddleRegion {
    std::array<float,3> center{};
    std::array<float,2> extent{};
    std::array<float,2> rotation{1,0};
};
using WorldPuddleUniforms=std::array<std::array<float,4>,worldPuddleCapacity*2>;
inline bool validWorldPuddle(const WorldPuddleRegion& p) {
    for(float v:p.center)if(!std::isfinite(v)||std::abs(v)>1000000)return false;
    for(float v:p.extent)if(!std::isfinite(v)||v<.001f||v>1000000)return false;
    for(float v:p.rotation)if(!std::isfinite(v))return false;
    return std::abs(p.rotation[0]*p.rotation[0]+p.rotation[1]*p.rotation[1]-1)<.001f;
}
inline float worldPuddleShore(float u,float v) {
    const float angle=std::atan2(v,u);
    const float shore=.78f+.11f*std::sin(angle*3+.8f)+.07f*std::cos(angle*5-.4f)+.035f*std::sin(angle*7+1.2f);
    const float edge=std::clamp((shore-std::hypot(u,v))/.12f,0.f,1.f);
    return edge*edge*(3-2*edge);
}
inline float worldPuddleCoverage(const WorldPuddleRegion& p,const std::array<float,3>& position,float normalY) {
    if(!validWorldPuddle(p)||!std::isfinite(normalY)||std::abs(normalY)<.985f)return 0;
    for(float v:position)if(!std::isfinite(v))return 0;
    if(std::abs(position[1]-p.center[1])>.035f)return 0;
    const float x=(position[0]-p.center[0])/p.extent[0],z=(position[2]-p.center[2])/p.extent[1];
    if(std::abs(x)>1||std::abs(z)>1)return 0;
    return worldPuddleShore(x*p.rotation[0]-z*p.rotation[1],x*p.rotation[1]+z*p.rotation[0]);
}
inline float worldPuddleFill(float amount) {
    return std::isfinite(amount)?std::clamp((amount-.02f)/.45f,0.f,1.f):0.f;
}
}
