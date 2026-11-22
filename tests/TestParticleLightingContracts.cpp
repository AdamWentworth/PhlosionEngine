#include "engine/render/IWorldParticleLighting.h"
#include "engine/render/ParticleEmission.h"
#include <iostream>
using namespace engine::render;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(false)
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
int main()try{
    ParticleEmission stream;
    CHECK(stream.advance(0,600,1,120).empty());
    std::size_t count=0;for(int i=1;i<=60;++i)count+=stream.advance(i/60.,600,1,120).size();
    CHECK(count>=599&&count<=600);
    CHECK(stream.advance(1,600,1,120).empty());CHECK(stream.advance(.5,600,1,120).empty());
    CHECK(stream.advance(1.1,0,1,120).empty());
    CHECK(stream.advance(1.2,600,1,120).size()>=59);
    CHECK(stream.advance(.8,600,1,120).empty());CHECK(stream.seconds()==1.2);
    CHECK(stream.advance(0,600,2,120).empty());CHECK(stream.seconds()==0);
    CHECK(stream.advance(.9,100000,2,12).size()==12);
    const auto delayed=stream.advance(40,600,2,120);CHECK(delayed.size()==120&&delayed.front().seconds>39.7&&delayed.back().seconds<=40);
    rejects([&]{stream.advance(1e300,600,2,120);});
    rejects([&]{stream.advance(-1,0,0,120);});rejects([&]{stream.advance(0,NAN,0,120);});
    // Equal time integral must not depend on the renderer's frame rate.
    ParticleEmission fast,slow;fast.advance(0,37,1,120);slow.advance(0,37,1,120);
    std::vector<ParticleEmission::Birth> a,b;
    for(int i=1;i<=120;++i){auto x=fast.advance(i/120.,37,1,120);a.insert(a.end(),x.begin(),x.end());}
    for(int i=1;i<=30;++i){auto x=slow.advance(i/30.,37,1,120);b.insert(b.end(),x.begin(),x.end());}
    CHECK(a.size()==b.size());for(unsigned i=0;i<a.size();++i)CHECK(a[i].id==b[i].id&&std::abs(a[i].seconds-b[i].seconds)<1e-12);
    WorldParticleLightingView u;u.owner=1;u.view.identity=1;u.view.cameraValid=true;u.view.width=u.view.height=1024;
    glm::mat4 matrix(1);std::copy_n(glm::value_ptr(matrix),16,u.view.viewProjection.begin());
    std::vector<WorldParticleQuad> quads(3);
    quads[0].corners={glm::vec3(-.01f,-.01f,0),glm::vec3(.01f,-.01f,0),glm::vec3(.01f,.01f,0),glm::vec3(-.01f,.01f,0)};
    quads[1]=quads[0];for(auto& p:quads[1].corners)p*=80;
    quads[2]=quads[0];for(auto& p:quads[2].corners)p.x+=3;
    u.quads=quads;
    auto layout=particleLightingLayout(u);CHECK(layout.result.tiles==2&&layout.result.texels==4100);
    CHECK(layout.result.coordinates[2][0]==glm::vec2(0));CHECK(layout.result.coordinates[0][0]!=layout.result.coordinates[1][0]);
    quads[1].lightingMipBias=2;CHECK(particleLightingLayout(u).result.texels==260);
    quads[1].lightingMipBias=4;CHECK(particleLightingLayout(u).result.texels==20);
    quads[1].lightingMipBias=9;rejects([&]{particleLightingLayout(u);});
    quads[1].lightingMipBias=0;
    u.quality=0;CHECK(particleLightingLayout(u).result.texels==1028);
    u.quality=1;u.view.viewProjection.fill(0);rejects([&]{particleLightingLayout(u);});
    std::copy_n(glm::value_ptr(matrix),16,u.view.viewProjection.begin());
    quads[0].corners[0].x=NAN;rejects([&]{particleLightingLayout(u);});quads[0]=quads[1];
    u.owner=0;rejects([&]{particleLightingLayout(u);});
    std::cout<<"PASS fractional births, frame-rate independence, stop/reset/stale generation, bounded work, projected atlas tiers, frustum and invalid view\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
