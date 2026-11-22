#pragma once
#include "engine/render/IWorldLocalShadowCache.h"
#include <deque>
#include <cstring>
#include <type_traits>
namespace engine::render {
struct WorldShadowContent {
    std::uint64_t value=14695981039346656037ull;
    void bytes(const void* data,std::size_t size) {
        const auto* p=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i){value^=p[i];value*=1099511628211ull;}
    }
    template<class T> void add(const T& v){static_assert(std::is_trivially_copyable_v<T>);bytes(&v,sizeof(v));}
};
struct WorldShadowUpdateKey {
    std::uint64_t scene=0,light=0,statics=0,dynamics=0;
    bool operator==(const WorldShadowUpdateKey&) const=default;
};
struct WorldShadowUpdateQuery {bool valid=false,exact=false;};
class IWorldShadowScheduling {
public:
    virtual ~IWorldShadowScheduling()=default;
    // D3D12 owns one atlas; Vulkan owns two independently fenced copies.
    static constexpr unsigned maxBanks=2;
    virtual unsigned worldShadowScheduleBank() const{return 0;}
    virtual WorldShadowUpdateQuery queryWorldShadowUpdate(std::span<const std::uint32_t>,std::uint32_t,
        const WorldDirectionalShadowView&,WorldShadowUpdateKey) const=0;
    virtual bool reuseWorldShadowUpdate(std::uint32_t,const WorldDirectionalShadowView&,WorldShadowUpdateKey)=0;
    virtual void commitWorldShadowUpdate(std::uint32_t,const WorldDirectionalShadowView&,WorldShadowUpdateKey)=0;
};
class WorldShadowUpdateCache {
    struct Entry {WorldShadowUpdateKey key{};WorldDirectionalShadowView view{};bool valid=false;};
    std::array<Entry,localShadowCapacity> entries_{};
public:
    WorldShadowUpdateQuery query(unsigned slot,const WorldDirectionalShadowView& view,WorldShadowUpdateKey key) const {
        if(slot>=entries_.size() || !key.scene || !key.light)return {};
        const auto& e=entries_[slot];
        const bool valid=e.valid && e.key.scene==key.scene && e.key.light==key.light &&
            WorldShadowCache::sameProjection(e.view,view) && e.view.momentKernel==view.momentKernel &&
            e.view.momentCentreRay==view.momentCentreRay && e.view.staticOnly==view.staticOnly;
        return {valid,valid && e.key==key};
    }
    void commit(unsigned slot,const WorldDirectionalShadowView& view,WorldShadowUpdateKey key) {
        if(slot>=entries_.size() || !key.scene || !key.light)throw std::invalid_argument("Invalid shadow update");
        entries_[slot]={key,view,true};
    }
    void erase(unsigned slot){if(slot<entries_.size())entries_[slot]={};}
    void clear(){entries_={};}
};
struct WorldShadowScheduleStats {unsigned admitted=0,refreshed=0,reused=0,deferred=0,unready=0;};
class WorldShadowSchedule {
    std::array<std::deque<std::uint64_t>,IWorldShadowScheduling::maxBanks> pending_;
    WorldShadowScheduleStats stats_{};
public:
    struct Plan {
        WorldRectShadowPlan shadows;
        std::vector<WorldShadowUpdateKey> keys;
        std::vector<bool> refresh,available;
        WorldShadowSchedule* owner=nullptr;
        unsigned bank=0;
        template<class Draw> void execute(IWorldLocalShadow& renderer,Draw draw) const {
            auto* schedule=dynamic_cast<IWorldShadowScheduling*>(&renderer);
            auto* cache=dynamic_cast<IWorldLocalShadowCache*>(&renderer);
            if(!schedule || !cache)throw std::runtime_error("Scheduled shadows unavailable");
            if(shadows.forceRefresh)cache->invalidateWorldStaticShadows();
            std::vector<std::uint32_t> sources;for(const auto& e:shadows.entries)sources.push_back(e.source);
            cache->setWorldLocalShadowIdentities(shadows.identities);
            cache->planWorldLocalShadowSources(sources);
            for(unsigned n=0;n<shadows.entries.size();++n) {
                const auto& e=shadows.entries[n];
                if(!refresh[n]) {
                    if(available[n] && !schedule->reuseWorldShadowUpdate(e.source,e.view,keys[n]))
                        throw std::runtime_error("Admitted shadow cache disappeared");
                    continue;
                }
                const auto result=cache->beginWorldStaticShadow(e.source,e.view,e.key);
                if(result!=e.admission) {
                    if(result==WorldShadowCacheResult::Draw)cache->endWorldStaticShadow();
                    cache->invalidateWorldStaticShadows();
                    throw std::runtime_error("Scheduled shadow admission changed");
                }
                try {
                    if(result==WorldShadowCacheResult::Draw) {
                        draw(n,e.view,WorldShadowCasters::Static);
                        if(!cache->endWorldStaticShadow())throw std::runtime_error("Static shadow close failed");
                    }
                    if(!renderer.beginWorldLocalShadow(e.source,e.view))throw std::runtime_error("Local shadow unavailable");
                    if(!e.view.staticOnly || result==WorldShadowCacheResult::Unavailable)
                        draw(n,e.view,e.view.staticOnly?WorldShadowCasters::Static:result==WorldShadowCacheResult::Unavailable?WorldShadowCasters::All:WorldShadowCasters::Dynamic);
                    if(!renderer.endWorldLocalShadow())throw std::runtime_error("Local shadow close failed");
                    schedule->commitWorldShadowUpdate(e.source,e.view,keys[n]);
                    std::erase(owner->pending_[bank],keys[n].light);
                } catch(...) {cache->endWorldStaticShadow();renderer.endWorldLocalShadow();cache->invalidateWorldStaticShadows();throw;}
            }
        }
    };
    const WorldShadowScheduleStats& stats()const{return stats_;}
    template<class DynamicKey>
    Plan prepare(IWorldLocalShadow& renderer,WorldRectShadowPlan shadows,std::uint64_t scene,
        unsigned budget,DynamicKey dynamicKey) {
        auto* schedule=dynamic_cast<IWorldShadowScheduling*>(&renderer);
        if(!schedule || !scene || (!budget&&!shadows.entries.empty()) || budget>localShadowCapacity)throw std::invalid_argument("Invalid shadow schedule");
        Plan p;p.shadows=std::move(shadows);p.owner=this;p.bank=schedule->worldShadowScheduleBank();stats_={};
        auto& pending=pending_.at(p.bank);
        std::vector<std::uint32_t> sources;for(const auto& e:p.shadows.entries)sources.push_back(e.source);
        p.refresh.resize(sources.size());p.available.resize(sources.size());
        std::vector<std::uint64_t> dirty;
        for(unsigned i=0;i<sources.size();++i) {
            const auto& e=p.shadows.entries[i];
            WorldShadowUpdateKey key{scene,e.identity?e.identity:0x8000000000000000ull+e.source,e.key.revision,e.view.staticOnly?std::uint64_t(0):std::uint64_t(dynamicKey(e.source,e.view))};
            p.keys.push_back(key);++stats_.admitted;
            const auto q=schedule->queryWorldShadowUpdate(sources,e.source,e.view,key);
            p.available[i]=q.valid && !p.shadows.forceRefresh;
            if(!q.exact || p.shadows.forceRefresh || e.admission==WorldShadowCacheResult::Unavailable)dirty.push_back(key.light);
        }
        std::erase_if(pending,[&](auto id){return std::find(dirty.begin(),dirty.end(),id)==dirty.end();});
        for(auto id:dirty)if(std::find(pending.begin(),pending.end(),id)==pending.end())pending.push_back(id);
        unsigned remaining=budget;
        for(auto id:pending) {
            if(!remaining)break;
            --remaining;
            const auto it=std::find_if(p.keys.begin(),p.keys.end(),[&](const auto& k){return k.light==id;});
            p.refresh[std::size_t(it-p.keys.begin())]=true;
        }
        for(unsigned i=0;i<sources.size();++i) {
            if(p.refresh[i])++stats_.refreshed;
            else if(p.available[i])++stats_.reused;
            else ++stats_.unready;
            if(!p.refresh[i] && std::find(dirty.begin(),dirty.end(),p.keys[i].light)!=dirty.end())++stats_.deferred;
        }
        return p;
    }
};
}
