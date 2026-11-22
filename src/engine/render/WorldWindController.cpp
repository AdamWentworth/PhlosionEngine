#include "engine/render/WorldWindController.h"
#include <algorithm>
#include <numbers>
namespace engine::render {
namespace {
constexpr double tick=1./120.,period=1400000000000.;
double smooth(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
double hash(std::uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return double(v)/4294967296.;}
void validate(const WorldWindSettings& s){
    if(!std::isfinite(s.speed)||s.speed<0||s.speed>150||!std::isfinite(s.angle)||
       !std::isfinite(s.gustStrength)||s.gustStrength<0||s.gustStrength>1||
       !std::isfinite(s.directionVariation)||s.directionVariation<0||s.directionVariation>3.141593f)
        throw std::invalid_argument("Invalid world wind settings");
}
double wrap(double a){return std::remainder(a,2*std::numbers::pi);}
}
WorldWindController::WorldWindController(WorldWindSettings s){reset(s);}
void WorldWindController::reset(WorldWindSettings s){
    validate(s);const auto generation=integrated_.generation+1;from_=to_=s;integrated_={};integrated_.generation=generation;remainder_=transitionBegin_=transitionEnd_=0;fixed_=false;refresh();
}
void WorldWindController::transition(WorldWindSettings s,double seconds){
    validate(s);if(s.seed!=to_.seed)throw std::invalid_argument("Changing the wind seed requires reset");
    if(!std::isfinite(seconds)||seconds<0||seconds>86400)throw std::invalid_argument("Invalid wind transition");
    const double now=integrated_.seconds+remainder_;
    const auto current=evaluate(now);
    integrated_=snapshot();remainder_=0;
    // Start from the current prevailing settings, not the gust peak.
    const double f=transitionEnd_>transitionBegin_?smooth((now-transitionBegin_)/(transitionEnd_-transitionBegin_)):1;
    from_.speed=float(from_.speed+(to_.speed-from_.speed)*f);
    from_.angle=float(from_.angle+wrap(to_.angle-from_.angle)*f);
    from_.gustStrength=float(from_.gustStrength+(to_.gustStrength-from_.gustStrength)*f);
    from_.directionVariation=float(from_.directionVariation+(to_.directionVariation-from_.directionVariation)*f);
    if(fixed_){from_.speed=float(std::hypot(current.velocity[0],current.velocity[1]));from_.angle=float(std::atan2(current.velocity[1],current.velocity[0]));from_.gustStrength=from_.directionVariation=0;}
    to_=s;transitionBegin_=now;transitionEnd_=now+seconds;fixed_=false;refresh();
}
void WorldWindController::setVelocity(std::array<float,2> v){
    WorldWindState checked;checked.setVelocity(v);
    integrated_=snapshot();remainder_=0;fixedVelocity_=v;fixed_=true;refresh();
}
WorldWindController::Flow WorldWindController::evaluate(double seconds) const {
    if(fixed_){const double speed=std::hypot(fixedVelocity_[0],fixedVelocity_[1]);return {{fixedVelocity_[0],fixedVelocity_[1]},std::clamp(speed/20.,0.,1.),0};}
    const double t=transitionEnd_>transitionBegin_?smooth((seconds-transitionBegin_)/(transitionEnd_-transitionBegin_)):1;
    const double speed=from_.speed+(to_.speed-from_.speed)*t;
    const double strength=std::clamp(speed/20.,0.,1.);
    const double amount=from_.gustStrength+(to_.gustStrength-from_.gustStrength)*t;
    const auto epoch=static_cast<std::uint32_t>(std::fmod(std::floor(seconds/32.),4294967296.));
    const auto seed=to_.seed+epoch*0x9e3779b9u;
    const double local=std::fmod(seconds,32.)-(2+hash(seed)*9);
    const double rise=2+hash(seed+1)*2,hold=1+hash(seed+2)*3,fall=4+hash(seed+3)*4;
    double gust=local<rise?smooth(local/rise):1-smooth((local-rise-hold)/fall);
    gust*=amount*(.5+.5*hash(seed+4));
    gust=std::min(gust,1-strength);
    const double directionSlot=std::floor(seconds/600.);
    const auto slot=static_cast<std::uint32_t>(std::fmod(directionSlot,4294967296.));
    const auto heading=[&](std::uint32_t n){return (hash(to_.seed+n*0x85ebca6bu)-.5)*2;};
    const double start=slot==0?0:heading(slot-1);
    const double drift=start+(heading(slot)-start)*smooth(std::fmod(seconds,600.)/500.);
    const double angle=from_.angle+wrap(to_.angle-from_.angle)*t+
        (from_.directionVariation+(to_.directionVariation-from_.directionVariation)*t)*drift;
    const double total=speed+gust*20;
    return {{std::cos(angle)*total,std::sin(angle)*total},std::clamp(strength+gust,0.,1.),gust};
}
void WorldWindController::integrate(WorldWindSnapshot& out,double begin,double seconds) const {
    if(seconds<=0)return;
    const auto flow=evaluate(begin+seconds*.5);
    for(unsigned axis=0;axis<2;++axis){
        const double speed=flow.velocity[axis];
        const double delta=speed==0?0:std::fmod(seconds,period/std::abs(speed))*speed;
        out.travel[axis]=std::fmod(out.travel[axis]+delta,period);
    }
    const double p=flow.strength*19;const auto low=std::min(18u,static_cast<unsigned>(p));
    const double norm=std::hypot(flow.velocity[0],flow.velocity[1]);
    for(unsigned i=low;i<=low+1;++i){
        const double weight=i==low?1-(p-low):p-low;
        const double dt=weight==0?0:std::fmod(seconds,period/weight)*weight;
        out.phaseTime[i]=std::fmod(out.phaseTime[i]+dt,period);
        for(unsigned axis=0;axis<2;++axis)
            out.phaseDirection[i][axis]=std::fmod(out.phaseDirection[i][axis]+(norm>1e-12?flow.velocity[axis]/norm:axis==0?1:0)*dt,period);
    }
}
WorldWindSnapshot WorldWindController::snapshot() const {
    auto out=integrated_;integrate(out,integrated_.seconds,remainder_);out.seconds+=remainder_;
    const auto flow=evaluate(out.seconds);out.strength=float(flow.strength);out.gust=float(flow.gust);
    out.velocity={float(flow.velocity[0]),float(flow.velocity[1])};return out;
}
void WorldWindController::refresh(){const auto out=snapshot();velocity=out.velocity;offset=out.travel;}
void WorldWindController::advance(double seconds){
    if(!std::isfinite(seconds)||seconds<0)throw std::invalid_argument("Invalid wind advance");
    if(seconds==0)return;
    if(fixed_){
        integrated_=snapshot();remainder_=0;integrate(integrated_,integrated_.seconds,seconds);
        integrated_.seconds=std::fmod(integrated_.seconds+std::fmod(seconds,period),period);refresh();return;
    }
    // Offline discontinuities do not iterate millions of simulation steps.
    // Invalidate phase history explicitly, then simulate the bounded remainder.
    if(seconds>3600||integrated_.seconds>1.e9){
        const auto generation=integrated_.generation+1;const auto settings=to_;
        reset(settings);integrated_.generation=generation;seconds=std::fmod(seconds,32.);
    }
    double remaining=seconds+remainder_;remainder_=0;
    const auto steps=static_cast<unsigned>(std::floor((remaining+1.e-10)/tick));
    for(unsigned i=0;i<steps;++i){integrate(integrated_,integrated_.seconds,tick);integrated_.seconds+=tick;}
    remainder_=std::max(0.,remaining-double(steps)*tick);refresh();
}
}
