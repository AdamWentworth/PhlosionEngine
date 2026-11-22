#pragma once
#include <array>
#include <span>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace engine::render {
class WorldProbeResidency {
    struct Slot {std::uint64_t id=0,used=0;};
    std::array<Slot,4> slots_{};
    std::uint64_t clock_=0;
public:
    std::array<std::uint64_t,4> reserve(std::span<const std::uint64_t> requested) {
        if(requested.empty() || requested.size()>4)throw std::invalid_argument("Invalid probe residency set");
        for(unsigned i=0;i<requested.size();++i)
            if(!requested[i] || std::find(requested.begin(),requested.begin()+i,requested[i])!=requested.begin()+i)
                throw std::invalid_argument("Duplicate or null probe identity");
        ++clock_;
        for(auto id:requested)for(auto& s:slots_)if(s.id==id)s.used=clock_;
        for(auto id:requested) {
            if(std::find_if(slots_.begin(),slots_.end(),[&](const auto& s){return s.id==id;})!=slots_.end())continue;
            auto victim=slots_.end();
            for(auto it=slots_.begin();it!=slots_.end();++it)
                if(it->used!=clock_ && (victim==slots_.end() || it->used<victim->used))victim=it;
            if(victim==slots_.end())throw std::logic_error("No unpinned probe slot");
            *victim={id,clock_};
        }
        return {slots_[0].id,slots_[1].id,slots_[2].id,slots_[3].id};
    }
    void clear(){slots_={};clock_=0;}
};
}
