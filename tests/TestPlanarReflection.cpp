#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr int width=512,height=320;
constexpr std::array<unsigned char,4> white{255,255,255,255};
constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
void cpu() {
    const auto vp=glm::perspective(glm::radians(55.f),1.6f,.1f,100.f)*glm::lookAt(glm::vec3(2,3,7),glm::vec3(0,0,0),glm::vec3(0,1,0));
    auto reflected=makeWorldPlanarReflectionView(glm::value_ptr(vp),{2,3,7},.25f,513,321);
    check(reflected.width==257&&reflected.heightPixels==161,"Odd reflection extent lost pixels");
    check(reflected.cameraPosition==std::array<float,3>{2,-2.5f,7},"Camera reflection plane mismatch");
    check(std::abs(packWorldPlanarReflection(reflected)[4][2]-161.f/257.f)<1e-6f,"Reflection filter aspect mismatch");
    auto matrix=glm::make_mat4(reflected.viewProjection.data());
    for (auto point : {glm::vec4(0,.25f,0,1),glm::vec4(3,.25f,-8,1)})
        check(glm::length(vp*point-matrix*point)<1e-5f,"Points on reflection plane changed projection");
    check(glm::length(matrix*glm::vec4(1,2,3,1)-vp*glm::vec4(1,-1.5f,3,1))<1e-5f,"Virtual object projection mismatch");
    auto twice=makeWorldPlanarReflectionView(reflected.viewProjection.data(),reflected.cameraPosition,.25f,512,320);
    for (int i=0;i<16;++i) check(std::abs(twice.viewProjection[i]-glm::value_ptr(vp)[i])<1e-5f,"Reflection is not involutory");
    bool rejected=false; try {makeWorldPlanarReflectionView(nullptr,{0,0,0},0,1,1);} catch (const std::invalid_argument&) {rejected=true;}
    check(rejected,"Null reflection matrix accepted");
}
}
int main(int argc,char** argv) try {
    cpu();
    if (argc==1) {std::cout<<"PASS planar projection contracts\n";return 0;}
    check(argc==4,"Expected API, mode, image path");
    const std::string api=argv[1],mode=argv[2];
    const bool emissive=mode=="emissive"||mode=="emissive-rough"||mode=="emissive-off";
    check(emissive||mode=="on"||mode=="off"||mode=="offscreen"||mode=="reversed"||mode=="dry"||mode=="clipped"||mode=="occluded"||mode=="reset"||mode=="extent-change","Unknown reflection test");
    SDL_SetMainReady();struct Quit {~Quit(){SDL_Quit();}} quit;
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
    std::filesystem::remove(argv[3]);
    const auto environment=[](const char* key,const char* value) {
#if defined(_MSC_VER)
        _putenv_s(key,value);
#else
        setenv(key,value,1);
#endif
    };
    environment("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    environment("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    environment("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    Window window("Planar reflection contracts",width,height,api=="opengl"?Window::GraphicsApi::OpenGL:
        api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if (api=="opengl") {
        check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");
        renderer=engine::render::createLegacyOpenGLBackend();renderer->onResize(width,height);
    } else if (api=="vulkan") renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if (api=="d3d12") renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown API");
    auto* planar=dynamic_cast<IWorldPlanarReflection*>(renderer.get());
    auto* radiance=dynamic_cast<IWorldSceneRadiance*>(renderer.get());
    check(planar&&radiance,"Reflection interfaces absent");
    const glm::vec3 camera(0,2,6);
    const auto vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,0,-1),glm::vec3(0,1,0));
    const float objectY=mode=="offscreen"?4.5f:1.5f;
    const auto reflected=makeWorldPlanarReflectionView(glm::value_ptr(vp),{camera.x,camera.y,camera.z},0,width,height);
    std::array<unsigned char,16*16*4> sign{};
    for (int y=0;y<16;++y) for(int x=0;x<16;++x) {
        const auto at=(y*16+x)*4;
        sign[at]=sign[at+1]=sign[at+2]=(x<3||y<3||y>=13||(y>=6&&y<10))?255:0;
        sign[at+3]=255;
    }
    const auto draw=[&](bool floor,bool reverse,int blocker=0) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for (int i=0;i<4;++i) {
            auto& v=vertices[i];
            v.x=(i==0||i==3)?-1.f:1.f;
            v.y=floor?0.f:objectY+(i<2?-.6f:.6f);
            v.z=floor?(i<2?8.f:-12.f):-1.f;
            if (blocker) {
                v.x*=.7f; v.y=(blocker<0?-.5f:.75f)+(i<2?-.25f:.25f);
                v.z=blocker<0?3.f:.5f;
            }
            if (floor) v.x*=12;
            v.u=(i==0||i==3)?0.f:1.f;v.v=i<2?0.f:1.f;
            v.ny=floor?1.f:0.f;v.nz=floor?0.f:1.f;v.tx=1;v.a=1;
            v.r=floor?.04f:1.f;v.g=v.b=floor?.04f:.005f;
            if (blocker) {v.r=.005f;v.g=blocker>0?1.f:.005f;v.b=blocker<0?1.f:.005f;}
        }
        auto order=indices;if(reverse) for(int i=0;i<6;i+=3) std::swap(order[i+1],order[i+2]);
        IRenderBackend::WorldTextureData t;
        t.key=t.cacheKey="planar-white";t.rgba=white.data();t.width=t.height=1;
        t.textureSrgb=0;t.materialMode=2;t.metallicFactor=0;t.roughnessFactor=floor?.16f:1.f;
        t.environment.enabled=true;t.environment.ambientRadiance={.5f,.5f,.5f};
        t.environment.sunRadiance={0,0,0};t.environment.hazeDensity=0;t.environment.exposure=.6f;
        t.environment.surfaceWeatherEnabled=floor;t.environment.waterSurface=floor;
        t.environment.surfaceWetness=mode=="dry"?0.f:1.f;
        if(emissive) {
            t.environment.ambientRadiance={0,0,0};
            if(floor) t.roughnessFactor=mode=="emissive-rough"?.4f:.06f;
            else {
                t.emissiveFactorR=mode=="emissive-off"?0.f:3.f;
                t.emissiveKey=t.emissiveCacheKey="planar-sign";
                t.emissiveRgba=sign.data();t.emissiveWidth=t.emissiveHeight=16;
                t.emissiveWrapS=t.emissiveWrapT=33071;
            }
        }
        t.cameraPosX=camera.x;t.cameraPosY=camera.y;t.cameraPosZ=camera.z;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),6,&t,glm::value_ptr(vp),width,height);
    };
    for(int frame=0;frame<4;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        radiance->setWorldSceneRadianceExposure(.6f);
        check(!planar->endWorldPlanarReflection(),"Prior view reflection capture leaked");
        if (mode!="off" && (mode!="reset" || frame==0)) {
            auto capture=reflected;
            if (mode=="extent-change" && frame%2==0) {capture.width=width;capture.heightPixels=height;}
            check(planar->beginWorldPlanarReflection(capture),"Reflection begin failed");
            check(!planar->beginWorldPlanarReflection(reflected),"Nested reflection accepted");
            draw(false,mode=="reversed");
            if (mode=="clipped"||mode=="occluded") draw(false,false,mode=="clipped"?-1:1);
            check(planar->endWorldPlanarReflection(),"Reflection end failed");
        }
        draw(true,false);draw(false,mode=="reversed");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation()) window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(image&&w==width&&h==height,"Missing reflection image");
    const auto projected=vp*glm::vec4(0,-objectY,-1,1);
    const int x=int((projected.x/projected.w*.5f+.5f)*width);
    const int y=int((.5f-projected.y/projected.w*.5f)*height);
    check(x>3&&x<width-3&&y>3&&y<height-3,"Oracle reflection outside viewport");
    int red=0,green=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
        const auto at=((y+dy)*width+x+dx)*3;red+=image.get()[at];green+=image.get()[at+1];
    }
    const bool active=mode!="off"&&mode!="dry"&&mode!="reset"&&mode!="emissive-off";
    std::cout<<api<<' '<<mode<<" reflection pixel="<<x<<','<<y<<" red="<<red/25.f<<" green="<<green/25.f<<'\n';
    check(mode=="occluded"?green>red+250:active?red>green+250:std::abs(red-green)<75,"Reflected object absent or dry/disabled receiver reflects");
    if(mode=="offscreen") {
        auto real=vp*glm::vec4(0,objectY-.6f,-1,1);
        check(real.y/real.w>1,"Offscreen oracle object remains visible in main view");
    }
    std::cout<<"PASS planar reflection\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
