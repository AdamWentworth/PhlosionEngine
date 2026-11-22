#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldSunShadows.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include <SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <iostream>
using namespace engine::render;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(int argc,char** argv)try{
    std::cout<<std::unitbuf;check(argc==2,"Expected API");std::string api=argv[1];check(api=="vulkan"||api=="d3d12","API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    constexpr int w=129,h=97;
    Window window("Sun shadow qualification",w,h,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),w,h,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),w,h,false);
    auto& shadows=dynamic_cast<IWorldDirectionalShadow&>(*renderer);
    auto& surfaces=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*renderer);
    auto& fog=dynamic_cast<IWorldPrecipitationLighting&>(*renderer);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*renderer);
    const auto owner=scopes.createWorldResourceScope(64*1024*1024);check(owner!=0,"Sun/fog owner");
    const glm::vec3 camera(0,6,0);
    const auto view=glm::lookAt(camera,glm::vec3(0),glm::vec3(0,0,-1));
    const auto vp=glm::ortho(-6.f,6.f,-4.5f,4.5f,.05f,40.f)*view;
    const auto perspective=glm::perspective(glm::radians(90.f),float(w)/h,.05f,10000.f)*view;
    WorldEnvironmentLighting env;env.enabled=true;env.sunDirection={0,1,0};env.sunRadiance={3,3,3};env.ambientRadiance={.01f,.01f,.01f};
    constexpr unsigned char white[]{255,255,255,255};constexpr std::uint32_t indices[]{0,2,1,0,3,2};
    const auto draw=[&](float height,float offset,float extent,const float* matrix) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(int k=0;k<4;++k){auto& v=vertices[k];v.x=offset+((k==0||k==3)?-extent:extent);v.y=height;v.z=k<2?extent:-extent;
            v.ny=v.tx=v.r=v.g=v.b=v.a=v.weight0=1;v.u=k<2?0.f:1.f;v.v=.5f;}
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="sun-native-white";t.rgba=white;t.width=t.height=1;t.textureSrgb=0;
        t.materialMode=2;t.normalScale=0;t.metallicFactor=0;t.roughnessFactor=1;t.environment=env;t.cameraPosY=6;t.cameraForwardY=-1;t.cameraForwardZ=0;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices,6,&t,matrix,w,h);
    };
    std::uint64_t frame=0;
    for(unsigned tier=0;tier<4;++tier) {
        shadows.prewarmWorldSunShadows(tier);WorldSunShadowPlanner planner;
        WorldSunShadowInput input;input.quality=tier;input.camera=camera;input.forward={0,-1,0};input.light={0,1,0};input.viewProjection=perspective;
        double reference=0,fogReference=0;
        for(unsigned mode=0;mode<4;++mode) {
            input.enabled=mode!=0;input.cut=true;++input.sceneRevision;
            if(mode==3)input.ranges={1,2,3,4};
            auto plan=planner.prepare(input);
            renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(w,h);radiance.setWorldSceneRadianceExposure(1);
            check(shadows.setWorldSunShadowPlan(plan),"Sun plan admission");
            for(unsigned tile=0;tile<9;++tile)if(plan.drawMask&(1u<<tile)){
                check(shadows.beginWorldDirectionalShadow(plan.draws[tile]),"Sun cascade begin");
                draw(2,mode==2?3.f:0.f,1.5f,plan.draws[tile].viewProjection.data());
                check(shadows.endWorldDirectionalShadow(),"Sun cascade end");
            }
            WorldPrecipitationLightingView fogView;fogView.owner=owner;fogView.quality=0;fogView.range=32;
            fogView.view={91,1,1,w,h,true};fogView.view.cameraValid=true;fogView.view.cameraPosition={0,6,0};
            std::copy_n(glm::value_ptr(perspective),16,fogView.view.viewProjection.begin());
            fogView.environment=env;fogView.environment.hazeDensity=.02f;fogView.environment.localHazeAlbedo=.5f;
            check(!fog.updateWorldPrecipitationLighting(fogView).texture.empty(),"Sun/fog publication");
            check(fog.captureWorldPrecipitationLighting(),"Sun/fog capture");
            check(surfaces.beginWorldSurfaceBuffer(glm::value_ptr(vp),w,h),"Sun receiver depth");
            draw(0,0,20,glm::value_ptr(vp));check(surfaces.endWorldSurfaceBuffer(),"Sun receiver depth end");
            WorldTemporalView temporal{71,1,++frame,w,h,true};temporal.cameraValid=true;temporal.cameraPosition={0,6,0};
            std::copy_n(glm::value_ptr(vp),16,temporal.viewProjection.begin());
            check(surfaces.beginWorldTemporalScene(temporal),"Sun capture begin");
            draw(0,0,20,glm::value_ptr(vp));
            check(surfaces.resolveWorldTemporalScene(),"Sun capture resolve");check(surfaces.captureWorldTemporalScene(),"Sun capture readback");
            renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
            const auto fogImage=fog.readWorldPrecipitationLighting();double fogEnergy=0;
            check(fogImage.width>=4&&fogImage.height>=4&&fogImage.depth==32,"Sun/fog dimensions");
            for(unsigned y=fogImage.height/2-1;y<=fogImage.height/2;++y)
                for(unsigned x=fogImage.width/2-1;x<=fogImage.width/2;++x)
                    fogEnergy+=fogImage.lighting[(12*fogImage.height+y)*fogImage.width+x][0];
            if(mode==0){fogReference=fogEnergy;check(fogReference>1e-5,"Sun/fog control empty");}
            else if(mode==2)check(fogEnergy>fogReference*.8,"Moved blocker did not restore fog sunlight");
            else check(fogEnergy<fogReference*.65,"Sun atlas did not shadow the volume consumer");
            std::cout<<"tier="<<tier<<" mode="<<mode<<" fog="<<fogEnergy<<" control="<<fogReference<<"\n";
            auto image=surfaces.readWorldTemporalScene();check(image.color.size()==w*h,"Sun image size");
            double centre=0;for(int y=h/2-2;y<=h/2+2;++y)for(int x=w/2-2;x<=w/2+2;++x)centre+=image.color[y*w+x][0]/25.;
            std::cout<<"tier="<<tier<<" case="<<mode<<" centre="<<centre<<" reference="<<reference<<"\n";
            if(mode==0){reference=centre;check(reference>.02,"Sun reference illumination");}
            else if(mode==2)check(centre>reference*.8,"Moved caster leaves no stale shadow");
            else check(centre<reference*.3,"Near/distant caster shadow missing");

        }
        shadows.clearWorldSunShadows();
    }
    renderer->shutdown();std::cout<<"PASS "<<api<<" sun cascades, distant coverage, moving caster and presets\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
