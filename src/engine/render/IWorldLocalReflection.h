#pragma once

#include <array>
#include <span>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::render {
using WorldLocalReflectionUniforms = std::array<std::array<float,4>,68>;
inline constexpr int worldLocalReflectionViewCount=12;
struct WorldProbeRadianceImage {
    unsigned width=0,height=0;
    std::vector<std::uint32_t> rgb111110;
    WorldLocalReflectionUniforms selection{};
};
enum class WorldLocalReflectionSampling { DepthTraced, PreparedRadiance };
struct WorldLocalReflectionProbe {
    // Capture position is independent of receiver influence bounds. A moving
    // camera may capture from outside the region in which reflections apply.
    std::array<float,3> position{}, minimum{-10,-10,-10}, maximum{10,10,10};
    WorldLocalReflectionSampling sampling=WorldLocalReflectionSampling::DepthTraced;
    int resolution=256;
    float fade=1.f, farPlane=120.f;
    // Rotate the influence/projection box about its bounds center. Captures
    // remain world-oriented, so rotating an influence never rotates geometry.
    std::array<float,4> orientation{0,0,0,1};
    float priority=0.f, intensity=1.f;
    std::uint8_t receiverChannels=255;
    bool boxProjection=true;
    bool ambientContribution=true,overrideIndirect=false;
    float reflectionDimming=.1f;
};
inline int worldLocalReflectionCaptureViewCount(const WorldLocalReflectionProbe& p) {
    return p.sampling==WorldLocalReflectionSampling::PreparedRadiance?6:worldLocalReflectionViewCount;
}
struct WorldLocalReflectionFace {
    std::array<float,16> viewProjection{};
    std::array<float,3> position{}, forward{}, up{};
    int resolution=256;
    float nearPlane=.05f, farPlane=120.f;
};
inline WorldLocalReflectionUniforms packWorldLocalReflection(const WorldLocalReflectionProbe& p) {
    if(p.resolution<16 || p.resolution>512 || (p.resolution&(p.resolution-1)) ||
       !std::isfinite(p.fade) || p.fade<=0 || !std::isfinite(p.farPlane) || p.farPlane<=.1f)
        throw std::invalid_argument("Invalid local reflection extent");
    for(int i=0;i<3;++i)
        if(!std::isfinite(p.position[i]) || !std::isfinite(p.minimum[i]) || !std::isfinite(p.maximum[i]) ||
           p.minimum[i]>=p.maximum[i])
            throw std::invalid_argument("Invalid reflection capture or influence bounds");
    float norm=0;
    for(float q:p.orientation){if(!std::isfinite(q))throw std::invalid_argument("Invalid probe orientation");norm+=q*q;}
    if(norm<1e-8f || !std::isfinite(norm) || !std::isfinite(p.priority) || p.priority<0 ||
       !std::isfinite(p.intensity) || p.intensity<0 || !std::isfinite(p.reflectionDimming) || p.reflectionDimming<0 || p.reflectionDimming>1)
        throw std::invalid_argument("Invalid probe participation");
    WorldLocalReflectionUniforms result{{{p.position[0],p.position[1],p.position[2],-1},
        {p.minimum[0],p.minimum[1],p.minimum[2],p.fade},
        {p.maximum[0],p.maximum[1],p.maximum[2],float(p.resolution)}, {}}};
    const auto rotation=glm::mat3_cast(glm::conjugate(glm::normalize(glm::quat(
        p.orientation[3],p.orientation[0],p.orientation[1],p.orientation[2]))));
    const glm::vec3 center((p.minimum[0]+p.maximum[0])*.5f,
        (p.minimum[1]+p.maximum[1])*.5f,(p.minimum[2]+p.maximum[2])*.5f);
    for(unsigned row=0;row<3;++row) {
        const float half=(p.maximum[row]-p.minimum[row])*.5f;
        glm::vec3 axis(rotation[0][row]/half,rotation[1][row]/half,rotation[2][row]/half);
        result[52+row]={axis.x,axis.y,axis.z,-glm::dot(axis,center)};
    }
    result[55]={p.priority,p.intensity,float(p.receiverChannels|(p.boxProjection?256u:0u)|(!p.ambientContribution?512u:0u)|(p.overrideIndirect?1024u:0u)),p.reflectionDimming};
    return result;
}
inline WorldLocalReflectionFace makeWorldLocalReflectionFace(const WorldLocalReflectionProbe& p,int face) {
    (void)packWorldLocalReflection(p);
    if(face<0 || face>=worldLocalReflectionCaptureViewCount(p))throw std::invalid_argument("Invalid reflection face");
    face%=6;
    const std::array<glm::vec3,6> forward{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const glm::vec3 up=face==2?glm::vec3(0,0,-1):face==3?glm::vec3(0,0,1):glm::vec3(0,1,0);
    const glm::vec3 origin(p.position[0],p.position[1],p.position[2]);
    const auto vp=glm::perspective(glm::radians(90.f),1.f,.05f,p.farPlane)*glm::lookAt(origin,origin+forward[face],up);
    WorldLocalReflectionFace out;out.position=p.position;out.resolution=p.resolution;out.farPlane=p.farPlane;
    std::copy_n(glm::value_ptr(vp),16,out.viewProjection.begin());
    std::copy_n(glm::value_ptr(forward[face]),3,out.forward.begin());
    std::copy_n(glm::value_ptr(up),3,out.up.begin());return out;
}
inline std::array<int,2> worldLocalReflectionAtlasOffset(int face,int size,int level,int base) {
    if(face<0)return {0,0};
    if(face<6)return {(face%3)*size,(face/3)*size+4*base-(4*base>>level)};
    // Prepared GGX mips reuse the geometry tiles. Their first level is half
    // size, and subsequent levels occupy a vertical strip within each tile.
    if(face>=12) {
        const int tile=face-12;
        return {(tile<2?3:2+tile%2)*base,
            (tile<2?tile:2+(tile-2)/2)*base+base-(base>>(level-1))};
    }
    const int geometry=face-6;
    return {(geometry<2?3:2+geometry%2)*size,(geometry<2?geometry:2+(geometry-2)/2)*size};
}
// Padded placed-probe faces keep raw and GGX pyramids in one 512-square bank.
// Border texels are prepared once; ordinary consumers use hardware bilinear.
inline std::array<int,2> worldPlacedReflectionAtlasOffset(int face,int level,int base) {
    const bool border=face>=24;face%=24;
    const bool prepared=face>=12;face%=6;
    const int size=base>>level,stride=size+2;
    int x=0,y=0;
    if(level==1){x=prepared?3*stride:0;y=2*(base+2);}
    else if(level>=2) {
        for(int l=2;l<level;++l)x+=6*((base>>l)+2);
        if(prepared)x+=3*stride;
        y=2*(base+2)+2*((base>>1)+2);
    }
    return {x+(face%3)*stride+(border?0:1),y+(face/3)*stride+(border?0:1)};
}
// Capture six color views; DepthTraced also captures six geometry views.
// Each end-face copies and clears the HDR/depth targets. No partial generation
// is sampled; beginWorldSceneColorPass invalidates the previous view's capture.
class IWorldLocalReflection {
public:
    virtual ~IWorldLocalReflection()=default;
    virtual void invalidateWorldPlacedProbes(std::span<const std::uint64_t>) {}
    virtual bool beginWorldPlacedProbeFace(std::uint64_t,std::uint64_t,const WorldLocalReflectionProbe&,unsigned,std::span<const std::uint64_t>) {return false;}
    virtual bool endWorldPlacedProbeFace() {return false;}
    virtual bool selectWorldPlacedProbes(std::span<const std::uint64_t>) {return false;}
    virtual bool beginWorldLocalReflection(const WorldLocalReflectionProbe&)=0;
    virtual bool endWorldLocalReflectionFace()=0;
    virtual void clearWorldLocalReflection()=0;
    // Explicit diagnostics only; ordinary frames never allocate readback storage.
    virtual bool captureWorldProbeRadiance(std::span<const std::uint64_t>) {return false;}
    virtual WorldProbeRadianceImage readWorldProbeRadiance() {return {};}
protected:
    WorldLocalReflectionUniforms worldLocalReflection_{};
    int worldLocalReflectionFace_=0;
    bool worldLocalReflectionPrepared_=false;
};
}
