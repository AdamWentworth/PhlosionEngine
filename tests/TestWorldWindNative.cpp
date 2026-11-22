#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldWindField.h"
#include <SDL.h>
#include <iostream>
#include <memory>
#include <cstring>
using namespace engine::render;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
glm::vec4 load(const std::vector<std::array<float,4>>& data,glm::ivec3 p){
    if(glm::any(glm::lessThan(p,glm::ivec3(0)))||glm::any(glm::greaterThan(p,glm::ivec3(63))))return {};
    const auto& v=data[64+unsigned(p.x+p.y*64+p.z*4096)];return {v[0],v[1],v[2],v[3]};
}
glm::vec4 interpolate(const std::vector<std::array<float,4>>& data,glm::vec3 p){
    const auto cell=glm::ivec3(glm::floor(p));const auto f=glm::fract(p);glm::vec4 result{};
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        result+=load(data,cell+glm::ivec3(x,y,z))*(x?f.x:1-f.x)*(y?f.y:1-f.y)*(z?f.z:1-f.z);
    return result;
}
glm::vec4 referenceStep(const std::vector<std::array<float,4>>& data,glm::ivec3 p){
    auto c=load(data,p);const auto x=load(data,p+glm::ivec3(1,0,0))-load(data,p-glm::ivec3(1,0,0));
    const auto y=load(data,p+glm::ivec3(0,1,0))-load(data,p-glm::ivec3(0,1,0));
    const auto z=load(data,p+glm::ivec3(0,0,1))-load(data,p-glm::ivec3(0,0,1));
    const glm::vec3 gradient(x.w*.5f,y.w*.5f,z.w*.5f);
    const auto advected=glm::vec3(interpolate(data,glm::vec3(p)-.016f*glm::vec3(c)));
    auto v=glm::mix(glm::vec3(c),advected,std::clamp(glm::length(advected-glm::vec3(c))/4.f,0.f,1.f));
    glm::vec3 laplacian=-6.f*glm::vec3(c);
    for(auto d:{glm::ivec3(1,0,0),glm::ivec3(-1,0,0),glm::ivec3(0,1,0),glm::ivec3(0,-1,0),glm::ivec3(0,0,1),glm::ivec3(0,0,-1)})laplacian+=glm::vec3(load(data,p+d));
    v+=.016f*.73f*laplacian-.2f*gradient;
    v=glm::clamp(glm::max(glm::abs(v)-.001f,glm::vec3(0))*glm::sign(v),glm::vec3(-8),glm::vec3(8));
    const float density=std::clamp(c.w-.016f*(glm::dot(gradient,glm::vec3(c))+(x.x+y.y+z.z)*.5f*c.w)-.0254f,0.f,1.6f);
    return {v,density};
}
int main(int argc,char** argv)try{
    std::cout<<std::unitbuf;check(argc==2,"API argument");std::string api=argv[1];
    check(api=="d3d12"||api=="vulkan","Unknown API");SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Wind volume qualification",128,96,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> backend;
    if(api=="vulkan")backend=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),128,96,false);
    else backend=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),128,96,false);
    auto& field=dynamic_cast<IWorldWindField&>(*backend);auto& scopes=dynamic_cast<IWorldResourceScope&>(*backend);
    WorldWindFieldInput input;input.owner=scopes.createWorldResourceScope(32*1024*1024);input.camera={0,-4,8};
    input.wind.strength=.35f;input.wind.velocity={7,0};
    const auto run=[&](){
        std::cout<<"Wind step "<<input.wind.seconds<<" camera "<<input.camera.x<<std::endl;
        backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(128,96);field.prepareWorldWindField(input);check(field.captureWorldWindField(),"Capture missing");
        backend->endWorldSceneColorPass();backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();auto out=field.readWorldWindField();
        check(out.field.size()==windFieldHeader+windFieldCells*2,"Readback dimensions");return out;
    };
    WorldWindImpulse sphere;sphere.radius=2;sphere.persistent=true;input.impulses={sphere};
    auto first=run();const glm::ivec3 cell(32);const auto actual=load(first.field,cell);
    const auto point=glm::vec3(.25f);const auto expected=glm::normalize(point)*(.4f*(1-glm::length(point)/2));
    check(glm::length(glm::vec3(actual)-expected)<2.e-5f&&std::abs(actual.w-glm::length(expected))<2.e-5f,"Sphere injection oracle");
    input.impulses.clear();input.wind.seconds=.016;auto evolved=run();
    for(auto p:{glm::ivec3(32),glm::ivec3(31,32,32),glm::ivec3(30,31,32),glm::ivec3(34,32,33)}){
        const auto actualStep=load(evolved.field,p),expectedStep=referenceStep(first.field,p);
        if(glm::length(actualStep-expectedStep)>=5.e-5f){
            std::cerr<<"Cell "<<p.x<<","<<p.y<<","<<p.z<<" steps "<<evolved.steps<<" actual ";
            for(unsigned c=0;c<4;++c)std::cerr<<actualStep[c]<<",";std::cerr<<" expected ";
            for(unsigned c=0;c<4;++c)std::cerr<<expectedStep[c]<<",";std::cerr<<'\n';
        }
        check(glm::length(actualStep-expectedStep)<5.e-5f,"Fluid advection/pressure/decay oracle");
    }
    check(evolved.field[64+windFieldCells+32+32*64+32*4096]==first.field[64+32+32*64+32*4096],"Previous field changed");
    input.camera.x+=1;auto shifted=run();
    check(glm::length(load(shifted.field,{30,32,32})-load(evolved.field,{32,32,32}))<1.e-6f,"Scrolled volume lost world anchoring");
    const auto paused=run();check(load(paused.field,{30,32,32})==load(shifted.field,{30,32,32}),"Paused field simulated");
    input.camera.x+=100;auto teleported=run();check(glm::length(load(teleported.field,{32,32,32}))==0,"Teleport retained disturbance");
    for(unsigned tier=0;tier<4;++tier){
        input.quality=tier;input.wind.seconds+=.016;sphere.a=sphere.b=glm::vec3(input.camera.x,0,0);
        input.impulses={sphere};const auto sample=run();check(sample.shapes==1,"Preset omitted wind producer");
    }
    input.camera={0,-4,8};input.quality=1;
    for(float age:{.1f,.5f,.99f,1.f}){
        ++input.wind.generation;sphere.a=sphere.b={0,0,0};sphere.persistent=false;sphere.age=age;
        input.impulses={sphere};auto sample=run();
        const float t=std::clamp(age,0.f,1.f);
        const float envelope=std::sqrt(std::clamp(t/.2f,0.f,1.f))*(1-t*t*(3-2*t));
        check(glm::length(glm::vec3(load(sample.field,{32,32,32}))-expected*envelope)<2.e-5f,"Transient envelope oracle");
        check(sample.shapes==(age<1?1u:0u),"Expired pulse not removed");
    }
    ++input.wind.generation;WorldWindImpulse capsule;capsule.a={0,-1,0};capsule.b={0,1,0};capsule.radius=2;capsule.persistent=true;
    input.impulses={capsule};auto capsuleSample=run();
    const auto capsuleDirection=glm::normalize(glm::vec3(.25f,.01f,.25f));
    const auto capsuleExpected=capsuleDirection*(.4f*(1-std::sqrt(.125f)/2));
    check(glm::length(glm::vec3(load(capsuleSample.field,{32,32,32}))-capsuleExpected)<2.e-5f,"Capsule projection oracle");
    ++input.wind.generation;sphere.persistent=true;input.impulses={sphere,sphere};
    auto overlap=run();check(glm::length(glm::vec3(load(overlap.field,{32,32,32}))-2.f*expected)<2.e-5f,"Overlapping impulse accumulation");
    check(std::abs(load(overlap.field,{32,32,32}).w-.4f)<2.e-5f,"Injection density cap");
    // Reject duplicate simulation of one scope, but allow both sides of a scene transition.
    backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(128,96);field.prepareWorldWindField(input);
    bool rejected=false;try{field.prepareWorldWindField(input);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Duplicate view advanced simulation");backend->endWorldSceneColorPass();
    auto incoming=input;incoming.owner=scopes.createWorldResourceScope(32*1024*1024);
    incoming.impulses={sphere};incoming.impulses.front().intensity=3;
    backend->beginWorldSceneColorPass(128,96);field.prepareWorldWindField(incoming);
    check(field.captureWorldWindField(),"Incoming scope capture");
    backend->endWorldSceneColorPass();backend->endFrame();
    const auto incomingSample=field.readWorldWindField();
    check(incomingSample.owner==incoming.owner,"Incoming wind scope not selected");
    check(glm::length(glm::vec3(load(incomingSample.field,{32,32,32}))-3.f*expected)<2.e-5f,"Incoming scope used outgoing wind");
    const auto retained=run();
    check(load(retained.field,{32,32,32})==load(overlap.field,{32,32,32}),"Transition overwrote outgoing wind");
    check(scopes.requestWorldResourceScopeRelease(incoming.owner),"Incoming scope release");
    check(scopes.requestWorldResourceScopeRelease(input.owner),"Scope release");
    for(unsigned i=0;i<4;++i){backend->beginFrame(0,0,0,1);backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();}
    backend->shutdown();std::cout<<"Wind field native: sphere/capsule/envelopes/overlap/fluid/history/scroll/pause/teleport/presets/admission passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
