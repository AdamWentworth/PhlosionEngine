#pragma once
#include "engine/render/IWorldDirectionalShadow.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>
namespace engine::render {
struct WorldSunShadowBudget {
    unsigned cascades=4,resolution=2048,distantResolution=2048;
    unsigned width() const { return 2*resolution; }
    unsigned height() const { return 2*resolution+((5+width()/distantResolution-1)/(width()/distantResolution))*distantResolution; }
};
WorldSunShadowBudget worldSunShadowBudget(unsigned quality);
using WorldSunShadowUniforms=std::array<std::array<float,4>,72>;
struct WorldSunShadowInput {
    glm::mat4 viewProjection{1};
    glm::vec3 camera{},forward{0,0,-1},light{0,1,0};
    glm::vec3 boundsMin{-4096,-64,-4096},boundsMax{4096,256,4096};
    std::array<float,4> ranges{16,48,128,320};
    float nearPlane=.05f,blendRange=.1f,casterExtension=300;
    float strength=1,normalOffset=.018f,depthBias=.00015f,filterTexels=1.5f;
    float distantBaseRadius=128,distantBlendSeconds=.4f,deltaSeconds=1.f/60;
    unsigned quality=1,width=1920,height=1080;
    std::uint64_t frame=0;
    std::uint64_t view=1,sceneRevision=1;
    bool cut=false,enabled=true;
};
struct WorldSunShadowPlan {
    WorldSunShadowBudget budget{};
    std::array<WorldDirectionalShadowView,9> draws{};
    WorldSunShadowUniforms uniforms{};
    unsigned drawMask=0;
    std::uint64_t identity=0;
};
class WorldSunShadowPlanner {
public:
    WorldSunShadowPlan prepare(const WorldSunShadowInput&);
    void reset();
private:
    std::array<glm::vec2,4> snapPhase_{};
    glm::vec3 oldRight_{},oldUp_{},oldLight_{},oldCamera_{};
    std::array<float,10> distantSettings_{};
    std::array<WorldDirectionalShadowView,4> distant_{};
    std::array<unsigned,4> slots_{4,5,6,7};
    WorldDirectionalShadowView previous_{};
    unsigned spare_=8,previousSlot_=8,updated_=0,iteration_=0,quality_=~0u;
    float blend_=1;
    std::uint64_t view_=0,revision_=0;
    bool valid_=false;
};
const std::vector<unsigned char>& worldSunShadowNoise();
void validateWorldSunShadowPlan(const WorldSunShadowPlan&);
}
