#pragma once
#include "WorldIndirectLighting.h"
#include "IWorldDirectionalShadow.h"
#include "IWorldLocalReflection.h"
namespace engine::render {
using IndirectGpuParameters=std::array<IndirectRow,234>;
inline IndirectGpuParameters packIndirectUpdate(const WorldIndirectLightingUpdate& update,
    const WorldLocalReflectionUniforms& probes,const WorldDirectionalShadowView& shadow,
    const std::array<float,4>& atlas,bool history) {
    if(!update.identity||!update.revision||!update.transport||update.quality>3||update.volume>1)
        throw std::invalid_argument("Invalid indirect update");
    validateIndirectControls(update.controls);
    IndirectGpuParameters p{};const auto& e=update.environment;const auto& c=update.controls;
    const auto env=packWorldEnvironment(e);if(env[0][3]<.5f)throw std::invalid_argument("Disabled indirect environment");
    p[0]={history?1.f:0.f,float(update.quality),float(update.volume),float(e.lightChannels)};
    const auto peak=[](const auto& v){return std::max({v[0],v[1],v[2]});};
    const bool moon=peak(env[7])>peak(env[1]);
    p[1]=env[moon?6:0];p[2]=env[moon?7:1];p[3]=env[2];
    p[4]={c.multibounce,c.saturation,c.compensation,c.emissiveScale};
    p[5]={c.localScale,c.skyScale,0,0};
    p[6]={c.shadowDistanceWeight,c.normalWeight,c.probeDistanceWeight,c.visibilityBias};
    p[7]={c.threshold,0,0,0};
    p[8]={update.camera[0],update.camera[1],update.camera[2],0};
    for(auto v:update.camera)if(!std::isfinite(v))throw std::invalid_argument("Invalid GI camera");
    unsigned n=0;
    const auto add=[&](const auto& position,float radius,const auto& color,unsigned channels) {
        if(!std::isfinite(radius)||radius<0||channels>255)throw std::invalid_argument("Invalid GI radius/channels");
        if(radius<=.25f||!channels)return;
        if(n>=20)throw std::invalid_argument("Indirect light capacity");
        for(float x:position)if(!std::isfinite(x))throw std::invalid_argument("Invalid GI source");
        for(float x:color)if(!std::isfinite(x)||x<0)throw std::invalid_argument("Invalid GI radiance");
        double energy=0;for(float x:color)energy+=double(x)*x;
        if(energy<=.01)return;
        p[12+n*2]={position[0],position[1],position[2],radius};
        p[13+n*2]={color[0],color[1],color[2],float(channels)};
        ++n;
    };
    for(const auto& l:e.pointLights)
        if((l.participation&WorldLightProbes)!=0) {
            const float scale=l.source.native?l.source.intensityScale:1.f;
            const std::array<float,3> intensity{l.intensity[0]*scale,l.intensity[1]*scale,l.intensity[2]*scale};
            add(l.position,l.radius,intensity,l.channels&e.lightChannels);
        }
    for(const auto& l:e.rectLights)if(l.affectsDiffuseProbes&&(l.participation&WorldLightProbes)!=0) {
        const glm::vec3 u(l.halfU[0],l.halfU[1],l.halfU[2]),v(l.halfV[0],l.halfV[1],l.halfV[2]);
        const float area=l.response==WorldRectLightResponse::FiniteSource
            ? l.source.intensityScale : 4*glm::length(glm::cross(u,v));
        const std::array<float,3> intensity{l.radiance[0]*area,l.radiance[1]*area,l.radiance[2]*area};
        add(l.position,l.range,intensity,l.channels&e.lightChannels);
    }
    p[5][2]=float(n);
    for(unsigned i=0;i<32;++i){for(float v:update.emission[i])if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid GI emission");p[52+i]=update.emission[i];}
    std::copy(probes.begin(),probes.end(),p.begin()+84);
    const auto state=packWorldDirectionalShadow(shadow);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)p[152+row][col]=state.viewProjection[col*4+row];
    p[156]=state.parameters;p[157]=atlas;p[158]=c.distantRange;p[159]=c.distantControls;
    return p;
}
}
