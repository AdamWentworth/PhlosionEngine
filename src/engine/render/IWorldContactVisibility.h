#pragma once

#include "engine/render/IRenderBackend.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cmath>
#include <stdexcept>

namespace engine::render {
using WorldContactUniforms = std::array<std::array<float,4>,24>;

struct WorldContactResponse {
    std::array<float,4> strengths{1.2f,.95f,1.f,.5f}; // indirect diffuse/specular, direct diffuse/specular
    std::array<float,3> material{-1.f,.75f,.5f}; // grazing threshold, foliage diffuse/specular dim
};
inline WorldContactUniforms packWorldContactVisibility(const float* matrix, int width, int height,
                                                       float radius, float bias, unsigned quality=2,
                                                       WorldContactResponse response={}) {
    if (quality>2 || !matrix || width<1 || height<1 || width>8192 || height>8192 ||
        !std::isfinite(radius) || radius<=0 || radius>10 ||
        !std::isfinite(bias) || bias<0 || bias>=radius*.25f)
        throw std::invalid_argument("Invalid contact visibility view");
    const auto vp=glm::make_mat4(matrix);
    const auto inverse=glm::inverse(vp);
    WorldContactUniforms result{};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
        if(!std::isfinite(vp[col][row]) || !std::isfinite(inverse[col][row]))
            throw std::invalid_argument("Singular contact visibility view");
        result[row][col]=vp[col][row];result[row+4][col]=inverse[col][row];
    }
    result[8]={float(width),float(height),radius,-1};
    result[9]={bias,0,0,0};
    const auto origin=inverse*glm::vec4(0,0,1,0);
    if(std::abs(origin.w)>.000001f) {
        const glm::vec3 camera=glm::vec3(origin)/origin.w;
        const glm::vec3 right(vp[0][0],vp[1][0],vp[2][0]);
        const glm::vec3 up(vp[0][1],vp[1][1],vp[2][1]);
        const glm::vec3 forward(vp[0][3],vp[1][3],vp[2][3]);
        const auto f=glm::normalize(forward);
        const auto planarRight=right-glm::dot(right,f)*f,planarUp=up-glm::dot(up,f)*f;
        const auto r=glm::normalize(planarRight),u=glm::normalize(planarUp);
        result[10]={camera.x,camera.y,camera.z,float(quality)};
        result[11]={r.x,r.y,r.z,1.f/glm::length(planarRight)};
        result[12]={u.x,u.y,u.z,1.f/glm::length(planarUp)};
        result[13]={f.x,f.y,f.z,0};
        result[9][3]=1;
    }
    for(float value:response.strengths)if(!std::isfinite(value)||value<0||value>2)
        throw std::invalid_argument("Invalid contact response strength");
    if(!std::isfinite(response.material[0])||response.material[0]<-1||response.material[0]>1)
        throw std::invalid_argument("Invalid contact grazing threshold");
    for(int i=1;i<3;++i)if(!std::isfinite(response.material[i])||response.material[i]<0||response.material[i]>1)
        throw std::invalid_argument("Invalid contact foliage dim");
    result[22]={response.material[0],response.material[1],response.material[2],0};
    result[23]=response.strengths;
    return result;
}

struct WorldContactImage {
    int width=0,height=0,halfWidth=0,halfHeight=0;
    std::array<std::array<float,4>,32> parameters{};
    // raw, spatial, temporal, full, half depth, half velocity, half normal/flags, native depth.
    std::array<std::vector<std::array<float,4>>,8> planes;
    std::uint64_t texelBytes=0;
};
// Optional depth prepass followed by forward indirect-light visibility. The
// callback draws the same opaque/masked geometry and poses as the color pass.
// Transparent surfaces are excluded by the prepass shader. Clear between views.
class IWorldContactVisibility {
public:
    virtual ~IWorldContactVisibility()=default;
    virtual bool beginWorldContactVisibility(const WorldContactUniforms&)=0;
    virtual bool endWorldContactVisibility()=0;
    virtual void clearWorldContactVisibility()=0;
    virtual bool captureWorldContactVisibility(){return false;}
    virtual WorldContactImage readWorldContactVisibility(){return {};}
protected:
    WorldContactUniforms worldContact_{};
};

template<class Draw>
void drawWorldContactVisibility(IRenderBackend& renderer, const float* matrix, int width, int height,
                                Draw&& draw, float radius=.8f, float bias=.015f) {
    auto* contact=dynamic_cast<IWorldContactVisibility*>(&renderer);
    if(!contact || !contact->beginWorldContactVisibility(packWorldContactVisibility(matrix,width,height,radius,bias)))
        throw std::runtime_error("Contact visibility prepass unavailable");
    try {
        draw();
        if(!contact->endWorldContactVisibility()) throw std::runtime_error("Contact visibility depth capture failed");
    } catch (...) {contact->clearWorldContactVisibility();throw;}
}
}
