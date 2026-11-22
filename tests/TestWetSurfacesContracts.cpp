#include "engine/render/IWorldWetSurfaces.h"
#include <iostream>
#include <limits>
using namespace engine::render;
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
int main()try {
    const auto close=[](double a,double b){return std::abs(a-b)<1e-11;};
    auto wet=advanceWorldWetSurface({},.4,12);
    check(close(wet.moisture,.4)&&close(wet.puddles,.12),"Independent accumulation rates");
    auto dry=advanceWorldWetSurface(wet,0,3);
    check(close(dry.moisture,.275)&&close(dry.puddles,.1075),"Independent evaporation");
    auto light=advanceWorldWetSurface(wet,.01,3);
    check(light.moisture>wet.moisture&&light.puddles>wet.puddles,"Light rain must not evaporate water");
    for(int rate:{15,30,60,144}) {
        WorldWetAccumulation state;
        for(int i=0;i<rate*12;++i)state=advanceWorldWetSurface(state,.4,1./rate);
        check(close(state.moisture,.4)&&close(state.puddles,.12),"Rate-independent accumulation");
    }
    check(advanceWorldWetSurface({},1,1e300).moisture==1,"Bounded long wet update");
    check(advanceWorldWetSurface({1,1},0,1e300).puddles==0,"Bounded long drying");
    for(double value:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try{advanceWorldWetSurface({},1,value);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid time rejected");
    }
    const std::array<unsigned char,16> mask{0,0,0,0,0,0,0,255,0,0,0,128,0,0,0,64};
    check(close(worldWetMaskChannel(mask,2,2,.25,.25,3),0),"Texel center");
    check(close(worldWetMaskChannel(mask,2,2,.75,.25,3),1),"Texel center 2");
    check(std::abs(worldWetMaskChannel(mask,2,2,0,.25,3)-.5f)<1e-6,"Repeat seam");
    check(worldWetMaskChannel(mask,2,2,-.25,.25,3)==worldWetMaskChannel(mask,2,2,.75,.25,3),"Negative repeat");
    check(worldWetPuddle(.9f,.9f,1,0)[1]==0&&worldWetPuddle(.9f,.9f,.96f,1)[1]==0,"Dry and sloped water exclusion");
    check(worldWetPuddle(.9f,.9f,1,1)[1]==1,"Actual water coverage");
    check(worldWetPuddle(.7f,.7f,1,1)[0]>.4&&worldWetPuddle(.7f,.7f,1,1)[1]>.59,"Wetness and water are separate");
    std::array<unsigned char,12> texels{};
    WorldWetSurfaceView view;view.owner=7;view.rainMap="rain";view.view={101,1,1,128,128,false};
    view.view.cameraValid=true;view.view.cameraPosition={0,3,0};
    const auto vp=glm::ortho(-2.f,2.f,-2.f,2.f,.05f,300.f)*glm::lookAt(glm::vec3(0,3,0),glm::vec3(0),glm::vec3(0,0,-1));
    std::copy_n(glm::value_ptr(vp),16,view.view.viewProjection.begin());
    for(auto& t:view.textures){t.key=t.cacheKey="wet";t.rgba=texels.data();t.width=t.height=1;t.textureSrgb=0;}
    view.textures[3].width=3;
    view.interior={0,0,1,1};view.interiorHeight={-8,1,32,0};view.vehicle={0,0,12,0};view.water={0,0,128,-64};
    for(auto& c:view.cascades){c.valid=true;c.step=.01f;}
    const auto packed=packWorldWetSurfaces(view);
    check(std::abs(packed[10][0])<1e-6&&std::abs(packed[10][1]+1)<1e-6&&std::abs(packed[10][2])<1e-6,"Axial view depth");
    auto perspective=view;
    const auto jittered=glm::perspective(glm::radians(65.f),1.f,.05f,300.f)*glm::lookAt(glm::vec3(0,3,0),glm::vec3(0),glm::vec3(0,0,-1));
    std::copy_n(glm::value_ptr(jittered),16,perspective.view.viewProjection.begin());
    jitterWorldTemporalProjection(perspective.view.viewProjection.data(),128,128,3);
    const auto axial=packWorldWetSurfaces(perspective);
    check(std::abs(axial[10][0])<1e-6&&std::abs(axial[10][1]+1)<1e-6&&std::abs(axial[10][2])<1e-6,"Jitter must not tilt axial wet depth");
    auto rejects=[&](auto mutate) {auto bad=view;mutate(bad);bool rejected=false;
        try{(void)packWorldWetSurfaces(bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid wet binding accepted");};
    rejects([](auto& v){v.owner=0;});rejects([](auto& v){v.view.generation=0;});
    rejects([](auto& v){v.textures[0].textureSrgb=1;});rejects([](auto& v){v.cascades[0].valid=false;});
    rejects([](auto& v){v.interior[2]=0;});rejects([](auto& v){v.view.cameraValid=false;});
    rejects([](auto& v){v.weaponDepthMaximum=2;});rejects([](auto& v){v.water[0]=std::numeric_limits<float>::quiet_NaN();});
    WorldWetBindingHistory history;history.validate(view,10);history.commit(view,10);history.validate(view,10);
    bool stale=false;try{history.validate(view,11);}catch(const std::invalid_argument&){stale=true;}check(stale,"Stale wet frame accepted");
    auto second=view;second.view.identity=102;history.validate(second,11);history.commit(second,11);
    auto changed=view;changed.view.width=64;
    bool collision=false;try{history.validate(changed,10);}catch(const std::invalid_argument&){collision=true;}check(collision,"Reused camera identity accepted");
    history.release(view.owner);history.validate(view,12);history.commit(view,12);
        std::cout<<"PASS wet accumulation, drying, clocks, invalid inputs, texture seams and water coverage\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
