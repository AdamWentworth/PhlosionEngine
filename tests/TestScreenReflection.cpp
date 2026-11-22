#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldScreenReflectionTiles.h"
#include "engine/render/WorldSurfaceReflection.h"
#include <cstdlib>
#include "engine/core/Environment.h"
#include "engine/core/Environment.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
using namespace engine::render;
namespace {
int width=640,height=480;
float thinSourceX=1.6f;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
glm::vec3 camera(0,1.5f,6);
glm::mat4 vp;
std::array<int,2> pixel(glm::vec3 p){auto c=vp*glm::vec4(p,1);return {int((c.x/c.w*.5f+.5f)*width),int((.5f-c.y/c.w*.5f)*height)};}
std::vector<unsigned char> capture(const std::string& api,const std::string& mode,bool enabled,const std::filesystem::path& path,int reuse=0) {
    std::filesystem::remove(path);
#if defined(_WIN32)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str());
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str(),1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Screen reflection contracts",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& screen=dynamic_cast<IWorldScreenReflection&>(*renderer);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*renderer);
    constexpr std::array<unsigned char,4> white{255,255,255,255},hole{255,255,255,0};
    constexpr std::array<std::uint32_t,6> order{0,1,2,0,2,3};
    bool captureMaterials=false;
    auto draw=[&] {
        for(int item=0;item<(mode=="occluded"||mode=="masked"||mode=="transparent"?3:2);++item) {
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            const glm::vec3 center=item==2 && mode=="transparent"?glm::vec3(0,1.5,.0001f):item==0?glm::vec3(0,1.5,0):item==1?glm::vec3(mode=="offscreen"?3.5f:mode=="edge"?2.95f:mode.starts_with("thin")?thinSourceX:1.6f,1.5,2):glm::vec3(1.4,1.5,1);
            float half=item==0?(mode=="offscreen"||mode=="edge"?3.5f:2.f):item==1?.3f:.45f;
            if(item==2 && mode=="transparent")half=2.f;
            const float halfWidth=item==1 && mode.starts_with("thin")?.004f:half;
            for(int j=0;j<4;++j){auto& v=vertices[j];v.x=center.x+(j==0||j==3?-halfWidth:halfWidth);v.y=center.y+(j<2?-half:half);v.z=center.z;
                v.nz=1;v.tx=1;v.tw=1;v.r=v.g=v.b=item==0?.8f:.01f;v.a=1;v.u=j==0||j==3?0.f:1.f;v.v=j<2?0.f:1.f;}
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey="screen-white";t.rgba=white.data();t.width=t.height=1;
            t.textureSrgb=0;t.normalScale=0;t.materialMode=mode=="pbr254"?254:2;t.metallicFactor=item==0?1.f:0.f;
            t.roughnessFactor=item==0?(mode=="rough"?.5f:mode=="matte"?1.f:.08f):1.f;
            // An intentionally matte captured receiver proves the prepared result is consumed.
            if(captureMaterials && mode=="surface-authority" && item==0)t.roughnessFactor=1;
            if(mode=="transparent") {
                if(item==0)t.roughnessFactor=.6f;
                if(item==2){t.alphaMode=2;t.metallicFactor=1;t.roughnessFactor=.08f;}
            }
            t.cameraPosX=camera.x;t.cameraPosY=camera.y;t.cameraPosZ=camera.z;
            t.environment.enabled=true;t.environment.ambientRadiance={.03f,.03f,.03f};t.environment.exposure=.6f;
            if(item==1 && mode!="source-off") {
                t.emissiveFactorR=.02f;t.emissiveFactorG=4.f;t.emissiveFactorB=3.f;
                t.emissiveKey=t.emissiveCacheKey="screen-emission";t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
            }
            if(item==2 && mode=="masked"){t.key=t.cacheKey="screen-hole";t.rgba=hole.data();t.alphaMode=1;}
            renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),6,&t,glm::value_ptr(vp),width,height);
        }
    };
    for(int frame=0;frame<4;++frame){
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);radiance.setWorldSceneRadianceExposure(.6f);
        check(!screen.endWorldScreenReflection(),"Reflection source leaked between views");
        auto& surfaces=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
        const bool shared=engine::env::get("PHLOSION_TEST_SHARED_REFLECTIONS").has_value();
        if(shared)check(!surfaces.resolveWorldSurfaceScreenReflection(),"Stale resolved reflection");
        if(shared && enabled && (mode!="reset"||frame==0)) {
            check(surfaces.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Surface capture begin");
            captureMaterials=true;draw();captureMaterials=false;
            check(surfaces.endWorldSurfaceBuffer(),"Surface capture end");
            check(!surfaces.resolveWorldSurfaceScreenReflection(),"Missing source accepted");
        }

        if(enabled && std::abs(reuse)==2 && (mode!="reset"||frame==0))
            drawWorldContactVisibility(*renderer,glm::value_ptr(vp),width,height,draw);
        if(enabled && (mode!="reset"||frame==0)){
            WorldScreenReflectionSettings settings;
            if(const auto steps=engine::env::get("PHLOSION_TEST_REFLECTION_STEPS"))
                settings={std::stoi(*steps),.6f};
            const auto packet=packWorldScreenReflection(glm::value_ptr(vp),width,height,40.f,.12f,reuse>0,settings);
            check(screen.beginWorldScreenReflection(packet),"Screen source begin failed");
            check(!screen.beginWorldScreenReflection(packet),"Nested screen source accepted");
            draw();check(screen.endWorldScreenReflection(),"Screen source end failed");
            if(shared && reuse!=-3) {
                check(surfaces.resolveWorldSurfaceScreenReflection(),"Shared reflection resolve failed");
                check(!surfaces.resolveWorldSurfaceScreenReflection(),"Duplicate reflection resolve accepted");
            }
        }
        draw();renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* data=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(data&&w==width&&h==height,"Missing screen reflection image");
    std::vector<unsigned char> result(data,data+w*h*3);stbi_image_free(data);return result;
}
}
int main(int argc,char** argv)try {
    vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,1.5,0),glm::vec3(0,1,0));
    if(argc==1){
        check(worldScreenReflectionTileLayout(1921,1081)==std::array<int,3>{241,136,1921},"Partial edge depth tiles lost coverage");
        check(worldScreenReflectionTileLayout(640,640)==std::array<int,3>{80,80,720},"Depth tiles overlap square radiance capture");
        auto p=packWorldScreenReflection(glm::value_ptr(vp),width,height);
        check(p[8][3]==-1&&p[8][0]==width&&p[9][3]==0,"Invalid/default-enabled screen packet");
        check(p[10][2]==4096 && p[10][3]==.75f,"Existing clients lost reference quality");
        const auto bounded=packWorldScreenReflection(glm::value_ptr(vp),width,height,40.f,.12f,false,{64,.6f});
        check(bounded[10][2]==64 && bounded[10][3]==.6f && bounded[8]==p[8],
              "Quality changed reflection range, camera or output extent");
        for(const auto invalid:std::array<WorldScreenReflectionSettings,5>{{{0,.6f},{4097,.6f},{64,.2f},{64,1.f},{64,NAN}}}) {
            bool rejected=false;
            try{packWorldScreenReflection(glm::value_ptr(vp),width,height,40.f,.12f,false,invalid);}
            catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"Invalid reflection budget accepted");
        }
        auto contact=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        contact[8][3]=1;
        check(!worldScreenReflectionCanReuseContactDepth(p,contact),"Default source reused contact depth");
        const auto surfaceView=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        check(!surfaceReflectionViewMatches(surfaceView,p),"Unfinished source matches surface");
        auto ready=p;ready[8][3]=1;
        check(surfaceReflectionViewMatches(surfaceView,ready),"Matching surface rejected");
        ready[0][3]+=.1f;check(!surfaceReflectionViewMatches(surfaceView,ready),"Wrong view accepted");
        ready=p;ready[8][3]=1;ready[8][0]+=1;
        check(!surfaceReflectionViewMatches(surfaceView,ready),"Wrong extent accepted");
        p=packWorldScreenReflection(glm::value_ptr(vp),width,height,40.f,.12f,true);
        check(worldScreenReflectionCanReuseContactDepth(p,contact),"Matching view cannot reuse contact depth");
        contact[8][0]+=1;
        check(!worldScreenReflectionCanReuseContactDepth(p,contact),"Different viewport reused contact depth");
        contact[8][0]-=1;contact[0][3]+=.1f;
        check(!worldScreenReflectionCanReuseContactDepth(p,contact),"Different camera reused contact depth");
        contact[0][3]-=.1f;contact[8][3]=-1;
        check(!worldScreenReflectionCanReuseContactDepth(p,contact),"Incomplete contact source reused depth");
        bool rejected=false;try{packWorldScreenReflection(nullptr,width,height);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Null screen view accepted");
        rejected=false;try{packWorldScreenReflection(glm::value_ptr(vp),width,height,0);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid trace range accepted");return 0;
    }
    check(argc==4,"Expected API mode output directory");
    const std::string api=argv[1],mode=argv[2];check(api=="d3d12"||api=="vulkan","Invalid API");
    check(mode=="mirror"||mode=="surface-authority"||mode=="transparent"||mode=="pbr254"||mode=="rough"||mode=="matte"||mode=="occluded"||mode=="masked"||mode=="reset"||mode=="source-off"||mode=="camera"||mode=="offscreen"||mode=="edge"||mode=="square"||mode.starts_with("thin"),"Invalid mode");
    if(mode=="square") {
        width=height=1024;
        vp=glm::perspective(glm::radians(60.f),1.f,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,1.5,0),glm::vec3(0,1,0));
    }
    if(mode.starts_with("thin")) {
        width=mode=="thin5"?1921:1920;height=mode=="thin5"?1081:1080;thinSourceX+=.003f*std::stoi(mode.substr(4));
        vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,1.5,0),glm::vec3(0,1,0));
    }
    if(mode=="camera"){camera.x=-.6f;vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,1.5,0),glm::vec3(0,1,0));}
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    const std::filesystem::path root=argv[3];std::filesystem::create_directories(root);
    auto off=capture(api,mode,false,root/"off.png"),on=capture(api,mode,true,root/"on.png");
    if(mode=="surface-authority") {
        const auto reference=capture(api,mode,true,root/"inline-control.png",-3);
        int changed=0;
        for(std::size_t i=0;i<on.size();++i)changed=std::max(changed,std::abs(int(on[i])-int(reference[i])));
        check(changed>20,"Prepared surface result did not replace the inline search");
    }
    if(mode=="transparent") {
        const auto reference=capture(api,mode,true,root/"forward-reference.png",-3);
        int maxStep=0;
        for(std::size_t i=0;i<on.size();++i)maxStep=std::max(maxStep,std::abs(int(on[i])-int(reference[i])));
        std::cout<<"Transparent fallback maximum step="<<maxStep<<'\n';
        check(maxStep<=1,"Transparent receiver reused opaque reflection");
    }
    // A vertical z=0 mirror reflects the emitter at z=+2 to z=-2, independent
    // of the tracing implementation. Its real projection is a separate control.
    const float sourceX=mode=="offscreen"?3.5f:mode=="edge"?2.95f:mode.starts_with("thin")?thinSourceX:1.6f;
    auto reflected=pixel({sourceX,1.5f,-2.f}),real=pixel({sourceX,1.5f,2.f});
    auto patch=[&](auto xy,int channel){double sum=0;for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){auto at=((xy[1]+y)*width+xy[0]+x)*3+channel;sum+=int(on[at])-off[at];}return sum/25.;};
    const auto green=patch(reflected,1),direct=mode=="offscreen"?0.0:patch(real,1);
    if(mode=="offscreen")check(real[0]>width+3,"Offscreen oracle emitter is still in view");
    int maxDelta=0;for(std::size_t i=0;i<on.size();++i)maxDelta=std::max(maxDelta,std::abs(int(on[i])-off[i]));
    std::cout<<api<<' '<<mode<<" reflected green delta="<<green<<" real delta="<<direct<<" max="<<maxDelta<<'\n';
    if(mode=="reset"||mode=="matte"||mode=="surface-authority")check(maxDelta==0,"Disabled/rough view changed");
    else if(mode=="source-off"||mode=="occluded"||mode=="offscreen")check(green<3,"Absent/blocked reflection retained bright source");
    else check(green>20,"Missing emitter at analytic vertical mirror projection");
    check(std::abs(direct)<=1,"Direct source changed during reflection composition");
    if(mode=="rough") {
        int softened=0;
        for(int y=reflected[1]-20;y<=reflected[1]+20;++y)
            for(int x=reflected[0]-22;x<=reflected[0]+22;++x) {
                const auto at=(y*width+x)*3+1;const int delta=int(on[at])-off[at];
                if(delta>green*.1 && delta<green*.9)++softened;
            }
        check(softened>16,"Rough reflection retained a hard mirror edge");
    }
    for(int reuse=1;reuse<=2;++reuse) {
        // Compare the same lighting configuration: the contact pass affects
        // indirect visibility, so it needs its own uncached-depth reference.
        const auto reference=reuse==1?on:capture(api,mode,true,root/"contact-reference.png",-2);
        const auto cached=capture(api,mode,true,root/(reuse==1?"reuse-screen.png":"reuse-contact.png"),reuse);
        int error=0;
        for(std::size_t i=0;i<reference.size();++i)error=std::max(error,std::abs(int(reference[i])-cached[i]));
        std::cout<<"reuse="<<reuse<<" maximum color step="<<error<<'\n';
        check(error<=1,"Depth reuse changed the visible reflection");
    }
    std::cout<<"PASS screen reflection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
