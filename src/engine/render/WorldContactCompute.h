#pragma once
#include "IWorldContactVisibility.h"
#include "WorldTemporalScene.h"
namespace engine::render {
inline std::array<std::array<float,4>,32> worldContactComputeParameters(
    const WorldContactUniforms& current,const WorldContactUniforms& previous,
    std::uint64_t frame,bool history) {
    std::array<std::array<float,4>,32> p{};
    std::copy(current.begin(),current.end(),p.begin());
    p[9][1]=float(std::fmod(.5+double(frame)*.6180339887498948482,1.0));
    p[9][2]=history?1.f:0.f;
    for(int i=0;i<8;++i)p[14+i]=previous[i];
    p[22]=previous[10];p[24]=previous[13];
    return p;
}
}
