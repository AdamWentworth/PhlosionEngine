#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <stdexcept>
namespace engine::render {
struct WorldCloudSettings {
    float bottom=400,top=1000,horizonCoverage=1,density=1;
    float coverageScale=1.3f,noiseScale=1.1f,detailScale=.75f;
    float shadowIntensity=1,worldShadowIntensity=1,lightIntensity=1,ambientIntensity=1;
    float inScatter=.2f,outScatter=.1f,inVsOut=.5f,silverLining=.8f,ambientOutscatter=.9f;
    float rayStart=25,rayFalloff=50,distantFogOpacity=1;
    std::array<float,2> coverageOffset{},noiseOffset{},detailOffset{};
};
struct WorldCloudBudget {
    unsigned downsample=2,steps=48,shadowSteps=4,shadowResolution=512;
    float stepScale=1.33f,shadowStepScale=2;
    bool enabled=true;
};
inline WorldCloudBudget worldCloudBudget(unsigned quality) {
    if(quality==0)return {2,0,0,512,1.33f,2,false};
    if(quality==1)return {};
    if(quality==2)return {2,64,6,512,1,2,true};
    if(quality==3)return {2,128,6,512,.5f,2,true};
    throw std::invalid_argument("Invalid cloud quality");
}
using WorldCloudRows=std::array<std::array<float,4>,72>;
using WorldCloudUniforms=std::array<std::array<float,4>,12>;
// Immutable authored inputs. Four weather channels and a 64-slice RGBA noise
// atlas; apps may replace them with prepared textures using the same semantics.
struct WorldCloudAssets {
    std::vector<unsigned char> weather,noise;
    WorldCloudAssets();
};
void validateCloudSettings(const WorldCloudSettings&);
}
