#include "engine/render/WorldTemporalScene.h"
#include <iostream>
#include "engine/render/WorldDisplay.h"
using namespace engine::render;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main()try {
    const auto invalidDisplay=[&](WorldDisplaySettings settings) {
        bool rejected=false;try{validateWorldDisplay(settings);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"invalid display controls accepted");
    };
    auto display=WorldDisplaySettings{};validateWorldDisplay(display);
    display.centerWeight=-3;validateWorldDisplay(display);
    check(worldDisplayParameters(display,320,240,0,false,1,1,0,1)[16]==-3,"signed exposure weight lost");
    display.centerWeight=-8.01f;invalidDisplay(display);
    display={};
    display.lowPercentile=display.highPercentile;invalidDisplay(display);
    display={};display.minEV=display.maxEV+1;invalidDisplay(display);
    display={};display.bloomLevels=11;invalidDisplay(display);
    display={};display.manualExposure=0;invalidDisplay(display);
    display={};display.grade[1][0]=0;invalidDisplay(display);
    display={};display.grade[7][2]=std::numeric_limits<float>::quiet_NaN();invalidDisplay(display);
    WorldTemporalHistory h;WorldTemporalView v{7,1,1,320,240};
    auto paused=v;paused.deltaSeconds=0;h.validate(paused);
    auto clocked=v;clocked.deltaSeconds=-1;h.validate(clocked);
    auto invalidTime=v;invalidTime.deltaSeconds=-.5f;
    bool badTime=false;try{h.validate(invalidTime);}catch(const std::invalid_argument&){badTime=true;}
    check(badTime,"invalid time sentinel accepted");
    h.validate(v);check(!h.accepts(v),"first view accepted history");h.commit(v);
    ++v.frame;check(h.accepts(v),"contiguous rendered view lost history");
    auto changed=v;changed.identity=8;check(!h.accepts(changed),"views mixed");
    changed=v;changed.generation++;check(!h.accepts(changed),"generations mixed");
    changed=v;changed.width++;check(!h.accepts(changed),"resized history reused");
    changed=v;changed.reset=true;check(!h.accepts(changed),"cut history reused");
    changed=v;changed.frame++;check(!h.accepts(changed),"missed render history reused");
    bool rejected=false;try{h.validate(h.previous);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"duplicate promotion accepted");
    for(float bad:{0.f,-1.f,std::numeric_limits<float>::infinity()}) {
        changed=v;changed.preExposure=bad;rejected=false;
        try{h.validate(changed);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid exposure accepted");
    }
    check(std::abs(std::pow(1-worldTemporalCurrentWeight(1.f/60),60)-.2f)<.00001,"time-scaled blend");
    auto m=worldMotionIdentity;jitterWorldTemporalProjection(m.data(),320,240,1);
    check(std::abs(m[13]-1.f/720)<.000001,"top-left jitter conversion");
    check(worldTemporalJitter(1)==worldTemporalJitter(9),"jitter period");
    const std::uint16_t encoded[]{0xbc00,0x0001,0x3c00,0x7bff};
    const auto decoded=unpackWorldTemporalHalf(encoded);
    check(decoded[0]==-1&&decoded[1]==std::ldexp(1.f,-24)&&decoded[2]==1&&decoded[3]==65504,
        "compact readback sign, subnormal or HDR range");
    WorldTemporalView camera{19,1,1,320,240};camera.cameraValid=true;
    auto projection=glm::perspective(glm::radians(60.f),4.f/3,.1f,1000.f);
    std::copy_n(glm::value_ptr(projection),16,camera.viewProjection.begin());
    const auto initial=worldTemporalCameraConstants(camera,{},false);
    check(initial[4]==1 && initial[5]==0,"first camera accepted sky history");
    for(float distance:{.2f,1.f,17.f,300.f}) {
        const auto clip=projection*glm::vec4(0,0,-distance,1);
        const float depth=(clip.z/clip.w+1)*.5f;
        check(std::abs(1.f/(initial[2]*depth+initial[3])-distance)<.002f*distance,
              "conventional depth conversion");
    }
    auto moved=camera;moved.frame=2;moved.cameraPosition={12,3,-9};
    auto translated=projection*glm::translate(glm::mat4(1),-glm::make_vec3(moved.cameraPosition.data()));
    std::copy_n(glm::value_ptr(translated),16,moved.viewProjection.begin());
    const auto translation=worldTemporalCameraConstants(moved,camera,true);
    auto sky=glm::make_mat4(translation.data()+8);
    for(const auto clip:{glm::vec4(0,0,1,1),glm::vec4(.4f,-.2f,1,1)}) {
        const auto old=sky*clip;
        check(glm::length(glm::vec2(old/old.w-clip))<.00003f,"sky inherited camera translation");
    }
    auto rotation=projection*glm::rotate(glm::mat4(1),.1f,glm::vec3(0,1,0));
    moved.cameraPosition={};std::copy_n(glm::value_ptr(rotation),16,moved.viewProjection.begin());
    const auto rotated=worldTemporalCameraConstants(moved,camera,true);
    sky=glm::make_mat4(rotated.data()+8);
    const auto old=sky*glm::vec4(0,0,1,1);
    check(std::abs(old.x/old.w)>0.05f,"sky rotation has zero motion");
    auto ortho=glm::ortho(-4.f,4.f,-3.f,3.f,.1f,50.f);
    std::copy_n(glm::value_ptr(ortho),16,moved.viewProjection.begin());
    const auto orthographic=worldTemporalCameraConstants(moved,moved,true);
    check(orthographic[4]==2,"orthographic projection lost depth mode");
    auto oc=ortho*glm::vec4(0,0,-7,1);
    check(std::abs(orthographic[2]*(oc.z+1)*.5f+orthographic[3]-7)<.00001f,
          "orthographic linear depth");
    WorldTemporalHistory cameraHistory;cameraHistory.commit(camera);
    changed=camera;changed.frame++;changed.cameraValid=false;
    check(!cameraHistory.accepts(changed),"camera enrollment changed without reset");
    changed=camera;changed.frame++;changed.viewProjection={};
    rejected=false;try{cameraHistory.validate(changed);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"singular camera accepted");
    std::cout<<"temporal lifecycle PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
