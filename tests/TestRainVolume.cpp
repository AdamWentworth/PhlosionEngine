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
    std::cout<<"PASS bounded deterministic rain, cover, camera anchoring, loop and invalid inputs\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
