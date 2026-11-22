#include "engine/render/LegacyOpenGL.h"
#include "engine/render/WorldSurfaceComposition.h"
#include "engine/render/IWorldPlanarReflection.h"
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
struct Capture {int width,height;std::vector<unsigned char> pixels;IRenderBackend::BackendFrameStats stats;};
Capture capture(const std::string& api,const std::string& variant,bool reuse,const std::filesystem::path& path) {
    const bool planar=variant.starts_with("planar-");
    const std::string mode=planar?variant.substr(7):variant;
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string());
    env("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    int width=640,height=480;
    Window window("Surface composition",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);

    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    std::array<unsigned char,16*16*4> mask{};
    for(int i=0;i<256;++i){mask[4*i]=mask[4*i+1]=mask[4*i+2]=255;mask[4*i+3]=((i%16)/4+(i/16)/4)%2?255:0;}
    WorldClusteredLightingView lighting;lighting.environment.enabled=true;
    lighting.camera={0,0,3};lighting.forward={0,0,-1};
    lighting.environment.sunDirection={0,0,1};lighting.environment.sunRadiance={.3f,.2f,.1f};
    lighting.environment.ambientRadiance={.08f,.09f,.12f};
    if(mode=="wet" || mode=="wet-emission") {lighting.environment.surfaceWeatherEnabled=true;lighting.environment.surfaceWetness=.8f;lighting.environment.surfaceAbsorption=.8f;}
    lighting.environment.pointLights[0]={{-.5f,.7f,1.5f},5,{.3f,.6f,1.f}};
    if(mode=="wet-emission") {
        lighting.environment.sunRadiance=lighting.environment.ambientRadiance={0,0,0};
        lighting.environment.pointLights[0].intensity={0,0,0};
    }
    if(mode=="coplanar") {
        // The physical light face and its analytic source share a plane. Depth
        // reconstruction must not turn that face into its own illuminated receiver.
        auto& light=lighting.environment.rectLights[0];
        light.position={0,0,.5f};light.halfU={.5f,0,0};light.halfV={0,.1f,0};
        light.range=5;light.radiance={.1f,2.f,20.f};light.response=WorldRectLightResponse::FiniteLine;
    }
    const float zShift=mode=="coplanar"?-23.137f:0.f;
    if(mode=="coplanar") {
        lighting.environment.rectLights[1]=lighting.environment.rectLights[0];
        lighting.environment.rectLights[1].halfV[1]*=-1;
        for(auto& light:lighting.environment.rectLights)light.position[2]+=zShift;
        lighting.environment.pointLights[0].intensity={0,0,0};
    }
    lighting.camera[2]+=zShift;
    glm::mat4 vp(1);int frame=0;bool finalSubmission=false;
    int drawWidth=width,drawHeight=height;
    const auto quad=[&](float half,float z,glm::vec3 color,int alpha=0,bool unlit=false,bool depth=true) {
        std::array<IRenderBackend::WorldMeshVertex,4> v{};
        for(int i=0;i<4;++i) {
            auto& q=v[i];q.x=(i==0||i==3?-half:half)+(mode=="motion"&&depth?frame*.025f:0.f);
            q.y=i<2?-half:half;q.z=z+(depth?zShift:0);q.u=i==0||i==3?0.f:1.f;q.v=i<2?0.f:1.f;
            q.r=q.g=q.b=1;q.a=alpha==2?.4f:1;q.nz=q.tx=q.tw=1;
        }
        if(finalSubmission && reuse && mode=="authority" && depth && !unlit && alpha<2)for(auto& q:v){q.x+=.6f;q.z+=.7f;}
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey=alpha==1?"reuse-mask":"reuse-white";
        t.rgba=alpha==1?mask.data():white.data();t.width=t.height=alpha==1?16:1;
        t.materialMode=unlit?0:2;t.textureSrgb=0;t.alphaMode=static_cast<std::uint8_t>(alpha);t.alphaCutoff=.5f;t.depthTestEnabled=depth?1:0;
        t.environment=lighting.environment;t.cameraPosX=lighting.camera[0];t.cameraPosY=lighting.camera[1];t.cameraPosZ=lighting.camera[2];t.cameraForwardX=t.cameraForwardY=0;t.cameraForwardZ=-1;t.metallicFactor=0;t.roughnessFactor=.8f;
        t.vertexColorMulR=color.r;t.vertexColorMulG=color.g;t.vertexColorMulB=color.b;
        t.emissiveFactorR=color.r;t.emissiveFactorG=color.g;t.emissiveFactorB=color.b;
        if(mode=="wet-emission" && !unlit) {
            // Exactly representable emission isolates wetness ownership from BRDF quantization.
            t.vertexColorMulR=t.vertexColorMulG=t.vertexColorMulB=0;
            t.emissiveFactorR=.5f;t.emissiveFactorG=.125f;t.emissiveFactorB=.0625f;
        }
        if(mode=="mixed" && half<.7f) {
            t.environment.reflectionResponse={.2f,.3f,.1f,.8f};
            t.environment.reflectionResponseWeight=1;t.environment.reflectionAlbedoF90=true;
        }
        const auto matrix=depth?vp:glm::mat4(1);
        if(mode=="cached") {
            const std::string key="composition-"+std::to_string(half);
            IRenderBackend::WorldMeshInstance instance;
            renderer->drawWorldIndexedMeshTexturedCachedInstanced(key.c_str(),v.data(),v.size(),indices.data(),indices.size(),
                &t,&instance,1,glm::value_ptr(matrix),drawWidth,drawHeight);
            return;
        }
        renderer->drawWorldIndexedMeshTextured(v.data(),v.size(),indices.data(),indices.size(),&t,glm::value_ptr(matrix),drawWidth,drawHeight);
    };
    for(frame=0;frame<4;++frame) {
        if(mode=="extent"&&frame==1) {
            width=641;height=481;SDL_SetWindowSize(window.getSDLWindow(),width,height);SDL_PumpEvents();renderer->onResize(width,height);
        }
        vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,20.f)*
            glm::lookAt(glm::vec3(mode=="motion"?frame*.01f:0,0,3+zShift),glm::vec3(0,0,zShift),glm::vec3(0,1,0));
        finalSubmission=false;lighting.camera[0]=mode=="motion"?frame*.01f:0;
        const auto primaryVp=vp;const auto primaryLights=lighting;
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        drawWidth=width;drawHeight=height;
        if(planar) {
            auto reflected=makeWorldPlanarReflectionView(glm::value_ptr(vp),lighting.camera,-1.f,width,height);
            check(dynamic_cast<IWorldPlanarReflection&>(*renderer).beginWorldPlanarReflection(reflected),"Planar begin");
            vp=glm::make_mat4(reflected.viewProjection.data());lighting.camera=reflected.cameraPosition;
            drawWidth=reflected.width;drawHeight=reflected.heightPixels;
        }
        const auto view=packWorldContactVisibility(glm::value_ptr(vp),drawWidth,drawHeight,.8f,.015f);
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1.15f);
        check(!surface.beginWorldSurfaceComposition(view,lighting),"Prior scene composition leaked");
        check(!surface.worldSurfaceCompositionLighting(),"Prior composition query leaked");
        if(mode=="probe-switch") {
            for(unsigned face=0;face<6;++face) {
                check(surface.beginWorldProbeSurface(77,frame+1,face,glm::value_ptr(vp),64),"Probe capture");
                check(surface.endWorldProbeSurface(),"Probe publish");
            }
        }
        check(surface.beginWorldSurfaceCompositionCapture(glm::value_ptr(vp),drawWidth,drawHeight,lighting),"Surface begin");
        check(!surface.beginWorldSurfaceComposition(view,lighting),"Active capture accepted");
        // Opaque, masked and enrolled forward-only coverage participate; glass does not.
        WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
        WorldMotionSample motion{&previous,123,true};
        if(mode=="forward")check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&motion,1}),"Forward motion enrollment");
        quad(.6f,.5f,{.05f,.12f,.3f},mode=="masked"?1:0,mode=="forward");
        if(mode=="forward")surface.setWorldSurfaceMotion(worldMotionIdentity,{});
        if(mode!="occluded")quad(1.4f,0,{.4f,.07f,.02f});
        if(mode=="transparent")quad(.9f,.75f,{.1f,.4f,.1f},2);
        check(surface.endWorldSurfaceBuffer(),"Surface end");
        check(surface.resolveWorldClusteredLighting(lighting),"Clustered lighting");
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(0);
        check(!surface.beginWorldSurfaceComposition(view,lighting),"Non-HDR composition accepted");
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1.15f);
        auto wrongLight=lighting;wrongLight.camera[0]+=1;
        check(!surface.beginWorldSurfaceComposition(view,wrongLight),"Wrong lighting camera accepted");
        wrongLight=lighting;wrongLight.environment.pointLights[0].intensity[0]+=1;
        check(!surface.beginWorldSurfaceComposition(view,wrongLight),"Wrong light state accepted");
        auto mismatch=view;mismatch[0][0]+=.01f;check(!surface.beginWorldSurfaceComposition(mismatch,lighting),"Wrong camera accepted");
        mismatch=view;mismatch[8][0]+=1;check(!surface.beginWorldSurfaceComposition(mismatch,lighting),"Wrong extent accepted");
        // A known scene color must survive depth publication untouched.
        quad(1,0,{.02f,.04f,.08f},0,true,false);
        check(dynamic_cast<IWorldDepthControl&>(*renderer).clearWorldDepth(),"Clear scratch depth");
        if(mode=="reset") {
            surface.clearWorldSurfaceBuffer();check(!surface.beginWorldSurfaceComposition(view,lighting),"Cleared composition accepted");
        } else if((mode=="wet-emission" || mode=="coplanar") && !reuse) {
            // Independently evaluate weather in the ordinary forward reference.
            surface.clearWorldSurfaceBuffer();
        } else if(reuse) {
            check(surface.beginWorldSurfaceComposition(view,lighting),"Composition failed");
            check(surface.worldSurfaceCompositionLighting()!=nullptr,"Active composition query missing");
            check(!surface.beginWorldSurfaceComposition(view,lighting),"Nested composition accepted");
        }
        finalSubmission=true;
        quad(1.4f,0,{.4f,.07f,.02f});
        if(mode!="occluded")quad(.6f,.5f,{.05f,.12f,.3f},mode=="masked"?1:0,mode=="forward");
        if(mode=="transparent")quad(.9f,.75f,{.1f,.4f,.1f},2);
        finalSubmission=false;
        if(mode=="depth-reset") {
            check(dynamic_cast<IWorldDepthControl&>(*renderer).clearWorldDepth(),"Depth-layer reset");
            if(!reuse)surface.clearWorldSurfaceBuffer();
            quad(.3f,.9f,{.15f,.4f,.1f});
        }
        surface.endWorldSurfaceComposition();
        check(!surface.worldSurfaceCompositionLighting(),"Composition query survived end");
        if(mode=="late") {
            if(!reuse)surface.clearWorldSurfaceBuffer();
            quad(.3f,.9f,{.15f,.4f,.1f});
        }
        if(mode=="probe-switch") {
            check(surface.selectWorldProbeSurface(77,0),"Probe selection");
            auto probeLighting=lighting;probeLighting.environment.ambientRadiance[0]+=.1f;
            check(!surface.resolveWorldClusteredLighting(probeLighting),"Compact probe exposed transient primary planes");
            std::array<std::array<float,4>,32> gains;gains.fill({1,1,1,1});
            check(surface.relightWorldProbeSurface(probeLighting,gains),"Retained probe relighting unavailable");
            check(!surface.beginWorldSurfaceComposition(
                packWorldContactVisibility(glm::value_ptr(vp),64,64,.8f,.015f),probeLighting),
                "Probe accepted primary composition");
        }
        if(planar) {
            check(dynamic_cast<IWorldPlanarReflection&>(*renderer).endWorldPlanarReflection(),"Planar end");
            check(!surface.beginWorldSurfaceComposition(view,lighting),"Reflected surfaces leaked into primary view");
            vp=primaryVp;lighting=primaryLights;drawWidth=width;drawHeight=height;
            // The actual water receiver exposes the captured radiance after close.
            std::array<IRenderBackend::WorldMeshVertex,4> ground{};
            for(int i=0;i<4;++i) {
                auto& v=ground[i];v.x=(i==0||i==3)?-5.f:5.f;v.y=-1;
                v.z=(i<2?2.f:-6.f)+zShift;v.r=v.g=v.b=v.a=1;v.ny=v.tx=v.tw=1;
            }
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey="planar-receiver";t.rgba=white.data();t.width=t.height=1;
            t.materialMode=2;t.environment=lighting.environment;t.environment.surfaceWeatherEnabled=true;
            t.environment.surfaceWetness=1;t.environment.surfaceAbsorption=0;t.roughnessFactor=.12f;t.metallicFactor=.1f;
            t.cameraPosX=lighting.camera[0];t.cameraPosY=lighting.camera[1];t.cameraPosZ=lighting.camera[2];t.cameraForwardZ=-1;
            renderer->drawWorldIndexedMeshTextured(ground.data(),ground.size(),indices.data(),indices.size(),
                &t,glm::value_ptr(vp),width,height);
            // A primary draw with the same material must not remain suppressed.
            quad(.6f,.5f,{.05f,.12f,.3f},mode=="masked"?1:0);
        }
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    IRenderBackend::BackendFrameStats stats;check(renderer->getLastFrameStats(stats),"Missing submission statistics");
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* pixels=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(pixels&&w==width&&h==height,"Missing depth reuse image");
    Capture result{w,h,{pixels,pixels+w*h*3},stats};stbi_image_free(pixels);return result;
}
}
int main(int argc,char** argv)try {
    check(argc==4,"Expected API case directory");const std::string api=argv[1],variant=argv[2];
    const std::string mode=variant.starts_with("planar-")?variant.substr(7):variant;
    check(api=="d3d12"||api=="vulkan","Invalid API");
    check(mode=="opaque"||mode=="masked"||mode=="transparent"||mode=="forward"||mode=="motion"||mode=="extent"||mode=="authority"||mode=="late"||mode=="mixed"||mode=="depth-reset"||mode=="cached"||mode=="wet"||mode=="wet-emission"||mode=="coplanar"||mode=="probe-switch"||mode=="reset","Invalid case");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    std::filesystem::path root=argv[3];std::filesystem::create_directories(root);
    auto reference=capture(api,variant,false,root/"reference.png"),result=capture(api,variant,true,root/"composed.png");
    check(reference.width==result.width&&reference.height==result.height,"Image extent changed");
    int maxDelta=0;for(std::size_t i=0;i<reference.pixels.size();++i)maxDelta=std::max(maxDelta,std::abs(int(reference.pixels[i])-result.pixels[i]));
    check(maxDelta<=1,"Surface composition changed visible output");
    const int suppressed=mode=="reset"?0:mode=="forward"||mode=="mixed"?1:2;
    const int addedDraws=mode=="reset"?0:api=="d3d12"?2:1;
    const int addedTriangles=mode=="reset"?0:api=="d3d12"?3:2;
    check(int(result.stats.drawCalls)-int(reference.stats.drawCalls)==addedDraws-suppressed,
        "Captured geometry was still submitted after composition");
    check(std::int64_t(result.stats.triangles)-std::int64_t(reference.stats.triangles)==addedTriangles-2*suppressed,
        "Captured triangles were still replayed after composition");
    std::cout<<"draws "<<reference.stats.drawCalls<<" -> "<<result.stats.drawCalls
        <<"; triangles "<<reference.stats.triangles<<" -> "<<result.stats.triangles<<'\n';

    const auto center=(result.height/2*result.width+result.width/2)*3;
    check(result.pixels[center]+result.pixels[center+1]+result.pixels[center+2]>30,"Blank composition passed");
    std::cout<<api<<' '<<mode<<" max delta="<<maxDelta<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
