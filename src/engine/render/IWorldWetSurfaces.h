#pragma once
#include "engine/render/IRenderBackend.h"
#include "engine/render/WorldWetSurface.h"
#include "engine/render/WorldRainMap.h"
#include "engine/render/WorldTemporalScene.h"

namespace engine::render {
struct WorldWetSurfaceView {
    WorldResourceScopeId owner=0;
    WorldTemporalView view{};
    // Droplets, wall leaks, ripple flipbook, and packed interior/vehicle/water fields.
    std::array<IRenderBackend::WorldTextureData,4> textures{};
    std::string rainMap;
    std::array<WorldRainMap::Published,2> cascades{};
    // Field texture has three square tiles, side by side:
    // 32 interior occupancy bits (RGBA8); top/bottom UNORM16; water UNORM16 + valid B.
    std::array<float,4> interior{}; // origin X/Z, horizontal cell size, tile resolution
    std::array<float,4> interiorHeight{}; // base Y, layer height, layer count, reserved
    std::array<float,4> vehicle{}; // origin X/Z, world extent, center Y (+/-64)
    std::array<float,4> water{}; // origin X/Z, world extent, minimum Y (128m depth)
    std::array<float,4> roughness{0,0,5,3}; // shift, clip, exponent, porosity exponent
    // Conventional device depth [0,1]. Negative disables the optional weapon band.
    float weaponDepthMaximum=-1;
    std::array<float,4> color{0,0,5,.1f}; // shift, clip, exponent, emissive preservation
};
inline WorldWetUniforms packWorldWetSurfaces(const WorldWetSurfaceView& v) {
    if(!v.owner||!v.view.identity||v.view.width<=0||v.view.height<=0||v.rainMap.empty())
        throw std::invalid_argument("Invalid wet-surface view");
    for(const auto& t:v.textures)if(!t.key||!t.rgba||t.width<=0||t.height<=0||t.textureSrgb)
        throw std::invalid_argument("Wet-surface linear texture required");
    if(v.textures[3].width!=3*v.textures[3].height||v.interior[3]!=v.textures[3].height||
       v.interior[2]<=0||v.interiorHeight[1]<=0||v.interiorHeight[2]<1||v.interiorHeight[2]>32||
       v.vehicle[2]<=0||v.water[2]<=0)throw std::invalid_argument("Invalid wet-surface fields");
    WorldTemporalHistory{}.validate(v.view);
    if(!v.view.cameraValid||!std::isfinite(v.weaponDepthMaximum)||v.weaponDepthMaximum>1)
        throw std::invalid_argument("Wet-surface camera/depth convention");
    WorldWetUniforms out{};
    out[0]={1,float(v.view.frame%16),float(WorldRainMap::resolution),0};
    out[1]={v.view.cameraPosition[0],v.view.cameraPosition[1],v.view.cameraPosition[2],0};
    for(unsigned i=0;i<2;++i) {
        const auto& c=v.cascades[i];
        if(!c.valid||c.step<=0)throw std::invalid_argument("Unpublished wet-surface rain map");
        out[2+i]={c.origin[0],c.origin[1],c.step,1};
    }
    out[4]=v.interior;out[5]=v.interiorHeight;out[6]=v.vehicle;out[7]=v.water;
    out[8]=v.roughness;out[9]=v.color;
    const auto matrix=glm::make_mat4(v.view.viewProjection.data());
    auto forward=glm::vec3(matrix[0][3],matrix[1][3],matrix[2][3]);
    if(glm::dot(forward,forward)<1e-12f) {
        const auto inverse=glm::inverse(matrix);
        const auto nearPoint=inverse*glm::vec4(0,0,-.5f,1),farPoint=inverse*glm::vec4(0,0,.5f,1);
        forward=glm::vec3(farPoint)/farPoint.w-glm::vec3(nearPoint)/nearPoint.w;
    }
    forward=glm::normalize(forward);
    out[10]={forward.x,forward.y,forward.z,v.weaponDepthMaximum};
    for(const auto& row:out)for(float x:row)if(!std::isfinite(x))throw std::invalid_argument("Nonfinite wet-surface view");
    return out;
}
class WorldWetBindingHistory {
    struct Entry {std::uint64_t owner=0,renderFrame=0;WorldTemporalView view{};};
    std::array<Entry,8> entries_{};
public:
    void validate(const WorldWetSurfaceView& v,std::uint64_t renderFrame) const {
        WorldTemporalHistory{}.validate(v.view);
        for(const auto& e:entries_)if(e.owner==v.owner&&e.view.identity==v.view.identity) {
            if(v.view.generation<e.view.generation||(v.view.generation==e.view.generation&&
               (v.view.frame<e.view.frame||(renderFrame>e.renderFrame&&v.view.frame==e.view.frame))))
                throw std::invalid_argument("Stale wet-surface view");
            if(renderFrame==e.renderFrame&&v.view.frame==e.view.frame&&
               (v.view.width!=e.view.width||v.view.height!=e.view.height||
                v.view.cameraPosition!=e.view.cameraPosition||v.view.viewProjection!=e.view.viewProjection))
                throw std::invalid_argument("Wet-surface view identity changed within frame");
            return;
        }
        if(std::none_of(entries_.begin(),entries_.end(),[](const auto& e){return !e.owner;}))
            throw std::runtime_error("Wet-surface view budget");
    }
    void commit(const WorldWetSurfaceView& v,std::uint64_t renderFrame) {
        for(auto& e:entries_)if(e.owner==v.owner&&e.view.identity==v.view.identity){e={v.owner,renderFrame,v.view};return;}
        for(auto& e:entries_)if(!e.owner){e={v.owner,renderFrame,v.view};return;}
        throw std::logic_error("Unvalidated wet-surface view");
    }
    void release(std::uint64_t owner) {
        for(auto& e:entries_)if(owner==~std::uint64_t{0}||e.owner==owner)e={};
    }
};
class IWorldWetSurfaces {
public:
    virtual ~IWorldWetSurfaces()=default;
    virtual void bindWorldWetSurfaces(const WorldWetSurfaceView*)=0;
};
}
