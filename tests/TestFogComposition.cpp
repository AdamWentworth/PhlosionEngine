#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldFog.h"
#include "engine/render/IWorldSceneRadiance.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include <SDL.h>
#include <stb_image.h>
#include <filesystem>
#include <iostream>
using namespace engine::render;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
double display(double x,double exposure) {
    x*=exposure/.6;x=std::clamp((x*(x+.0245786)-.000090537)/(x*(.983729*x+.432951)+.238081),0.,1.);
    return 255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055);
}
std::array<double,2> sample(const WorldPrecipitationLightingDiagnostic& v,double depth,double range) {
    double z=std::clamp(std::sqrt(std::max(depth/range,0.))*v.depth-.5,0.,double(v.depth-1));
    const unsigned a=unsigned(z),b=std::min(a+1,v.depth-1);
    std::array<double,2> result{};
    for(unsigned y:{v.height/2-1,v.height/2})for(unsigned x:{v.width/2-1,v.width/2}) {
        const auto& left=v.scattering[(a*v.height+y)*v.width+x];const auto& right=v.scattering[(b*v.height+y)*v.width+x];
        result[0]+=(left[0]*(1-(z-a))+right[0]*(z-a))*.25;
        result[1]+=(left[3]*(1-(z-a))+right[3]*(z-a))*.25;
    }
    return result;
}
int main(int argc,char** argv)try{
    check(argc==4,"Expected API, case, screenshot");
    const std::string api=argv[1],mode=argv[2];
    const bool analytic=mode=="analytic"||mode=="analytic-low";
    check(api=="vulkan"||api=="d3d12","API");
    check(mode=="opaque"||mode=="alpha"||mode=="distant"||mode=="zero"||mode=="exposure"||mode=="surface"||mode=="additive"||mode=="prepared"||mode=="analytic"||mode=="analytic-low","Case");
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
#ifdef _WIN32
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);_putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");_putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#endif
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Fog composition",256,256,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> r;
    if(api=="vulkan")r=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
    else r=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*r);auto& volume=dynamic_cast<IWorldPrecipitationLighting&>(*r);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*r);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*r);
    auto owner=scopes.createWorldResourceScope(64ull*1024*1024);
    WorldPrecipitationLightingView u;u.owner=owner;u.range=64;u.quality=0;
    u.view.identity=101;u.view.generation=1;u.view.width=u.view.height=256;u.view.cameraValid=true;u.view.cameraPosition={0,-300,0};
    auto matrix=glm::perspective(glm::radians(10.f),1.f,.05f,5000.f)*glm::lookAt(glm::vec3(0,-300,0),glm::vec3(0,-300,-1),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(matrix),16,u.view.viewProjection.begin());
    auto& env=u.environment;env.enabled=true;env.hazeDensity=mode=="zero"||analytic?0:.02f;env.localHazeAlbedo=.5f;
    env.ambientRadiance={.4f,.4f,.4f};env.volumetricFogDeferred=env.localHazeDeferred=true;
    const double backgroundDepth=mode=="distant"||analytic?200:20,exposure=mode=="exposure"?.3:1;
    if(analytic) {
        u.distantFog.weight=1;u.distantFog.start=5;u.distantFog.density={.002f,.003f};
        u.distantFog.height={-302,-298};u.distantFog.falloff={.04f,.08f};
        u.distantFog.nearAlbedo=u.distantFog.farAlbedo={0,0,0};
    }
    const auto plane=[&](float depth,float energy,float alpha) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(unsigned i=0;i<4;++i){auto& v=vertices[i];v.x=i==0||i==3?-depth:depth;v.y=-300+(i<2?-depth:depth);v.z=-depth;v.r=v.g=v.b=v.nz=v.tx=v.tw=1;v.a=alpha;}
        constexpr unsigned char black[]{0,0,0,255},white[]{255,255,255,255};constexpr std::uint32_t indices[]{0,1,2,0,2,3};
        IRenderBackend::WorldTextureData m;m.key=m.cacheKey="fog-black";m.rgba=black;m.width=m.height=1;
        m.emissiveKey=m.emissiveCacheKey="fog-white";m.emissiveRgba=white;m.emissiveWidth=m.emissiveHeight=1;
        m.materialMode=2;m.alphaMode=alpha<1?2:0;m.blendMode=mode=="additive"&&alpha<1?1:0;m.emissiveFactorR=m.emissiveFactorG=m.emissiveFactorB=energy;
        m.environment=env;m.environment.ambientRadiance={0,0,0};m.normalScale=0;m.metallicFactor=0;
        m.cameraPosY=-300;m.cameraForwardY=0;m.cameraForwardZ=-1;
        r->drawWorldIndexedMeshTextured(vertices.data(),4,indices,6,&m,u.view.viewProjection.data(),256,256);
    };
    WorldPrecipitationLightingDiagnostic closeData,farData;
    for(unsigned frame=0;frame<4;++frame) {
        r->beginFrame(0,0,0,1);r->beginWorldSceneColorPass(256,256);radiance.setWorldSceneRadianceExposure(float(exposure));
        if(mode=="prepared")check(surface.beginWorldSurfaceBuffer(u.view.viewProjection.data(),256,256),"Prepared surface capture");
        if(!scopes.beginWorldResourceScope(owner)){
            WorldResourceScopeStats stats;scopes.getWorldResourceScopeStats(owner,stats);
            throw std::runtime_error("Fog draw owner frame "+std::to_string(frame)+" status "+
                std::to_string(int(stats.status))+" bytes "+std::to_string(stats.retained.payloadBytes)+
                " error "+stats.error);
        }
        plane(float(backgroundDepth),.15f,1);
        check(scopes.endWorldResourceScope(),"Fog draw end");
        if(mode=="prepared")check(surface.endWorldSurfaceBuffer(),"Prepared surface publication");
        u.usePreparedSurfaceDepth=mode=="prepared";
        u.view.frame=frame+1;
        if(mode=="analytic-low"){
            check(dynamic_cast<IWorldDepthControl&>(*r).captureWorldDepthSnapshot(),"Analytic opaque depth");
            volume.bindWorldFog(&u);
        }
        else {
        u.distant=false;auto close=volume.updateWorldPrecipitationLighting(u);
        if(frame==0)check(volume.captureWorldPrecipitationLighting(),"Near capture");
        u.distant=true;auto distant=volume.updateWorldPrecipitationLighting(u);
        if(frame==1)check(volume.captureWorldPrecipitationLighting(),"Far capture");
        u.distant=false;volume.bindWorldFog(&u,&close,&distant);
        }
        check(scopes.beginWorldResourceScope(owner),"Fog compose owner");if(mode=="surface"||mode=="prepared")plane(float(backgroundDepth),.15f,1);else composeWorldFog(*r,u);
        if(mode=="alpha"||mode=="additive")plane(5,.5f,.5f);
        check(scopes.endWorldResourceScope(),"Fog compose end");
        r->endWorldSceneColorPass();r->endFrame();if(!r->handlesPresentation())window.swapBuffers();
        if(frame==0&&mode!="analytic-low")closeData=volume.readWorldPrecipitationLighting();
        if(frame==1&&mode!="analytic-low")farData=volume.readWorldPrecipitationLighting();
    }
    auto integrated=mode=="analytic-low"?std::array<double,2>{0,1}:sample(closeData,backgroundDepth,64);
    if(backgroundDepth>64&&mode!="analytic-low"){auto farValue=sample(farData,(backgroundDepth-64)/20,64);integrated[0]+=integrated[1]*farValue[0];integrated[1]*=farValue[1];}
    double expected=integrated[0]+integrated[1]*.15;
    if(analytic)expected*=distantFogTransmission(u.distantFog,-300,0,backgroundDepth);
    if(mode=="alpha"){auto front=sample(closeData,5,64);expected=.5*(front[0]+front[1]*.5)+.5*expected;}
    if(mode=="additive"){auto front=sample(closeData,5,64);expected+=.5*front[1]*.5;}
    r->shutdown();r.reset();
    int w=0,h=0,c=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(image&&w==256&&h==256,"Fog screenshot missing");
    double error=0;for(int channel=0;channel<3;++channel)error=std::max(error,std::abs(image.get()[(128*256+128)*3+channel]-display(expected,exposure)));
    std::cout<<"expected linear="<<expected<<" display="<<display(expected,exposure)<<" error="<<error<<'\n';
    check(error<4,"Fog RGB/transmittance, depth, alpha or exposure mismatch");
    std::cout<<"PASS "<<api<<' '<<mode<<" independent volume-to-scene composition\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
