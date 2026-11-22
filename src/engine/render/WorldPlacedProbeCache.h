#pragma once
#include "engine/render/IWorldLocalReflection.h"
#include <span>
#include <cstdint>
namespace engine::render {
class WorldPlacedProbeCache {
    struct Probe {
        std::uint64_t id=0,generation=0,pending=0;
        WorldLocalReflectionProbe published{},writing{};
        unsigned bank=0,mask=0,filterStep=0;
        float blend=1;
    };
    std::array<Probe,5> probes_{}; // four bounded local residents plus the independent sky fallback
    int active_=-1;
    unsigned face_=0;
    bool deferred_=false;
public:
    static constexpr int resolution=128,atlasExtent=16*resolution;
    static constexpr std::uint64_t globalId=~std::uint64_t(0);
    bool begin(std::uint64_t id,std::uint64_t generation,const WorldLocalReflectionProbe& p,unsigned face) {
        if(active_>=0 || !id || !generation || face>=6 || p.resolution!=resolution)return false;
        (void)packWorldLocalReflection(p);
        unsigned slot=0;
        if(id==globalId){slot=4;probes_[slot].id=id;}
        else {
            for(;slot<4;++slot)if(probes_[slot].id==id)break;
            if(slot==4){for(slot=0;slot<4;++slot)if(!probes_[slot].id)break;if(slot==4)return false;probes_[slot].id=id;}
        }
        auto& probe=probes_[slot];
        if(probe.generation==generation)return false;
        if(probe.pending!=generation){if(probe.blend<1)return false;probe.pending=generation;probe.mask=0;probe.filterStep=0;probe.writing=p;}
        probe.mask&=~(1u<<face);active_=int(slot);face_=face;return true;
    }
    bool active() const {return active_>=0;}
    unsigned face() const {return face_;}
    std::array<int,2> offset() const {
        if(!active())return {};
        const auto tile=unsigned(active_)*2+1-probes_[active_].bank;
        return {int(tile%4)*4*resolution,int(tile/4)*4*resolution};
    }
    WorldLocalReflectionUniforms filterState() const {
        if(!active())return {};
        auto p=packWorldLocalReflection(probes_[active_].writing);
        auto o=offset();p[3]={float(resolution)/atlasExtent,float(resolution)/atlasExtent,0,0};
        p[4]={float(o[0])/atlasExtent,float(o[1])/atlasExtent,0,1};return p;
    }
    bool lastFace() const {return active() && (probes_[active_].mask|(1u<<face_))==63;}
    void finish(bool filtered=true) {
        if(!active())return;
        auto& p=probes_[active_];p.mask|=1u<<face_;
        if(p.mask==63 && filtered){publish(p,false);}
        active_=-1;
    }
    void cancel(){active_=-1;}
    void deferFiltering(bool value){deferred_=value;}
    bool deferredFiltering() const{return deferred_;}
    bool published(std::uint64_t id) const {for(const auto& p:probes_)if(p.id==id)return p.generation!=0;return false;}
    bool blending(std::uint64_t id) const {for(const auto& p:probes_)if(p.id==id)return p.blend<1;return false;}
    void advanceBlend(float delta) {if(!std::isfinite(delta)||delta<0)throw std::invalid_argument("Invalid probe blend delta");
        for(auto& p:probes_)p.blend=std::min(1.f,p.blend+delta);}
    void retain(std::span<const std::uint64_t> ids) {
        if(active())throw std::logic_error("Probe eviction during a face");
        for(auto& p:probes_)if(p.id && p.id!=globalId && std::find(ids.begin(),ids.end(),p.id)==ids.end())p={};
    }
    bool beginFilter(std::uint64_t id) {
        if(active())return false;
        for(unsigned i=0;i<probes_.size();++i)if(probes_[i].id==id && probes_[i].pending && probes_[i].mask==63) {
            active_=int(i);face_=0;return true;
        }
        return false;
    }
    unsigned filterStep() const {if(!active())throw std::logic_error("No probe filter");return probes_[active_].filterStep;}
    bool finishFilterStep() {
        if(!active() || probes_[active_].mask!=63)throw std::logic_error("Incomplete probe filter");
        auto& p=probes_[active_];const bool done=++p.filterStep==10;
        if(done)publish(p,true);active_=-1;return done;
    }
private:
    static void publish(Probe& p,bool blend) {
        p.blend=blend && p.generation?0.f:1.f;p.bank=1-p.bank;p.generation=p.pending;
        p.published=p.writing;p.pending=0;p.mask=0;p.filterStep=0;
    }
public:
    void clear(){probes_={};active_=-1;}
    void invalidate(std::span<const std::uint64_t> ids) {
        for(auto& p:probes_)if(p.id && std::find(ids.begin(),ids.end(),p.id)!=ids.end()){
            p.generation=p.pending=0;p.mask=0;p.filterStep=0;p.blend=1;
        }
        active_=-1;
    }
    WorldLocalReflectionUniforms select(std::span<const std::uint64_t> ids,std::uint64_t exclude=0) const {
        WorldLocalReflectionUniforms state{};state[0][3]=3;
        const auto& global=probes_[4];
        if(global.generation && exclude!=globalId) {
            const unsigned tile=8+global.bank,old=8+1-global.bank;
            state[1]={float(resolution)/atlasExtent,float(resolution)/atlasExtent,float(resolution),1+global.blend};
            for(unsigned part=0;part<4;++part)state[28][part]=float((global.generation>>(part*16))&65535u);
            state[2]={float((tile%4)*4*resolution)/atlasExtent,float((tile/4)*4*resolution)/atlasExtent,
                      float((old%4)*4*resolution)/atlasExtent,float((old/4)*4*resolution)/atlasExtent};
        }
        unsigned out=4;
        std::array<unsigned,4> order{0,1,2,3};
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){
            if(probes_[a].published.priority!=probes_[b].published.priority)
                return probes_[a].published.priority<probes_[b].published.priority;
            return probes_[a].id<probes_[b].id;
        });
        for(unsigned slot:order) {
            const auto& p=probes_[slot];
            if(!p.id || !p.generation || p.id==exclude ||
               std::find(ids.begin(),ids.end(),p.id)==ids.end())continue;
            const auto packed=packWorldLocalReflection(p.published);
            state[out]=packed[0];state[out][3]=1+p.blend;state[out+1]=packed[1];state[out+2]=packed[2];
            const auto tile=slot*2+p.bank;
            state[out+3]={float(resolution)/atlasExtent,float(resolution)/atlasExtent,
                         float((tile%4)*4*resolution)/atlasExtent,float((tile/4)*4*resolution)/atlasExtent};
            std::copy_n(packed.begin()+52,4,state.begin()+52+(out-4));
            // Four exact 16-bit lanes identify the published probe independently
            // of its position, orientation or atlas bank. These rows are emission
            // gains only in the separate capture/relight uniform mode.
            for(unsigned part=0;part<4;++part)state[20+(out-4)/4][part]=float((p.id>>(part*16))&65535u);
            for(unsigned part=0;part<4;++part)state[24+(out-4)/4][part]=float((p.generation>>(part*16))&65535u);
            out+=4;
        }
        return state;
    }
    std::array<std::uint64_t,4> ids() const {return {probes_[0].id,probes_[1].id,probes_[2].id,probes_[3].id};}
};
}
