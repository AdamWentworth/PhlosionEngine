#include "engine/render/WorldRainMap.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include "engine/render/WorldWind.h"
#include <iostream>
using namespace engine::render;
void check(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
struct Target final:IWorldRainMap{
    bool open=false,lastClear=false;unsigned begins=0,ends=0,publishes=0;
    void prewarmWorldRainMap(WorldResourceScopeId,unsigned)override{}
    bool beginWorldRainMap(WorldResourceScopeId,const WorldDirectionalShadowView&,bool clear)override{check(!open,"nested map");open=true;lastClear=clear;++begins;return true;}
    bool endWorldRainMap()override{if(!open)return false;open=false;++ends;return true;}
    bool publishWorldRainMap(WorldResourceScopeId,unsigned)override{check(!open,"published while drawing");++publishes;return true;}
    std::string worldRainMapTexture(WorldResourceScopeId)const override{return "fixture";}
    bool captureWorldRainMap(WorldResourceScopeId)override{return false;}
    WorldRainMapDiagnostic readWorldRainMap()override{return {};}
};
int main()try{
    {
        WorldDistantFog fog;fog.weight=1;fog.density={.001f,.005f};fog.falloff={.04f,.04f};fog.start=120;
        fog.nearDistance=30;fog.farDistance=50000;
        const auto rows=packDistantFog(fog,1);
        check(rows[2][3]==120&&rows[3][3]==1,"Distant fog packing");
        for(double eye:{-5.,0.,20.})for(double distance:{0.,100.,120.,121.,500.,2000.})
            for(double vertical:{-40.,0.,30.}) {
                double optical=0;constexpr unsigned steps=8192;
                if(distance>fog.start)for(unsigned i=0;i<steps;++i) {
                    const double t=fog.start+(distance-fog.start)*(i+.5)/steps;
                    const double height=eye+vertical*t/distance;
                    for(unsigned layer=0;layer<2;++layer)
                        optical+=fog.density[layer]*std::exp2(-fog.falloff[layer]*(height-fog.height[layer]))*(distance-fog.start)/steps;
                }
                const double expected=std::exp2(-optical*std::log(2.));
                check(std::abs(distantFogTransmission(fog,eye,vertical,distance)-expected)<1e-5,"Distant fog numerical integration");
            }
        fog.weight=0;check(distantFogTransmission(fog,2,0,500)==1,"Disabled distant fog");
        fog.weight=1;fog.farDistance=29;
        bool rejected=false;try{packDistantFog(fog,1);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid distant fog range admitted");
    }
    Target target;WorldRainMap maps;auto jobs=maps.plan({3,2,-5},1);
    check(jobs.size()==2&&!maps.ready(),"initial two cascades");
    unsigned dynamic=0;
    for(const auto& job:jobs)maps.execute(target,1,job,[&](std::size_t& cursor,std::size_t proxies,std::uint64_t triangles,bool& done){
        check(proxies>300&&triangles>200000&&cursor==0,"initial map must finish");cursor=7;done=true;
    },[&]{++dynamic;});
    check(maps.ready()&&target.publishes==2&&dynamic==2,"initial publication");
    const auto old=maps.published();jobs=maps.plan({5,2,-8},1);check(jobs.size()==1&&jobs[0].cascade==0,"cascade rotation");
    const auto pending=jobs[0];
    maps.execute(target,1,pending,[&](std::size_t& cursor,std::size_t proxies,std::uint64_t triangles,bool& done){
        check(proxies==300&&triangles==200000&&cursor==0&&target.lastClear,"steady budgets");cursor=300;done=false;
    },[&]{++dynamic;});
    check(target.publishes==2&&dynamic==2&&maps.published()[0].origin==old[0].origin,"partial map leaked");
    jobs=maps.plan({99,2,88},1);check(jobs[0].origin==pending.origin,"pending origin changed");
    maps.execute(target,1,jobs[0],[&](std::size_t& cursor,std::size_t,std::uint64_t,bool& done){
        check(cursor==300&&!target.lastClear,"partial map was cleared");cursor=400;done=true;
    },[&]{++dynamic;});
    check(target.publishes==3&&dynamic==3&&maps.published()[0].origin==pending.origin,"atomic map transform");
    check(maps.plan({99,2,88},1)[0].cascade==1,"far map starved");
    check(maps.plan({99,2,88},2).size()==2&&!maps.ready(),"scene revision retained shelter");
    WorldWindState wind;wind.advance(10);wind.setVelocity({-8,12});wind.advance(5);
    check(wind.offset[0]==140&&wind.offset[1]==110,"wind integrated history");
    wind.advance(0);check(wind.offset[0]==140,"paused wind advanced");
    WorldPrecipitationLightingView a;a.owner=1;a.view.identity=4;a.view.generation=1;a.view.frame=1;
    a.view.width=1920;a.view.height=1080;a.view.cameraValid=true;a.view.cameraPosition={0,2,0};
    const auto vp=glm::perspective(glm::radians(60.f),16.f/9,.1f,500.f)*glm::lookAt(glm::vec3(0,2,0),glm::vec3(0,2,-1),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(vp),16,a.view.viewProjection.begin());a.environment.enabled=true;
    auto b=a;++b.view.frame;check(precipitationHistoryValid(b,a),"ordinary history rejected");
    for(unsigned i=0;i<7;++i){
        auto cut=b;if(i==0)cut.view.reset=true;if(i==1)++cut.view.identity;if(i==2)++cut.view.generation;
        if(i==3)++cut.view.frame;if(i==4)++cut.view.width;if(i==5)cut.quality=2;if(i==6)cut.environment.timeOfDayHours+=2;
        check(!precipitationHistoryValid(cut,a),"history cut accepted");
    }

    auto weather=b;weather.environment.hazeDensity=.02f;
    check(!precipitationHistoryValid(weather,a),"Abrupt weather retained fog history");
    auto gradual=weather;++gradual.view.frame;gradual.environment.hazeDensity+=.00001f;
    check(precipitationHistoryValid(gradual,weather),"Continuous weather rejected fog history");
    check(fogBindingViewMatches(a,a),"Current fog binding rejected");
    auto stale=a;++stale.view.frame;check(!fogBindingViewMatches(stale,a),"Stale fog frame accepted");
    stale=a;stale.view.viewProjection[0]+=.01f;check(!fogBindingViewMatches(stale,a),"Other camera fog accepted");
    auto dims=precipitationDimensions(1920,1080,1);check(dims==std::array<unsigned,3>{120,68,64},"medium volume layout");
    for(unsigned q=0;q<4;++q){auto d=precipitationDimensions(1919,1079,q);check(d[0]%4==0&&d[1]%4==0&&d[2]%4==0,"volume group alignment");}
    for(unsigned f=0;f<64;++f){auto j=precipitationJitter(f);check(j[0]>=-.5f&&j[0]<.5f&&j[1]>=-.5f&&j[1]<.5f&&j[2]>=0&&j[2]<1,"jitter range");}
    auto distant=a;distant.distant=true;
    check(precipitationDimensions(distant)==std::array<unsigned,3>{60,34,32},"Distant grid not half resolution");
    check(!precipitationHistoryValid(distant,a),"Near history admitted for distant domain");
    for(unsigned i=0;i<40;++i) {
        WorldFogBox box;box.position={0,2,-5};box.priority=int(i);box.density=float(i)*.001f;a.medium.boxes.push_back(box);
    }
    WorldFogBox hidden;hidden.position={10000,0,0};hidden.priority=999;a.medium.boxes.push_back(hidden);
    for(unsigned i=0;i<70;++i){WorldFogParticle particle;particle.position={0,2,-float(i+1)};particle.radius=.1f;a.medium.particles.push_back(particle);}
    auto& fixture=a.environment.rectLights[0];fixture.position={0,3,-5};fixture.halfU={2,0,0};fixture.halfV={0,0,1};
    fixture.range=10;fixture.radiance={1,1,1};fixture.response=WorldRectLightResponse::FiniteSource;fixture.source.length=4;
    auto tube=packPrecipitationLighting(a,a,false,{}, {}, {});
    check(tube[37][3]==0,"Native tube incorrectly clipped to the rectangular proxy hemisphere");
    fixture.response=WorldRectLightResponse::Ltc;
    auto area=packPrecipitationLighting(a,a,false,{}, {}, {});
    check(area[37][3]==1,"One-sided area emitter lost its hemisphere");
    auto packed=packPrecipitationLighting(a,a,false,{}, {}, {});
    check(packed[172][0]==32&&packed[172][1]==64,"Fog visible limits");
    check(std::abs(packed[176][3]-.039f)<1e-6f,"Fog priority or frustum selection");
    check(packed[400][2]==-1&&packed[400+63*3][2]==-64,"Fog nearest-particle selection");
    auto revision=a;++revision.view.frame;++revision.medium.revision;
    check(!precipitationHistoryValid(revision,a),"Fog discontinuity retained history");
    bool invalid=false;a.medium.boxes[0].halfUp=a.medium.boxes[0].halfRight;
    try{packPrecipitationLighting(a,a,false,{}, {}, {});}catch(const std::invalid_argument&){invalid=true;}
    check(invalid,"Nonorthogonal fog box admitted");
    std::cout<<"PASS rain-map atomic scheduling, wind history and precipitation history contracts\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
