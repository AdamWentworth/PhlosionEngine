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
void prepare(RainVolume& rain,bool spray=false) {
    const int columns=spray?12:8;
    std::vector<unsigned char> streak(8*3*16*4,255),impact(8*columns*8*4,255);
    rain.setStreakAtlas("impact-test-streak",8,16,streak);
    rain.setImpactAtlas("impact-test-atlas",8,8,impact,columns);
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
    check(s.impacts<=81&&s.newImpacts<=81&&s.impactQuads<=RainVolume::maxImpactQuads&&s.queries<=RainVolume::maxQueries,"Impact work exceeded declared caps");
    check(s.sprayQuads<=RainVolume::maxSprayQuads && s.glowQuads<=81 && s.mistQuads<=RainVolume::maxMistQuads && s.impactQuads+s.sprayQuads+s.glowQuads<=RainVolume::maxContactQuads,"Unbounded contact/mist work");
    check(s.solidImpacts+s.waterImpacts==s.impacts&&s.impactQuads+s.sprayQuads+s.glowQuads>=s.impacts&&s.impactQuads<=s.impacts*21,
        "Impact event and quad accounting disagree");
    check(rain.impactVertices().size()+rain.sprayVertices().size()==(s.impactQuads+s.sprayQuads+s.glowQuads)*4,"Impact geometry count disagrees");
    check(rain.mistVertices().size()==s.mistQuads*4,"Mist geometry count disagrees");
    for (const auto& v : rain.impactVertices()) check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
        v.a>0&&v.a<=1&&v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1,"Unsafe impact geometry");
}
void mistContracts() {
    WorldEnvironmentLighting e;e.enabled=true;e.rainIntensity=1;e.precipitationSeed=417;
    e.ambientRadiance={.3f,.3f,.3f};RainMotion motion{};motion.generation=1;
    auto bands=layers();RainVisualSettings s;s.mistOpacity=.5f;s.glowOpacity=.2f;s.sprayOpacity=.2f;
    RainVolume rain;prepare(rain);
    rain.setImpactAtlas("native-contact",8,8,std::vector<unsigned char>(8*16*8*4,255),16);
    bool rejected=false;
    try {rain.build(e,{0,3,0},plane(0),s,&motion,&bands);} catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Missing mist texture silently accepted");
    rejected=false;
    try {rain.setMistTexture("bad",8,8,std::vector<unsigned char>(5));} catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Malformed mist texture accepted");
    rain.setMistTexture("native-mist",8,8,std::vector<unsigned char>(8*8*4,255));
    RainVolume mistOnly;
    mistOnly.setStreakAtlas("mist-only-streak",8,16,std::vector<unsigned char>(8*3*16*4,255));
    mistOnly.setMistTexture("mist-only",8,8,std::vector<unsigned char>(8*8*4,255));
    auto mistSettings=s;mistSettings.glowOpacity=mistSettings.sprayOpacity=0;
    check(mistOnly.build(e,{0,3,0},plane(0),mistSettings,&motion,&bands).mistQuads>0,
        "Independent mist incorrectly requires a contact atlas");
    const auto first=rain.build(e,{0,3,0},plane(0),s,&motion,&bands);bounds(rain,first);
    check(first.mistQuads>0&&first.glowQuads>0,"Native mist/glow fixtures empty");
    check(first.mistQuads%16==0 && first.sprayQuads%8==0,"Density integration slice counts changed");
    {
        RainVolume close;prepare(close);
        close.setImpactAtlas("spatial-contact",8,8,std::vector<unsigned char>(8*16*8*4,255),16);
        auto settings=s;settings.mistOpacity=settings.sprayOpacity=0;
        auto ground=plane(0);
        const auto isolated=[&](glm::vec3 p,glm::vec3 d)->std::optional<RainHit> {
            auto hit=ground(p,d);
            if(hit && std::abs(hit->point.x)<4 && std::abs(hit->point.z)<4) return hit;
            return {};
        };
        const auto counts=close.build(e,{0,3,0},isolated,settings,&motion,&bands);
        const auto vertices=std::vector(close.impactVertices().begin(),close.impactVertices().end());
        check(counts.impacts>0 && counts.impactQuads>counts.impacts*2,"Spatial crown/droplets absent");
        close.build(e,{2,3,0},isolated,settings,&motion,&bands);
        check(vertices.size()==close.impactVertices().size(),"Orbit changed fixed spatial impact topology");
        for(std::size_t i=0;i<vertices.size();++i) {
            const auto& a=vertices[i];const auto& b=close.impactVertices()[i];
            check(a.x==b.x&&a.y==b.y&&a.z==b.z,"Impact silhouette still rotates to face the camera");
        }
    }
    auto frozen=std::vector(rain.mistVertices().begin(),rain.mistVertices().end());
    rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    check(rain.mistVertices().size()==frozen.size(),"Paused mist count changed");
    for(std::size_t i=0;i<frozen.size();++i) {
        const auto& a=frozen[i];const auto& b=rain.mistVertices()[i];
        check(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.a==b.a,"Paused mist moved");
    }
    s.mistOpacity=.25f;rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    check(rain.mistVertices().size()==frozen.size(),"Mist opacity changed population abruptly");
    for(std::size_t i=0;i<frozen.size();++i) check(std::abs(rain.mistVertices()[i].a-frozen[i].a*.5f)<1e-6f,"Mist opacity not continuous");
    s.mistOpacity=.5f;
    e.rainIntensity=100;rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    for(const auto& v:rain.mistVertices()) check(v.a<=s.mistOpacity,"Over-range weather intensity amplified mist");
    e.rainIntensity=1;
    // Mist continues without new falling-drop contacts, well beyond their lifetime.
    for(auto& b:bands.bands) b.density=0;
    e.precipitationSeconds=2;motion.travel={1,-44,0};
    const auto independent=rain.build(e,{0,3,0},plane(0),s,&motion,&bands);bounds(rain,independent);
    check(independent.impacts==0&&independent.mistQuads>0,"Mist depends on live contacts");
    check(rain.mistVertices()[0].x!=frozen[0].x||rain.mistVertices()[0].a!=frozen[0].a,"Mist failed to drift/change over time");
    const auto roof=rain.build(e,{0,3,0},plane(8,0,.1f),s,&motion,&bands);bounds(rain,roof);
    for(std::size_t i=0;i<rain.mistVertices().size();i+=4) {
        const auto& a=rain.mistVertices()[i];const auto& b=rain.mistVertices()[i+2];
        const float localY=(a.sourceUv1V+b.sourceUv1V)*.5f;
        if (localY>=-1 && localY<=1) check((a.y+b.y)*.5f-8-.1f*(a.x+b.x)*.5f>.039f,"Mist support leaked beneath first roof/slope");
    }
    check(rain.build(e,{0,3,0},[](auto,auto)->std::optional<RainHit>{return {};},s,&motion,&bands).mistQuads==0,"Mist invented missing ground");
    e.rainIntensity=0;const auto dry=rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    check(dry.mistQuads==0&&dry.queries==0&&rain.mistVertices().empty(),"Dry mist retained work");
    e.rainIntensity=1;e.precipitationSeconds=0;motion={};motion.generation=2;bands=layers();
    rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    check(rain.mistVertices().size()==frozen.size(),"Mist reset changed population");
    for(std::size_t i=0;i<frozen.size();++i) check(rain.mistVertices()[i].x==frozen[i].x&&rain.mistVertices()[i].a==frozen[i].a,"Mist reset not deterministic");
    motion.generation=1;motion.travel={200,-200,100};e.precipitationSeconds=8191.9;
    rain.build(e,{0,3,0},plane(0),s,&motion,&bands);
    for(std::size_t i=0;i<frozen.size();++i) check(rain.mistVertices()[i].x==frozen[i].x&&rain.mistVertices()[i].a==frozen[i].a,"Older view rewound mist");
}
void sprayContracts() {
    WorldEnvironmentLighting e;e.enabled=true;e.rainIntensity=1;e.precipitationSeed=417;
    e.ambientRadiance={.3f,.3f,.3f};RainMotion motion{};motion.generation=1;
    const auto bands=layers();RainVisualSettings settings;
    settings.sprayOpacity=.2f;settings.sprayRadius=.6f;settings.sprayHeight=.32f;
    RainVolume rain;prepare(rain,true);
    const auto stats=rain.build(e,{0,3,0},plane(0,1),settings,&motion,&bands);bounds(rain,stats);
    check(stats.sprayQuads>0 && stats.waterImpacts>0,"Contact spray or water ripples absent");
    auto noSpray=settings;noSpray.sprayOpacity=0;
    RainVolume control;prepare(control,true);
    const auto baseline=control.build(e,{0,3,0},plane(0,1),noSpray,&motion,&bands);
    check(baseline.sprayQuads==0 && baseline.queries==stats.queries && baseline.drops==stats.drops &&
        baseline.impactQuads==stats.impactQuads,"Spray changed drop/query/ripple budgets");
    const auto geometry=std::vector(rain.impactVertices().begin(),rain.impactVertices().end());
    const auto repeated=rain.build(e,{0,3,0},plane(0,1),settings,&motion,&bands);
    check(repeated.newImpacts==0 && rain.impactVertices().size()==geometry.size(),"Frozen spray spawned contacts");
    for (std::size_t i=0;i<geometry.size();++i) {
        const auto& a=geometry[i];const auto& b=rain.impactVertices()[i];
        check(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.a==b.a&&a.u==b.u,"Frozen spray changed geometry");
    }
    const auto sprayEnergy=[](const RainVolume& volume) {
        double sum=0;for (const auto& v:volume.impactVertices()) if (v.u>=8.f/12) sum+=v.a;return sum;
    };
    const double heavy=sprayEnergy(rain);settings.sprayOpacity=.1f;
    rain.build(e,{0,3,0},plane(0,1),settings,&motion,&bands);
    check(sprayEnergy(rain)>heavy*.45 && sprayEnergy(rain)<=heavy*.501,"Spray strength did not blend continuously");
    settings.sprayOpacity=.2f;
    for (const float slope : {0.f,.2f}) {
        RainVolume roof;prepare(roof,true);
        const auto covered=roof.build(e,{0,3,0},plane(8,0,slope),settings,&motion,&bands);bounds(roof,covered);
        check(covered.covered>0 && covered.sprayQuads>0,"Roof/slope spray fixture empty");
        for (const auto& v:roof.impactVertices())
            check((v.y-8-slope*v.x)/std::sqrt(1+slope*slope)>.027f,"Spray penetrated first roof/slope");
    }
    settings.impactRateScale=4;settings.impactLifetime=1;
    for (int tick=1;tick<=180;++tick) {
        e.precipitationSeconds=tick/60.;motion.travel.y=-22*e.precipitationSeconds;
        bounds(rain,rain.build(e,{0,3,0},plane(0,1),settings,&motion,&bands));
    }
    e.precipitationSeconds+=2;
    check(rain.build(e,{0,3,0},[](auto,auto)->std::optional<RainHit>{return {};},settings,&motion,&bands).sprayQuads==0,
        "Spray outlived its contact");
    e.rainIntensity=0;const auto dry=rain.build(e,{0,3,0},plane(0),settings,&motion,&bands);
    check(dry.queries==0 && dry.sprayQuads==0 && rain.impactVertices().empty(),"Dry spray retained work");
    e.rainIntensity=1;e.enabled=false;
    check(rain.build(e,{0,3,0},plane(0),settings,&motion,&bands).sprayQuads==0,"Disabled/sheltered spray visible");
    RainVolume oldAtlas;prepare(oldAtlas);bool rejected=false;
    try { oldAtlas.build(e,{0,3,0},plane(0),settings,&motion,&bands); } catch(const std::runtime_error&) {rejected=true;}
    check(rejected,"Spray silently used an atlas without spray variants");
}
}
int main() try {
    mistContracts();
    sprayContracts();
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
