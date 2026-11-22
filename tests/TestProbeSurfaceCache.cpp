#include "engine/render/WorldPlacedProbeCache.h"
#include "engine/render/WorldProbeSurfaceCache.h"
#include "engine/render/WorldProbeResidency.h"
#include <iostream>
#include <stdexcept>
using engine::render::WorldProbeSurfaceCache;
void check(bool v){if(!v)throw std::runtime_error("Probe cache contract failed");}
int main()try {
    engine::render::WorldPlacedProbeCache radiance;
    engine::render::WorldLocalReflectionProbe definition;definition.resolution=128;
    constexpr std::array<std::uint64_t,2> ids{1,2};
    check(!radiance.begin(0,1,definition,0));
    for(unsigned face=0;face<6;++face) {
        check(radiance.begin(1,1,definition,face));
        check(radiance.lastFace()==(face==5));radiance.finish();
        check(radiance.select(ids)[4][3]==(face==5?2.f:0.f));
    }
    const auto complete=radiance.select(ids);
    check(radiance.begin(1,2,definition,0));radiance.finish();
    check(radiance.begin(1,2,definition,1));radiance.cancel();
    check(radiance.select(ids)==complete);
    for(unsigned face=0;face<6;++face){check(radiance.begin(1,3,definition,face));radiance.finish();}
    check(radiance.select(ids)!=complete && radiance.select(ids,1)[4][3]==0);
    radiance.clear();check(radiance.select(ids)[4][3]==0);
    radiance.deferFiltering(true);
    for(unsigned f=0;f<6;++f){check(radiance.begin(1,10,definition,f));radiance.finish(false);}
    check(!radiance.published(1));
    for(unsigned step=0;step<10;++step) {
        check(radiance.beginFilter(1) && radiance.filterStep()==step);
        if(step==3){radiance.cancel();check(radiance.beginFilter(1) && radiance.filterStep()==step);}
        check(radiance.finishFilterStep()==(step==9));
        check(radiance.published(1)==(step==9));
    }
    const auto firstBank=radiance.select(ids);
    for(unsigned f=0;f<6;++f){check(radiance.begin(1,11,definition,f));radiance.finish(false);}
    for(unsigned step=0;step<10;++step){check(radiance.beginFilter(1));radiance.finishFilterStep();}
    check(radiance.blending(1) && radiance.select(ids)[4][3]==1);
    check(!radiance.begin(1,12,definition,0)); // both banks are still sampled
    radiance.advanceBlend(.5f);check(radiance.select(ids)[4][3]==1.5f);
    radiance.advanceBlend(.5f);check(!radiance.blending(1));
    check(radiance.select(ids)[7]!=firstBank[7]);
    engine::render::WorldProbeResidency residency;
    auto retained=residency.reserve(std::array<std::uint64_t,4>{1,2,3,4});
    radiance.retain(retained);
    retained=residency.reserve(std::array<std::uint64_t,2>{4,5});
    check(std::find(retained.begin(),retained.end(),4)!=retained.end());
    check(std::find(retained.begin(),retained.end(),5)!=retained.end());
    radiance.retain(retained);check(!radiance.published(1));
    check(radiance.begin(5,12,definition,0));radiance.cancel();
    bool duplicate=false;try{residency.reserve(std::array<std::uint64_t,2>{8,8});}catch(const std::invalid_argument&){duplicate=true;}
    check(duplicate);
    // A global sky generation occupies its own banks and survives local eviction.
    const auto global=engine::render::WorldPlacedProbeCache::globalId;
    for(unsigned face=0;face<6;++face){check(radiance.begin(global,30,definition,face));radiance.finish();}
    check(radiance.published(global) && radiance.select({})[1][3]==2);
    radiance.retain({});check(radiance.published(global));
    const auto globalState=radiance.select({});
    check(globalState[2][1]>=.5f); // independently owned lower atlas half
    for(unsigned face=0;face<6;++face){check(radiance.begin(global,31,definition,face));radiance.finish(false);}
    check(radiance.select({})==globalState);
    for(unsigned step=0;step<10;++step){check(radiance.beginFilter(global));radiance.finishFilterStep();}
    check(radiance.blending(global) && radiance.select({})[1][3]==1);
    radiance.invalidate(std::span(&global,1));check(!radiance.published(global));
    radiance.clear();
    auto low=definition,high=definition;low.priority=0;high.priority=2;
    high.orientation={0,0,.70710678f,.70710678f};high.minimum={-2,-4,-1};high.maximum={2,4,1};
    high.receiverChannels=4;high.boxProjection=false;high.intensity=.5f;
    for(unsigned face=0;face<6;++face){check(radiance.begin(2,40,high,face));radiance.finish();}
    for(unsigned face=0;face<6;++face){check(radiance.begin(1,40,low,face));radiance.finish();}
    const auto spatial=radiance.select(ids);
    check(spatial[55][0]==0 && spatial[59][0]==2 && spatial[59][1]==.5f && spatial[59][2]==4);
    check(std::abs(spatial[56][0])<1e-6f && std::abs(spatial[56][1]-.5f)<1e-6f);
    check(std::abs(spatial[57][0]+.25f)<1e-6f && std::abs(spatial[57][1])<1e-6f);
    auto invalid=definition;invalid.orientation={0,0,0,0};
    bool badOrientation=false;try{(void)engine::render::packWorldLocalReflection(invalid);}catch(const std::invalid_argument&){badOrientation=true;}
    check(badOrientation);
    // Every core plus seam border stays inside its bank without sharing texels.
    std::vector<bool> atlasBank(512*512);
    for(int level=0;level<6;++level)for(int prepared=0;prepared<2;++prepared) {
        if(level==0 && prepared)continue;
        const int extent=(128>>level)+2;
        for(int face=0;face<6;++face) {
            const auto border=engine::render::worldPlacedReflectionAtlasOffset(face+prepared*12+24,level,128);
            const auto core=engine::render::worldPlacedReflectionAtlasOffset(face+prepared*12,level,128);
            check(core[0]==border[0]+1 && core[1]==border[1]+1);
            check(border[0]>=0 && border[1]>=0 && border[0]+extent<=512 && border[1]+extent<=512);
            for(int y=border[1];y<border[1]+extent;++y)for(int x=border[0];x<border[0]+extent;++x) {
                check(!atlasBank[y*512+x]);atlasBank[y*512+x]=true;
            }
        }
    }
    WorldProbeSurfaceCache c;check(!c.release(0));
    check(c.begin(0,1,0,128).empty() && c.begin(1,0,0,128).empty() &&
          c.begin(1,1,6,128).empty() && c.begin(1,1,0,127).empty());
    for(unsigned f=0;f<6;++f) {
        check(!c.begin(1,1,f,128).empty());
        check(c.select(1,0).empty() && c.begin(2,1,0,128).empty());
        c.finish();check(c.revision(1)==(f==5?1:0));
    }
    auto first=c.select(1,0);check(!first.empty());
    check(c.begin(1,1,0,128).empty() && c.begin(1,2,0,256).empty());
    for(unsigned f=0;f<3;++f){check(!c.begin(1,2,f,128).empty());c.finish();}
    check(!c.begin(1,2,3,128).empty());c.cancel();
    check(c.select(1,0)==first && c.revision(1)==1);
    // A newer revision abandons every partial face of its predecessor.
    for(unsigned f=3;f<6;++f){check(!c.begin(1,3,f,128).empty());c.finish();}
    check(c.revision(1)==1);
    for(unsigned f=0;f<3;++f){check(!c.begin(1,3,f,128).empty());c.finish();}
    check(c.revision(1)==3 && c.select(1,0)!=first);
    for(unsigned id=2;id<=4;++id){check(!c.begin(id,1,0,64).empty());c.cancel();}
    check(c.begin(5,1,0,64).empty());
    check(c.release(1) && c.revision(1)==0 && c.select(1,0).empty());
    check(!c.begin(5,1,0,64).empty());c.finish();
    c.clear();check(!c.active() && c.revision(5)==0);
    std::cout<<"PASS complete publication, cancellation, supersession, limits, release\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
