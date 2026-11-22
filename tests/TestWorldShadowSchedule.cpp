#include "engine/render/WorldShadowSchedule.h"
#include <iostream>
#include <map>
using namespace engine::render;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
struct Fixture: IWorldLocalShadow,IWorldLocalShadowCache,IWorldShadowScheduling,IWorldLocalShadowPlan {
    WorldLocalShadowAtlas atlas;WorldShadowUpdateCache final;
    std::map<unsigned,WorldShadowCacheKey> statics;
    unsigned bank=0;
    unsigned worldShadowScheduleBank()const override{return bank;}
    unsigned current=0;WorldDirectionalShadowView view;bool writing=false,fail=false;
    Fixture(){atlas.enabled=true;}
    void prewarmWorldLocalShadows()override{}
    void setWorldLocalShadowIdentities(std::span<const std::uint64_t> ids)override{atlas.identities(ids);}
    WorldShadowCacheResult queryWorldStaticShadow(std::span<const std::uint32_t>,std::uint32_t i,
        const WorldDirectionalShadowView&,WorldShadowCacheKey key)const override{
        const auto found=statics.find(i);
        return found!=statics.end() && found->second==key?WorldShadowCacheResult::Reuse:WorldShadowCacheResult::Draw;
    }
    void clearWorldLocalShadows()override{atlas.clear();}
    void planWorldLocalShadowSources(std::span<const std::uint32_t> s)override{atlas.plan(s);}
    bool beginWorldLocalShadow(std::uint32_t i,const WorldDirectionalShadowView& v)override{
        if(!atlas.begin(i,v))return false;current=i;view=v;return true;}
    bool endWorldLocalShadow()override{if(atlas.activeLight<0)return false;atlas.finish(view);return true;}
    WorldShadowCacheResult beginWorldStaticShadow(std::uint32_t i,const WorldDirectionalShadowView&,WorldShadowCacheKey k)override{
        if(statics.contains(i)&&statics[i]==k)return WorldShadowCacheResult::Reuse;statics[i]=k;writing=true;return WorldShadowCacheResult::Draw;}
    bool endWorldStaticShadow()override{writing=false;return true;}
    void invalidateWorldStaticShadows()override{statics.clear();final.clear();}
    WorldShadowUpdateQuery queryWorldShadowUpdate(std::span<const std::uint32_t> ids,std::uint32_t i,const WorldDirectionalShadowView& v,WorldShadowUpdateKey k)const override {
        auto a=atlas;a.clear();a.plan(ids);return final.query(a.slot(i),v,k);}
    bool reuseWorldShadowUpdate(std::uint32_t i,const WorldDirectionalShadowView& v,WorldShadowUpdateKey k)override{
        if(!final.query(atlas.slot(i),v,k).valid)return false;atlas.begin(i,v);atlas.finish(v);return true;}
    void commitWorldShadowUpdate(std::uint32_t i,const WorldDirectionalShadowView& v,WorldShadowUpdateKey k)override{final.commit(atlas.slot(i),v,k);}
};
int main()try{
    Fixture f;WorldShadowSchedule schedule;unsigned draws=0;
    WorldRectLight light{{0,4,0},20,{2,0,0},{0,0,.2f},{4,0,0}};light.shadowFilter=WorldLocalShadowFilter::DistanceMoments;
    auto view=rectLightShadowView(light);
    auto make=[&](unsigned revision,unsigned budget=2) {
        f.clearWorldLocalShadows();WorldRectShadowPlan base;
        for(unsigned i=0;i<8;++i)base.entries.push_back({i,view,{1,1},f.statics.contains(i)?WorldShadowCacheResult::Reuse:WorldShadowCacheResult::Draw});
        return schedule.prepare(f,std::move(base),1,budget,[&](auto,const auto&){return revision;});
    };
    auto draw=[&](auto,const auto&,auto){++draws;};
    for(unsigned i=0;i<4;++i){auto plan=make(1);check(schedule.stats().refreshed==2,"Budget exceeded");plan.execute(f,draw);}
    check(draws==16,"Prewarm lost static or dynamic casters");
    auto steady=make(1);check(schedule.stats().refreshed==0 && schedule.stats().reused==8,"Unchanged maps redrawn");steady.execute(f,draw);
    // A continuously dirty source cannot jump ahead of older dirty work.
    auto first=make(2);check(first.refresh[0]&&first.refresh[1],"FIFO initial order");first.execute(f,draw);
    auto second=make(3);check(second.refresh[2]&&second.refresh[3],"New changes starved queued work");
    second.execute(f,draw);
    auto third=make(3);check(third.refresh[4]&&third.refresh[5],"FIFO continuation");third.execute(f,draw);
    auto fourth=make(3);check(fourth.refresh[6]&&fourth.refresh[7],"FIFO tail");fourth.execute(f,draw);
    auto last=make(3);check(last.refresh[0]&&last.refresh[1],"Reinvalidated source was lost");last.execute(f,draw);
    auto fixed=make(3);check(schedule.stats().refreshed==0,"Queue failed to drain");fixed.execute(f,draw);
    WorldShadowUpdateKey key{1,0x8000000000000000ull,1,3};
    check(f.final.query(0,view,key).exact,"Completed key not exact");
    key.dynamics=4;check(f.final.query(0,view,key).valid&&!f.final.query(0,view,key).exact,"Dirty map cannot be retained");
    key.light=99;check(!f.final.query(0,view,key).valid,"Wrong owner reused");
    key.light=0x8000000000000000ull;key.scene=2;check(!f.final.query(0,view,key).valid,"Scene crossed");
    auto filtered=view;filtered.momentKernel=10;key.scene=1;
    check(!f.final.query(0,filtered,key).valid,"Changed filter reused old moments");
    filtered=view;filtered.momentCentreRay=!filtered.momentCentreRay;
    check(!f.final.query(0,filtered,key).valid,"Changed ray convention reused old moments");
    filtered=view;filtered.staticOnly=true;
    check(!f.final.query(0,filtered,key).valid,"Changed caster policy reused old moments");
    auto moved=view;moved.viewProjection[12]+=1;key.scene=1;check(!f.final.query(0,moved,key).valid,"Moved projection reused");
    auto failure=make(4);
    bool failed=false;try{failure.execute(f,[](auto,const auto&,auto){throw std::runtime_error("injected");});}catch(...){failed=true;}
    check(failed&&!f.final.query(0,view,key).valid,"Failed writer left cache valid");
    auto retry=make(4);check(retry.refresh[0]&&retry.refresh[1],"Failed jobs were consumed");retry.execute(f,draw);
    std::array<Fixture,2> banks; banks[1].bank=1;
    WorldShadowSchedule bankSchedule;std::array<std::array<unsigned,8>,2> serviced{};
    for(unsigned frame=0;frame<32;++frame) {
        auto& b=banks[frame%2];b.clearWorldLocalShadows();WorldRectShadowPlan base;
        for(unsigned i=0;i<8;++i)base.entries.push_back({i,view,{1,1},b.statics.contains(i)?WorldShadowCacheResult::Reuse:WorldShadowCacheResult::Draw});
        auto plan=bankSchedule.prepare(b,std::move(base),1,2,[&](auto,const auto&){return frame+1;});
        for(unsigned i=0;i<8;++i)if(plan.refresh[i])++serviced[frame%2][i];
        if(frame>=8)check(bankSchedule.stats().unready==0,"Alternating buffer left a shadow uninitialized");
        plan.execute(b,draw);
    }
    for(const auto& bank:serviced)for(auto count:bank)check(count==4,"A shadow buffer starved a continuously dirty light");
    Fixture sequential;WorldShadowSchedule sequentialSchedule;
    WorldEnvironmentLighting sequentialEnvironment;sequentialEnvironment.enabled=true;
    sequentialEnvironment.rectLights[0]=light;sequentialEnvironment.rectLights[0].identity=41;
    unsigned sequentialDraws=0;
    const auto receiver=[&](std::uint64_t scene) {
        auto base=planWorldRectShadowsCachedByLight(sequential,sequentialEnvironment,
            [&](auto,const auto&){return WorldShadowCacheKey{scene,1};});
        auto plan=sequentialSchedule.prepare(sequential,std::move(base),scene,8,
            [](auto,const auto&){return 1;});
        plan.execute(sequential,[&](auto,const auto&,auto){++sequentialDraws;});
        check(sequential.atlas.tileCount==1,"Receiver view lost its published shadow");
    };
    receiver(1);check(sequentialDraws==2,"First receiver lost static/dynamic draws");
    receiver(1);check(sequentialDraws==2,"Second receiver discarded completed caches");
    receiver(2);check(sequentialDraws==4,"New receiver scene reused another scene's maps");
    Fixture staticFixture;WorldShadowSchedule staticSchedule;
    WorldEnvironmentLighting staticEnvironment;staticEnvironment.enabled=true;
    staticEnvironment.rectLights[0]=light;staticEnvironment.rectLights[0].shadow.staticOnly=true;
    unsigned staticCalls=0,dynamicCalls=0;
    for(unsigned revision:{1u,2u,3u}) {
        auto base=planWorldRectShadowsCachedByLight(staticFixture,staticEnvironment,
            [](auto,const auto&){return WorldShadowCacheKey{9,1};});
        auto plan=staticSchedule.prepare(staticFixture,std::move(base),9,8,
            [&](auto,const auto&){return revision;});
        plan.execute(staticFixture,[&](auto,const auto&,auto casters){
            if(casters==WorldShadowCasters::Static)++staticCalls;else ++dynamicCalls;
        });
    }
    check(staticCalls==1&&dynamicCalls==0,"Static-only policy rendered animated casters or rebuilt unchanged maps");
    auto faded=view;faded.strength=.25f;
    WorldShadowUpdateCache fadeCache;WorldShadowUpdateKey fadeKey{1,2,3,4};
    fadeCache.commit(0,view,fadeKey);
    check(fadeCache.query(0,faded,fadeKey).exact,"Opacity-only change rebuilt unchanged moments");
    WorldLocalShadowAtlas atlas;atlas.enabled=true;
    std::array<std::uint64_t,worldLocalShadowSourceCapacity> owners{};owners[0]=43;owners[2]=91;
    const std::array<std::uint32_t,2> visible{0,2};
    atlas.identities(owners);atlas.plan(visible);const auto firstSlot=atlas.slot(0),secondSlot=atlas.slot(2);
    atlas.clear();std::swap(owners[0],owners[2]);atlas.identities(owners);atlas.plan(visible);
    check(atlas.slot(2)==firstSlot&&atlas.slot(0)==secondSlot,"Reordered authored lights lost their slots");
    bool duplicate=false;atlas.clear();owners[0]=owners[2];
    try{atlas.identities(owners);}catch(const std::invalid_argument&){duplicate=true;}
    check(duplicate,"Duplicate authored owner accepted");
    for(unsigned receiverMask:{0u,1u,2u,255u})for(unsigned channel:{0u,1u,128u,255u})
        for(unsigned flags:{0u,1u,2u,4u,8u,16u,32u,64u,128u,255u}) {
            auto packed=unsigned(packWorldLightParticipation(channel,flags,receiverMask));
            check((packed&255)==channel&&((packed>>8)&255)==flags&&(packed>>16)==receiverMask,"Lossy light packing");
        }
    WorldEnvironmentLighting e;e.enabled=true;e.lightChannels=37;
    check((unsigned(packWorldPointLights(e)[1][3])>>16)==37,"Inactive point zero lost receiver channels");
    light.participation&=~WorldLightShadows;check(!rectLightShadowView(light).enabled,"Shadow participation ignored");
    std::cout<<"PASS shadow scheduling: budgets, FIFO fairness, unchanged reuse, ownership, motion, removal-equivalent revision, failure/retry.\\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}
