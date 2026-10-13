#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <glm/glm.hpp>

namespace engine::render {
struct RainHit { glm::vec3 point{}, normal{0,1,0}; float fraction = 0, water = 0; };
using RainCoverQuery = std::function<std::optional<RainHit>(glm::vec3, glm::vec3)>;
struct RainVolumeStats {
    std::size_t drops = 0, impacts = 0, covered = 0, queries = 0;
    std::array<std::size_t,3> bandDrops{};
    std::size_t solidImpacts=0, waterImpacts=0, impactQuads=0, newImpacts=0;
    std::size_t sprayQuads=0, glowQuads=0, mistQuads=0;
};
struct RainImpact {
    glm::vec3 point{},normal{0,1,0};
    double age=0;
    std::uint32_t id=0;
    float water=0;
};

struct RainVisualSettings {
    float densityScale=1, opacity=.65f;
    float halfWidth=.024f, widthVariation=.014f, length=.55f, lengthVariation=.55f;
    float fallSpeed=22, windResponse=.25f;
    float horizontalRange=32, horizontalFade=8, verticalRange=16, verticalFade=5;
    float nearDistance=.6f, nearFade=1.4f;
    float tintR=.72f, tintG=.82f, tintB=1;
    float ambientScale=2, ambientBias=.15f, brightnessMin=.18f, brightnessMax=.85f;
    float impactRateScale=1, impactLifetime=.55f, impactOpacity=.3f;
    float impactRadius=.035f, impactGrowth=.25f;
    // Contact spray is optional and uses four additional atlas variants.
    float sprayOpacity=0, sprayRadius=.25f, sprayHeight=.2f;
    float glowOpacity=0, glowRadius=.1f;
    float mistOpacity=0, mistRadius=2, mistHeight=1.5f;
};
struct RainSettingField {
    std::string_view name;
    float RainVisualSettings::*member;
    float minimum, maximum;
};
inline constexpr std::array rainSettingFields{
    RainSettingField{"density_scale",&RainVisualSettings::densityScale,0,2},
    RainSettingField{"opacity",&RainVisualSettings::opacity,0,1},
    RainSettingField{"half_width",&RainVisualSettings::halfWidth,.001f,.1f},
    RainSettingField{"width_variation",&RainVisualSettings::widthVariation,0,.1f},
    RainSettingField{"length",&RainVisualSettings::length,.01f,2},
    RainSettingField{"length_variation",&RainVisualSettings::lengthVariation,0,2},
    RainSettingField{"fall_speed",&RainVisualSettings::fallSpeed,1,100},
    RainSettingField{"wind_response",&RainVisualSettings::windResponse,0,1},
    RainSettingField{"horizontal_range",&RainVisualSettings::horizontalRange,1,32},
    RainSettingField{"horizontal_fade",&RainVisualSettings::horizontalFade,.1f,32},
    RainSettingField{"vertical_range",&RainVisualSettings::verticalRange,1,16},
    RainSettingField{"vertical_fade",&RainVisualSettings::verticalFade,.1f,16},
    RainSettingField{"near_distance",&RainVisualSettings::nearDistance,0,10},
    RainSettingField{"near_fade",&RainVisualSettings::nearFade,.1f,10},
    RainSettingField{"tint_r",&RainVisualSettings::tintR,0,1},
    RainSettingField{"tint_g",&RainVisualSettings::tintG,0,1},
    RainSettingField{"tint_b",&RainVisualSettings::tintB,0,1},
    RainSettingField{"ambient_scale",&RainVisualSettings::ambientScale,0,4},
    RainSettingField{"ambient_bias",&RainVisualSettings::ambientBias,0,1},
    RainSettingField{"brightness_min",&RainVisualSettings::brightnessMin,0,1},
    RainSettingField{"brightness_max",&RainVisualSettings::brightnessMax,0,1},
    RainSettingField{"impact_rate_scale",&RainVisualSettings::impactRateScale,.1f,4},
    RainSettingField{"impact_lifetime",&RainVisualSettings::impactLifetime,.01f,1},
    RainSettingField{"impact_opacity",&RainVisualSettings::impactOpacity,0,1},
    RainSettingField{"impact_radius",&RainVisualSettings::impactRadius,.001f,.5f},
    RainSettingField{"impact_growth",&RainVisualSettings::impactGrowth,0,1},
    RainSettingField{"spray_opacity",&RainVisualSettings::sprayOpacity,0,.5f},
    RainSettingField{"spray_radius",&RainVisualSettings::sprayRadius,.01f,1},
    RainSettingField{"spray_height",&RainVisualSettings::sprayHeight,.01f,1},
    RainSettingField{"glow_opacity",&RainVisualSettings::glowOpacity,0,.4f},
    RainSettingField{"glow_radius",&RainVisualSettings::glowRadius,.01f,.4f},
    RainSettingField{"mist_opacity",&RainVisualSettings::mistOpacity,0,.6f},
    RainSettingField{"mist_radius",&RainVisualSettings::mistRadius,.1f,4},
    RainSettingField{"mist_height",&RainVisualSettings::mistHeight,.1f,4}
};
// Returns the invalid field, or an empty view for a valid bounded configuration.
std::string_view invalidRainSetting(const RainVisualSettings&);
struct RainBandSettings {
    float density=1, width=1, length=1, opacity=1, speed=1;
    float nearDistance=0, nearFade=1, farDistance=16, farFade=4;
    float cellSize=4;
};
struct RainBandField {
    std::string_view name;
    float RainBandSettings::*member;
    float minimum,maximum;
};
inline constexpr std::array rainBandFields{
    RainBandField{"density",&RainBandSettings::density,0,1},
    RainBandField{"width",&RainBandSettings::width,.1f,2},
    RainBandField{"length",&RainBandSettings::length,.1f,3},
    RainBandField{"opacity",&RainBandSettings::opacity,0,1},
    RainBandField{"speed",&RainBandSettings::speed,.25f,2},
    RainBandField{"near_distance",&RainBandSettings::nearDistance,0,30},
    RainBandField{"near_fade",&RainBandSettings::nearFade,.1f,16},
    RainBandField{"far_distance",&RainBandSettings::farDistance,1,32},
    RainBandField{"far_fade",&RainBandSettings::farFade,.1f,16},
    RainBandField{"cell_size",&RainBandSettings::cellSize,2,8}
};
struct RainLayerSettings { std::array<RainBandSettings,3> bands{}; };
std::string_view invalidRainBand(const RainBandSettings&,std::size_t band);
// Optional integrated motion lets an owner change velocity/rate without moving
// all existing drops by the new speed multiplied by the elapsed play clock.
// Monotonically increasing nonzero generation identifies owner resets/wraps.
// Contacts use the latest accepted view time; older fixed/interpolated views
// neither rewind them nor spawn again. Zero retains legacy backward-time reset.
struct RainMotion { glm::dvec3 travel{}; double impactSeconds=0; std::uint64_t generation=0; };

// Analytic, world-anchored rain in a bounded camera-local volume. The owner
// supplies collision cover and accepted play time, including presentation remainder.
class RainVolume {
public:
    static constexpr std::size_t maxDrops = 1215, maxImpacts = 81;
    static constexpr std::size_t maxImpactQuads = maxImpacts*21;
    static constexpr std::size_t maxSprayQuads = maxImpacts*8;
    static constexpr std::size_t maxGlowQuads = maxImpacts, maxMistVolumes = 49, maxMistQuads = maxMistVolumes*16;
    static constexpr std::size_t maxContactQuads = maxImpactQuads+maxSprayQuads+maxGlowQuads;
    static constexpr std::size_t maxQueries = maxDrops+maxImpacts+maxMistVolumes;
    RainVolume();
    void setStreakAtlas(std::string key,int tileWidth,int height,std::span<const unsigned char> rgba);
    // 8/12: four splash frames, four ripple frames, optional four spray variants.
    // 16: three stable splash variants (+ spare), four ripples, four spray, four glow.
    void setImpactAtlas(std::string key,int tileWidth,int height,std::span<const unsigned char> rgba,int columns=8);
    void setMistTexture(std::string key,int width,int height,std::span<const unsigned char> rgba);
    RainVolumeStats build(const WorldEnvironmentLighting&, glm::vec3 camera,
        const RainCoverQuery&, const RainVisualSettings& = {}, const RainMotion* = nullptr,
        const RainLayerSettings* = nullptr);
    void prewarm(IRenderBackend&, WorldResourceScopeId) const;
    void draw(IRenderBackend&, WorldResourceScopeId, const float* viewProjection,
        int width, int height,unsigned layerMask=7) const;
    std::span<const IRenderBackend::WorldMeshVertex> dropVertices() const { return drops_; }
    std::span<const IRenderBackend::WorldMeshVertex> impactVertices() const { return impacts_; }
    std::span<const IRenderBackend::WorldMeshVertex> mistVertices() const { return mist_; }
    std::span<const IRenderBackend::WorldMeshVertex> sprayVertices() const { return spray_; }
    std::span<const RainImpact> visibleImpacts() const { return visibleImpacts_; }
private:
    IRenderBackend::WorldTextureData texture(bool impact,bool layered=false) const;
    IRenderBackend::WorldTextureData mistTexture() const;
    void buildMist(const WorldEnvironmentLighting&,glm::vec3,const RainCoverQuery&,
        const RainVisualSettings&,RainVolumeStats&);
    void densityVolume(std::vector<IRenderBackend::WorldMeshVertex>&,glm::vec3,glm::vec3,
        float,float,glm::vec3,float,std::uint32_t,int,glm::vec3);
    RainVolumeStats buildLayers(const WorldEnvironmentLighting&,glm::vec3,const RainCoverQuery&,
        const RainVisualSettings&,const RainMotion&,const RainLayerSettings&);
    struct Contact {
        glm::vec3 point{}, normal{0,1,0};
        double born=0;
        std::uint32_t id=0, seed=0;
        std::size_t band=0;
        float water=0;
        bool active=false;
    };
    void contact(const RainHit&,glm::vec3 tip,float speed,std::uint32_t id,std::uint32_t seed,std::size_t band,
        const WorldEnvironmentLighting&,const RainVisualSettings&,RainVolumeStats&);
    void drawContacts(const WorldEnvironmentLighting&,glm::vec3,const RainVisualSettings&,
        const RainLayerSettings&,RainVolumeStats&);
    void quad(std::vector<IRenderBackend::WorldMeshVertex>&, glm::vec3 start,
        glm::vec3 end, glm::vec3 side, glm::vec3 color, float alpha,int tile=-1,int columns=3,int tileWidth=0);
    std::array<unsigned char,32*64*4> streakPixels_{};
    std::array<unsigned char,32*32*4> impactPixels_{};
    std::vector<IRenderBackend::WorldMeshVertex> drops_, impacts_, mist_,spray_,fog_;
    struct FogQuad { std::array<IRenderBackend::WorldMeshVertex,4> vertices; float distance; };
    std::vector<FogQuad> fogSort_;
    std::vector<std::uint32_t> indices_;
    std::vector<unsigned char> atlasPixels_;
    std::string atlasKey_;
    int tileWidth_=0,atlasHeight_=0;
    std::vector<unsigned char> impactAtlasPixels_;
    std::string impactAtlasKey_;
    int impactTileWidth_=0,impactAtlasHeight_=0,impactColumns_=8;
    std::vector<unsigned char> mistPixels_;
    std::string mistKey_;
    int mistWidth_=0,mistHeight_=0;
    glm::dvec3 acceptedTravel_{};
    std::array<Contact,maxImpacts> contacts_{};
    std::vector<RainImpact> visibleImpacts_;
    double contactSeconds_=-1;
    std::uint32_t contactSeed_=0;
    std::uint64_t contactGeneration_=0;
    bool acceptContacts_=false;
    bool layered_=false;
};
} // namespace engine::render
