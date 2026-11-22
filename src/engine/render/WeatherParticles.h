#pragma once
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::render {
inline float particleRandom(std::uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;
    return float(state>>8)*(1.f/16777216.f);
}
inline bool particleFinite(glm::vec4 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);
}
struct ParticleEvaluator {
    unsigned kind=0,axes=4;
    bool spill=false;
    std::array<unsigned,4> locks{};
    std::vector<glm::vec4> values{glm::vec4(0)};
    glm::vec4 sample(float time,std::uint32_t& random) const {
        if(kind==0)return values[0];
        if(kind==1){
            glm::vec4 unit;
            for(unsigned c=0;c<axes;++c)unit[c]=locks[c]&&c?unit[locks[c]-1]:particleRandom(random);
            for(unsigned c=axes;c<4;++c)unit[c]=spill?unit[axes-1]:0;
            return values[0]+(values[1]-values[0])*unit;
        }
        const float x=std::clamp(time,0.f,1.f)*float(values.size()-1);
        const auto i=std::min(std::size_t(x),values.size()-1);
        return glm::mix(values[i],values[std::min(i+1,values.size()-1)],x-float(i));
    }
    static ParticleEvaluator parse(const nlohmann::json& j) {
        ParticleEvaluator e;e.kind=j.at("kind").get<unsigned>();
        e.axes=j.value("axes",4u);e.spill=j.value("spill",0)!=0;
        e.locks=j.value("locks",std::array<unsigned,4>{});
        const auto& rows=j.at("values");
        if(e.kind>2||e.axes<1||e.axes>4||rows.size()!=(e.kind==2?16u:e.kind==1?2u:1u))
            throw std::invalid_argument("Particle evaluator layout");
        for(unsigned c=0;c<4;++c)if(e.locks[c]>c)throw std::invalid_argument("Particle evaluator axis lock");
        e.values.clear();
        for(const auto& row:rows){
            if(row.size()!=4)throw std::invalid_argument("Particle evaluator components");
            glm::vec4 v;
            for(unsigned c=0;c<4;++c)v[c]=row[c].get<float>();
            if(!particleFinite(v)||glm::any(glm::greaterThan(glm::abs(v),glm::vec4(1e6f))))
                throw std::invalid_argument("Particle evaluator value");
            e.values.push_back(v);
        }
        return e;
    }
};
enum class WeatherParticleOp : unsigned {
    Alpha,Life,Position,Rotation,RotationRate,Size,Sphere,Velocity,Acceleration,
    AlphaLife,SizeLife,Animation,EffectAlpha,AlphaDistance,Noise,Drag,SizeDistance,Box,Color
};
struct WeatherParticleModule {
    WeatherParticleOp op{};
    unsigned flags=0;
    ParticleEvaluator a,b,c;
    glm::vec4 p{},q{};
};
struct WeatherParticleLod {
    float distance=100;
    ParticleEvaluator rate;
    glm::vec2 delay{},duration{1};
    bool delayOnce=false,enabled=true;
};
struct WeatherParticleEmitter {
    std::string name;
    unsigned capacity=0,loops=0;
    float wind=0,stretch=0,backLighting=.5f,wrap=.65f,lodDistance=100,lightingMipBias=0,fadeoutRange=10;
    glm::vec2 delay{},duration{1};
    bool delayOnce=false,sort=false,local=false,spherical=false;
    std::vector<WeatherParticleLod> lods;
    ParticleEvaluator rate,trackAlpha,trackEmission;
    float trackDuration=1;
    std::vector<WeatherParticleModule> modules;
};
inline glm::vec4 particleRow(const nlohmann::json& j) {
    if(j.size()!=4)throw std::invalid_argument("Particle row layout");
    glm::vec4 v;for(unsigned c=0;c<4;++c)v[c]=j[c].get<float>();
    if(!particleFinite(v))throw std::invalid_argument("Particle row value");
    return v;
}
inline std::vector<WeatherParticleEmitter> parseWeatherParticles(const nlohmann::json& j) {
    if(j.at("schema")!="phlosion.weather-particles/v1"||j.at("emitters").empty()||j.at("emitters").size()>64)
        throw std::invalid_argument("Weather particle program");
    std::vector<WeatherParticleEmitter> result;unsigned total=0;
    for(const auto& v:j.at("emitters")){
        WeatherParticleEmitter e;e.name=v.at("name").get<std::string>();e.capacity=v.at("capacity").get<unsigned>();
        e.loops=v.at("loops").get<unsigned>();e.wind=v.at("wind");e.stretch=v.at("stretch");
        e.backLighting=v.at("backLighting");e.wrap=v.at("wrap");e.lodDistance=v.at("lodDistance");e.lightingMipBias=v.at("lightingMipBias");
        e.fadeoutRange=v.at("fadeoutRange");
        e.delayOnce=v.at("delayOnce");e.sort=v.at("sort");
        e.local=v.value("local",false);e.spherical=v.value("spherical",false);
        if(v.at("delay").size()!=2||v.at("duration").size()!=2)throw std::invalid_argument("Particle timing layout");
        for(unsigned c=0;c<2;++c){e.delay[c]=v.at("delay")[c];e.duration[c]=v.at("duration")[c];}
        e.rate=ParticleEvaluator::parse(v.at("rate"));
        e.trackAlpha=ParticleEvaluator::parse(v.at("track").at("alpha"));
        e.trackEmission=ParticleEvaluator::parse(v.at("track").at("emission"));
        e.trackDuration=v.at("track").at("duration");
        if(!e.capacity||e.capacity>4096||(total+=e.capacity)>4096||e.loops>65536||
            !particleFinite({e.wind,e.stretch,e.backLighting,e.wrap})||e.wind<0||e.wind>100||
            e.stretch<0||e.stretch>100||e.backLighting<0||e.backLighting>1||e.wrap<0||e.wrap>2||
            !particleFinite({e.delay,e.duration})||e.delay.x<0||e.delay.y<e.delay.x||e.delay.y>86400||
            e.duration.x<=0||e.duration.y<e.duration.x||e.duration.y>86400||
            !std::isfinite(e.trackDuration)||e.trackDuration<=0||e.trackDuration>86400||
            !std::isfinite(e.lodDistance)||e.lodDistance<=0||e.lodDistance>1e6||
            !std::isfinite(e.fadeoutRange)||e.fadeoutRange<0||e.fadeoutRange>1e6||
            !std::isfinite(e.lightingMipBias)||e.lightingMipBias<0||e.lightingMipBias>8||std::floor(e.lightingMipBias)!=e.lightingMipBias)
            throw std::invalid_argument("Particle emitter bounds");
        if(v.contains("lods")){
            if(v.at("lods").empty()||v.at("lods").size()>8)throw std::invalid_argument("Particle LOD count");
            for(const auto& l:v.at("lods")){
                WeatherParticleLod lod;lod.distance=l.at("distance");lod.rate=ParticleEvaluator::parse(l.at("rate"));
                lod.enabled=l.at("enabled");const auto& d=l.at("delay");const auto& t=l.at("duration");
                lod.delay={d.at(d.at("useEmitterDelayRange").get<int>()?"emitterDelayLow":"emitterDelay"),d.at("emitterDelay")};
                lod.duration={t.at(t.at("useEmitterDurationRange").get<int>()?"emitterDurationLow":"emitterDuration"),t.at("emitterDuration")};
                lod.delayOnce=d.at("useEmitterDelayOnce").get<int>()!=0;
                if(!std::isfinite(lod.distance)||lod.distance<=0||lod.distance>1e6||
                   (!e.lods.empty()&&lod.distance<=e.lods.back().distance)||
                   !particleFinite({lod.delay,lod.duration})||lod.delay.x<0||lod.delay.y<lod.delay.x||
                   lod.delay.y>86400||lod.duration.x<=0||lod.duration.y<lod.duration.x||lod.duration.y>86400)
                    throw std::invalid_argument("Particle LOD values");
                e.lods.push_back(std::move(lod));
            }
        }
        if(v.at("modules").size()>64)throw std::invalid_argument("Particle module count");
        for(const auto& m:v.at("modules")){
            const unsigned op=m.at("op");
            if(op>unsigned(WeatherParticleOp::Color))throw std::invalid_argument("Particle operation");
            WeatherParticleModule mod;mod.op=WeatherParticleOp(op);mod.flags=m.at("flags");
            if(mod.flags>511)throw std::invalid_argument("Particle flags");
            mod.a=ParticleEvaluator::parse(m.at("a"));mod.b=ParticleEvaluator::parse(m.at("b"));mod.c=ParticleEvaluator::parse(m.at("c"));
            mod.p=particleRow(m.at("p"));mod.q=particleRow(m.at("q"));e.modules.push_back(std::move(mod));
        }
        result.push_back(std::move(e));
    }
    return result;
}
struct WeatherParticle {
    glm::vec3 position{},velocity{};
    glm::vec4 color{1};
    glm::vec2 size{1},baseSize{1};
    float age=0,life=1,alpha=1,baseAlpha=1,rotation=0,rotationRate=0,frame=0;
    std::uint64_t id=0;
    std::uint32_t random=1;
};
struct WeatherParticleFrame {
    std::vector<std::vector<WeatherParticle>> emitters;
    double seconds=0;
    std::uint64_t generation=0,discontinuities=0;
    std::size_t count()const{std::size_t n=0;for(const auto& v:emitters)n+=v.size();return n;}
};
inline glm::vec3 weatherParticleSphere(unsigned flags,float inner,float outer,std::uint32_t& rng) {
    constexpr float pi=3.14159265358979323846f;
    const bool off[3]={(flags&3)==0,(flags&12)==0,(flags&48)==0};
    const float theta=particleRandom(rng)*2*pi;
    const float radius=(flags&64)?outer:glm::mix(inner,outer,particleRandom(rng));
    glm::vec3 delta(0);
    if(off[0]||off[1]||off[2]){
        if(off[0]){if(!off[1])delta.y=std::cos(theta)*radius;if(!off[2])delta.z=std::sin(theta)*radius;}
        if(off[1]){if(!off[0])delta.x=std::cos(theta)*radius;if(!off[2])delta.z=std::sin(theta)*radius;}
        if(off[2]){if(!off[0])delta.x=std::cos(theta)*radius;if(!off[1])delta.y=std::sin(theta)*radius;}
    }else{
        const float phi=(particleRandom(rng)-.5f)*pi;
        delta={std::cos(theta)*std::cos(phi)*radius,std::sin(theta)*std::cos(phi)*radius,std::sin(phi)*radius};
    }
    for(unsigned c=0;c<3;++c){
        const auto sign=(flags>>(2*c))&3;
        if(sign==1)delta[c]=std::abs(delta[c]);else if(sign==2)delta[c]=-std::abs(delta[c]);
    }
    return delta;
}
inline glm::vec3 weatherParticleDrag(glm::vec3 velocity,float coefficient,float dt) {
    const float speed=glm::length(velocity);
    return speed>1e-7f&&coefficient>0?velocity/(1+coefficient*speed*dt):velocity;
}
inline float weatherParticleFade(float distance,glm::vec4 range){
    if(distance<range.x)return std::clamp((distance-range.y)/std::max(range.x-range.y,1e-6f),0.f,1.f);
    if(distance>range.w)return std::clamp((range.z-distance)/std::max(range.z-range.w,1e-6f),0.f,1.f);
    return 1;
}
inline float weatherParticleSize(float d,glm::vec4 nearRange,glm::vec4 farRange) {
    if(d<nearRange.x)return nearRange.z;
    if(d<nearRange.y)return glm::mix(nearRange.z,1.f,(d-nearRange.x)/std::max(nearRange.y-nearRange.x,1e-6f));
    if(d<farRange.x)return 1;
    if(d<farRange.y)return glm::mix(1.f,farRange.z,(d-farRange.x)/std::max(farRange.y-farRange.x,1e-6f));
    return farRange.z;
}
// Coordinates are supplied by the caller. Positions persist in world space;
// moving the emitter changes new births, never the existing particles.
struct WeatherParticleQuadGeometry {
    std::array<glm::vec3,4> corners;
    glm::vec3 binormal;
};
inline WeatherParticleQuadGeometry weatherParticleGeometry(glm::vec3 center,glm::vec3 camera,
        glm::vec3 forward,glm::vec3 right,glm::vec3 up,glm::vec2 size,
        float turns,glm::vec3 velocity,float stretch,float pivot=0,bool spherical=false) {
    if(spherical){
        const auto toCamera=camera-center;
        const auto toward=glm::length(toCamera)>1e-6f?glm::normalize(toCamera):-forward;
        auto axis=glm::cross(glm::vec3(0,1,0),toward);
        right=glm::length(axis)>1e-6f?glm::normalize(axis):right;
        up=glm::cross(toward,right);
    }
    const float angle=turns*6.28318530718f,c=std::cos(angle),s=std::sin(angle);
    glm::vec3 x=right*c-up*s,y=right*s+up*c;
    WeatherParticleQuadGeometry out;out.binormal=right*s-up*c;
    if(stretch>0) {
        const float speed=glm::length(velocity);
        const auto motion=speed>1e-6f?velocity/speed:up;
        const auto toward=camera-center;
        const auto facing=glm::length(toward)>1e-6f?-glm::normalize(toward):forward;
        auto side=glm::cross(facing,motion);
        side=glm::length(side)>1e-6f?glm::normalize(side):right;
        x=side*(c-s);
        y=motion*((1+std::min(1.f,speed)*stretch*speed)*(s+c));
        out.binormal=-motion;
    }
    if(spherical)out.binormal=glm::normalize(y);
    constexpr std::array<glm::vec2,4> signs{{{-1,-1},{1,-1},{1,1},{-1,1}}};
    for(unsigned i=0;i<4;++i)
        out.corners[i]=center+x*(size.x*signs[i].x)+y*(size.y*(signs[i].y+pivot));
    return out;
}
class WeatherParticles {
public:
    explicit WeatherParticles(std::vector<WeatherParticleEmitter> program):program_(std::move(program)),states_(program_.size()){}
    struct State {
        std::vector<WeatherParticle> particles;
        double cycle=0,delayClock=0,fraction=0;
        float delay=0,duration=1;
        unsigned cycles=0,lod=0;
        std::uint32_t random=1;
    };
    void reset(std::uint64_t generation,double seconds,std::uint32_t seed=417) {
        generation_=generation;seconds_=seconds;remainder_=0;nextId_=0;
        for(std::size_t i=0;i<states_.size();++i){
            auto& s=states_[i];s=State{};s.random=seed^(std::uint32_t(i+1)*2654435761u);
            if(!s.random)s.random=1;s.particles.reserve(program_[i].capacity);
            s.delay=glm::mix(program_[i].delay.x,program_[i].delay.y,particleRandom(s.random));
            s.duration=glm::mix(program_[i].duration.x,program_[i].duration.y,particleRandom(s.random));
        }
    }
    void advance(double seconds,std::uint64_t generation,float effectSeconds,bool emitting,
                 glm::vec3 origin,glm::vec3 wind,unsigned quality=3) {
        advanceView(seconds,generation,effectSeconds,emitting,origin,origin,wind,quality);
    }
    void advanceView(double seconds,std::uint64_t generation,float effectSeconds,bool emitting,
                 glm::vec3 origin,glm::vec3 observer,glm::vec3 wind,unsigned quality=3) {
        if(!particleFinite({observer,0}))throw std::invalid_argument("Particle observer");
        if(!std::isfinite(seconds)||seconds<0||seconds>1e9||!std::isfinite(effectSeconds)||effectSeconds<0||
           !particleFinite({origin,0})||!particleFinite({wind,0})||quality>3)
            throw std::invalid_argument("Weather particle frame");
        if(seconds_<0||generation!=generation_||seconds<seconds_)reset(generation,seconds);
        double delta=seconds-seconds_;seconds_=seconds;
        if(delta==0)return;
        // Bound catch-up after a discontinuous view jump. Report it; ordinary
        // accepted simulation ticks never take this path.
        if(delta>10){reset(generation,seconds-10);delta=10;++discontinuities_;seconds_=seconds;}
        remainder_+=delta;
        constexpr double step=1./60.;
        while(remainder_+1e-10>=step){
            remainder_-=step;
            tick(float(step),effectSeconds,emitting,origin,observer,wind,quality);
        }
    }
    const std::vector<State>& states()const{return states_;}
    const std::vector<WeatherParticleEmitter>& program()const{return program_;}
    std::uint64_t discontinuities()const{return discontinuities_;}
    std::size_t count()const{std::size_t n=0;for(const auto& s:states_)n+=s.particles.size();return n;}
private:
    void spawn(std::size_t index,glm::vec3 origin,float normalizedTime) {
        auto& s=states_[index];const auto& e=program_[index];
        WeatherParticle p;p.id=nextId_++;p.random=s.random^std::uint32_t(p.id*747796405ull+2891336453u);
        if(!p.random)p.random=1;p.position=origin;
        for(const auto& m:e.modules){
            const auto eval=[&](const ParticleEvaluator& v){return v.sample(normalizedTime,p.random);};
            switch(m.op){
            case WeatherParticleOp::Alpha:p.baseAlpha=eval(m.a).x;break;
            case WeatherParticleOp::Life:p.life=std::max(eval(m.a).x,1e-5f);break;
            case WeatherParticleOp::Position:p.position+=glm::vec3(eval(m.a));break;
            case WeatherParticleOp::Rotation:p.rotation=eval(m.a).x;break;
            case WeatherParticleOp::RotationRate:p.rotationRate=eval(m.a).x;break;
            case WeatherParticleOp::Size:p.baseSize=glm::vec2(eval(m.a));break;
            case WeatherParticleOp::Sphere:{
                const auto d=weatherParticleSphere(m.flags,eval(m.a).x,eval(m.b).x,p.random);
                p.position+=d;if(m.flags&256)p.velocity+=d*eval(m.c).x;break;}
            case WeatherParticleOp::Box:{
                glm::vec3 box;for(unsigned c=0;c<3;++c)box[c]=2*particleRandom(p.random)-1;
                p.position+=box*glm::vec3(eval(m.a));break;}
            case WeatherParticleOp::Color:p.color=eval(m.a);break;
            case WeatherParticleOp::Velocity:p.velocity+=glm::vec3(eval(m.a));break;
            case WeatherParticleOp::Animation:p.frame=eval(m.a).x;break;
            default:break;
            }
        }
        p.alpha=p.baseAlpha;p.size=p.baseSize;s.particles.push_back(p);
    }
    void tick(float dt,float effectSeconds,bool emitting,glm::vec3 origin,glm::vec3 observer,glm::vec3 wind,unsigned quality){
        constexpr float scales[]={.35f,.6f,.85f,1.f};
        for(std::size_t index=0;index<states_.size();++index){
            auto& s=states_[index];const auto& e=program_[index];
            const float emitterDistance=glm::distance(observer,origin);
            const WeatherParticleLod* lod=nullptr;
            if(!e.lods.empty()){
                unsigned indexLod=0;
                while(indexLod+1<e.lods.size()&&emitterDistance>e.lods[indexLod].distance)++indexLod;
                lod=&e.lods[indexLod];
                if(indexLod!=s.lod){
                    s.lod=indexLod;
                    s.delay=glm::mix(lod->delay.x,lod->delay.y,particleRandom(s.random));
                    s.duration=glm::mix(lod->duration.x,lod->duration.y,particleRandom(s.random));
                }
            }
            const auto delay=lod?lod->delay:e.delay,duration=lod?lod->duration:e.duration;
            const bool delayOnce=lod?lod->delayOnce:e.delayOnce;
            const float maxDistance=lod?lod->distance:e.lodDistance;
            const bool admittedDistance=emitterDistance<=maxDistance&&(!lod||lod->enabled);
            const bool lastLod=!lod||lod==&e.lods.back();
            const float distanceAlpha=lastLod?std::clamp((maxDistance-emitterDistance)/
                std::max(.001f,e.fadeoutRange),0.f,1.f):1.f;
            s.delayClock+=dt;
            if(s.delayClock>=s.delay)s.cycle+=dt;
            if(s.cycle>=s.duration){
                s.cycle=std::fmod(s.cycle,double(s.duration));++s.cycles;
                s.duration=glm::mix(duration.x,duration.y,particleRandom(s.random));
                if(!delayOnce){s.delayClock=0;s.delay=glm::mix(delay.x,delay.y,particleRandom(s.random));}
            }
            const float emitterTime=std::clamp(float(s.cycle)/duration.y,0.f,1.f);
            const float progress=std::clamp(effectSeconds/e.trackDuration,0.f,1.f);
            const auto cap=std::max(1u,unsigned(std::ceil(float(e.capacity)*scales[quality])));
            if(emitting&&admittedDistance&&effectSeconds<e.trackDuration&&s.delayClock>=s.delay&&(!e.loops||s.cycles<e.loops)){
                const float rate=std::max(0.f,(lod?lod->rate:e.rate).sample(emitterTime,s.random).x*e.trackEmission.sample(progress,s.random).x);
                s.fraction+=rate*scales[quality]*dt;
                const auto births=unsigned(std::min(std::floor(s.fraction),100000.));
                s.fraction-=births;
                const auto admitted=std::min(births,cap>unsigned(s.particles.size())?cap-unsigned(s.particles.size()):0u);
                for(unsigned i=0;i<admitted;++i)spawn(index,origin,emitterTime);
            }
            for(auto& p:s.particles){
                p.position+=p.velocity*dt;p.age+=dt;
                if(p.age>=p.life)continue;
                const float life=p.age/p.life;p.alpha=p.baseAlpha;p.size=p.baseSize;
                for(const auto& m:e.modules){
                    const auto eval=[&](const ParticleEvaluator& v){return v.sample(life,p.random);};
                    switch(m.op){
                    case WeatherParticleOp::Acceleration:p.velocity+=glm::vec3(eval(m.a))*eval(m.b).x*dt;break;
                    case WeatherParticleOp::AlphaLife:{const float a=eval(m.a).x;p.alpha=(m.flags&2)?p.alpha*a:a;break;}
                    case WeatherParticleOp::SizeLife:{const auto z=glm::vec2(eval(m.a));p.size=(m.flags&2)?p.size*z:z;break;}
                    case WeatherParticleOp::Animation:p.frame+=m.b.sample(emitterTime,p.random).x*dt;break;
                    case WeatherParticleOp::EffectAlpha:p.alpha*=e.trackAlpha.sample(progress,p.random).x;break;
                    case WeatherParticleOp::AlphaDistance:p.alpha*=weatherParticleFade(glm::distance(p.position,observer),m.p);break;
                    case WeatherParticleOp::Noise:{
                        glm::vec3 random;for(unsigned c=0;c<3;++c)random[c]=particleRandom(p.random)-.5f;
                        const auto impulse=random*glm::vec3(eval(m.a))*dt;
                        if(m.flags&1)p.position+=impulse;else p.velocity+=impulse;break;}
                    case WeatherParticleOp::Drag:p.velocity=weatherParticleDrag(p.velocity,m.a.sample(emitterTime,p.random).x,dt);break;
                    case WeatherParticleOp::SizeDistance:p.size*=weatherParticleSize(glm::distance(p.position,observer),m.p,m.q);break;
                    default:break;
                    }
                }
                p.rotation+=p.rotationRate*dt;p.position+=wind*(e.wind*dt);
                p.alpha=admittedDistance?std::max(0.f,p.alpha)*distanceAlpha:0;
            }
            std::erase_if(s.particles,[](const auto& p){return p.age>=p.life;});
        }
    }
    std::vector<WeatherParticleEmitter> program_;
    std::vector<State> states_;
    std::uint64_t generation_=0,nextId_=0,discontinuities_=0;
    double seconds_=-1,remainder_=0;
};
} // namespace engine::render
