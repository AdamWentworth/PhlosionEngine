#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldLocalShadow.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr int width=320,height=200;
constexpr std::array<unsigned char,4> white{255,255,255,255};
constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
}
int main(int argc,char** argv) try {
    check(argc==4,"Expected API, case, output");const std::string api=argv[1],mode=argv[2];
    check(mode=="blocked"||mode=="empty"||mode=="clear"||mode=="pcf"||mode=="border"||mode=="self"||mode=="range-edge","Unknown moment case");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}} quit;
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());std::filesystem::remove(argv[3]);
#if defined(_MSC_VER)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);_putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");_putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3],1);setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Live moment shadows",width,height,api=="opengl"?Window::GraphicsApi::OpenGL:api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="opengl"){check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");renderer=engine::render::createLegacyOpenGLBackend();renderer->onResize(width,height);}
    else if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if(api=="d3d12")renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown API");
    auto* shadows=dynamic_cast<IWorldLocalShadow*>(renderer.get());auto* hdr=dynamic_cast<IWorldSceneRadiance*>(renderer.get());
    check(shadows&&hdr,"Missing shadow/HDR interface");shadows->prewarmWorldLocalShadows();shadows->prewarmWorldLocalShadows();
    WorldEnvironmentLighting e;e.enabled=true;e.exposure=1;
    // Exercise all eight tiles with sparse source indices and alternating occupancy.
    for(int i=0;i<8;++i){auto& light=e.rectLights[i*2];light={{0,4,0},20,{2,0,0},{0,0,.2f},i%2?std::array<float,3>{0,0,4}:std::array<float,3>{4,0,0}};
        light.response=WorldRectLightResponse::FiniteLine;light.shadowFilter=mode=="pcf"?WorldLocalShadowFilter::Pcf:WorldLocalShadowFilter::DistanceMoments;
        if(mode=="range-edge"){light.position[1]=2;light.range=4;light.halfU[0]=8;}}
    auto shadowEnvironment=e;
    // Blue is the unshadowed control for empty red maps at the end of a long
    // source, where radial distance can exceed the normalized shadow range.
    if(mode=="range-edge")for(int i=1;i<8;i+=2)shadowEnvironment.rectLights[i*2].radiance={};
    const auto vp=glm::ortho(-5.f,5.f,-3.125f,3.125f,.1f,20.f)*glm::lookAt(glm::vec3(0,6,0),glm::vec3(0),glm::vec3(0,0,-1));
    const auto draw=[&](float y,const float* matrix,bool half) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(int i=0;i<4;++i){auto& v=vertices[i];v.x=(i==0||i==3)?-40.f:half?0.f:40.f;v.y=y;v.z=i<2?40.f:-40.f;v.ny=1;v.tx=1;v.r=v.g=v.b=v.a=1;}
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="moment-white";t.rgba=white.data();t.width=t.height=1;t.textureSrgb=0;t.materialMode=2;t.normalScale=0;t.metallicFactor=0;t.roughnessFactor=1;
        t.environment=e;t.cameraPosY=6;t.cameraPosZ=0;t.cameraForwardY=-1;t.cameraForwardZ=0;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,matrix,width,height);
    };
    for(int frame=0;frame<4;++frame){renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);hdr->setWorldSceneRadianceExposure(1);
        if(mode=="range-edge"){auto* bloom=dynamic_cast<IWorldBloom*>(renderer.get());check(bloom,"Missing bloom control");bloom->setWorldBloomStrength(0);}
        for(int repeat=0;repeat<2;++repeat){shadows->clearWorldLocalShadows();int tile=0;
            drawWorldRectShadows(*shadows,shadowEnvironment,[&](const auto& view){const bool occupied=(tile++%2)==0;
                if(mode=="self"){draw(0,view.viewProjection.data(),false);return;}
                if(occupied && mode!="empty" && mode!="range-edge" && !(mode=="clear"&&frame>=1))draw(2,view.viewProjection.data(),mode=="border");});}
        draw(0,glm::value_ptr(vp),false);renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();}
    renderer->shutdown();renderer.reset();int w=0,h=0,c=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(pixels&&w==width&&h==height,"Missing moment surface image");
    const auto mean=[&](int cx,int channel){double value=0;for(int y=95;y<105;++y)for(int x=cx-5;x<cx+5;++x)value+=pixels.get()[(y*width+x)*3+channel]/100.;return value;};
    const double red=mean(mode=="border"?100:160,0),blue=mean(mode=="border"?100:160,2);
    std::cout<<api<<' '<<mode<<" R="<<red<<" B="<<blue<<" rightR="<<mean(220,0)<<'\n';
    check(blue>80,"Empty blue tiles shadowed or surface absent");
    if(mode=="range-edge")check(red>=blue*.9,"Empty map shadows the source centre");
    else if(mode=="empty"||mode=="clear"||mode=="self")check(std::abs(red-blue)<3,"Empty/reset maps retained occlusion or tile cross-talk");
    else check(red<blue*.25,"Red blocker leaked or moment receivers use wrong depth");
    if(mode=="border")check(mean(220,0)>mean(220,2)*.9,"Partial blocker shadow crossed its silhouette");
    // ACES mixes channels at this brightness; use the established lit-side ratio.
    if(mode=="range-edge")check(mean(313,2)>80 && mean(313,0)>mean(313,2)*.9,
        "Empty map falsely shadows a lit surface beyond the radial range");
    std::cout<<"PASS live moment surface, sparse atlas, repeated passes and clear\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
