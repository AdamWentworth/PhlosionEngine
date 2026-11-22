#pragma once
#include "engine/render/WorldMotionHistory.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace engine::render {
// Scene-linear radiance. preExposure is the multiplier already applied to RGB,
// NOT the final display exposure. Phlosion's current lighting uses 1.
struct WorldTemporalView {
    std::uint64_t identity=0,generation=0,frame=0;
    int width=0,height=0;
    bool reset=false;
    // -1 uses rendered wall time; nonnegative values support deterministic replay/pause.
    float deltaSeconds=-1,preExposure=1;
    // Actual jittered primary camera. Empty legacy/diagnostic views reject sky history.
    bool cameraValid=false;
    // Pixel displacement in the rendered image; remove once at presentation.
    std::array<float,2> jitterPixels{};
    WorldMotionMatrix viewProjection=worldMotionIdentity;
    std::array<float,3> cameraPosition{};
};
// p2-p7 occupy the remainder of a 128-byte cross-API constant packet.
inline std::array<float,24> worldTemporalCameraConstants(const WorldTemporalView& current,
    const WorldTemporalView& previous,bool validHistory) {
    std::array<float,24> out{};
    out[0]=float(current.width);out[1]=float(current.height);out[3]=out[7]=1.f;
    for(float v:current.jitterPixels)if(!std::isfinite(v)||std::abs(v)>1.f)
        throw std::invalid_argument("Invalid temporal jitter");
    if(!current.cameraValid)return out;
    const auto matrix=[](const WorldTemporalView& view) {
        for(float value:view.viewProjection)if(!std::isfinite(value))
            throw std::invalid_argument("Nonfinite temporal camera");
        for(float value:view.cameraPosition)if(!std::isfinite(value))
            throw std::invalid_argument("Nonfinite temporal camera position");
        const auto m=glm::make_mat4(view.viewProjection.data());
        if(std::abs(glm::determinant(m))<1e-12f)throw std::invalid_argument("Singular temporal camera");
        return m;
    };
    const auto inverse=glm::inverse(matrix(current));
    const auto depth=[](const glm::mat4& inv,const WorldTemporalView& view,float& a,float& b) {
        if(std::abs(inv[2][3])>1e-7f) {
            a=2.f*inv[2][3];b=inv[3][3]-inv[2][3];return 1.f;
        }
        const auto direction=glm::vec3(inv[2]);
        const float length=glm::length(direction);
        if(length<1e-8f)throw std::invalid_argument("Degenerate temporal projection");
        a=2.f*length;b=glm::dot(glm::vec3(inv[3])-glm::make_vec3(view.cameraPosition.data()),direction/length)-length;
        return 2.f;
    };
    out[4]=depth(inverse,current,out[2],out[3]);
    if(!validHistory || !previous.cameraValid)return out;
    const auto old=matrix(previous),oldInverse=glm::inverse(old);
    out[5]=depth(oldInverse,previous,out[6],out[7]);
    const auto displacement=glm::make_vec3(previous.cameraPosition.data())-
        glm::make_vec3(current.cameraPosition.data());
    const auto sky=old*glm::translate(glm::mat4(1.f),displacement)*inverse;
    std::copy_n(glm::value_ptr(sky),16,out.begin()+8);
    return out;
}
struct WorldTemporalFeedback {
    std::uint64_t view=0,frame=0;
    int width=0,height=0,mips=0;
    float preExposure=1;
    bool valid=false;
};
struct WorldTemporalImage {
    int width=0,height=0,maskWidth=0,maskHeight=0,feedbackWidth=0,feedbackHeight=0;
    std::vector<std::array<float,4>> color,mask,feedback;
    // Populated only by explicit diagnostic capture, never normal presentation.
    std::vector<std::array<float,4>> display,meter,exposure,histogram,displayLut;

    // Diagnostic payload of allocated per-view history images, excluding SSR and
    // allocator alignment. Readback always expands compact formats to float.
    std::uint64_t historyTexelBytes=0;
    // Optional diagnostic readback; production keeps radiance/fade separate.
    int reflectionDepthWidth=0,reflectionDepthHeight=0;
    std::vector<std::array<float,4>> reflection;
    std::vector<float> reflectionDepth;
};
inline float worldReflectionUnsignedFloat(std::uint32_t bits,int mantissaBits) {
    const auto exponent=bits>>mantissaBits;
    const auto fraction=bits&((1u<<mantissaBits)-1);
    if(exponent==31)return std::numeric_limits<float>::infinity();
    return std::ldexp(exponent?1.f+float(fraction)/float(1u<<mantissaBits):float(fraction)/float(1u<<mantissaBits),
        exponent?int(exponent)-15:-14);
}
inline std::array<float,4> unpackWorldReflection(std::uint32_t rgb,unsigned char confidence) {
    return {worldReflectionUnsignedFloat(rgb&2047,6),worldReflectionUnsignedFloat((rgb>>11)&2047,6),
        worldReflectionUnsignedFloat(rgb>>22,5),confidence/255.f};
}
inline std::array<float,4> unpackWorldTemporalHalf(const std::uint16_t* rgba) {
    std::array<float,4> result{};
    for(unsigned i=0;i<4;++i)
        result[i]=std::copysign(worldReflectionUnsignedFloat(rgba[i]&0x7fff,10),rgba[i]&0x8000?-1.f:1.f);
    return result;
}
// Rendered view counters deliberately ignore the backend's global frame number:
// editor and game views can be rendered at different rates.
struct WorldTemporalHistory {
    WorldTemporalView previous{};
    bool committed=false;
    bool accepts(const WorldTemporalView& v) const {
        return committed && !v.reset && v.identity==previous.identity &&
            v.generation==previous.generation && v.frame>previous.frame &&
            v.frame-previous.frame==1 && v.width==previous.width && v.height==previous.height && v.cameraValid==previous.cameraValid;
    }
    void validate(const WorldTemporalView& v) const {
        if(!v.identity||!v.generation||!v.frame||v.width<1||v.height<1||
           !std::isfinite(v.deltaSeconds)||(v.deltaSeconds<0&&v.deltaSeconds!=-1)||
           !std::isfinite(v.preExposure)||v.preExposure<=0)
            throw std::invalid_argument("Invalid temporal scene view");
        if(v.cameraValid)(void)worldTemporalCameraConstants(v,{},false);
        if(committed && v.frame<=previous.frame)
            throw std::invalid_argument("Temporal view promoted twice or out of order");
    }
    void commit(const WorldTemporalView& v) {previous=v;committed=true;}
};
inline int worldTemporalMipCount(int width,int height) {
    int levels=1;int w=std::max(1,width/2),h=std::max(1,height/2);
    while(w>1||h>1){w=std::max(1,w/2);h=std::max(1,h/2);++levels;}return levels;
}
inline float worldTemporalCurrentWeight(float deltaSeconds) {
    return 1.f-std::pow(.2f,std::clamp(deltaSeconds,1.f/60.f,1.f/20.f));
}
// Eight pixel offsets; apply to the PRIMARY camera only. Motion must use the
// actual jittered current and previous projections, so no extra UV correction
// is added in reconstruction. Shadow/probe cameras keep their own projection.
inline std::array<float,2> worldTemporalJitter(std::uint64_t frame) {
    constexpr std::array<std::array<float,2>,8> offsets{{
        {{0,-1.f/6}},{{-.25f,1.f/6}},{{.25f,-7.f/18}},{{-.375f,-1.f/18}},
        {{.125f,5.f/18}},{{-.125f,-5.f/18}},{{.375f,1.f/18}},{{-.4375f,7.f/18}}}};
    return offsets[(frame-1)%offsets.size()];
}
inline void jitterWorldTemporalProjection(float* matrix,int width,int height,std::uint64_t frame) {
    const auto p=worldTemporalJitter(frame);
    for(int c=0;c<4;++c) {matrix[c*4]+=2*p[0]/width*matrix[c*4+3];matrix[c*4+1]-=2*p[1]/height*matrix[c*4+3];}
}
}
