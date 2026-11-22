#include "engine/render/WorldPreparedView.h"
#include <iostream>
#include <stdexcept>
using namespace engine::render;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main() try {
    WorldPreparedViews<unsigned> views, other;
    backend::WorldTextureData t;
    unsigned builds=0;
    auto prepare=[&]{return views.prepare(WorldPreparedViewInput(&t),[&]{return ++builds;});};
    views.beginFrame();views.beginBatch();
    auto first=prepare();
    for(unsigned i=0;i<1000;++i) {
        t.modelMatrix[12]=float(i);t.materialTimeSec=float(i);t.roughnessFactor=.2f;
        check(*views.get(first)==1,"Object changes invalidated shared state");
        check(*views.get(prepare())==1,"Identical view prepared repeatedly");
    }
    check(builds==1,"Preparation scales with object count");
    t.environment.surfaceAbsorption=.2f;auto changed=prepare();
    check(*views.get(changed)==2,"Material weather override lost");
    t.cameraPosX=3;check(*views.get(prepare())==3,"Camera change lost");
    t.directionalShadowReceiver=0;check(*views.get(prepare())==4,"Receiver change lost");
    t.alphaMode=2;check(*views.get(prepare())==5,"Transparency participation lost");
    check(!other.get(changed),"Foreign renderer token accepted");
    views.beginBatch();check(views.get(first),"Nested batch unnecessarily rebuilt the view");
    views.invalidate();check(!views.get(first),"Native view transition retained old state");
    prepare();views.endBatch();check(!views.get(changed),"Nested pass restored stale state");
    auto sameView=prepare();views.endBatch();check(views.get(sameView),"Asset batch discarded shared view");
    auto immediate=prepare();prepare();check(views.get(immediate),"Same view was rebuilt outside batching");
    views.invalidate();check(!views.get(immediate),"View generation did not retire immediate record");
    views.beginFrame();check(views.stats.preparations==0,"Frame counters not reset");
    views.beginBatch();
    for(unsigned i=0;i<WorldPreparedViews<unsigned>::capacity;++i){t.cameraPosX=float(i);prepare();}
    bool bounded=false;t.cameraPosX=-1;
    try{prepare();}catch(const std::runtime_error&){bounded=true;}
    check(bounded,"Unbounded prepared state allocation");
    views.endBatch();
    bool balanced=false;try{views.endBatch();}catch(const std::logic_error&){balanced=true;}
    check(balanced,"Unbalanced batch accepted");
    std::cout<<"PASS prepared views: constant preparation under 1000 objects, overrides, nesting, frames, foreign/stale tokens, bounded lifetime\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
