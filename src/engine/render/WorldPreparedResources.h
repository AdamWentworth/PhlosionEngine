#pragma once
#include "engine/render/IWorldPreparedResources.h"
#include <atomic>
#include <vector>
#include <utility>

namespace engine::render {
inline std::uint64_t nextWorldPreparedResourceGeneration() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1,std::memory_order_relaxed);
}
// O(1) checked native handles; no borrowed CPU input or identity-string lookup.
// Invalidate BEFORE any referenced native resource is replaced/destroyed.
// No eviction of live entries: capacity exhaustion preserves the ordinary path.
template<class T,std::size_t Capacity=16384> class WorldPreparedResources {
    struct Entry {std::uint64_t scope;T value;};
    std::uint64_t generation_=nextWorldPreparedResourceGeneration();
    std::vector<Entry> entries_;
public:
    WorldPreparedResources()=default;
    WorldPreparedResources(const WorldPreparedResources&)=delete;
    WorldPreparedResources& operator=(const WorldPreparedResources&)=delete;
    WorldPreparedResourceToken insert(std::uint64_t scope,T value) {
        if(!scope || entries_.size()>=Capacity)return {};
        const auto index=static_cast<std::uint32_t>(entries_.size());
        entries_.push_back({scope,std::move(value)});
        return {this,generation_,scope,index};
    }
    const T* get(WorldPreparedResourceToken token,std::uint64_t scope) const {
        if(!scope || token.owner!=this || token.generation!=generation_ ||
           token.scope!=scope || token.index>=entries_.size() || entries_[token.index].scope!=scope)return nullptr;
        return &entries_[token.index].value;
    }
    void clear(){generation_=nextWorldPreparedResourceGeneration();entries_.clear();}
    std::size_t size()const{return entries_.size();}
};
}
