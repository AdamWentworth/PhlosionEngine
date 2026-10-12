#include "engine/render/RainVolume.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine::render;
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
RainCoverQuery plane(float height) {
    return [height](glm::vec3 p,glm::vec3 d)->std::optional<RainHit> {
        const float t=(height-p.y)/d.y;
        if (t<0||t>1) return {};
        return RainHit{p+d*t,{0,1,0},t};
    };
}
int main() try {
    WorldEnvironmentLighting e;e.enabled=true;e.rainIntensity=1;e.precipitationSeed=417;
    e.precipitationWind={18,5};e.precipitationSeconds=12;e.ambientRadiance={.3f,.3f,.3f};
    RainVolume rain;
    auto stats=rain.build(e,{0,3,0},plane(0));
    check(stats.drops>0&&stats.impacts>0&&stats.covered>0,"Missing rain or ground response");
    check(stats.drops<=RainVolume::maxDrops&&stats.impacts<=RainVolume::maxImpacts&&
        stats.queries<=RainVolume::maxDrops+RainVolume::maxImpacts,"Unbounded rain work");
    for (const auto& v : rain.dropVertices()) check(v.y>=-.001f,"Rain passed through ground");
    const auto original=std::vector(rain.dropVertices().begin(),rain.dropVertices().end());
    const auto originalImpacts=std::vector(rain.impactVertices().begin(),rain.impactVertices().end());
    RainVisualSettings steady;
    rain.build(e,{0,3,0},plane(0),steady);
    check(original.size()==rain.dropVertices().size() && originalImpacts.size()==rain.impactVertices().size(),"Steady settings changed population");
    for (std::size_t i=0;i<original.size();++i) check(original[i].x==rain.dropVertices()[i].x &&
        original[i].a==rain.dropVertices()[i].a,"Explicit steady settings changed geometry");
    auto light=steady;light.densityScale=.4f;
    const auto lightStats=rain.build(e,{0,3,0},plane(0),light);
    check(lightStats.drops<stats.drops && lightStats.drops>0,"Density setting did not affect population");
    light.densityScale=0;
    check(rain.build(e,{0,3,0},plane(0),light).queries==0,"Zero profile density retained cover work");
    auto shallow=steady;shallow.fallSpeed=1;
    check(invalidRainSetting(shallow)=="wind_response","Shallow cover trajectory accepted");
    for (const auto& field : rainSettingFields) {
        auto invalid=steady;invalid.*field.member=std::numeric_limits<float>::quiet_NaN();
        check(invalidRainSetting(invalid)==field.name,"Non-finite setting accepted");
    }
    auto invalid=steady;invalid.impactLifetime=0;
    bool rejected=false;
    try { rain.build(e,{0,3,0},plane(0),invalid); } catch (const std::runtime_error&) { rejected=true; }
    check(rejected,"Invalid settings silently rendered");
    RainMotion motion{glm::dvec3(4.5,-22,1.25)*e.precipitationSeconds,e.precipitationSeconds};
    rain.build(e,{0,3,0},plane(0),steady,&motion);
    check(original.size()==rain.dropVertices().size(),"Integrated baseline changed population");
    for (std::size_t i=0;i<original.size();++i) check(original[i].x==rain.dropVertices()[i].x &&
        original[i].y==rain.dropVertices()[i].y,"Integrated baseline changed positions");
    rain.build(e,{0,3,0},plane(0));
    check(original.size()==rain.dropVertices().size(),"Seed is not deterministic");
    for (std::size_t i=0;i<original.size();++i) check(original[i].x==rain.dropVertices()[i].x&&
        original[i].y==rain.dropVertices()[i].y&&original[i].a==rain.dropVertices()[i].a,"Rain changed while frozen");
    // Camera motion changes the bounded selection/fade, never an existing drop's world position.
    rain.build(e,{.05f,3,0},plane(0));
    bool shared=false;
    for (std::size_t i=0;i<original.size();i+=4) for (std::size_t j=0;j<rain.dropVertices().size();j+=4) {
        const float ax=(original[i].x+original[i+1].x)*.5f;
        const float bx=(rain.dropVertices()[j].x+rain.dropVertices()[j+1].x)*.5f;
        const float ay=(original[i].y+original[i+1].y)*.5f;
        const float by=(rain.dropVertices()[j].y+rain.dropVertices()[j+1].y)*.5f;
        if (std::abs(ax-bx)<.00001f && std::abs(ay-by)<.00001f) shared=true;
    }
    check(shared,"Rain follows camera translation");
    rain.build(e,{0,3,0},plane(8));
    for (const auto& v : rain.dropVertices()) check(v.y>=7.999f,"Rain passed through roof");
    for (const auto& v : rain.impactVertices()) check(v.y>8,"Impacts appeared underneath roof");
    auto before=e;before.precipitationSeconds=8192;
    auto after=e;after.precipitationSeconds=0;
    rain.build(before,{0,3,0},plane(0));
    const auto loop=std::vector(rain.dropVertices().begin(),rain.dropVertices().end());
    const auto impactLoop=std::vector(rain.impactVertices().begin(),rain.impactVertices().end());
    rain.build(after,{0,3,0},plane(0));
    check(loop.size()==rain.dropVertices().size(),"Rain loop changed population");
    for (std::size_t i=0;i<loop.size();++i) check(std::abs(loop[i].x-rain.dropVertices()[i].x)<.0001f&&
        std::abs(loop[i].y-rain.dropVertices()[i].y)<.0001f,"Rain loop jumped world positions");
    check(impactLoop.size()==rain.impactVertices().size(),"Impact loop changed population");
    for (std::size_t i=0;i<impactLoop.size();++i) check(impactLoop[i].x==rain.impactVertices()[i].x&&
        impactLoop[i].a==rain.impactVertices()[i].a,"Impact loop jumped");
    e.rainIntensity=0;check(rain.build(e,{0,3,0},plane(0)).queries==0&&rain.dropVertices().empty(),"Dry weather retained rain");
    e.rainIntensity=1;e.enabled=false;check(rain.build(e,{0,3,0},plane(0)).queries==0,"Disabled world retained rain");
    e.enabled=true;e.precipitationSeconds=std::numeric_limits<double>::infinity();
    check(rain.build(e,{0,3,0},plane(0)).queries==0,"Non-finite rain time accepted");
    e.precipitationSeconds=0;
    check(rain.build(e,{std::numeric_limits<float>::max(),3,0},plane(0)).queries==0,"Unsafe cell conversion accepted");
    check(rain.build(e,{0,3,0},{}).queries==0,"Uncovered rain was silently enabled");
    std::vector<unsigned char> atlas(8*3*16*4,255);rain.setStreakAtlas("rain-test-atlas",8,16,atlas);
    RainLayerSettings layers;
    layers.bands={RainBandSettings{1,.45f,.65f,.9f,.75f,0,1,8,2,2},
        RainBandSettings{1,.75f,1,.85f,1,4,2,16,4,4},RainBandSettings{1,.45f,1.4f,.65f,1.25f,10,6,32,8,8}};
    RainMotion phase{};
    auto layered=rain.build(e,{0,3,0},plane(0),steady,&phase,&layers);
    check(layered.drops>0 && layered.bandDrops[0]>0 && layered.bandDrops[1]>0 && layered.bandDrops[2]>0,"Rain distance bands absent");
    check(layered.bandDrops[0]+layered.bandDrops[1]+layered.bandDrops[2]==layered.drops &&
        layered.queries<=1296,"Rain bands multiplied the candidate budget");
    const auto dense=std::vector(rain.dropVertices().begin(),rain.dropVertices().end());
    for (const auto& v : dense) check(std::isfinite(v.x)&&v.u>=0&&v.u<=1&&v.a<=1,"Invalid atlas geometry");
    rain.build(e,{.05f,3,0},plane(0),steady,&phase,&layers);
    bool worldAnchored=false;
    for(std::size_t i=0;i<dense.size();i+=4) for(std::size_t j=0;j<rain.dropVertices().size();j+=4) {
        const auto& a=dense[i];const auto& b=dense[i+1];const auto& c=rain.dropVertices()[j];const auto& d=rain.dropVertices()[j+1];
        if(std::abs(a.x+b.x-c.x-d.x)<.00001f&&std::abs(a.y+b.y-c.y-d.y)<.00001f&&
            std::abs(a.z+b.z-c.z-d.z)<.00001f) worldAnchored=true;
    }
    check(worldAnchored,"Rain bands follow camera translation");
    auto moderate=steady;moderate.densityScale=.58f;
    const auto moderateStats=rain.build(e,{0,3,0},plane(0),moderate,&phase,&layers);
    auto drizzle=steady;drizzle.densityScale=.22f;
    const auto drizzleStats=rain.build(e,{0,3,0},plane(0),drizzle,&phase,&layers);
    check(drizzleStats.drops<moderateStats.drops && moderateStats.drops*1.4<layered.drops,"Profile densities remain saturated");
    phase.travel={8192,8192,8192};rain.build(e,{0,3,0},plane(0),steady,&phase,&layers);
    check(dense.size()==rain.dropVertices().size(),"Layered motion repetition changed population");
    for(std::size_t i=0;i<dense.size();++i) check(dense[i].x==rain.dropVertices()[i].x&&
        dense[i].y==rain.dropVertices()[i].y&&dense[i].z==rain.dropVertices()[i].z,"Layered motion wrap jumped");
    phase={};rain.build(e,{0,3,0},plane(8),steady,&phase,&layers);
    for (const auto& v : rain.dropVertices()) check(v.y>=7.99f,"Layered rain leaked beneath roof");
    auto badBand=layers.bands[0];badBand.speed=.8f;
    check(invalidRainBand(badBand,0)=="speed","Non-periodic band speed accepted");
    e.rainIntensity=0;check(rain.build(e,{0,3,0},plane(0),steady,&phase,&layers).queries==0,"Dry layered rain retained work");
    std::cout<<"PASS bounded deterministic rain, cover, camera anchoring, loop and invalid inputs\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
