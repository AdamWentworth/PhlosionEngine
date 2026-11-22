#pragma once
#include "WorldHeatHaze.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::render {
struct WorldDisplaySettings {
    WorldHeatHazeSettings heatHaze;
    float speedUp=8, speedDown=5, lowPercentile=.05f, highPercentile=.7f;
    float minEV=-8, maxEV=30, compensationEV=1, skyWeight=1;
    float centerWeight=0, velocityDamping=0, emissiveWeight=1;
    float manualBlend=0, manualExposure=1;
    float bloomMin=0, bloomMax=2, sceneScale=.8f, bloomScale=.2f;
    int bloomLevels=6;
    // Lift, inverse gamma, gain, offset/brightness, shadow/pivot,
    // midtone/low range, highlight/high range, contrast/saturation/hue.
    std::array<std::array<float,4>,8> grade{{
        {0,0,0,0},{1,1,1,0},{1,1,1,0},{0,0,0,0},
        {0,0,0,.435f},{0,0,0,.1f},{0,0,0,.45f},{1,1,0,0}}};
    bool operator==(const WorldDisplaySettings&) const = default;
};
inline void validateWorldDisplay(const WorldDisplaySettings& s) {
    validateWorldHeatHaze(s.heatHaze);
    const float values[]{s.speedUp,s.speedDown,s.lowPercentile,s.highPercentile,s.minEV,s.maxEV,
        s.compensationEV,s.skyWeight,s.centerWeight,s.velocityDamping,s.emissiveWeight,
        s.manualBlend,s.manualExposure,s.bloomMin,s.bloomMax,s.sceneScale,s.bloomScale};
    for(float v:values)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite display setting");
    for(auto row:s.grade)for(float v:row)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite color grade");
    if(s.speedUp<0||s.speedDown<0||s.lowPercentile<0||s.highPercentile>1||s.lowPercentile>=s.highPercentile||
       s.minEV< -16||s.maxEV>40||s.minEV>s.maxEV||std::abs(s.compensationEV)>24||
       s.skyWeight<0||s.emissiveWeight<0||s.centerWeight< -8||s.centerWeight>8||s.velocityDamping<0||
       s.manualBlend<0||s.manualBlend>1||s.manualExposure<=0||
       s.bloomMin<0||s.bloomMax<0||s.sceneScale<0||s.bloomScale<0||s.bloomLevels<2||s.bloomLevels>10||
       s.grade[5][3]<=0||s.grade[6][3]>=1||s.grade[5][3]>=s.grade[6][3]||
       s.grade[7][0]<0||s.grade[7][1]<0)
        throw std::invalid_argument("Invalid display setting");
    for(int i=0;i<3;++i)if(s.grade[1][i]<=0||s.grade[2][i]<0)
        throw std::invalid_argument("Invalid display gamma/gain");
}
inline std::array<float,32> worldDisplayParameters(const WorldDisplaySettings& s,int w,int h,
    float dt,bool valid,float preExposure,float compensation,float velocity,float bloom) {
    validateWorldDisplay(s);
    if(w<=0||h<=0||!std::isfinite(dt)||dt<0||!std::isfinite(preExposure)||preExposure<=0||
       !std::isfinite(compensation)||compensation<=0||!std::isfinite(velocity)||velocity<0)
        throw std::invalid_argument("Invalid display view");
    return {float(w),float(h),float(w),float(h),
        dt,valid?1.f:0.f,preExposure,std::exp2(s.compensationEV)*compensation,
        s.lowPercentile,s.highPercentile,.125f*std::exp2(s.minEV),.125f*std::exp2(s.maxEV),
        s.speedUp,s.speedDown,std::exp(-.01f*s.velocityDamping*velocity),s.skyWeight,
        s.centerWeight,s.emissiveWeight,s.manualBlend,s.manualExposure,
        s.bloomMin,s.bloomMax,s.sceneScale,s.bloomScale*bloom/s.bloomLevels,
        0,0,0,0,0,0,0,0};
}
inline int worldDisplayAlignedSize(int size,int levels) {
    const int block=1<<levels;return ((size+block-1)/block)*block;
}
}
