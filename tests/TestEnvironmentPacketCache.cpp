#include "engine/render/WorldEnvironmentPacketCache.h"
#include "engine/render/WorldHeatHaze.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine::render;
namespace {
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void compare(WorldEnvironmentPacketCache& cache, const WorldEnvironmentLighting& input) {
    const auto& cached = cache.resolve(input);
    const auto environment = packWorldEnvironment(input);
    const auto reflection = packWorldReflectionResponse(input);
    auto sky=environment[0][3]>.5f?packSkyModel(input.sky,input.sunDirection,input.timeOfDayHours,input.sky.indirect):WorldSkyUniforms{};
    if(environment[0][3]>.5f)sky[21]={input.globalSpecularTint[0],input.globalSpecularTint[1],input.globalSpecularTint[2],1};
    check(cached.sky==sky,"Stale or invalid sky snapshot");
    const auto points = packWorldPointLights(input);
    check(cached.puddles==packWorldPuddles(input),"Stale puddle receiver map");
    const auto rects = packWorldRectLights(input);
    check(std::memcmp(cached.environment.data(), environment.data(), sizeof(environment)) == 0, "Stale environment");
    check(std::memcmp(cached.reflectionResponse.data(), reflection.data(), sizeof(reflection)) == 0, "Stale receiver response");
    check(std::memcmp(cached.pointLights.data(), points.data(), sizeof(points)) == 0, "Stale point lights");
    check(std::memcmp(cached.rectLights.data(), rects.data(), sizeof(rects)) == 0, "Stale area lights");
}
}
int main() try {
    WorldHeatHazeNoise heatNoise;heatNoise.key="contract";heatNoise.width=heatNoise.height=heatNoise.layers=2;
    heatNoise.rgba.resize(32,128);heatNoise.validate();
    WorldTemporalView heatView{1,1,1,320,240,false,1.f/60.f};heatView.cameraValid=true;heatView.cameraPosition={1,2,3};
    WorldHeatHazeSettings heat{.4f,60,500,1,1,2};
    const auto heatParameters=worldHeatHazeParameters(heat,heatView,heatNoise);
    check(heatParameters[16]==1&&heatParameters[17]==-3&&heatParameters[18]==2,"Heat camera basis");
    check(heatParameters[23]==.1f&&heatParameters[24]==30,"Heat authoring conversion");
    for(int invalid=0;invalid<4;++invalid){
        bool rejected=false;auto h=heat;auto n=heatNoise;auto v=heatView;
        if(invalid==0)h.maxDistance=h.startDistance;
        if(invalid==1)n.rgba.pop_back();
        if(invalid==2)v.cameraValid=false;
        if(invalid==3)h.seconds=std::numeric_limits<float>::quiet_NaN();
        try{(void)worldHeatHazeParameters(h,v,n);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid heat input accepted");
    }
    WorldEnvironmentPacketCache tintCache;
    WorldEnvironmentLighting tinted;tinted.enabled=true;
    tinted.globalSpecularTint={.39f,.4614f,.6f};compare(tintCache,tinted);
    tinted.globalSpecularTint={1,1,1};compare(tintCache,tinted);
    check(tintCache.packedSnapshots()==2,"Directional tint did not invalidate packet");
    for(float bad:{-1.f,65.f,std::numeric_limits<float>::quiet_NaN()}) {
        tinted.globalSpecularTint[1]=bad;bool rejected=false;
        try{(void)tintCache.resolve(tinted);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid directional tint accepted");
    }
    WorldEnvironmentPacketCache channelCache;
    WorldEnvironmentLighting channelInput;channelInput.enabled=true;
    channelInput.rectLights[0]={{0,2,0},10,{1,0,0},{0,0,1},{1,2,3}};
    for(unsigned channel=0;channel<256;++channel){channelInput.lightChannels=channel;compare(channelCache,channelInput);}
    check(channelCache.packedSnapshots()==1,"Receiver masks repacked the shared light collection");
    auto another=channelInput;another.exposure=2;compare(channelCache,another);
    channelInput.lightChannels=19;compare(channelCache,channelInput);
    check(channelCache.packedSnapshots()==2,"Revisited receiver mask repacked shared lights");
    static_assert(std::is_trivially_copyable_v<WorldEnvironmentLighting>);
    WorldEnvironmentPacketCache cache;
    WorldEnvironmentLighting e;
    compare(cache,e);
    check(&cache.resolve(e)==&cache.resolve(e),"Identical snapshot was not reused");
    check(cache.resolve(nullptr).environment==WorldEnvironmentUniforms{},"Missing input changed defaults");
    e.enabled=true;e.surfaceWeatherEnabled=true;e.reflectionResponseWeight=1;
    e.cloudCoverage=.6f;e.localHazeAlbedo=.4f;
    for(auto& l:e.pointLights){l.radius=8;l.intensity={3,2,1};}
    for(auto& l:e.rectLights){l.range=10;l.halfU={1,0,0};l.halfV={0,0,1};l.radiance={2,3,4};}
    compare(cache,e);
    e.surfaceWetness=e.puddleAmount=1;
    e.puddles[0]={{3,2,-1},{2,1},{1,0}};
    compare(cache,e);
    check(packWorldPuddles(e)[0]==std::array<float,4>{3,-1,2,1},"Puddle center packing");
    check(packWorldPuddles(e)[1]==std::array<float,4>{.5f,1,1,0},"Puddle shape packing");
    check(worldPuddleCoverage(e.puddles[0],{3,2,-1},1)==1,"Puddle center missing");
    check(worldPuddleCoverage(e.puddles[0],{3,2.1f,-1},1)==0,"Puddle leaked onto raised geometry");
    check(worldPuddleCoverage(e.puddles[0],{3,2,-1},.8f)==0,"Puddle leaked onto slope");
    check(worldPuddleCoverage(e.puddles[0],{6,2,-1},1)==0,"Puddle escaped footprint");
    for(float value:{0.f,-1.f,std::numeric_limits<float>::infinity()}) {
        auto invalid=e;invalid.puddles[0].extent[0]=value;
        compare(cache,invalid);
        check(packWorldPuddles(invalid)[0][3]==0,"Invalid puddle admitted");
    }
    auto disabled=e;disabled.surfaceWeatherEnabled=false;
    check(packWorldPuddles(disabled)==WorldPuddleUniforms{},"Disabled material retained puddles");
    disabled=e;disabled.puddleAmount=0;
    check(packWorldPuddles(disabled)==WorldPuddleUniforms{},"Dry material retained puddles");
    for(std::size_t i=0;i<worldPuddleCapacity;++i) {
        e.puddles[i]={{float(i),0,0},{1,2},{0,1}};compare(cache,e);
        e.puddles[i].center[1]=.5f;compare(cache,e);
    }
    e.sky.turbidity=6;compare(cache,e);e.sky.indirectTurbidity=2;compare(cache,e);
    e.sky.indirect=true;compare(cache,e);e.timeOfDayHours=19;compare(cache,e);
    const auto base=e;
    for(float* value:{&e.sunDirection[0],&e.sunRadiance[1],&e.ambientRadiance[2],&e.exposure,
                     &e.hazeRadiance[0],&e.hazeDensity,&e.hazeBaseHeight,&e.hazeHeightFalloff,&e.hazeMaxOpacity,
                     &e.localHazeAlbedo,&e.localHazeAnisotropy,&e.distantHazeStart,&e.distantHazeDensity,&e.distantHazeMaxOpacity,
                     &e.skyIntensity,&e.nightSkyRadiance[1],&e.sunAngularRadius,&e.sunHaloStrength,&e.sunHaloWidth,
                     &e.moonDirection[1],&e.moonRadiance[2],&e.moonAngularRadius,&e.moonPhase,&e.starIntensity,
                     &e.cloudCoverage,&e.cloudDensity,&e.cloudOffsets[2],&e.cloudTint[0],&e.cloudLightResponse,
                     &e.rainIntensity,&e.surfaceWetness,&e.puddleAmount,&e.surfaceAbsorption,
                     &e.reflectionResponse[0],&e.reflectionResponseWeight}) {
        e=base;*value+=.125f;compare(cache,e);
        *value=std::numeric_limits<float>::quiet_NaN();compare(cache,e);
        e=base;compare(cache,e);
    }
    for(bool* value:{&e.enabled,&e.skyDraw,&e.localHazeDeferred,&e.surfaceWeatherEnabled,&e.waterSurface,&e.reflectionAlbedoF90}) {
        e=base;*value=!*value;compare(cache,e);
    }
    e=base;e.precipitationSeconds=19;compare(cache,e);
    e.precipitationSeconds=std::numeric_limits<double>::infinity();compare(cache,e);
    for(unsigned slot=0;slot<e.rectLights.size();++slot) {
        e=base;e.rectLights[slot].radiance[0]=.25f;compare(cache,e);
        e.rectLights[slot].halfU[0]=2;compare(cache,e);
        e.rectLights[slot].response=WorldRectLightResponse::FiniteLine;compare(cache,e);
        e.rectLights[slot].shadowFilter=WorldLocalShadowFilter::DistanceMoments;compare(cache,e);
        e.rectLights[slot].response=WorldRectLightResponse::FiniteSource;compare(cache,e);
        e.rectLights[slot].source.length=1;compare(cache,e);
        e.rectLights[slot].source.intensityScale=.25f;compare(cache,e);
        e.rectLights[slot].range=-1;compare(cache,e);
    }
    for(unsigned slot=0;slot<e.pointLights.size();++slot) {
        e=base;e.pointLights[slot].position[1]=5;compare(cache,e);
        e.pointLights[slot].intensity[0]=9;compare(cache,e);
        e.pointLights[slot].source.cone=true;e.pointLights[slot].source.axis={0,-1,0};compare(cache,e);
        e.pointLights[slot].source.intensityScale=.25f;compare(cache,e);
        e.pointLights[slot].radius=-1;compare(cache,e);
        e=base;e.weatherShelters[slot]={{-2,-3,4,5},7};compare(cache,e);
    }
    e=base;e.sunRadiance[0]=0.f;compare(cache,e);
    e.sunRadiance[0]=-0.f;compare(cache,e);
    for(unsigned i=0;i<WorldEnvironmentPacketCache::capacity*3;++i) {e=base;e.exposure=1.f+i*.1f;compare(cache,e);}
    compare(cache,base);
    std::cout<<"PASS environment packets: exact fresh equivalence, mutation, invalid inputs, bounds and reuse\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
