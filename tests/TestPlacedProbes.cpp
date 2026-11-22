#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldPlacedProbes.h"
#include <SDL.h>
#include <stb_image.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <fstream>
using namespace engine::render;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void setenvValue(const char* name,const std::string& value){
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    setenv(name,value.c_str(),1);
#endif
}

int numericProbe(const std::string& api,const std::string& mode,const std::filesystem::path& output) {
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}} quit;
    Window window("Probe numerical qualification",256,256,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
    auto& reflection=dynamic_cast<IWorldLocalReflection&>(*renderer);
    WorldLocalReflectionProbe probe;probe.resolution=128;probe.sampling=WorldLocalReflectionSampling::PreparedRadiance;
    constexpr std::array<std::uint64_t,1> ids{41};
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    const auto identity=glm::mat4(1);
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1);
    for(unsigned face=0;face<6;++face) {
        check(reflection.beginWorldPlacedProbeFace(ids[0],1,probe,face,{}),"Numerical face admission");
        const auto view=makeWorldLocalReflectionFace(probe,int(face));
        const glm::vec3 forward(view.forward[0],view.forward[1],view.forward[2]);
        const glm::vec3 up(view.up[0],view.up[1],view.up[2]),right=glm::cross(forward,up);
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(unsigned i=0;i<4;++i) {
            auto& v=vertices[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;
            v.nz=v.tx=v.tw=v.a=1;
            const auto direction=forward+right*v.x+up*v.y;
            const auto color=mode=="energy"?glm::vec3(1.f):glm::vec3(.3f)+direction*.2f;
            v.r=color.r;v.g=color.g;v.b=color.b;
        }
        IRenderBackend::WorldTextureData t;t.materialMode=255;t.depthTestEnabled=0;
        if(mode=="energy") {
            // Ordinary emissive material carries HDR. Unlit byte/color mode 255
            // deliberately clamps to display-range values and cannot test HDR.
            static constexpr unsigned char white[]{255,255,255,255},black[]{0,0,0,255};
            t.materialMode=2;t.textureSrgb=0;t.rgba=black;t.width=t.height=1;t.key="probe-energy-black";
            t.emissiveRgba=white;t.emissiveWidth=t.emissiveHeight=1;t.emissiveKey="probe-energy-white";
            t.emissiveFactorR=3;t.emissiveFactorG=.4f;t.emissiveFactorB=.8f;
            t.environment.enabled=true;t.environment.sunRadiance={};t.environment.ambientRadiance={};
            t.normalScale=0;t.metallicFactor=0;
        }
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(identity),128,128);
        check(reflection.endWorldPlacedProbeFace(),"Numerical face publication");
    }
    check(reflection.selectWorldPlacedProbes(ids),"Numerical selection");
    check(reflection.captureWorldProbeRadiance(ids),"Numerical radiance capture");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    const auto image=reflection.readWorldProbeRadiance();
    check(image.width==2048 && image.height==2048 && image.rgb111110.size()==2048u*2048u,"Numerical radiance readback");
    std::filesystem::create_directories(output.parent_path());
    std::ofstream data(output.string()+".bin",std::ios::binary);
    data.write(reinterpret_cast<const char*>(image.rgb111110.data()),std::streamsize(image.rgb111110.size()*4));data.close();
    std::ofstream metadata(output.string()+".json");
    metadata<<"{\"width\":"<<image.width<<",\"height\":"<<image.height<<",\"selection\":[";
    for(unsigned i=0;i<image.selection.size();++i){if(i)metadata<<',';metadata<<'[';
        for(unsigned j=0;j<4;++j){if(j)metadata<<',';metadata<<image.selection[i][j];}metadata<<']';}
    metadata<<"]}\n";metadata.close();
    renderer->shutdown();
    std::cout<<"PASS raw compact radiance "<<api<<' '<<mode<<"\n";
    return 0;
}
int main(int argc,char** argv)try {
    check(argc==4 || argc==5,"Expected API mode image [reference]");
    std::string api=argv[1],mode=argv[2];check(api=="d3d12"||api=="vulkan","Invalid API");
    if(mode=="energy"||mode=="gradient")return numericProbe(api,mode,argv[3]);
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
    setenvValue("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    setenvValue("PHLOSION_BACKEND_SCREENSHOT_FRAME",(mode=="partial"||mode=="stable")?"6":"30");
    setenvValue("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    const int extent=mode=="narrow"?128:256;
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}} quit;
    Window window("Placed probe publication",extent,extent,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),extent,extent,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),extent,extent,false);
    WorldPlacedProbes updater;
    std::array<WorldLocalReflectionProbe,2> probes;
    for(auto& p:probes){p.sampling=WorldLocalReflectionSampling::PreparedRadiance;p.resolution=128;p.fade=2;}
    probes[0].position={-2,0,3};probes[0].minimum={-8,-5,-5};probes[0].maximum={2,5,8};
    probes[1].position={2,0,3};probes[1].minimum={-2,-5,-5};probes[1].maximum={8,5,8};
    if(mode=="priority")probes[1].priority=2;
    if(mode=="channels")probes[1].receiverChannels=0;
    if(mode=="global")for(auto& p:probes){p.minimum[0]=20;p.maximum[0]=30;}
    if(mode=="oriented") {
        probes[0].minimum={-5,-1,-5};probes[0].maximum={5,1,8};
        probes[0].orientation={0,0,.70710678f,.70710678f};probes[0].fade=.25f;
        probes[1].minimum[0]=20;probes[1].maximum[0]=30;
    }
    std::array<std::uint64_t,2> ids{1,2};bool canceled=false;
    std::array<std::array<float,4>,32> emission;emission.fill({1,1,1,1});
    WorldEnvironmentLighting environment;environment.enabled=true;
    constexpr std::array<std::uint32_t,6> order{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> quad{};
    for(unsigned i=0;i<4;++i){auto& v=quad[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;v.nz=1;v.tx=1;v.tw=1;v.r=v.g=v.b=v.a=1;}
    const auto identity=glm::mat4(1);
    const auto vp=glm::perspective(glm::radians(60.f),1.f,.1f,40.f)*glm::lookAt(glm::vec3(0,0,5),glm::vec3(0),glm::vec3(0,1,0));
    unsigned captures=0,publications=0;
    for(unsigned frame=0;frame<32;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(extent,extent);
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1);
        if(mode=="evict") {
            if(frame==12)ids={5,6};
            if(frame==14)ids={7,8};
            if(frame==16)ids={1,2};
        }
        if(mode=="relocate"&&frame==12)probes[0].position[0]=-3;
        environment.animationSeconds=((mode=="partial"||mode=="published") && frame>=2)?1:0;
        const auto clock=mode=="paused"?0.:frame/60.0;
        const unsigned budget=mode=="quality"?(frame<10?1u:frame<20?6u:3u):1u;
        const auto planned=updater.captureViews(*renderer,ids,probes,1,{},clock);
        check(planned.size()==updater.captureViews(*renderer,ids,probes,1,{},clock).size(),"Probe planning mutated residency");
        unsigned consumed=0;
        try { updater.update(*renderer,ids,probes,1,environment,emission,budget,
            [&](const auto& view){
                check(consumed<planned.size() && view.viewProjection==planned[consumed].viewProjection,"Unplanned probe capture");
                ++consumed;++captures;
            },
            [&](const auto& face,const auto& e) {
                if((mode=="cancel"||mode=="global-cancel")&&frame==4&&!canceled && ((face.position[0]==0)==(mode=="global-cancel"))){canceled=true;throw std::runtime_error("Injected probe cancellation");}
                auto vertices=quad;const bool red=face.position[0]<0;
                for(auto& v:vertices){v.r=red && e.animationSeconds==0?1.f:0.f;v.g=red && e.animationSeconds>0?1.f:0.f;v.b=red?0.f:1.f;}
                IRenderBackend::WorldTextureData t;t.materialMode=255;t.depthTestEnabled=0;
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),6,&t,glm::value_ptr(identity),128,128);
            }); }catch(const std::exception& e) {
                if((mode!="cancel"&&mode!="global-cancel")||frame!=4||std::string(e.what())!="Injected probe cancellation")throw;
            }
        publications+=updater.stats().published;
        if(mode=="quality" && !updater.stats().captures)check(updater.stats().faces<=budget,"Live probe quality budget exceeded");
        check(consumed==planned.size(),"Planned probe capture was not consumed");
        auto vertices=quad;for(auto& v:vertices){v.x*=4;v.y*=4;}
        IRenderBackend::WorldTextureData t;t.materialMode=2;t.textureSrgb=0;t.normalScale=0;t.metallicFactor=1;t.roughnessFactor=.2f;
        t.environment=environment;t.cameraPosX=t.cameraPosY=0;t.cameraPosZ=5;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),6,&t,glm::value_ptr(vp),extent,extent);
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    }
    std::cout<<"Captured material faces "<<captures<<"\n";
    check(captures==(mode=="evict"?48u:mode=="relocate"?18u:12u),"Placed probe capture/reuse count");
    if(mode=="quality"||mode=="paused")check(publications>=3,"Live quality/paused clock stalled probe publication");
    check((mode!="cancel"&&mode!="global-cancel")||canceled,"Cancellation wasn't exercised");
    renderer->shutdown();renderer.reset();
    int w,h,c;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(image && w==extent && h==extent,"Missing placed probe capture");
    const auto pixel=[&](int x,int ch){return image.get()[((extent/2)*extent+x*extent/256)*3+ch];};
    if(mode=="global"||mode=="oriented") {
        check(pixel(36,2)>pixel(36,0)+30 && pixel(220,2)>pixel(220,0)+30,"Global fallback not blue outside bounds");
        if(mode=="oriented")check(pixel(128,0)>pixel(128,2)+30,"Rotated influence missing at center");
        std::cout<<"PASS "<<api<<' '<<mode<<" global/oriented consumer\n";return 0;
    }
    if(mode=="priority"||mode=="channels") {
        const auto dominant=mode=="priority"?2:0,other=2-dominant;
        check(pixel(128,dominant)>pixel(128,other)+30,"Priority/channel selection failed");
        std::cout<<"PASS "<<api<<' '<<mode<<" overlap participation\n";return 0;
    }
    check(pixel(220,2)>pixel(220,0)+30,"Right placed probe not blue");
    if(mode=="published")check(pixel(36,1)>pixel(36,0)+30,"Completed radiance update not published");
    else check(pixel(36,0)>pixel(36,2)+30,"Left placed probe not red");
    check(pixel(128,2)>20 && (pixel(128,0)>20 || pixel(128,1)>20),"Overlap did not blend both probes");
    if(argc==5) {
        int rw,rh,rc;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> ref(stbi_load(argv[4],&rw,&rh,&rc,3),stbi_image_free);
        check(ref && rw==w && rh==h,"Missing publication reference");
        for(int i=0;i<w*h*3;++i)check(image.get()[i]==ref.get()[i],"Partial generation became visible");
    }
    std::cout<<"PASS "<<api<<" "<<mode<<"; static faces "<<captures<<"; spatial blend and complete publication\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
