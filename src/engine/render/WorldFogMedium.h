#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace engine::render {
struct WorldFogBox {
    std::array<float,3> position{},halfRight{1,0,0},halfUp{0,1,0},halfForward{0,0,1};
    std::array<float,3> albedo{1,1,1};
    float density=0,absorption=1,blendRange=.1f,densityFalloff=.1f,ambientScale=1;
    int priority=0;
    std::uint8_t channels=255;
};
struct WorldFogParticle {
    std::array<float,3> position{},albedo{1,1,1};
    float radius=1,density=0,falloff=1;
};
struct WorldFogMedium {
    // Live emitter lists belong to the view producer, never a per-draw packet.
    std::vector<WorldFogBox> boxes;
    std::vector<WorldFogParticle> particles;
    std::array<float,3> albedoTint{1,1,1},distantAlbedoTint{1,1,1};
    float heightMaxCut=1,globalLightScale=1,localLightScale=1,ambientScale=1,localAmbientScale=1;
    float phaseScale=1,phaseBase=0,distantGlobalLightScale=1;
    float distantSkyScale=1,distantGroundScale=1,distantGroundSaturation=1,shadowAmbientDarkening=0;
    std::array<float,3> distantGroundIrradiance{};
    float groundElevation=0,groundHeightVariation=0;
    // Increment only for discontinuous authoring/weather changes.
    std::uint64_t revision=0;
};
inline bool fogBoundsVisible(const glm::vec3& center,const glm::vec3& extent,const glm::mat4& matrix) {
    unsigned common=63;
    for(unsigned i=0;i<8;++i) {
        const auto h=matrix*glm::vec4(center+extent*glm::vec3(i&1?1:-1,i&2?1:-1,i&4?1:-1),1);
        unsigned outside=(h.x < -h.w?1:0)|(h.x>h.w?2:0)|(h.y< -h.w?4:0)|
            (h.y>h.w?8:0)|(h.z< -h.w?16:0)|(h.z>h.w?32:0);
        common&=outside;
    }
    return common==0;
}
inline void validateFogMedium(const WorldFogMedium& m) {
    const auto unit=[](float v){return std::isfinite(v)&&v>=0&&v<=1;};
    const auto positive=[](float v){return std::isfinite(v)&&v>=0&&v<=65504;};
    const auto color=[&](const auto& c){return std::all_of(c.begin(),c.end(),unit);};
    const auto position=[](const auto& c){return std::all_of(c.begin(),c.end(),[](float x){return std::isfinite(x);});};
    if(!color(m.albedoTint)||!color(m.distantAlbedoTint)||
       !unit(m.distantGroundSaturation)||!unit(m.shadowAmbientDarkening)||
       !position(m.distantGroundIrradiance)||!std::isfinite(m.groundElevation))
        throw std::invalid_argument("Fog global medium");
    for(float v:{m.heightMaxCut,m.globalLightScale,m.localLightScale,m.ambientScale,m.localAmbientScale,
                 m.phaseScale,m.phaseBase,m.distantGlobalLightScale,m.distantSkyScale,m.distantGroundScale,m.groundHeightVariation})
        if(!positive(v))throw std::invalid_argument("Fog medium scale");
    for(const auto& b:m.boxes) {
        if(!position(b.position)||!color(b.albedo)||!positive(b.density)||!positive(b.absorption)||
           !unit(b.blendRange)||!unit(b.densityFalloff)||!unit(b.ambientScale))
            throw std::invalid_argument("Fog box medium");
        const auto x=glm::make_vec3(b.halfRight.data()),y=glm::make_vec3(b.halfUp.data()),z=glm::make_vec3(b.halfForward.data());
        if(!position(b.halfRight)||!position(b.halfUp)||!position(b.halfForward)||
           std::min({glm::length(x),glm::length(y),glm::length(z)})<.0001f||
           std::max({std::abs(glm::dot(glm::normalize(x),glm::normalize(y))),
                     std::abs(glm::dot(glm::normalize(x),glm::normalize(z))),
                     std::abs(glm::dot(glm::normalize(y),glm::normalize(z)))})>.001f)
            throw std::invalid_argument("Fog box axes");
    }
    for(const auto& p:m.particles)
        if(!position(p.position)||!color(p.albedo)||!unit(p.density)||!positive(p.radius)||p.radius<.0001f||
           !std::isfinite(p.falloff)||p.falloff<0||p.falloff>2)
            throw std::invalid_argument("Fog particle");
}
}
