#include "engine/render/WorldMaterialFrameCache.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace engine::render;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
    WorldMaterialFrameCache cache;cache.beginFrame();
    std::array<std::byte,256> material{},copy{};
    std::uint64_t next=256;unsigned writes=0;
    const auto build=[&]()->std::optional<std::uint64_t>{++writes;auto result=next;next+=256;return result;};
    const auto a=cache.resolve(material,build);
    check(a==256 && cache.resolve(copy,build)==a && writes==1,"Equal material values did not share one allocation");
    material[95]=std::byte{1};const auto b=cache.resolve(material,build);
    check(b!=a && cache.resolve(copy,build)==a,"Mutation aliased previously published material");
    // Failed publication must be retried; a null allocation is never cached.
    material[95]=std::byte{2};
    check(!cache.resolve(material,[]()->std::optional<std::uint64_t>{return {};}),"Failed allocation became usable");
    check(cache.resolve(material,build).has_value() && writes==3,"Failed allocation did not retry");
    for(unsigned i=0;i<4100;++i) {
        material[0]=std::byte(i&255);material[1]=std::byte(i>>8);
        check(cache.resolve(material,build).has_value(),"Bounded cache discarded a material draw");
    }
    check(cache.stats.bypasses>0,"Cache retention was not bounded");
    cache.beginFrame();const auto fresh=cache.resolve(copy,build);
    check(fresh!=a && cache.stats.builds==1 && cache.stats.reuses==0,"A new frame reused stale native allocation");
    std::cout<<"Frame material contracts passed: value identity, mutation, failure/retry, bounded retention and frame reset.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
