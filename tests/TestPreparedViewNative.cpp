#include "engine/models/WorldIndexedResources.h"
#include "engine/render/WorldViewSubmission.h"
#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"
#include <SDL.h>
#include <stb_image.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
}
void capture(const std::string& api,const std::string& mode,const std::string& path,bool retained) {
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path);env("PHLOSION_BACKEND_SCREENSHOT_FRAME","6");env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    Window window("Prepared view bindings",256,128,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,128,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,128,false);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*renderer);
    auto left=scopes.createWorldResourceScope(1<<20),right=scopes.createWorldResourceScope(1<<20);
    check(left&&right,"Scope creation failed");
    const std::array<unsigned char,4> red{255,0,0,255},green{0,255,0,255},blue{0,0,255,255};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(unsigned i=0;i<4;++i){auto& v=vertices[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;v.r=v.g=v.b=v.a=1;v.nz=v.tx=v.tw=1;}
    const std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const bool viewRecording=mode=="view-recording";
    const bool recording=mode=="recording" || viewRecording;
    WorldResourceScopeId retired=0;
    WorldPreparedResourceStats previousResources;
    engine::models::batches::WorldIndexedResources nativeBindings;
    for(unsigned frame=0;frame<7;++frame) {
        if(frame==3 && (mode=="retire" || mode=="native-resources" || viewRecording)) {
            retired=left;
            check(scopes.requestWorldResourceScopeRelease(left),"Scope retirement failed");
            left=scopes.createWorldResourceScope(1<<20);check(left!=0,"Replacement scope failed");
        }
        const int drawWidth=recording && frame>=2 && frame<4?320:256;
        if(recording && (frame==2 || frame==4)) {
            SDL_SetWindowSize(window.getSDLWindow(),drawWidth,128);
            SDL_PumpEvents();
            renderer->onResize(drawWidth,128);
        }
        renderer->beginFrame(0,0,0,1);
        if(retired){WorldResourceScopeStats old;check(!scopes.getWorldResourceScopeStats(retired,old),"Retired native resources remain live");}
        {
        std::unique_ptr<WorldViewSubmission> view;
        if(retained && viewRecording)view=std::make_unique<WorldViewSubmission>(*renderer);
        for(unsigned side=0;side<2;++side) {
            RenderCpuPhaseScope prepare(*renderer,"fixture-prepare");
            if(retained)check(scopes.beginWorldResourceScope(side?right:left),"Scope admission failed");
            IRenderBackend::WorldTextureData t;
            const bool replaced=(mode=="retire" || mode=="native-resources" || viewRecording)&&frame>=3;
            t.key=t.cacheKey=replaced&&side?"replacement-map":"shared-map";
            if(!retained)t.key=t.cacheKey=side?(replaced?"reference-right-red":"reference-right-blue"):(replaced?"reference-left-green":"reference-left-red");
            t.rgba=side?(replaced?red.data():blue.data()):(replaced?green.data():red.data());
            t.width=t.height=1;t.textureSrgb=0;t.materialMode=0;t.depthTestEnabled=0;
            t.modelMatrix=identity;t.modelMatrix[0]=.45f;t.modelMatrix[5]=.8f;t.modelMatrix[12]=side?.5f:-.5f;
            t.vertexColorMulR=t.vertexColorMulG=t.vertexColorMulB=frame<2?.2f:1.f;
            t.materialMode=2;t.environment.enabled=true;
            t.environment.sunRadiance={0,0,0};t.environment.ambientRadiance={1,1,1};
            t.environment.pointLights[0].position={0,0,2};
            t.environment.pointLights[0].radius=8;
            t.environment.pointLights[0].intensity={1.f+float(frame)*.1f,.5f,1};
            t.environment.surfaceAbsorption=side?.1f:.8f;
            t.cameraPosX=float(frame)*.1f;
            if(recording) { t.alphaMode=2;t.vertexColorMulA=.04f; }
            if(retained) {
                renderer->beginWorldIndexedBatchSubmission();
                t.preparedView=renderer->prepareWorldViewData(t);
                if(mode=="transition") dynamic_cast<IWorldDirectionalShadow&>(*renderer).clearWorldDirectionalShadow();
            }
            for(unsigned repeat=0;repeat<(recording?280u:64u);++repeat) {
            if(recording && repeat==32 && retained)renderer->beginWorldIndexedBatchSubmission();
            if(recording && repeat==40 && retained)renderer->endWorldIndexedBatchSubmission();
            if(recording)
                t.vertexColorMulR=t.vertexColorMulG=t.vertexColorMulB=.4f+.6f*float(repeat)/279.f;
            if(recording && repeat==278) {
                auto immediate=t;immediate.vertexColorMulR=immediate.vertexColorMulG=immediate.vertexColorMulB=.05f;immediate.vertexColorMulA=.5f;
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&immediate,identity.data(),drawWidth,128);
            }
            if(!retained) dynamic_cast<IWorldDirectionalShadow&>(*renderer).clearWorldDirectionalShadow();
            bool submitted=false;
            if(retained && mode=="native-resources") {
                nativeBindings.bind(&dynamic_cast<IWorldPreparedResources&>(*renderer),side?right:left);
                submitted=nativeBindings.draw("binding-quad",vertices.data(),4,indices.data(),6,t,identity.data(),drawWidth,128);
                check(submitted,"Native model bindings unexpectedly fell back");
            }
            if(!submitted)renderer->drawWorldIndexedMeshTexturedCached("binding-quad",vertices.data(),4,indices.data(),6,&t,identity.data(),drawWidth,128);
            }
            if(retained)renderer->endWorldIndexedBatchSubmission();
            if(retained)check(scopes.endWorldResourceScope(),"Scope close failed");
        }
        }
        const auto stats=renderer->worldPreparedViewStats();
        check(stats.preparations>0 && stats.tokenReuses>0,"Prepared view counters missing");
        if(retained && mode=="transition") check(stats.staleTokens>=128,"Stale native tokens were not rejected");
        // Fresh reference deliberately resets native view inputs for every draw.
        check(stats.preparations <= (retained?(mode=="transition"?4u:2u):(recording?560u:128u)),"Prepared view work scales with draw count");
        std::cout<<"view-preparations="<<stats.preparations<<" token-reuses="<<stats.tokenReuses<<'\n';
        if(retained && mode=="native-resources") {
            const auto resourceStats=dynamic_cast<IWorldPreparedResources&>(*renderer).worldPreparedResourceStats();
            check(resourceStats.geometryPreparations-previousResources.geometryPreparations==2 &&
                resourceStats.materialPreparations-previousResources.materialPreparations==2 &&
                resourceStats.draws-previousResources.draws==128 && !resourceStats.staleDraws,
                "Native bindings did not reuse resources within each scope");
            previousResources=resourceStats;
        }
        renderer->endFrame();
    }
    renderer->shutdown();renderer.reset();

}
int main(int argc,char** argv)try {
    check(argc==4,"Expected API, reuse/retire and image path");
    const std::string api=argv[1],mode=argv[2],path=argv[3];
    check((api=="d3d12"||api=="vulkan")&&(mode=="reuse"||mode=="retire"||mode=="transition"||mode=="recording"||mode=="view-recording"||mode=="native-resources"),"Invalid case");
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    const auto reference=path+".reference.png";
    capture(api,mode,reference,false);capture(api,mode,path,true);
    int width,height,channels,rw,rh,rc;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(path.c_str(),&width,&height,&channels,3),stbi_image_free);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> expected(stbi_load(reference.c_str(),&rw,&rh,&rc,3),stbi_image_free);
    check(image&&expected&&width==256&&height==128&&rw==width&&rh==height,"Missing binding capture");
    check(std::equal(image.get(),image.get()+width*height*3,expected.get()),"Retained bindings differ from normal uncached material resolution");
    const auto pixel=[&](unsigned x){return expected.get()+(64*256+x)*3;};
    check(!std::equal(pixel(64),pixel(64)+3,pixel(192)),"Reference did not distinguish the two materials");
    check(*std::max_element(pixel(64),pixel(64)+3)>100 && *std::max_element(pixel(192),pixel(192)+3)>100,"Missing reference geometry");
    std::cout<<"PASS prepared view binding "<<api<<' '<<mode<<'\n';return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
