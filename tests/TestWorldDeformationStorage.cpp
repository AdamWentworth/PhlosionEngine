#include "engine/render/WorldDeformationStorage.h"
#include <array>
#include <iostream>
using namespace engine::render;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid use accepted");}
std::span<const std::byte> bytes(const WorldDeformation& v){return std::as_bytes(v->values());}
int main(){try{
    std::array<float,16> pose{};pose[0]=1;
    WorldDeformationSet producer;
    auto first=producer.publish(4,pose);pose[0]=2;
    auto second=producer.publish(4,pose);
    check(first!=second && first->values()[0]==1,"Updated pose mutated an earlier collected version");
    check(producer.publish(4,pose)==second,"Unchanged producer did not retain its version");
    pose[0]=1;check(producer.publish(4,pose)==first,"Retained afterimage version lost identity");
    for(int i=3;i<10;++i){pose[0]=float(i);producer.publish(4,pose);}
    producer.release(4);
    check(first->values()[0]==1 && second->values()[0]==2,"Producer release invalidated an owned view");
    std::vector<std::byte> memory(2048);
    bool fail=false;unsigned flushes=0;
    WorldDeformationStorage gpu;
    gpu.configure(memory.data(),memory.size(),0,1,[&]{++flushes;if(fail)throw std::runtime_error("injected flush failure");});
    gpu.beginFrame(1,0);gpu.beginBatch();
    std::array<WorldSkinPalette,2> pair{first,second};gpu.prepare(pair);
    rejects([&]{gpu.resolve(bytes(first),first);});
    rejects([&]{gpu.beginFrame(2,0);});
    fail=true;rejects([&]{gpu.commitBatch();});check(gpu.residentCount()==0,"Failed flush published ready poses");
    fail=false;gpu.commitBatch();
    auto a=gpu.resolve(bytes(first),first)->value,b=gpu.resolve(bytes(second),second)->value;
    check(a!=b && std::memcmp(memory.data()+a,first->values().data(),64)==0,"Current/previous storage aliases");
    check(gpu.stats().preparedUploads==2 && gpu.stats().preparationJoins==1,"Copies did not share one commit");
    gpu.beginFrame(2,0);gpu.prepare(pair);
    check(gpu.resolve(bytes(first),first)->value==a && gpu.stats().preparedBytes==0,"Unchanged pose uploaded again");
    check(gpu.stats().persistentReuses==2,"Keepalive did not retain both versions");
    // An unchanged logical previous pose can be older than the last manager update.
    pose[0]=10;auto third=producer.publish(4,pose);std::array<WorldSkinPalette,1> next{third};
    gpu.prepare(next);check(gpu.resolve(bytes(first),first)->value==a,"A different update changed per-view previous binding");
    gpu.beginFrame(3,0);rejects([&]{gpu.resolve(bytes(third),third);});
    gpu.beginBatch();gpu.prepare(next);gpu.cancelBatch();rejects([&]{gpu.resolve(bytes(third),third);});
    gpu.beginFrame(4,1);gpu.prepare(next);check(gpu.resolve(bytes(third),third).has_value(),"Fence reset did not clear canceled batch");

    // Force exhaustion while the GPU still owns every range, then retire and retry.
    std::vector<std::byte> tiny(768);WorldDeformationStorage bounded;
    bounded.configure(tiny.data(),tiny.size(),0,1);
    bounded.beginFrame(1,0);bounded.prepare(pair);
    const auto oldA=bounded.resolve(bytes(first),first)->value;
    bounded.beginFrame(2,0);bounded.beginBatch();bounded.prepare(next);
    rejects([&]{bounded.commitBatch();});
    check(std::memcmp(tiny.data()+oldA,first->values().data(),64)==0,"Pressure overwrote an in-flight pose");
    bounded.cancelBatch();bounded.beginFrame(3,1);bounded.prepare(next);
    check(bounded.stats().evictions==2 && bounded.stats().preparedUploads==1,"Completed cold entries did not retire");
    bounded.beginFrame(4,3);bounded.prepare(std::span(pair).first(1));
    check(bounded.resolve(bytes(first),first).has_value(),"Evicted live CPU version could not be restored");
    // Cold owner release must remain fenced; address reuse never resurrects old bytes.
    WorldDeformationSet temporary;pose[0]=99;auto last=temporary.publish(0,pose);
    gpu.beginFrame(5,4);gpu.prepare(std::array<WorldSkinPalette,1>{last});
    auto weak=WorldDeformationWeak(last);temporary.clear();last.reset();
    check(weak.expired(),"GPU residency kept the producer alive");
    gpu.beginFrame(6,4);check(gpu.stats().retiredBytes==0,"Released in-flight allocation reclaimed too early");
    gpu.beginFrame(7,5);check(gpu.stats().retiredBytes>=256,"Completed released allocation leaked");
    rejects([&]{gpu.beginFrame(7,5);});
    rejects([&]{gpu.beginFrame(8,4);});
    gpu.reset();gpu.configure(memory.data(),memory.size(),0,1);gpu.beginFrame(1,0);
    rejects([&]{gpu.resolve(bytes(first),first);});
    std::cout<<"Persistent deformation contracts passed: ownership, retained versions, keepalive, atomic publication, GPU lag, pressure, release and reset.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
