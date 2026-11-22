#pragma once
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/IWorldContactVisibility.h"
namespace engine::render {
enum class WorldTemporalReflectionQuality { Low, Medium, High, Ultra };
struct WorldTemporalReflectionBudget { int rays,steps; float fullRoughness,probeRoughness; };
inline WorldTemporalReflectionBudget worldTemporalReflectionBudget(WorldTemporalReflectionQuality quality) {
    switch(quality) {
    case WorldTemporalReflectionQuality::Low:return {1,8,.3f,.5f};
    case WorldTemporalReflectionQuality::Medium:return {2,12,.4f,.6f};
    case WorldTemporalReflectionQuality::High:return {3,12,.5f,.7f};
    case WorldTemporalReflectionQuality::Ultra:return {4,16,.6f,.8f};
    }
    throw std::invalid_argument("Invalid temporal reflection quality");
}
struct WorldTemporalReflectionView {
    std::array<float,3> camera{};
    // Column-major OpenGL camera. Projection comes from the shared surface,
    // including its actual jitter. No second projection convention is inferred.
    WorldMotionMatrix worldToView=worldMotionIdentity;
    WorldTemporalReflectionQuality quality=WorldTemporalReflectionQuality::Medium;
};
using WorldTemporalReflectionUniforms=std::array<std::array<float,4>,16>;
inline WorldTemporalReflectionUniforms packWorldTemporalReflection(
    const WorldContactUniforms& surface,const WorldTemporalReflectionView& camera,
    const WorldTemporalView& current,const WorldTemporalFeedback& feedback) {
    auto budget=worldTemporalReflectionBudget(camera.quality);
    WorldTemporalReflectionUniforms p{};
    std::copy_n(surface.begin(),8,p.begin());
    for(float x:camera.camera)if(!std::isfinite(x))throw std::invalid_argument("Invalid reflection camera");
    for(float x:camera.worldToView)if(!std::isfinite(x))throw std::invalid_argument("Invalid reflection view");
    const auto& m=camera.worldToView;
    for(int row=0;row<3;++row) {
        float length=0;
        for(int c=0;c<3;++c){p[9+row][c]=m[c*4+row]*(row==2?-1.f:1.f);length+=p[9+row][c]*p[9+row][c];}
        if(std::abs(length-1.f)>.002f)throw std::invalid_argument("Non-rigid reflection camera");
    }
    p[8]={camera.camera[0],camera.camera[1],camera.camera[2],feedback.valid?1.f:0.f};
    p[9][3]=feedback.valid?current.preExposure/feedback.preExposure:1.f;
    p[10][3]=float(current.frame%64);
    p[11][3]=float(std::min(4,worldTemporalMipCount(current.width,current.height)-1));
    p[12]={float(current.width),float(current.height),float(std::max(1,current.width/4)),float(std::max(1,current.height/4))};
    p[13]={budget.fullRoughness,budget.probeRoughness,.4f,.8f};
    return p;
}
}
