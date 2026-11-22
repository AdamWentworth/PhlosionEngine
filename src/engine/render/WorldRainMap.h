#pragma once
#include "engine/render/IWorldRainMap.h"
#include "engine/render/IRenderBackend.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace engine::render {
class WorldRainMap {
public:
    static constexpr unsigned resolution=2048;
    struct Job {unsigned cascade=0;WorldDirectionalShadowView view;std::array<float,2> origin{};bool initial=false;};
    struct Published {std::array<float,2> origin{};float step=0;bool valid=false;};
    // One pending depth target is reused. Initial missing cascades are completed
    // before consumers run; steady updates resume a bounded batch each frame.
    std::vector<Job> plan(std::array<float,3> camera,std::uint64_t revision) {
        for(float x:camera)if(!std::isfinite(x)||std::abs(x)>1000000)throw std::invalid_argument("Invalid rain-map camera");
        if(revision!=revision_) {published_={};pending_=false;cursor_=0;revision_=revision;}
        std::vector<Job> out;
        for(unsigned i=0;i<2;++i)if(!published_[i].valid)out.push_back(makeJob(camera,i,true));
        if(!out.empty())return out;
        if(pending_)return {job_};
        job_=makeJob(camera,next_,false);pending_=true;cursor_=0;return {job_};
    }
    template<class StaticDraw,class DynamicDraw>
    void execute(IWorldRainMap& gpu,WorldResourceScopeId scope,const Job& job,StaticDraw&& draw,DynamicDraw&& dynamic) {
        const bool initial=job.initial;
        if(initial)cursor_=0;
        if(!gpu.beginWorldRainMap(scope,job.view,cursor_==0))throw std::runtime_error("Rain-map pass admission failed");
        bool complete=false;
        try {
            draw(cursor_,initial?std::numeric_limits<std::size_t>::max():300,
                 initial?std::numeric_limits<std::uint64_t>::max():200000,complete);
            if(complete)dynamic();
            if(!gpu.endWorldRainMap())throw std::runtime_error("Rain-map pass close failed");
        } catch(...) {gpu.endWorldRainMap();throw;}
        if(complete) {
            if(!gpu.publishWorldRainMap(scope,job.cascade))throw std::runtime_error("Rain-map publication failed");
            published_[job.cascade]={job.origin,(job.cascade?200.f:20.f)/resolution,true};
            pending_=false;cursor_=0;next_=(job.cascade+1)%2;++publications_;
        } else {job_=job;pending_=true;}
    }
    const std::array<Published,2>& published() const{return published_;}
    bool ready() const{return published_[0].valid&&published_[1].valid;}
    std::uint64_t publications()const{return publications_;}
private:
    static Job makeJob(std::array<float,3> camera,unsigned cascade,bool initial) {
        const float size=cascade?200.f:20.f,step=size/(2*resolution);
        const float x=std::round(camera[0]/step)*step,z=std::round(camera[2]/step)*step;
        Job job;job.cascade=cascade;job.initial=initial;job.origin={x-size*.5f,z-size*.5f};
        job.view.enabled=true;job.view.resolution=resolution;job.view.depthBias=job.view.normalOffset=0;
        const auto matrix=glm::ortho(-size*.5f,size*.5f,-size*.5f,size*.5f,0.f,16000.f)*
            glm::lookAt(glm::vec3(x,8000,z),glm::vec3(x,0,z),glm::vec3(0,0,-1));
        std::copy_n(glm::value_ptr(matrix),16,job.view.viewProjection.begin());return job;
    }
    std::array<Published,2> published_{};
    Job job_;std::size_t cursor_=0;unsigned next_=0;bool pending_=false;
    std::uint64_t revision_=0,publications_=0;
};
}
