#include "engine/render/WorldResourceScopeState.h"
#include "engine/render/WorldMaterialBindingCache.h"
#include "engine/render/WorldPreparedResources.h"
#include <memory>

#include <limits>
#include <string>
#include <vector>

bool test_world_resource_scope_contract(std::string& outFail) {
    using namespace engine::render;
    const auto require = [&](bool value, const char* message) {
        if (!value) outFail = message;
        return value;
    };
    {
        WorldPreparedResources<int,2> geometry,material;
        const auto g=geometry.insert(11,27),m=material.insert(11,91);
        if(!require(g && m && geometry.get(g,11) && *geometry.get(g,11)==27,
            "Prepared resources did not preserve their native value"))return false;
        if(!require(!geometry.get(g,12) && !geometry.get(m,11) && !material.get(g,11) &&
            !geometry.insert(0,3),"Prepared resources crossed scope/owner/kind"))return false;
        auto invalid=g;invalid.index=UINT32_MAX;
        if(!require(!geometry.get(invalid,11),"Invalid token index was accepted"))return false;
        const auto second=geometry.insert(11,33);
        if(!require(second && !geometry.insert(11,44) && *geometry.get(g,11)==27,
            "Capacity exhaustion evicted a live resource handle"))return false;
        geometry.clear();
        const auto replacement=geometry.insert(11,49);
        if(!require(replacement && !geometry.get(g,11) && !geometry.get(second,11) &&
            *geometry.get(replacement,11)==49,"Retirement reused stale token storage"))return false;
        using Table=WorldPreparedResources<int,2>;
        alignas(Table) std::byte storage[sizeof(Table)];
        auto* old=std::construct_at(reinterpret_cast<Table*>(storage));
        const auto oldToken=old->insert(11,7);std::destroy_at(old);
        auto* recreated=std::construct_at(reinterpret_cast<Table*>(storage));
        const auto newToken=recreated->insert(11,8);
        const bool safe=!recreated->get(oldToken,11) && *recreated->get(newToken,11)==8;
        std::destroy_at(recreated);
        if(!require(safe,"Renderer address reuse accepted a previous lifetime's handle"))return false;
    }
    {
        WorldResourceScopeState shared;WorldResourceScopeStats state;
        const auto x=shared.create(16),y=shared.create(16);
        if(!require(shared.bindAssetGroup(x,77,64) && shared.bindAssetGroup(y,77,64) &&
            !shared.bindAssetGroup(x,78,64), "Immutable group binding is not stable"))return false;
        shared.query(x,state);const auto group=state.assetGroup;
        if(!require(group && !shared.release(group) && !shared.begin(group),"Asset owner escaped consumer lifetime"))return false;
        shared.begin(x);const auto dynamicKey=shared.key("mesh");std::string immutableKey;
        {
            WorldResourceScopeState::AssetResources guard(shared);
            immutableKey=shared.key("mesh");
            WorldResourceCounts c;c.payloadBytes=48;c.geometryBuffers=1;
            if(!require(shared.active()==group && shared.admit(48),"Asset allocation did not route to group"))return false;
            shared.commit(c);
        }
        if(!require(shared.active()==x && shared.key("mesh")==dynamicKey,"Asset phase leaked into view state"))return false;
        shared.end();shared.begin(y);
        if(!require(shared.assetKey("mesh")==immutableKey && shared.key("mesh")!=dynamicKey,
            "Compatible consumers failed to share immutable identity or leaked dynamic identity"))return false;
        shared.end();shared.release(x);shared.collect(x);
        if(!require(shared.pending().empty() && shared.begin(y),"First consumer released a shared upload"))return false;
        { WorldResourceScopeState::AssetResources guard(shared);
          if(!require(!shared.admit(17),"Shared payload limit was bypassed"))return false; }
        shared.end();
        if(!require(shared.query(y,state) && state.status==WorldResourceScopeStatus::BudgetExceeded &&
            !shared.begin(y),"Group allocation failure was hidden from consumer"))return false;
        shared.release(y);
        const auto z=shared.create(16);
        if(!require(shared.bindAssetGroup(z,77,64),"Pending group prevented fresh replacement"))return false;
        shared.query(z,state);
        if(!require(state.assetGroup!=group && shared.pending().size()==2,"Replacement reused retiring GPU resources"))return false;
        for(auto id:shared.pending())shared.collect(id);
        if(!require(shared.stats().reclaimed.payloadBytes==48 && shared.stats().retained.payloadBytes==0,
            "Shared upload accounting reclaimed incorrectly"))return false;
        shared.release(z);for(auto id:shared.pending())shared.collect(id);
        if(!require(shared.stats().liveScopes==0,"Shared resource lifetime leaked"))return false;
    }
    WorldResourceScopeState first;
    WorldResourceScopeState second;
    const auto a = first.create(120);
    const auto b = first.create(240);
    const auto foreign = second.create(120);
    if (!require(a && b > a && foreign > b, "Scope handles must be unique across renderer instances") ||
        !require(!first.begin(0) && !first.begin(foreign), "Zero/foreign handles must be rejected") ||
        !require(first.begin(a) && !first.begin(b), "Scopes must reject nesting")) return false;
    const auto aKey = first.key("same mesh");
    WorldResourceCounts geometry;
    geometry.geometryBuffers = 2;
    geometry.payloadBytes = 100;
    geometry.apiAllocationBytes = 131072;
    if (!require(first.admit(100), "Initial allocation must fit")) return false;
    first.commit(geometry);
    if (!require(first.admit(20) && !first.admit(21), "Payload cap must include retained allocations")) return false;
    WorldResourceScopeStats query;
    if (!require(first.query(a, query) && query.status == WorldResourceScopeStatus::BudgetExceeded &&
        query.retained.payloadBytes == 100, "Budget failure must preserve actual allocation counts")) return false;
    if (!require(first.end() && !first.end() && first.begin(b), "End must balance one active scope") ||
        !require(first.key("same mesh") != aKey, "Identical cache keys must be scope independent") ||
        !require(first.release(b) && !first.release(b) && !first.admitted(), "Release must close active admission")) return false;
    first.collect(b);
    if (!require(!first.query(b, query) && !first.admitted(), "Collected scopes must disappear without making stale active draws unowned")) return false;
    first.end();
    if (!require(first.key("same mesh") == "same mesh", "Legacy identity must remain unchanged") ||
        !require(first.release(a), "Failed scopes must still be releasable")) return false;
    auto totals = first.stats();
    if (!require(totals.pendingReleaseScopes == 1 && totals.pendingRelease.geometryBuffers == 2 &&
        totals.pendingRelease.payloadBytes == 100, "Pending counters must retain native resources")) return false;
    first.fail(a, "Completion failed", WorldResourceScopeStatus::GpuWaitFailed);
    first.collect(a);
    totals = first.stats();
    if (!require(totals.liveScopes == 0 && totals.retained.payloadBytes == 0 &&
        totals.allocated.payloadBytes == 100 && totals.reclaimed.payloadBytes == 100,
        "Collection must reclaim metadata and retain allocation/reclamation history")) return false;

    const auto rebuilt = second.create(120);
    if (!require(second.begin(rebuilt) && second.admit(100), "Pipeline rebuild fixture admission")) return false;
    second.commit(geometry);
    second.end();
    second.reclaim(rebuilt, geometry);
    if (!require(second.query(rebuilt, query) && query.status == WorldResourceScopeStatus::Ready &&
        query.retained.geometryBuffers == 0 && query.retained.payloadBytes == 0,
        "Native cache invalidation must debit resources while keeping the scope live")) return false;
    if (!require(second.begin(rebuilt) && second.admit(100), "Invalidated payload capacity must be reusable")) return false;
    second.commit(geometry);
    second.end();
    second.release(rebuilt);
    second.fail(rebuilt, "Test completion failure", WorldResourceScopeStatus::GpuWaitFailed);
    totals = second.stats();
    if (!require(second.query(rebuilt, query) && query.releasePending &&
        query.status == WorldResourceScopeStatus::GpuWaitFailed &&
        totals.retained.payloadBytes == 100 && totals.pendingRelease.payloadBytes == 100 &&
        totals.allocated.payloadBytes - totals.reclaimed.payloadBytes == totals.retained.payloadBytes,
        "Failed completion must keep rebuilt resources pending and preserve allocation history")) return false;
    second.collect(rebuilt);
    totals = second.stats();
    if (!require(totals.allocated.payloadBytes == 200 && totals.reclaimed.payloadBytes == 200 &&
        totals.retained.payloadBytes == 0, "Rebuilt resources must be reclaimed exactly once")) return false;

    std::vector<WorldResourceScopeId> ids;
    for (std::uint64_t i = 0; i < WorldResourceScopeState::maxScopes; ++i) ids.push_back(first.create(0));
    if (!require(first.create(1) == 0, "Live scope count must be bounded")) return false;
    first.release(ids.front());
    if (!require(first.create(1) == 0, "Pending scopes must count towards admission limits")) return false;
    first.collect(ids.front());
    if (!require(first.create(1) != 0, "Collected metadata capacity must be reusable")) return false;
    std::uint64_t bytes = 0;
    if (!require(worldGeometryPayload(3, 112, 3, bytes) && bytes == 348 &&
        !worldGeometryPayload((std::numeric_limits<std::size_t>::max)(), 112, 3, bytes),
        "Geometry byte accounting must check arithmetic") ||
        !require(worldTexturePayload(4, 2, 3, bytes) && bytes == 44 &&
        !worldTexturePayload(0, 2, 1, bytes) && !worldTexturePayload(4, 2, 33, bytes),
        "Texture accounting must include all retained mip levels")) return false;

    backend::WorldTextureData texture;
    const unsigned char pixels[4]{255,255,255,255};
    std::string identity="binding-a";
    texture.key=texture.cacheKey=identity.c_str();texture.rgba=pixels;texture.width=texture.height=1;
    WorldMaterialBindingCache<int> bindings;
    auto binding=worldMaterialBindingIdentity(&texture,1);
    if(!require(binding.has_value(),"Managed named texture must support binding reuse"))return false;
    bindings.insert(*binding,17);
    texture.emissiveFactorR=5;texture.modelMatrix[12]=42;texture.environment.exposure=2;
    auto same=worldMaterialBindingIdentity(&texture,1);
    if(!require(same && bindings.find(*same) && *bindings.find(*same)==17,
        "Per-draw transforms/light/material constants must not invalidate texture bindings"))return false;
    identity="binding-b";texture.key=texture.cacheKey=identity.c_str();
    auto changed=worldMaterialBindingIdentity(&texture,1);
    if(!require(changed && !bindings.find(*changed),"Mutable caller identity text must not alias retained bindings"))return false;
    identity="binding-a";texture.key=texture.cacheKey=identity.c_str();binding=worldMaterialBindingIdentity(&texture,1);
    if(!require(bindings.find(*binding),"Cache must own identity text independently of callers"))return false;
    for(unsigned role=0;role<8;++role) {
        auto different=*binding;different.textures[role]="different-map";
        if(!require(!bindings.find(different),"All eight texture roles participate in binding identity"))return false;
        for(unsigned field=0;field<6;++field) {
            different=*binding;++different.sampling[role][field];
            if(!require(!bindings.find(different),"Dimensions/sampler/color-space/mip count must distinguish bindings"))return false;
        }
    }
    changed=worldMaterialBindingIdentity(&texture,2);
    if(!require(changed && !bindings.find(*changed),"Resource scopes must not share material bindings"))return false;
    texture.linearMipSampling=1;changed=worldMaterialBindingIdentity(&texture,1);
    if(!require(changed && !bindings.find(*changed),"Linear mip sampling must distinguish bindings"))return false;
    texture.linearMipSampling=0;
    if(!require(!worldMaterialBindingIdentity(&texture,0) && !worldMaterialBindingIdentity(nullptr,1),
        "Unmanaged or missing textures must use normal resolution"))return false;
    texture.key="";if(!require(!worldMaterialBindingIdentity(&texture,1),"Anonymous textures cannot be retained"))return false;
    texture.key=identity.c_str();
    texture.lightProjectionKey=IWorldDepthControl::snapshotTextureKey;
    if(!require(!worldMaterialBindingIdentity(&texture,1),"Frame-owned depth snapshots must bypass reuse"))return false;
    texture.lightProjectionKey=nullptr;
    for(unsigned i=2;i<1100;++i){auto k=*binding;k.scope=i;bindings.insert(k,i);}
    if(!require(bindings.size()==WorldMaterialBindingCache<int>::capacity,"CPU binding storage must be bounded"))return false;
    bindings.clear();
    if(!require(!bindings.find(*binding) && bindings.size()==0,"Native retirement must invalidate retained lookups"))return false;
    return true;
}
