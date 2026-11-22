#include "engine/render/WeatherParticles.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace engine::render;
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}assert(caught);}
int main(int argc,char** argv){
    auto v=weatherParticleDrag({3,4,0},.1f,2);
    assert(glm::distance(v,glm::vec3(1.5f,2,0))<1e-6f);
    assert(weatherParticleFade(3,{4,2,80,50})==.5f);
    assert(weatherParticleFade(65,{4,2,80,50})==.5f);
    assert(weatherParticleSize(15,{10,20,.5f,0},{40,100,3,0})==.75f);
    auto q=weatherParticleGeometry({0,0,-4},{0,0,0},{0,0,-1},{1,0,0},{0,1,0},
        {2,3},0,{0,0,0},0);
    assert(q.corners[0]==glm::vec3(-2,-3,-4)&&q.corners[2]==glm::vec3(2,3,-4));
    q=weatherParticleGeometry({0,0,-4},{0,0,0},{0,0,-1},{1,0,0},{0,1,0},
        {2,3},.25f,{0,0,0},0);
    assert(glm::distance(q.corners[2],glm::vec3(3,-2,-4))<1e-5f);
    q=weatherParticleGeometry({0,0,-4},{0,0,0},{0,0,-1},{1,0,0},{0,1,0},
        {.5f,1},0,{2,0,0},.5f);
    assert(q.corners[0]==glm::vec3(-2,.5f,-4)&&q.binormal==glm::vec3(-1,0,0));
    q=weatherParticleGeometry({0,0,-4},{4,0,0},{0,0,-1},{1,0,0},{0,1,0},
        {2,3},0,{0,0,0},0,0,true);
    const auto edge=q.corners[1]-q.corners[0],facing=glm::normalize(glm::vec3(4,0,4));
    assert(std::abs(glm::dot(edge,facing))<1e-5f&&std::abs(glm::length(edge)-4)<1e-5f);
    assert(glm::distance(q.binormal,glm::vec3(0,1,0))<1e-5f);
    {
        WeatherParticleEmitter e;e.capacity=100;e.duration={100,100};e.rate.values={glm::vec4(60)};
        e.trackEmission.values={glm::vec4(1)};
        WeatherParticleModule life;life.op=WeatherParticleOp::Life;life.a.values={glm::vec4(10)};
        WeatherParticleModule box;box.op=WeatherParticleOp::Box;box.a.values={glm::vec4(2,3,4,0)};
        e.modules={life,box};WeatherParticleLod near,far;
        near.distance=10;near.enabled=false;near.duration=far.duration={100,100};near.rate=e.rate;
        far.distance=100;far.rate=e.rate;e.lods={near,far};
        WeatherParticles particles({e});
        particles.advanceView(0,1,0,true,{0,0,0},{5,0,0},{0,0,0});
        particles.advanceView(.5,1,.5f,true,{0,0,0},{5,0,0},{0,0,0});assert(particles.count()==0);
        particles.advanceView(.75,1,.75f,true,{0,0,0},{50,0,0},{0,0,0});assert(particles.count()>0);
        for(const auto& p:particles.states()[0].particles)
            assert(std::abs(p.position.x)<=2&&std::abs(p.position.y)<=3&&std::abs(p.position.z)<=4);
        const auto count=particles.count();
        particles.advanceView(.8,1,.8f,false,{0,0,0},{95,0,0},{0,0,0});
        for(const auto& p:particles.states()[0].particles)assert(std::abs(p.alpha-.5f)<1e-6f);
        particles.advanceView(.9,1,.9f,true,{0,0,0},{150,0,0},{0,0,0});assert(particles.count()==count);
        for(const auto& p:particles.states()[0].particles)assert(p.alpha==0);
    }
    std::uint32_t rng=17;
    for(unsigned i=0;i<1000;++i){
        auto p=weatherParticleSphere(15,5,25,rng);assert(p.z==0&&glm::length(p)>=4.999f&&glm::length(p)<=25.001f);
        p=weatherParticleSphere(31|64,5,25,rng);assert(p.z>=0&&std::abs(glm::length(p)-25)<1e-4f);
    }
    rejects([]{ParticleEvaluator::parse({{"kind",2},{"values",{{0,0,0,0}}}});});
    if(argc==2){
        std::ifstream f(argv[1]);nlohmann::json j;f>>j;
        auto program=parseWeatherParticles(j);
        assert(program.size()==8||program.size()==21);
        if(program.size()==21){
            assert(std::count_if(program.begin(),program.end(),[](const auto& e){return e.local;})==13);
            rejects([&]{auto bad=j;bad["emitters"][8]["lods"][1]["distance"]=1;parseWeatherParticles(bad);});
        }
        std::erase_if(program,[](const auto& e){return e.local;});assert(program.size()==8);
        WeatherParticles a(program),b(program);a.advance(0,1,6,true,{0,0,0},{0,20,0});b.advance(0,1,6,true,{0,0,0},{0,20,0});
        for(unsigned i=1;i<=1200;++i)a.advance(double(i)/60,1,6,true,{0,0,0},{0,20,0});
        for(unsigned i=1;i<=600;++i)b.advance(double(i)/30,1,6,true,{0,0,0},{0,20,0});
        assert(a.count()>100&&a.count()==b.count());
        for(std::size_t k=0;k<a.states().size();++k){
            assert(a.states()[k].particles.size()==b.states()[k].particles.size());
            for(std::size_t i=0;i<a.states()[k].particles.size();++i){
                const auto& x=a.states()[k].particles[i];const auto& y=b.states()[k].particles[i];
                assert(x.id==y.id&&glm::distance(x.position,y.position)<1e-6f);
            }
        }
        auto count=a.count();auto pos=a.states()[3].particles[0].position;
        a.advance(20,1,6,true,{900,900,900},{0,20,0});assert(a.count()==count&&a.states()[3].particles[0].position==pos);
        for(unsigned i=1;i<=300;++i)a.advance(20+double(i)/60,1,16.344126f,false,{0,0,0},{0,20,0});
        assert(a.count()==0);a.advance(25,2,0,true,{0,0,0},{0,20,0});assert(a.count()==0);
        rejects([&]{auto bad=j;bad["emitters"][0]["capacity"]=99999;parseWeatherParticles(bad);});
        rejects([&]{auto bad=j;bad["emitters"][0]["modules"][0]["op"]=999;parseWeatherParticles(bad);});
        rejects([&]{a.advance(26,2,0,true,{0,0,0},{0,20,0},4);});
        std::cout<<"PASS eight authored emitters; active="<<count<<"; deterministic 30/60 Hz, pause, world-space, tail, reset, malformed package\n";
    }
    std::cout<<"PASS particle math contracts\n";
}
