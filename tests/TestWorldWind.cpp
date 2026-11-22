#include "engine/render/WorldWindController.h"
#include "engine/render/IWorldWindField.h"
#include <iostream>
#include <algorithm>
#include <limits>
#include <stdexcept>
using namespace engine::render;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try{
    WorldWindController reference;reference.advance(35);const auto expected=reference.snapshot();
    for(unsigned rate:{15u,30u,60u,144u}){
        WorldWindController flow;
        for(unsigned i=0;i<35*rate;++i)flow.advance(1./rate);
        const auto actual=flow.snapshot();
        for(unsigned a=0;a<2;++a)check(std::abs(actual.travel[a]-expected.travel[a])<1.e-7,"Update rate changes integrated wind");
        for(unsigned i=0;i<20;++i)check(std::abs(actual.phaseTime[i]-expected.phaseTime[i])<1.e-8,"Update rate changes phase");
    }
    WorldWindController constant;constant.setVelocity({6,8});constant.advance(2);
    const auto c=constant.snapshot();check(c.travel==std::array<double,2>{12,16},"Constant travel integral");
    check(std::abs(c.phaseTime[9]-1)<1.e-12&&std::abs(c.phaseTime[10]-1)<1.e-12,"Independent hat integral");
    check(std::abs(c.phaseDirection[9][0]-.6)<1.e-12&&std::abs(c.phaseDirection[9][1]-.8)<1.e-12,"Directional phase integral");
    constant.setVelocity({-6,8});constant.advance(2);const auto d=constant.snapshot();
    check(std::abs(d.phaseDirection[9][0])<1.e-12&&std::abs(d.phaseDirection[9][1]-1.6)<1.e-12,"Direction change recomputed old phase");
    WorldWindController gust;float peak=0;double maxDelta=0;auto prior=gust.snapshot();
    for(unsigned i=0;i<120*120;++i){gust.advance(1./120);const auto next=gust.snapshot();peak=std::max(peak,next.gust);maxDelta=std::max<double>(maxDelta,std::hypot(next.velocity[0]-prior.velocity[0],next.velocity[1]-prior.velocity[1]));prior=next;}
    check(peak>.1f&&maxDelta<.1,"Gust absent or discontinuous");
    WorldWindController fractional;fractional.advance(7.003);
    const auto beforeTransition=fractional.snapshot();auto changing=WorldWindSettings{};
    changing.speed=15;changing.angle=2;fractional.transition(changing,3);
    const auto afterTransition=fractional.snapshot();
    check(beforeTransition.travel==afterTransition.travel&&beforeTransition.phaseTime==afterTransition.phaseTime&&beforeTransition.phaseDirection==afterTransition.phaseDirection,"Fractional transition rewrote accumulated phase");
    auto target=WorldWindSettings{};target.angle+=3.14159265f;target.speed=20;target.gustStrength=0;
    gust.transition(target,4);const auto start=gust.snapshot();
    gust.advance(.01);check(std::hypot(gust.velocity[0]-start.velocity[0],gust.velocity[1]-start.velocity[1])<.1,"Transition jumped");
    gust.advance(5);check(std::hypot(gust.velocity[0],gust.velocity[1])>19.99,"Storm target not reached");
    const auto frozen=gust.snapshot();gust.advance(0);check(gust.snapshot().travel==frozen.travel,"Zero update moved wind");
    auto copy=gust;copy.advance(.005);check(gust.snapshot().travel==frozen.travel,"Snapshot interpolation changed owner");
    gust.advance(std::numeric_limits<double>::max());check(gust.snapshot().generation>0&&std::isfinite(gust.velocity[0]),"Offline discontinuity unbounded");
    const auto valid=gust.snapshot();bool rejected=false;try{gust.setVelocity({0,std::numeric_limits<float>::infinity()});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&gust.snapshot().travel==valid.travel,"Invalid mutation accepted");
    gust.reset();check(gust.snapshot().travel==std::array<double,2>{}&&gust.snapshot().phaseTime==std::array<double,20>{},"Reset retains history");
    WorldWindController resetAtZero;
    WorldWindFieldInput zeroInput;zeroInput.owner=1;zeroInput.wind=resetAtZero.snapshot();
    WorldWindFieldHistory zeroHistory;
    const auto zeroUpdate=updateWorldWindField(zeroInput,zeroHistory);
    commitWorldWindField(zeroHistory,zeroInput,zeroUpdate);
    resetAtZero.reset();zeroInput.wind=resetAtZero.snapshot();
    check(zeroInput.wind.seconds==0&&updateWorldWindField(zeroInput,zeroHistory).reset,"Same-clock reset retained field history");
    WorldWindFieldInput input;input.owner=1;input.camera={0,-4,8};
    input.impulses.resize(17);for(auto& shape:input.impulses){shape.radius=1.9f;shape.persistent=true;}
    const auto plan=planWorldWindField(input);
    check(plan.shapes==17&&plan.overflow==8,"Cluster occupancy cap");
    const auto cluster=windClusterRows+(8+8*16+8*256)*9;
    check(plan.input[cluster][0]==16,"Cluster accepted more than sixteen shapes");
    auto mixed=input;
    for(unsigned i=0;i<17;++i){auto capsule=input.impulses[i];capsule.a.y=-.1f;capsule.b.y=.1f;mixed.impulses.push_back(capsule);}
    check(planWorldWindField(mixed).input[cluster][0]==32,"Sphere and capsule cluster budgets conflated");
    auto saturated=input;saturated.impulses.resize(256,input.impulses[0]);
    const auto capped=planWorldWindField(saturated);check(capped.shapes==255&&capped.rejected==1,"Per-type shape cap");
    input.impulses[0].persistent=false;input.impulses[0].age=1;
    input.impulses[1].a=input.impulses[1].b={100,0,0};
    check(planWorldWindField(input).rejected==2,"Expired/distant impulse not rejected");
    const auto invalid=[&](WorldWindFieldInput bad){
        bool caught=false;try{(void)planWorldWindField(bad);}catch(const std::invalid_argument&){caught=true;}
        check(caught,"Invalid field input accepted");
    };
    auto bad=input;bad.wind.velocity[0]=std::numeric_limits<float>::quiet_NaN();invalid(bad);
    bad=input;bad.camera.x=2.e6f;invalid(bad);
    bad=input;bad.impulses.resize(511);invalid(bad);
    bad=input;bad.shelters.resize(13);invalid(bad);
    bad=input;bad.impulses[0].radius=0;invalid(bad);
    bad=input;bad.quality=4;invalid(bad);
    for(unsigned rate:{15u,30u,60u,144u}){
        WorldWindFieldInput u;u.owner=1;WorldWindFieldHistory h;
        auto update=updateWorldWindField(u,h);commitWorldWindField(h,u,update);
        unsigned steps=0;
        for(unsigned i=1;i<=rate*2;++i){
            u.wind.seconds=double(i)/rate;update=updateWorldWindField(u,h);
            check(!update.reset,"Ordinary frame rate resets wind volume");
            for(const auto& step:update.steps)if(step[1][3]>0)++steps;
            commitWorldWindField(h,u,update);
        }
        check(steps==125,"Fluid clock depends on frame rate");
        auto paused=updateWorldWindField(u,h);check(paused.steps.size()==1&&paused.steps[0][1][3]==0,"Paused clock advanced fluid");
        ++u.wind.generation;check(updateWorldWindField(u,h).reset,"Generation retained history");
        u.wind.seconds=100;check(updateWorldWindField(u,h).reset,"Offline jump retained history");
    }
    target=WorldWindSettings{};target.seed++;rejected=false;
    try{gust.transition(target,1);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Seed identity changed without reset");
    std::cout<<"Shared wind: deterministic flow, gusts, transitions, phase integrals, bounds passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
