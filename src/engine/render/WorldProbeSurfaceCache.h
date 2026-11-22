#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <algorithm>
#include <stdexcept>

namespace engine::render {
// Four resident probes, two generations each. Only all six finished faces can
// replace the published generation. GPU allocations use bounded slot/bank keys.
class WorldProbeSurfaceCache {
    struct Probe {
        std::uint64_t id=0,published=0,pending=0;
        unsigned bank=0,mask=0;
        int resolution=0;
    };
    std::array<Probe,4> probes_{};
    int active_=-1;
    unsigned face_=0;
    static std::string key(unsigned slot,unsigned bank,unsigned face) {
        return "probe/"+std::to_string(slot)+"/"+std::to_string(bank)+"/"+std::to_string(face);
    }
public:
    bool active() const {return active_>=0;}
    std::string begin(std::uint64_t id,std::uint64_t revision,unsigned face,int resolution) {
        if(active() || !id || !revision || face>=6 || resolution<16 || resolution>512 ||
           (resolution&(resolution-1)))return {};
        unsigned slot=0;
        for(;slot<probes_.size();++slot)if(probes_[slot].id==id)break;
        if(slot==probes_.size()) {
            for(slot=0;slot<probes_.size();++slot)if(!probes_[slot].id)break;
            if(slot==probes_.size())return {};
            probes_[slot].id=id;
        }
        auto& p=probes_[slot];
        if(p.published==revision || (p.resolution && p.resolution!=resolution))return {};
        if(p.pending!=revision) {p.pending=revision;p.mask=0;p.resolution=resolution;}
        if(p.resolution!=resolution)return {};
        p.mask&=~(1u<<face);
        active_=int(slot);face_=face;
        return key(slot,1-p.bank,face);
    }
    void finish() {
        if(!active())return;
        auto& p=probes_[active_];p.mask|=1u<<face_;
        if(p.mask==63) {p.published=p.pending;p.bank=1-p.bank;p.pending=0;p.mask=0;}
        active_=-1;
    }
    void cancel() {active_=-1;}
    std::string select(std::uint64_t id,unsigned face) const {
        if(active() || !id || face>=6)return {};
        for(unsigned i=0;i<probes_.size();++i)
            if(probes_[i].id==id && probes_[i].published)return key(i,probes_[i].bank,face);
        return {};
    }
    std::uint64_t revision(std::uint64_t id) const {
        for(const auto& p:probes_)if(p.id==id)return p.published;
        return 0;
    }
    bool release(std::uint64_t id) {
        if(active() || !id)return false;
        for(auto& p:probes_)if(p.id==id) {p={};return true;}
        return false;
    }
    void retain(std::span<const std::uint64_t> ids) {
        if(active())throw std::logic_error("Probe eviction during material capture");
        for(auto& p:probes_)if(p.id && std::find(ids.begin(),ids.end(),p.id)==ids.end())p={};
    }
    void clear() {probes_={};active_=-1;}
};
}
