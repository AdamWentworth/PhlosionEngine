
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldClouds.h"
#include <SDL.h>
#include <fstream>
#include <filesystem>
#include "engine/core/Environment.h"
#include <iostream>
#include <memory>
#include <algorithm>
using namespace engine::render;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
    std::cout<<std::unitbuf;check(argc==2,"Expected API");std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Unknown API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    constexpr int w=129,h=97;
    Window window("Cloud qualification",w,h,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> backend;
    if(api=="vulkan")backend=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),w,h,false);
    else backend=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),w,h,false);
    auto& clouds=dynamic_cast<IWorldClouds&>(*backend);auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*backend);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*backend);
    const auto owner=scopes.createWorldResourceScope(64*1024*1024);check(owner!=0,"Cloud scope");
    const auto vp=glm::perspective(glm::radians(60.f),float(w)/h,.05f,10000.f)*glm::lookAt(glm::vec3(0),glm::vec3(0,1,-.01f),glm::vec3(0,0,-1));
    std::array<unsigned char,4> white{255,255,255,255};
    WorldCloudView u;u.owner=owner;u.view={41,1,0,w,h,true};u.view.cameraValid=true;
    std::copy_n(glm::value_ptr(vp),16,u.view.viewProjection.begin());
    u.environment.enabled=true;u.environment.sunDirection={0,1,0};u.environment.sunRadiance={10,10,10};
    u.environment.cloudCoverage=1;u.environment.clouds.density=.005f;
    u.weather.key="cloud-test-weather";u.weather.rgba=white.data();u.weather.width=u.weather.height=1;u.weather.textureSrgb=0;
    u.noise=u.weather;u.noise.key="cloud-test-noise";
    for(unsigned tier=0;tier<4;++tier){
        u.quality=tier;++u.view.frame;
        backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(w,h);
        clouds.prepareWorldClouds(u);
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),w,h),"Cloud opaque depth");
        check(surface.endWorldSurfaceBuffer(),"Cloud opaque depth close");
        clouds.resolveWorldClouds(u);
        if(tier)check(clouds.captureWorldClouds(),"Cloud capture");
        else check(!clouds.captureWorldClouds(),"Low must omit volumetric cloud integration and capture");
        backend->endWorldSceneColorPass();backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();
        if(!tier){clouds.clearWorldClouds();continue;}
        auto image=clouds.readWorldClouds();check(image.resolved.size()==w*h,"Cloud output shape");
        check(image.shadows.size()==512*512,"Cloud shadow shape");
        check(image.skyModel.size()==128*64,"Filtered sky model shape");
        const auto model=prepareSkyModel(u.environment.sky.indirectTurbidity,u.environment.sky.groundReflectance,glm::half_pi<float>());
        for(unsigned level=1;level<=4;++level) {
            const unsigned width=128u>>level,footprint=1u<<level;
            const unsigned origin=level==1?0:level==2?64:level==3?96:112;
            for(unsigned sample:{0u,width*width/3,width*width-1}) {
                std::array<double,3> expected{};
                for(unsigned sy=0;sy<footprint;++sy)for(unsigned sx=0;sx<footprint;++sx) {
                    const double x=1-2*((sample%width)*footprint+sx+.5)/128.0,z=1-2*((sample/width)*footprint+sy+.5)/128.0;
                    const double r2=x*x+z*z,y=(1-r2)/(1+r2);
                    const auto radiance=evaluateSkyModel(model,std::max(float(y),u.environment.sky.horizonMinimum),float(y));
                    for(unsigned c=0;c<3;++c)expected[c]+=radiance[c]*u.environment.sky.skylightColor[c]/(footprint*footprint);
                }
                const auto& actual=image.skyModel[(sample/width)*128+origin+sample%width];
                for(unsigned c=0;c<3;++c)check(std::abs(actual[c]-expected[c])<.003*std::max(expected[c],.001),"Filtered sky GPU footprint differs from numerical reference");
            }
        }
        const auto s=image.shadows[(512/2)*512+512/2];
        // Constant weather and noise, vertical sun, 200m steps: density after
        // coverage remapping is 11/18 at 1/3 and 17/18 at 2/3.
        const double expected=tier?(11.0/18.0+17.0/18.0)*200*.005/10:0;
        std::cout<<"tier="<<tier<<" shadow="<<s[0]<<" expected="<<expected<<" tiles="<<image.visibleTiles<<"\n";
        check(std::abs(s[0]-expected)<.0003,"Cloud shadow independent height-profile reference");
        double opacity=0,energy=0;
        for(const auto& px:image.resolved){
            for(float v:px)check(std::isfinite(v),"Nonfinite cloud output");
            check(px[3]>=0&&px[3]<=1,"Cloud transmission range");
            opacity+=1-px[3];energy+=px[0]+px[1]+px[2];
        }
        if(tier){check(opacity>1&&energy>1,"Enabled cloud integration");check(image.visibleTiles==9*7,"Visible cloud tile count");}
        else{check(opacity==0&&energy==0&&image.visibleTiles==0,"Low tier disables volumetric cloud work");}
        std::ofstream f(std::filesystem::path(engine::env::get("PHLOSION_SKY_OUTPUT").value_or("."))/("cloud-"+api+"-"+std::to_string(tier)+".bin"),std::ios::binary);
        f.write(reinterpret_cast<const char*>(image.resolved.data()),std::streamsize(image.resolved.size()*sizeof(image.resolved[0])));
        clouds.clearWorldClouds();
    }
    const auto otherOwner=scopes.createWorldResourceScope(64*1024*1024);check(otherOwner!=0,"Second cloud scope");
    u.quality=1;
    for(unsigned scenario=0;scenario<4;++scenario) {
        const int rw=scenario==3?131:w,rh=scenario==3?99:h;
        if(scenario==3){SDL_SetWindowSize(window.getSDLWindow(),rw,rh);backend->onResize(rw,rh);}
        const auto matrix=glm::perspective(glm::radians(60.f),float(rw)/rh,.05f,10000.f)*
            glm::lookAt(glm::vec3(0),glm::vec3(0,1,-.01f),glm::vec3(0,0,-1));
        u.view.width=rw;u.view.height=rh;++u.view.frame;u.view.reset=true;
        if(scenario>=2){u.owner=otherOwner;u.view.identity=42;}
        std::copy_n(glm::value_ptr(matrix),16,u.view.viewProjection.begin());
        backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(rw,rh);
        clouds.prepareWorldClouds(u);
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(matrix),rw,rh),"Cloud lifecycle depth begin");
        if(scenario==1) {
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            const auto inverse=glm::inverse(matrix);
            for(unsigned k=0;k<4;++k) {
                const auto point=inverse*glm::vec4(k==0||k==3?-1.f:1.f,k<2?-1.f:1.f,0,1);
                auto& v=vertices[k];v.x=point.x/point.w;v.y=point.y/point.w;v.z=point.z/point.w;
                v.ny=v.tx=v.r=v.g=v.b=v.a=v.weight0=1;
            }
            constexpr std::uint32_t indices[]{0,1,2,0,2,3,0,2,1,0,3,2};
            auto texture=u.weather;texture.materialMode=2;texture.environment={};
            backend->drawWorldIndexedMeshTextured(vertices.data(),4,indices,12,&texture,glm::value_ptr(matrix),rw,rh);
        }
        check(surface.endWorldSurfaceBuffer(),"Cloud lifecycle depth end");
        clouds.resolveWorldClouds(u);check(clouds.captureWorldClouds(),"Cloud lifecycle capture");
        backend->endWorldSceneColorPass();backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();
        const auto image=clouds.readWorldClouds();
        check(image.width==unsigned(rw)&&image.height==unsigned(rh)&&image.resolved.size()==std::size_t(rw*rh),"Cloud resize dimensions");
        check(image.owner==u.owner&&image.identity==u.view.identity,"Cloud scope/view separation");
        double opacity=0;for(const auto& pixel:image.resolved){for(float v:pixel)check(std::isfinite(v),"Cloud lifecycle finite");opacity+=1-pixel[3];}
        std::cout<<"cloud scenario="<<scenario<<" tiles="<<image.visibleTiles<<" opacity="<<opacity<<"\n";
        if(scenario==1){check(image.visibleTiles==0,"Opaque roof must remove cloud tile work");check(std::abs(opacity)<.001,"Clouds leaked through opaque roof");}
        else {check(image.visibleTiles==unsigned(((rw+15)/16)*((rh+15)/16)),"Cloud cut/resize tile coverage");check(opacity>1,"Cloud cut/view/resize lost sky");}
    }
    clouds.clearWorldClouds();
    check(scopes.requestWorldResourceScopeRelease(otherOwner),"Second cloud release");
    check(scopes.requestWorldResourceScopeRelease(owner),"Cloud release");backend->shutdown();
    std::cout<<"PASS "<<api<<" cloud budgets, shadow profile, integration and resources\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
