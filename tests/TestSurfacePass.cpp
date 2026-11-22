#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void env(const char* name,const std::string& value){
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    setenv(name,value.c_str(),1);
#endif
}
}
int main(int argc,char** argv)try {
    check(argc==4,"Expected API case image");
    const std::string api=argv[1],mode=argv[2];check(api=="vulkan"||api=="d3d12","Invalid API");
    const std::filesystem::path path=argv[3];std::filesystem::create_directories(path.parent_path());
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string());env("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    int width=320,height=240;
    Window window("Surface buffers",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    auto vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,20.f)*
        glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0));
    auto view=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
    check(!surface.endWorldSurfaceBuffer() && !surface.showWorldSurfaceBuffer(0) &&
          !surface.resolveWorldSurfaceContact(view),"Stale surface available before capture");
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i) {
        auto& v=vertices[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;
        v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;v.r=.04f;v.g=.16f;v.b=.36f;v.a=1;v.nz=1;v.tx=1;v.tw=1;
    }
    constexpr std::array<unsigned char,4> white{255,255,255,255},hole{255,255,255,0},normal{255,128,128,255};
    IRenderBackend::WorldTextureData t;t.key=t.cacheKey="surface-white";t.rgba=white.data();t.width=t.height=1;
    t.materialMode=mode=="pbr254"?254:2;t.textureSrgb=0;t.metallicFactor=.25f;t.roughnessFactor=.65f;
    t.cameraPosZ=3;t.emissiveFactorR=.04f;t.emissiveFactorG=.16f;t.emissiveFactorB=.36f;
    if(mode=="masked"){t.key=t.cacheKey="surface-hole";t.rgba=hole.data();t.alphaMode=1;}
    if(mode=="transparent")t.alphaMode=2;
    if(mode=="unsupported"||mode=="forward")t.materialMode=0;
    if(mode=="masked-solid")t.alphaMode=1;
    if(mode=="properties"){t.metallicFactor=.231f;t.roughnessFactor=.457f;}
    WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
    WorldMotionSample motion{&previous,0xfedcba98u,true};
    if(mode=="mapped") {t.normalKey="surface-normal";t.normalRgba=normal.data();t.normalWidth=t.normalHeight=1;}
    const unsigned plane=mode=="normal"||mode=="mapped"?1:mode=="material"||mode=="properties"?2:mode=="emission"?3:0;
    for(int frame=0;frame<4;++frame) {
        if(mode=="extent" && frame==1) {
            width=401;height=257;
            SDL_SetWindowSize(window.getSDLWindow(),width,height);SDL_PumpEvents();
            renderer->onResize(width,height);
            vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,20.f)*
                glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0));
            view=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        }
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        check(!surface.showWorldSurfaceBuffer(0),"Prior-view surface leaked into new scene");
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Begin surface failed");
        check(!surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Nested capture accepted");
        if(mode=="forward")check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&motion,1}),"Forward-only motion enrollment");
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(vp),width,height);
        if(mode=="abort")surface.clearWorldSurfaceBuffer();
        else check(surface.endWorldSurfaceBuffer(),"End surface failed");
        if(mode=="reset" || mode=="abort") {
            surface.clearWorldSurfaceBuffer();check(!surface.showWorldSurfaceBuffer(0),"Cleared surface remained available");
            check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height) && surface.endWorldSurfaceBuffer(),"Empty recapture failed");
        }
        auto mismatch=view;mismatch[0][0]+=.01f;
        check(!surface.resolveWorldSurfaceContact(mismatch),"Mismatched contact camera accepted");
        check(!surface.showWorldSurfaceBuffer(4),"Out-of-range plane accepted");
        check(surface.captureWorldSurfaceMotion(),"Surface diagnostic capture failed");
        check(surface.showWorldSurfaceBuffer(plane),"Surface display failed");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        const auto data=surface.readWorldSurfaceMotion();
        check(data.surfaceTexelBytes==std::uint64_t(width)*height*32,"Shared surface with independent full identity and light channels");
        const auto center=(height/2)*width+width/2;
        const bool empty=mode=="masked"||mode=="transparent"||mode=="unsupported"||mode=="reset"||mode=="abort";
        check(data.materialClass[center]==(empty?0:mode=="forward"?3:mode=="masked-solid"?2:1),"Incorrect material participation class");
        check(data.materialClass[3*width+3]==0,"Clear class was not empty");
        check((data.albedo[center]>>30)==0,"Standard material inherited specialized lighting");
        if(!empty&&mode!="forward") {
            const auto packed=data.material[center];
            const float expected[]{t.metallicFactor,.04f,1.f,t.roughnessFactor};
            // Float-to-UNORM permits 0.6 integer ULP (Direct3D functional spec 3.2.3.6).
            for(unsigned channel=0;channel<4;++channel)
                check(std::abs(float((packed>>(8*channel))&255)/255-expected[channel])<=.6f/255+.00001f,
                    ("Material quantization exceeds 0.6 UNORM8 step: channel "+std::to_string(channel)+" packed "+std::to_string((packed>>(8*channel))&255)+" expected "+std::to_string(expected[channel])).c_str());
        }
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* pixels=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(pixels && w==width && h==height,"Missing surface image");
    const auto sample=[&](int x,int y,int channel){return pixels[(y*w+x)*3+channel];};
    const bool empty=mode=="masked"||mode=="transparent"||mode=="unsupported"||mode=="forward"||mode=="reset"||mode=="abort";
    glm::vec3 expected=empty?glm::vec3(0):plane==0?glm::vec3(.04,.16,.36):
        plane==1?(mode=="mapped"?glm::normalize(glm::vec3(1,1.f/255,1.f/255))*.5f+.5f:glm::vec3(.5,.5,1)):
        plane==2?glm::vec3(t.metallicFactor,.04,1):glm::vec3(.04,.16,.36);
    for(int channel=0;channel<3;++channel) {
        const float linear=expected[channel];
        const int wanted=int(std::round((linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1/2.4f)-.055f)*255));
        const int actual=sample(w/2,h/2,channel);
        std::cout<<"channel "<<channel<<" expected "<<wanted<<" actual "<<actual<<'\n';
        check(std::abs(actual-wanted)<=2,"Surface pixel differs from material value");
        check(sample(3,3,channel)<=1,"Cleared surface background was not black");
    }
    stbi_image_free(pixels);std::cout<<api<<' '<<mode<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
