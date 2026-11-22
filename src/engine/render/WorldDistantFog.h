#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::render {
struct WorldDistantFog {
    std::array<float,3> nearAlbedo{1,1,1},farAlbedo{1,1,1};
    std::array<float,2> density{},height{},falloff{};
    float start=0,nearDistance=0,farDistance=2000,weight=0;
};
using WorldDistantFogRows=std::array<std::array<float,4>,4>;
inline WorldDistantFogRows packDistantFog(const WorldDistantFog& f,float skyScale) {
    const auto bounded=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(!bounded(f.weight,0,1)||!bounded(f.start,0,100000)||!bounded(f.nearDistance,0,100000)||
       !bounded(f.farDistance,f.nearDistance+1,100000)||!bounded(skyScale,.1f,4))
        throw std::invalid_argument("Distant fog range");
    for(unsigned i=0;i<2;++i)
        if(!bounded(f.density[i],0,1)||!bounded(f.height[i],-100000,100000)||!bounded(f.falloff[i],0,1))
            throw std::invalid_argument("Distant fog layer");
    for(const auto& rgb:{f.nearAlbedo,f.farAlbedo})for(float v:rgb)
        if(!bounded(v,0,65504))throw std::invalid_argument("Distant fog color");
    WorldDistantFogRows rows{};
    if(f.weight==0)return rows;
    const float scale=1/(f.farDistance-f.nearDistance);
    rows[0]={f.nearAlbedo[0]*skyScale,f.nearAlbedo[1]*skyScale,f.nearAlbedo[2]*skyScale,-f.nearDistance*scale};
    rows[1]={f.farAlbedo[0]*skyScale,f.farAlbedo[1]*skyScale,f.farAlbedo[2]*skyScale,scale};
    rows[2]={f.density[0],f.height[0],f.falloff[0],f.start};
    rows[3]={f.density[1],f.height[1],f.falloff[1],f.weight};
    return rows;
}
// Base-2 exponential height layers, excluding the clear interval at the eye.
inline double distantFogTransmission(const WorldDistantFog& fog,double eyeHeight,double vertical,double distance) {
    if(distance<=fog.start||fog.weight==0)return 1;
    double integral=0;
    const double startHeight=eyeHeight+vertical*fog.start/distance;
    const double length=distance-fog.start,dy=vertical*length/distance;
    for(unsigned layer=0;layer<2;++layer) {
        const double x=std::max(-127.,double(fog.falloff[layer])*dy);
        const double average=std::abs(x)>.01?-std::expm1(-std::log(2.)*x)/x:
            std::log(2.)-.5*std::log(2.)*std::log(2.)*x;
        integral+=double(fog.density[layer])*std::exp2(-double(fog.falloff[layer])*(startHeight-fog.height[layer]))*average*length;
    }
    const double t=std::isfinite(integral)?std::clamp(std::exp2(-integral),0.,1.):0;
    return std::lerp(1.,t,double(fog.weight));
}
}
