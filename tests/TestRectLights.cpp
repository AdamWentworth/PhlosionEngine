#include "engine/render/WorldEnvironmentLighting.h"
#include "engine/render/AreaLightLut.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
float half(std::uint16_t h) {
    const int exponent=(h>>10)&31, fraction=h&1023;
    check(exponent!=31,"Nonfinite LTC coefficient");
    return (h&32768?-1.f:1.f)*std::ldexp(exponent?1.f+fraction/1024.f:fraction/1024.f,
        exponent?exponent-15:-14);
}
}
int main() try {
    WorldEnvironmentLighting e;
    e.rectLights[15]={{1,4,-2},12,{1,0,0},{0,0,.25f},{12,2,4}};
    check(packWorldRectLights(e)==WorldRectLightUniforms{},"Disabled environment retained emitter");
    e.enabled=true;
    const auto packet=packWorldRectLights(e);
    check(packet[60]==std::array<float,4>{1,4,-2,12}&&packet[63]==std::array<float,4>{12,2,4,0},"Last emitter truncated");
    check(packet[0]==std::array<float,4>{},"Empty slot retained light");
    backend::WorldTextureData material;material.environment=e;
    check(vulkan_backend::makeWorldViewState(&material).rectLights==packet,"View packing changed rectangle");
    for (int test=0;test<6;++test) {
        auto invalid=e;auto& light=invalid.rectLights[15];
        if(test==0) light.halfU={};
        if(test==1) light.halfV={1,0,0};
        if(test==2) light.range=-1;
        if(test==3) light.position[0]=std::numeric_limits<float>::quiet_NaN();
        if(test==4) light.radiance[0]=-1;
        if(test==5) light.halfU[0]=std::numeric_limits<float>::infinity();
        check(packWorldRectLights(invalid)==WorldRectLightUniforms{},"Malformed emitter reached shader");
    }
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const auto i=static_cast<std::size_t>((y*128+x)*4);
        const float a=half(areaLightLut[i]),b=half(areaLightLut[i+1]);
        const float c=half(areaLightLut[i+2]),d=half(areaLightLut[i+3]);
        check(a*d-b*c>0,"LTC transform is singular or reversed");
        const float magnitude=half(areaLightLut[i+256]),fresnel=half(areaLightLut[i+257]);
        check(magnitude>=0&&magnitude<=1.001f&&fresnel>=0&&fresnel<=magnitude+.001f,"Invalid LTC energy");
    }
    std::cout<<"PASS rectangle capacity, disable, validation, view layout and 4096 LTC transforms\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
