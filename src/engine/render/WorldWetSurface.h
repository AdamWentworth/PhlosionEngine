#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace engine::render {
struct WorldWetAccumulation {
    double moisture=0,puddles=0;
};
// Rates are per caller second. Clock acceleration is owned by the game.
struct WorldWetRates {
    double moistureAccumulation=5./60.,puddleAccumulation=1.5/60.;
    double moistureEvaporation=2.5/60.,puddleEvaporation=.25/60.;
};
inline WorldWetAccumulation advanceWorldWetSurface(WorldWetAccumulation state,double rain,double seconds,
                                                   WorldWetRates rates={}) {
    for(double x:{state.moisture,state.puddles,rain,seconds,rates.moistureAccumulation,
                  rates.puddleAccumulation,rates.moistureEvaporation,rates.puddleEvaporation})
        if(!std::isfinite(x)||x<0)throw std::invalid_argument("Invalid wet-surface accumulation");
    if(rain>1||state.moisture>1||state.puddles>1)throw std::invalid_argument("Wet-surface range");
    state.moisture=std::clamp(state.moisture+seconds*(rain>0?rain*rates.moistureAccumulation:-rates.moistureEvaporation),0.,1.);
    state.puddles=std::clamp(state.puddles+seconds*(rain>0?rain*rates.puddleAccumulation:-rates.puddleEvaporation),0.,1.);
    return state;
}
// Exact normalized RGBA8 repeat/bilinear lookup shared by CPU water queries.
// Material normal details may use derivatives/mips; physical water coverage uses LOD0.
inline float worldWetMaskChannel(std::span<const unsigned char> image,unsigned width,unsigned height,
                                 double u,double v,unsigned channel) {
    if(!width||!height||channel>3||image.size()!=std::size_t(width)*height*4||!std::isfinite(u)||!std::isfinite(v))
        throw std::invalid_argument("Invalid wet-surface mask");
    const double x=(u-std::floor(u))*width-.5,y=(v-std::floor(v))*height-.5;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const auto at=[&](int a,int b){a=(a+int(width))%int(width);b=(b+int(height))%int(height);
        return double(image[(std::size_t(b)*width+a)*4+channel])/255.;};
    return float(std::lerp(std::lerp(at(ix,iy),at(ix+1,iy),x-ix),
                           std::lerp(at(ix,iy+1),at(ix+1,iy+1),x-ix),y-iy));
}
inline std::array<float,2> worldWetPuddle(float maskNear,float maskFar,float normalUp,float puddles) {
    const float top=std::clamp((normalUp+std::lerp(-1.f,-.96f,puddles))*25.f,0.f,1.f);
    const float mask=maskNear*maskFar*top;
    return {std::clamp((mask-.3f)*2.5f,0.f,1.f),std::clamp((mask-.45f)*15.f,0.f,1.f)};
}
using WorldWetUniforms=std::array<std::array<float,4>,11>;
}
