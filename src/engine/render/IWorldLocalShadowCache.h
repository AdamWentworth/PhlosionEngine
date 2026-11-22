#pragma once
#include "engine/render/IWorldLocalShadow.h"
#include <utility>
#include <vector>

namespace engine::render {
// The owner changes revision whenever ANY cached caster's geometry, transform,
// membership or alpha coverage changes. Scene identities must not be recycled.
// Skinned, wind-deformed and other animated casters belong in the fresh pass.
struct WorldShadowCacheKey {
    std::uint64_t scene = 0, revision = 0;
    bool operator==(const WorldShadowCacheKey&) const = default;
};
enum class WorldShadowCacheResult { Unavailable, Reuse, Draw };
enum class WorldShadowCasters { All, Static, Dynamic };

class IWorldLocalShadowCache {
public:
    virtual ~IWorldLocalShadowCache() = default;
    virtual void setWorldLocalShadowIdentities(std::span<const std::uint64_t>) {}
    virtual void planWorldLocalShadowSources(std::span<const std::uint32_t>) {}
    virtual WorldShadowCacheResult beginWorldStaticShadow(std::uint32_t light,
        const WorldDirectionalShadowView& view, WorldShadowCacheKey key) = 0;
    virtual bool endWorldStaticShadow() = 0;
    virtual void invalidateWorldStaticShadows() = 0;
};

// Read-only admission against the target bank and the future source allocation.
// Separate optional interface leaves existing renderer/provider vtables intact.
class IWorldLocalShadowPlan {
public:
    virtual ~IWorldLocalShadowPlan() = default;
    virtual WorldShadowCacheResult queryWorldStaticShadow(std::span<const std::uint32_t>,
        std::uint32_t,const WorldDirectionalShadowView&,WorldShadowCacheKey) const = 0;
};

// One bank per independently fenced GPU target. Atlas reset clears a prepared
// read, but retains completed entries. Slot, light identity and projection must
// all match. Planned sources retain slots; legacy sequential callers may compact.
struct WorldShadowCache {
    struct Entry {
        WorldShadowCacheKey key{};
        WorldDirectionalShadowView view{};
        std::uint64_t light = 0;
        bool valid = false;
    };
    std::array<Entry,localShadowCapacity> entries{};
    Entry pending{};
    std::uint32_t slot = 0;
    bool writing = false, prepared = false, reading = false;

    static bool sameProjection(const WorldDirectionalShadowView& a, const WorldDirectionalShadowView& b) {
        return a.viewProjection==b.viewProjection && worldShadowCasterProjection(a)==worldShadowCasterProjection(b) && a.distanceMoments==b.distanceMoments && a.resolution==b.resolution;
    }
    bool matches(std::uint32_t tile,std::uint64_t light,const WorldDirectionalShadowView& view,WorldShadowCacheKey key) const {
        const auto& e=entries.at(tile);
        return e.valid && e.key==key && e.light==light && sameProjection(e.view,view);
    }
    bool prepare(std::uint32_t tile, std::uint64_t light, const WorldDirectionalShadowView& view, WorldShadowCacheKey key) {
        slot=tile; pending={key,view,light,true}; reading=false; prepared=true;
        writing=!matches(tile,light,view,key);
        if(writing) entries[tile].valid=false;
        return writing;
    }
    void commit() { entries.at(slot)=pending; writing=false; }
    void activate(std::uint32_t tile, std::uint64_t light, const WorldDirectionalShadowView& view) {
        reading=prepared && !writing && tile==slot && pending.light==light && sameProjection(pending.view,view);
        prepared=false;
    }
    void resetPass() { writing=prepared=reading=false; }
    void invalidate() { entries={}; resetPass(); }
};

struct WorldRectShadowPlan {
    struct Entry {
        std::uint32_t source;
        WorldDirectionalShadowView view;
        WorldShadowCacheKey key;
        WorldShadowCacheResult admission;
        std::uint64_t identity=0;
    };
    std::vector<Entry> entries;
    std::array<std::uint64_t,worldLocalShadowSourceCapacity> identities{};
    bool forceRefresh=false;
    template<class Draw> void execute(IWorldLocalShadow& renderer,Draw&& draw) const {
        auto* cache=dynamic_cast<IWorldLocalShadowCache*>(&renderer);
        if(cache && forceRefresh)cache->invalidateWorldStaticShadows();
        std::array<std::uint32_t,localShadowCapacity> sources{};
        for(std::size_t n=0;n<entries.size();++n)sources[n]=entries[n].source;
        if(cache)cache->setWorldLocalShadowIdentities(identities);
        if(cache)cache->planWorldLocalShadowSources(std::span(sources).first(entries.size()));
        for(std::size_t n=0;n<entries.size();++n) {
            const auto& e=entries[n];
            const auto result=cache?cache->beginWorldStaticShadow(e.source,e.view,e.key):WorldShadowCacheResult::Unavailable;
            if(result!=e.admission) {
                if(result==WorldShadowCacheResult::Draw)cache->endWorldStaticShadow();
                if(cache)cache->invalidateWorldStaticShadows();
                throw std::runtime_error("Shadow admission changed after scene planning");
            }
            if(result==WorldShadowCacheResult::Draw) {
                try {draw(n,e.view,WorldShadowCasters::Static);}
                catch(...) {cache->endWorldStaticShadow();cache->invalidateWorldStaticShadows();throw;}
                if(!cache->endWorldStaticShadow()){cache->invalidateWorldStaticShadows();throw std::runtime_error("Static shadow close failed");}
            }
            if(!renderer.beginWorldLocalShadow(e.source,e.view))throw std::runtime_error("Planned local shadow unavailable");
            try {if(!e.view.staticOnly || result==WorldShadowCacheResult::Unavailable)
                draw(n,e.view,e.view.staticOnly?WorldShadowCasters::Static:result==WorldShadowCacheResult::Unavailable?WorldShadowCasters::All:WorldShadowCasters::Dynamic);}
            catch(...) {renderer.endWorldLocalShadow();throw;}
            if(!renderer.endWorldLocalShadow())throw std::runtime_error("Planned local shadow close failed");
        }
    }
};
template<class Key>
WorldRectShadowPlan planWorldRectShadowsCachedByLight(IWorldLocalShadow& renderer,
    const WorldEnvironmentLighting& environment,Key&& keyForLight,bool forceRefresh=false,const std::array<float,3>& camera={}) {
    // A new receiver view may follow a completed view in the same host frame.
    // Reset allocation/read state while retaining completed static/update maps.
    renderer.clearWorldLocalShadows();
    WorldRectShadowPlan plan;plan.forceRefresh=forceRefresh;
    for(unsigned i=0;i<worldLocalShadowSourceCapacity;++i)plan.identities[i]=worldLocalShadowIdentity(environment,i);
    if(auto* cache=dynamic_cast<IWorldLocalShadowCache*>(&renderer))cache->setWorldLocalShadowIdentities(plan.identities);
    if(packWorldEnvironment(environment)[0][3]==0)return plan;
    std::array<std::uint32_t,localShadowCapacity> sources{};
    for(std::uint32_t i=0;i<worldLocalShadowSourceCapacity && plan.entries.size()<localShadowCapacity;++i) {
        const auto view=worldLocalShadowView(environment,i,camera);
        if(view.enabled){sources[plan.entries.size()]=i;plan.entries.push_back({i,view,keyForLight(i,view),WorldShadowCacheResult::Unavailable,worldLocalShadowIdentity(environment,i)});}
    }
    auto* query=dynamic_cast<IWorldLocalShadowPlan*>(&renderer);
    if(dynamic_cast<IWorldLocalShadowCache*>(&renderer) && !query)
        throw std::runtime_error("Renderer lacks read-only shadow admission");
    if(query)for(auto& e:plan.entries) {
        e.admission=query->queryWorldStaticShadow(std::span(sources).first(plan.entries.size()),e.source,e.view,e.key);
        if(forceRefresh && e.admission==WorldShadowCacheResult::Reuse)e.admission=WorldShadowCacheResult::Draw;
    }
    return plan;
}

template<class Key, class Draw>
void drawWorldRectShadowsCachedByLight(IWorldLocalShadow& renderer, const WorldEnvironmentLighting& environment,
                                      Key&& keyForLight, Draw&& draw, bool forceRefresh = false, const std::array<float,3>& camera={}) {
    auto* cache=dynamic_cast<IWorldLocalShadowCache*>(&renderer);
    if(cache && forceRefresh) cache->invalidateWorldStaticShadows();
    if(packWorldEnvironment(environment)[0][3]==0) return;
    std::array<std::uint32_t,localShadowCapacity> sources{};
    std::array<WorldDirectionalShadowView,localShadowCapacity> views{};
    std::size_t count=0;
    for(std::uint32_t i=0;i<worldLocalShadowSourceCapacity && count<localShadowCapacity;++i) {
        const auto view=worldLocalShadowView(environment,i,camera);
        if(view.enabled){sources[count]=i;views[count++]=view;}
    }
    if(cache) {
        std::array<std::uint64_t,worldLocalShadowSourceCapacity> ids{};
        for(unsigned i=0;i<ids.size();++i)ids[i]=worldLocalShadowIdentity(environment,i);
        cache->setWorldLocalShadowIdentities(ids);
        cache->planWorldLocalShadowSources(std::span(sources).first(count));
    }
    for(std::size_t n=0;n<count;++n) {
        const auto i=sources[n];const auto& view=views[n];
        const auto key=keyForLight(i,view);
        const auto result=cache ? cache->beginWorldStaticShadow(i,view,key) : WorldShadowCacheResult::Unavailable;
        if(result==WorldShadowCacheResult::Draw) {
            try { draw(view,WorldShadowCasters::Static); }
            catch(...) { cache->endWorldStaticShadow(); cache->invalidateWorldStaticShadows(); throw; }
            if(!cache->endWorldStaticShadow()) {
                cache->invalidateWorldStaticShadows();
                throw std::runtime_error("Static local shadow pass close failed");
            }
        }
        if(!renderer.beginWorldLocalShadow(i,view)) throw std::runtime_error("Local shadow pass unavailable");
        try { if(!view.staticOnly || result==WorldShadowCacheResult::Unavailable)
            draw(view,view.staticOnly?WorldShadowCasters::Static:result==WorldShadowCacheResult::Unavailable ? WorldShadowCasters::All : WorldShadowCasters::Dynamic); }
        catch(...) { renderer.endWorldLocalShadow(); throw; }
        if(!renderer.endWorldLocalShadow()) throw std::runtime_error("Local shadow pass close failed");
    }
}
template<class Draw>
void drawWorldRectShadowsCached(IWorldLocalShadow& renderer, const WorldEnvironmentLighting& environment,
                               WorldShadowCacheKey key, Draw&& draw, bool forceRefresh = false, const std::array<float,3>& camera={}) {
    drawWorldRectShadowsCachedByLight(renderer,environment,[&](auto,const auto&){return key;},
        std::forward<Draw>(draw),forceRefresh,camera);
}
}
