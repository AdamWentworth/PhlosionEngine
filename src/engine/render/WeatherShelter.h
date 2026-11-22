#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::render {
// Horizontal airborne dust is blocked by upwind walls, not by an open canopy.
// Callers supply their scene's solid-geometry query; no scene ownership here.
template<class Query> float windExposure(glm::vec3 position,glm::vec3 velocity,const Query& query,bool soft=true) {
    for(unsigned i=0;i<3;++i)if(!std::isfinite(position[i])||!std::isfinite(velocity[i]))
        throw std::invalid_argument("Wind shelter input");
    velocity.y=0;
    const float speed=glm::length(velocity);
    if(speed<1e-5f)return 1;
    const auto wind=velocity/speed,side=glm::cross(wind,glm::vec3(0,1,0));
    float sum=0;
    const unsigned count=soft?5:1;
    for(unsigned i=0;i<count;++i){
        const auto offset=i==1?side*.35f:i==2?-side*.35f:i==3?glm::vec3(0,.35f,0):i==4?glm::vec3(0,-.35f,0):glm::vec3(0);
        sum+=query(position+offset,-wind*64.f)?0.f:1.f;
    }
    return sum/float(count);
}
}
