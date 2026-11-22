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
#include <vector>
using namespace engine::render;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
struct Capture {int width,height;std::vector<unsigned char> pixels;};
Capture capture(const std::string& api,const std::string& mode,bool reuse,const std::filesystem::path& path) {
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string());
    env("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    int width=640,height=480;
    Window window("Surface depth reuse",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    check(surface.prefersWorldSurfaceDepthReuse()==(api=="d3d12"),"Unexpected depth scheduling policy");
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    std::array<unsigned char,16*16*4> mask{};
    for(int i=0;i<256;++i){mask[4*i]=mask[4*i+1]=mask[4*i+2]=255;mask[4*i+3]=((i%16)/4+(i/16)/4)%2?255:0;}
    glm::mat4 vp(1);int frame=0;
    const auto quad=[&](float half,float z,glm::vec3 color,int alpha=0,bool unlit=false,bool depth=true) {
        std::array<IRenderBackend::WorldMeshVertex,4> v{};
        for(int i=0;i<4;++i) {
            auto& q=v[i];q.x=(i==0||i==3?-half:half)+(mode=="motion"&&depth?frame*.025f:0.f);
            q.y=i<2?-half:half;q.z=z;q.u=i==0||i==3?0.f:1.f;q.v=i<2?0.f:1.f;
            q.r=q.g=q.b=1;q.a=alpha==2?.4f:1;q.nz=q.tx=q.tw=1;
        }
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey=alpha==1?"reuse-mask":"reuse-white";
        t.rgba=alpha==1?mask.data():white.data();t.width=t.height=alpha==1?16:1;
        t.materialMode=unlit?0:2;t.textureSrgb=0;t.alphaMode=static_cast<std::uint8_t>(alpha);t.alphaCutoff=.5f;t.depthTestEnabled=depth?1:0;
        t.cameraPosZ=3;t.metallicFactor=0;t.roughnessFactor=.8f;
        t.vertexColorMulR=color.r;t.vertexColorMulG=color.g;t.vertexColorMulB=color.b;
        t.emissiveFactorR=color.r;t.emissiveFactorG=color.g;t.emissiveFactorB=color.b;
        const auto matrix=depth?vp:glm::mat4(1);
        renderer->drawWorldIndexedMeshTextured(v.data(),v.size(),indices.data(),indices.size(),&t,glm::value_ptr(matrix),width,height);
    };
    for(frame=0;frame<4;++frame) {
        if(mode=="extent"&&frame==1) {
            width=641;height=481;SDL_SetWindowSize(window.getSDLWindow(),width,height);SDL_PumpEvents();renderer->onResize(width,height);
        }
        vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,20.f)*
            glm::lookAt(glm::vec3(mode=="motion"?frame*.01f:0,0,3),glm::vec3(0),glm::vec3(0,1,0));
        const auto view=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        check(!surface.restoreWorldSurfaceDepth(view),"Prior scene depth leaked");
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Surface begin");
        check(!surface.restoreWorldSurfaceDepth(view),"Active capture depth accepted");
        // Opaque, masked and enrolled forward-only coverage participate; glass does not.
        WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
        WorldMotionSample motion{&previous,123,true};
        if(mode=="forward")check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&motion,1}),"Forward motion enrollment");
        quad(.6f,.5f,{.05f,.12f,.3f},mode=="masked"?1:0,mode=="forward");
        if(mode=="forward")surface.setWorldSurfaceMotion(worldMotionIdentity,{});
        if(mode!="occluded")quad(1.4f,0,{.4f,.07f,.02f});
        if(mode=="transparent")quad(.9f,.75f,{.1f,.4f,.1f},2);
        check(surface.endWorldSurfaceBuffer(),"Surface end");
        auto mismatch=view;mismatch[0][0]+=.01f;check(!surface.restoreWorldSurfaceDepth(mismatch),"Wrong camera accepted");
        mismatch=view;mismatch[8][0]+=1;check(!surface.restoreWorldSurfaceDepth(mismatch),"Wrong extent accepted");
        // A known scene color must survive depth publication untouched.
        quad(1,0,{.02f,.04f,.08f},0,true,false);
        check(dynamic_cast<IWorldDepthControl&>(*renderer).clearWorldDepth(),"Clear scratch depth");
        if(mode=="reset") {
            surface.clearWorldSurfaceBuffer();check(!surface.restoreWorldSurfaceDepth(view),"Cleared depth accepted");
        } else if(reuse)check(surface.restoreWorldSurfaceDepth(view),"Depth reuse failed");
        quad(1.4f,0,{.4f,.07f,.02f});
        if(mode!="occluded")quad(.6f,.5f,{.05f,.12f,.3f},mode=="masked"?1:0,mode=="forward");
        if(mode=="transparent")quad(.9f,.75f,{.1f,.4f,.1f},2);
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* pixels=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(pixels&&w==width&&h==height,"Missing depth reuse image");
    Capture result{w,h,{pixels,pixels+w*h*3}};stbi_image_free(pixels);return result;
}
}
int main(int argc,char** argv)try {
    check(argc==4,"Expected API case directory");const std::string api=argv[1],mode=argv[2];
    check(api=="d3d12"||api=="vulkan","Invalid API");
    check(mode=="opaque"||mode=="masked"||mode=="transparent"||mode=="forward"||mode=="motion"||mode=="extent"||mode=="occluded"||mode=="reset","Invalid case");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    std::filesystem::path root=argv[3];std::filesystem::create_directories(root);
    auto reference=capture(api,mode,false,root/"reference.png"),result=capture(api,mode,true,root/"reuse.png");
    check(reference.width==result.width&&reference.height==result.height,"Image extent changed");
    int maxDelta=0;for(std::size_t i=0;i<reference.pixels.size();++i)maxDelta=std::max(maxDelta,std::abs(int(reference.pixels[i])-result.pixels[i]));
    if(mode=="occluded") {
        const auto center=(result.height/2*result.width+result.width/2)*3;
        check(int(reference.pixels[center])-result.pixels[center]>50,"Hidden geometry was not rejected");
        for(int c=0;c<3;++c)check(result.pixels[center+c]==result.pixels[(3*result.width+3)*3+c],"Depth publication changed scene color");
        const auto outside=(result.height/2*result.width+result.width/4)*3;
        for(int c=0;c<3;++c)check(reference.pixels[outside+c]==result.pixels[outside+c],"Depth blocked uncovered pixels");
    } else check(maxDelta<=1,"Depth reuse changed visible composition");
    std::cout<<api<<' '<<mode<<" max delta="<<maxDelta<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
