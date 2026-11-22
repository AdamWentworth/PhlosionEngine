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
    Window window("Scoped material bindings",256,128,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
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
    WorldResourceScopeId retired=0;
    for(unsigned frame=0;frame<7;++frame) {
        if(frame==3 && mode=="retire") {
            retired=left;
            check(scopes.requestWorldResourceScopeRelease(left),"Scope retirement failed");
            left=scopes.createWorldResourceScope(1<<20);check(left!=0,"Replacement scope failed");
        }
        renderer->beginFrame(0,0,0,1);
        if(retired){WorldResourceScopeStats old;check(!scopes.getWorldResourceScopeStats(retired,old),"Retired native resources remain live");}
        for(unsigned side=0;side<2;++side) {
            if(retained)check(scopes.beginWorldResourceScope(side?right:left),"Scope admission failed");
            IRenderBackend::WorldTextureData t;
            const bool replaced=mode=="retire"&&frame>=3;
            t.key=t.cacheKey=replaced&&side?"replacement-map":"shared-map";
            if(!retained)t.key=t.cacheKey=side?(replaced?"reference-right-red":"reference-right-blue"):(replaced?"reference-left-green":"reference-left-red");
            t.rgba=side?(replaced?red.data():blue.data()):(replaced?green.data():red.data());
            t.width=t.height=1;t.textureSrgb=0;t.materialMode=0;t.depthTestEnabled=0;
            t.modelMatrix=identity;t.modelMatrix[0]=.45f;t.modelMatrix[5]=.8f;t.modelMatrix[12]=side?.5f:-.5f;
            t.vertexColorMulR=t.vertexColorMulG=t.vertexColorMulB=frame<2?.2f:1.f;
            renderer->drawWorldIndexedMeshTexturedCached("binding-quad",vertices.data(),4,indices.data(),6,&t,identity.data(),256,128);
            if(retained)check(scopes.endWorldResourceScope(),"Scope close failed");
        }
        renderer->endFrame();
    }
    renderer->shutdown();renderer.reset();

}
int main(int argc,char** argv)try {
    check(argc==4,"Expected API, reuse/retire and image path");
    const std::string api=argv[1],mode=argv[2],path=argv[3];
    check((api=="d3d12"||api=="vulkan")&&(mode=="reuse"||mode=="retire"),"Invalid case");
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
    check(*std::max_element(pixel(64),pixel(64)+3)>150 && *std::max_element(pixel(192),pixel(192)+3)>150,"Missing reference geometry");
    std::cout<<"PASS scoped material binding "<<api<<' '<<mode<<'\n';return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
