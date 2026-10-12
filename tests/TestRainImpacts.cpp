#include "engine/render/RainVolume.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine::render;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
RainLayerSettings layers() {
    return {{RainBandSettings{1,.45f,.65f,.9f,.75f,0,1,8,2,2},
        RainBandSettings{1,.75f,1,.85f,1,4,2,16,4,4},
        RainBandSettings{1,.45f,1.4f,.65f,1.25f,10,6,32,8,8}}};
}
void prepare(RainVolume& rain) {
    std::vector<unsigned char> streak(8*3*16*4,255),impact(8*8*8*4,255);
    rain.setStreakAtlas("impact-test-streak",8,16,streak);
    rain.setImpactAtlas("impact-test-atlas",8,8,impact);
}
RainCoverQuery plane(float height,float water=0,float slope=0,std::vector<glm::vec3>* hits=nullptr) {
    return [=](glm::vec3 p,glm::vec3 d)->std::optional<RainHit> {
        const float t=(height+slope*p.x-p.y)/(d.y-slope*d.x);
        if (t<0||t>1) return {};
        const auto point=p+d*t;
        if (hits) hits->push_back(point);
        return RainHit{point,glm::normalize(glm::vec3(-slope,1,0)),t,water};
    };
}
glm::vec3 base(std::span<const IRenderBackend::WorldMeshVertex> vertices,std::size_t first) {
    const auto& a=vertices[first+2];const auto& b=vertices[first+3];
    return {(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};
}
void bounds(const RainVolume& rain,const RainVolumeStats& s) {
    check(s.impacts<=81&&s.newImpacts<=81&&s.impactQuads<=162&&s.queries<=1296,"Impact work exceeded declared caps");
    check(s.solidImpacts+s.waterImpacts==s.impacts&&s.impactQuads>=s.impacts&&s.impactQuads<=s.impacts*2,
        "Impact event and quad accounting disagree");
    check(rain.impactVertices().size()==s.impactQuads*4,"Impact geometry count disagrees");
    for (const auto& v : rain.impactVertices()) check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
        v.a>0&&v.a<=1&&v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1,"Unsafe impact geometry");
}
}
int main() try {
    WorldEnvironmentLighting e;e.enabled=true;e.rainIntensity=1;e.precipitationSeed=417;
    e.ambientRadiance={.3f,.3f,.3f};
    RainMotion motion{};motion.generation=1;const auto bands=layers();RainVisualSettings settings;
    RainVolume solid;prepare(solid);std::vector<glm::vec3> hits;
    const auto first=solid.build(e,{0,3,0},plane(0,0,0,&hits),settings,&motion,&bands);
    check(first.solidImpacts>0&&first.waterImpacts==0&&first.newImpacts>0,"Solid contact splashes absent");
    bounds(solid,first);
    const auto frozen=std::vector(solid.impactVertices().begin(),solid.impactVertices().end());
    for (std::size_t i=0;i<frozen.size();i+=4) {
        const auto point=base(frozen,i)-glm::vec3(0,.028f,0);
        check(std::abs(point.y)<.0001f,"Splash base missed first surface");
        bool traced=false;for (const auto hit : hits) if (glm::length(point-hit)<.0001f) traced=true;
        check(traced,"Splash was not attached to a falling-drop trace");
    }
    const auto repeated=solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(repeated.newImpacts==0&&solid.impactVertices().size()==frozen.size(),"Repeated rendering spawned contacts");
    for (std::size_t i=0;i<frozen.size();++i) {
        const auto& a=frozen[i];const auto& b=solid.impactVertices()[i];
        check(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.a==b.a&&a.u==b.u,"Paused contact animation advanced");
    }
    const auto moved=solid.build(e,{.05f,3,0},plane(0),settings,&motion,&bands);
    check(moved.newImpacts==0,"Camera movement spawned contacts at frozen time");
    std::size_t anchored=0;
    for (std::size_t i=0;i<frozen.size();i+=4) for (std::size_t j=0;j<solid.impactVertices().size();j+=4)
        if (glm::length(base(frozen,i)-base(solid.impactVertices(),j))<.00001f) ++anchored;
    check(anchored>first.impacts/2,"Contact followed the camera");
    e.precipitationSeconds=.01;motion.travel={.2,-.165,0};e.precipitationWind={80,0};
    solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    bool retained=false;
    for (std::size_t i=0;i<frozen.size();i+=4) for (std::size_t j=0;j<solid.impactVertices().size();j+=4)
        if (glm::length(base(frozen,i)-base(solid.impactVertices(),j))<.00001f) retained=true;
    check(retained,"A landed splash moved with changing wind");
    e.precipitationSeconds=.005;
    const auto fixedView=solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(fixedView.newImpacts==0,"An older presentation remainder reset/spawned contacts");
    retained=false;
    for (std::size_t i=0;i<frozen.size();i+=4) for (std::size_t j=0;j<solid.impactVertices().size();j+=4)
        if (glm::length(base(frozen,i)-base(solid.impactVertices(),j))<.00001f) retained=true;
    check(retained,"Switching to the fixed view lost landed contacts");
    e.precipitationSeconds=0;e.precipitationWind={0,0};motion={};
    RainVolume water;prepare(water);
    const auto waterStats=water.build(e,{0,3,0},plane(0,1),settings,&motion,&bands);
    bounds(water,waterStats);
    check(waterStats.waterImpacts>0&&waterStats.solidImpacts==0&&waterStats.impactQuads>waterStats.impacts,
        "Water did not add distinct ripples");
    bool ripple=false;
    for (std::size_t i=0;i<water.impactVertices().size();i+=4) if (water.impactVertices()[i].u>=.5f) {
        ripple=true;
        for (std::size_t j=i;j<i+4;++j) check(std::abs(water.impactVertices()[j].y-.028f)<.0001f,"Ripple left water plane");
    }
    check(ripple,"Water ripple atlas frames absent");
    RainVolume elevated;prepare(elevated);
    const auto elevatedStats=elevated.build(e,{0,7,0},plane(0),settings,&motion,&bands);
    check(elevatedStats.impacts>0,"Elevated battle camera lost ground contacts");
    RainVolume sloped;prepare(sloped);
    const auto slopeStats=sloped.build(e,{0,3,0},plane(0,0,.2f),settings,&motion,&bands);
    check(slopeStats.impacts>0,"Sloped contact splashes absent");
    for (std::size_t i=0;i<sloped.impactVertices().size();i+=4) for (std::size_t j=i+2;j<i+4;++j) {
        const auto& v=sloped.impactVertices()[j];
        check(std::abs((v.y-.2f*v.x)/std::sqrt(1.04f)-.028f)<.0001f,"Splash base did not follow surface normal");
    }
    RainVolume roof;prepare(roof);const auto sheltered=roof.build(e,{0,3,0},plane(8),settings,&motion,&bands);
    check(sheltered.covered>0,"Roof failed to cover near rain");
    for (const auto& v : roof.impactVertices()) check(v.y>=8.027f,"Splash leaked beneath first roof surface");
    RainVolume invalid;prepare(invalid);
    check(invalid.build(e,{0,3,0},plane(0,std::numeric_limits<float>::quiet_NaN()),settings,&motion,&bands).impacts==0,
        "Invalid water metadata admitted contacts");
    settings.impactRateScale=4;settings.impactLifetime=1;
    for (int tick=0;tick<180;++tick) {
        e.precipitationSeconds=tick/60.;motion.travel.y=-16.5*e.precipitationSeconds/.75;
        bounds(water,water.build(e,{0,3,0},plane(0,1),settings,&motion,&bands));
    }
    e.precipitationSeconds+=2;motion.travel.y-=44;
    check(water.build(e,{0,3,0},[](auto,auto)->std::optional<RainHit> {return {};},settings,&motion,&bands).impacts==0,
        "Contacts survived their accepted-time lifetime");
    e.rainIntensity=0;
    const auto dry=solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(dry.queries==0&&dry.impacts==0&&solid.impactVertices().empty(),"Dry weather retained contacts");
    e.rainIntensity=1;e.precipitationSeconds=0;motion={};settings={};
    const auto restarted=solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(restarted.impacts==first.impacts&&restarted.newImpacts==first.newImpacts,"Reset did not reproduce initial contacts");
    ++motion.generation;
    const auto explicitReset=solid.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(explicitReset.newImpacts==first.newImpacts,"Owner generation did not reset frozen contact state");
    RainVolume wrapped;prepare(wrapped);
    motion.generation=2;e.precipitationSeconds=.01;
    const auto afterWrap=wrapped.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(afterWrap.impacts>0,"Clock-wrap fixture has no contacts");
    const auto wrapContacts=std::vector(wrapped.visibleImpacts().begin(),wrapped.visibleImpacts().end());
    motion.generation=1;e.precipitationSeconds=8191.99;
    const auto beforeWrap=wrapped.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(beforeWrap.newImpacts==0&&beforeWrap.impacts==afterWrap.impacts,
        "Older fixed view reset contacts across a presentation clock wrap");
    ++e.precipitationSeed;
    const auto oldSeed=wrapped.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(oldSeed.newImpacts==0&&oldSeed.impacts==afterWrap.impacts,
        "Older generation with another seed replaced newer contacts");
    e.rainIntensity=0;
    check(wrapped.build(e,{0,3,0},plane(0),settings,&motion,&bands).impacts==0,
        "Older dry view rendered precipitation");
    --e.precipitationSeed;e.rainIntensity=1;
    motion.generation=2;e.precipitationSeconds=.01;
    const auto wrapRepeat=wrapped.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(wrapRepeat.newImpacts==0&&wrapRepeat.impacts==afterWrap.impacts,
        "Returning to interpolated view respawned clock-wrap contacts");
    for (std::size_t i=0;i<wrapContacts.size();++i) {
        const auto& a=wrapContacts[i];const auto& b=wrapped.visibleImpacts()[i];
        check(a.id==b.id&&a.age==b.age&&a.point==b.point,"Clock-wrap view switching changed retained contacts");
    }
    bool rejected=false;
    try { solid.setImpactAtlas("bad",8,8,std::vector<unsigned char>(12)); } catch (const std::runtime_error&) { rejected=true; }
    check(rejected,"Invalid impact atlas accepted");
    std::cout<<"PASS trajectory contacts, solid/water animation, normals, roofs, pause/camera/wind, lifetimes/reset and bounded work\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
