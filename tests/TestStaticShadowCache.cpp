#include "engine/core/Environment.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldLocalShadowCache.h"
#include "engine/render/WorldShadowSchedule.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace engine::render;
namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void contracts() {
    WorldRectLight light{{0,4,0},20,{2,0,0},{0,0,.2f},{4,0,0}};
    light.shadowFilter=WorldLocalShadowFilter::DistanceMoments;
    auto view=rectLightShadowView(light); WorldShadowCache c;
    check(c.prepare(0,3,view,{1,1}),"New cache did not draw"); c.commit(); c.activate(0,3,view);
    check(c.reading,"Completed cache not readable"); c.resetPass();
    check(c.matches(0,3,view,{1,1}) && c.matches(0,3,view,{1,1}),"Read-only cache query changed admission");
    check(!c.prepare(0,3,view,{1,1}),"Unchanged cache redrawn"); c.activate(0,4,view);
    check(!c.reading,"Wrong light reused prepared cache");
    check(c.prepare(0,3,view,{1,2}),"Caster revision ignored"); c.commit();
    check(c.prepare(0,3,view,{2,2}),"Scene identity ignored"); c.commit();
    light.range=21;view=rectLightShadowView(light);
    check(c.prepare(0,3,view,{2,2}),"Range change ignored");c.commit();
    light.position[0]=1;view=rectLightShadowView(light);
    check(c.prepare(0,3,view,{2,2}),"Light transform ignored");c.commit();
    check(c.prepare(0,4,view,{2,2}),"Source compaction reused another light");c.commit();
    check(c.prepare(1,4,view,{2,2}),"Cache leaked between atlas slots");c.commit();
    view.resolution=512;check(c.prepare(1,4,view,{2,2}),"Resolution change reused cache");c.commit();
    c.invalidate();check(c.prepare(1,4,view,{2,2}),"Explicit invalidation ignored");
    c.resetPass();c.activate(1,4,view);check(!c.reading,"Interrupted writer published cache");
}
}
int main(int argc,char** argv) try {
    contracts();
    if(argc==1) { std::cout<<"PASS cache identity, revisions, projection, slots and abort\n";return 0; }
    check(argc==5||argc==6,"Expected API case cached|fresh|refresh|scheduled output [reference]");
    const std::string api=argv[1],scenario=argv[2],mode=argv[3];
    check(api=="d3d12"||api=="vulkan","Unknown API");check(mode=="cached"||mode=="fresh"||mode=="refresh"||mode=="scheduled","Unknown cache mode");
    check(scenario=="steady"||scenario=="move"||scenario=="remove"||scenario=="dynamic"||scenario=="scene"||
          scenario=="projection"||scenario=="source-off"||scenario=="replacement"||scenario=="masked"||scenario=="reset"||scenario=="reorder"||scenario=="continuous"||scenario=="multiview","Unknown case");
    constexpr int width=320,height=200;
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}} quit;
    std::filesystem::create_directories(std::filesystem::path(argv[4]).parent_path());
    std::filesystem::remove(argv[4]);
#ifdef _WIN32
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[4]);_putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","22");_putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[4],1);setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","22",1);setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Static shadow cache",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> r;
    if(api=="vulkan") r=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else r=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto* shadows=dynamic_cast<IWorldLocalShadow*>(r.get());auto* cache=dynamic_cast<IWorldLocalShadowCache*>(r.get());
    check(shadows&&cache,"Missing cache interface");shadows->prewarmWorldLocalShadows();
    auto* hdr=dynamic_cast<IWorldSceneRadiance*>(r.get());check(hdr,"Missing HDR interface");
    const auto vp=glm::ortho(-5.f,5.f,-3.125f,3.125f,.1f,20.f)*glm::lookAt(glm::vec3(0,6,0),glm::vec3(0),glm::vec3(0,0,-1));
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    WorldEnvironmentLighting e;e.enabled=true;e.exposure=1;
    e.localShadowMapResolution=unsigned(std::stoul(engine::env::get("PHLOSION_TEST_LOCAL_SHADOW_RESOLUTION").value_or("1024")));
    int staticPasses=0,dynamicPasses=0;WorldShadowSchedule schedule;unsigned maxUpdates=0;
    for(int frame=0;frame<24;++frame) {
        const bool changed=frame>=8;
        // Sparse indices exercise all eight tiles, including compaction when a
        // source turns off. One red and one blue blocker occupy different areas.
        for(int i=0;i<8;++i) {
            auto& light=e.rectLights[i*2];light={{0,4,0},20,{2,0,0},{0,0,.2f},i%2?std::array<float,3>{0,0,.65f}:std::array<float,3>{.65f,0,0}};
            light.response=WorldRectLightResponse::FiniteLine;light.shadowFilter=WorldLocalShadowFilter::DistanceMoments;
            if(scenario=="reorder")light.identity=100+i;
            if(scenario=="projection"&&changed) {light.position[0]=1;light.range=16;}
        }
        if(scenario=="reorder"&&changed)std::swap(e.rectLights[0],e.rectLights[2]);
        if(scenario=="source-off"&&changed) e.rectLights[0].radiance={};
        if(scenario=="replacement") {
            e.rectLights[1]={};
            if(!changed){e.rectLights[1]=e.rectLights[0];e.rectLights[0].radiance={};}
        }
        r->beginFrame(0,0,0,1);r->beginWorldSceneColorPass(width,height);hdr->setWorldSceneRadianceExposure(1);
        if(frame==8 && scenario=="reset")cache->invalidateWorldStaticShadows();
        const auto draw=[&](float x,float y,float z,float half,const float* matrix,bool masked) {
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            for(int i=0;i<4;++i) {auto& v=vertices[i];v.x=x+((i==0||i==3)?-half:half);v.y=y;v.z=z+(i<2?half:-half);
                v.u=(i==0||i==3)?0.f:1.f;v.v=i<2?0.f:1.f;v.ny=1;v.tx=1;v.r=v.g=v.b=v.a=1;}
            std::array<unsigned char,8*8*4> mask{};
            for(int i=0;i<64;++i){mask[i*4]=mask[i*4+1]=mask[i*4+2]=255;mask[i*4+3]=((i%8/2+i/16+int(changed))%2)?0:255;}
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey=masked?(changed?"mask-b":"mask-a"):"cache-white";
            t.rgba=masked?mask.data():white.data();t.width=t.height=masked?8:1;t.alphaMode=masked?1:0;t.alphaCutoff=.5f;
            t.textureSrgb=0;t.materialMode=2;t.normalScale=0;t.metallicFactor=0;t.roughnessFactor=1;t.environment=e;
            t.cameraPosY=6;t.cameraForwardY=-1;t.cameraForwardZ=0;
            r->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,matrix,width,height);
        };
        for(int receiverView=0;receiverView<(scenario=="multiview"?2:1);++receiverView) {
        if(receiverView==0)shadows->clearWorldLocalShadows();int slot=0;
        WorldShadowCacheKey key{scenario=="scene"&&changed?2u:1u,
            changed&&(scenario=="move"||scenario=="remove"||scenario=="masked")?2u:1u};
        if(mode=="fresh")key={};
        const auto planned=planWorldRectShadowsCachedByLight(*shadows,e,[&](auto,const auto&){return key;},mode=="refresh");
        const auto repeated=planWorldRectShadowsCachedByLight(*shadows,e,[&](auto,const auto&){return key;},mode=="refresh");
        check(planned.entries.size()==repeated.entries.size(),"Read-only shadow plan changed the source set");
        for(std::size_t i=0;i<planned.entries.size();++i)
            check(planned.entries[i].admission==repeated.entries[i].admission,"Read-only shadow plan mutated cache");
        const auto renderCasters=[&](std::size_t n,const auto& view,auto casters) {
            slot=scenario=="reorder"?int(e.rectLights[planned.entries[n].source].identity-100):int(n);
            if(casters!=WorldShadowCasters::Dynamic) {
                ++staticPasses;
                if(!(changed&&(scenario=="remove"||scenario=="scene")))
                    draw(changed&&scenario=="move"?1.f:-1.f,2,0,.7f,view.viewProjection.data(),scenario=="masked");
            }
            if(casters!=WorldShadowCasters::Static) {
                ++dynamicPasses;++slot;
                // Moves without invalidating the static cache; half the tiles
                // have a second blocker that can win or lose the depth merge.
                if(slot%2) draw(changed?1.8f:-1.8f,1,1,.5f,view.viewProjection.data(),false);
                if(scenario=="dynamic" && !changed)draw(0,3,0,1,view.viewProjection.data(),false);
            }
        };
        if(mode=="scheduled") {
            auto p=schedule.prepare(*shadows,planned,key.scene,2,[&](auto,const auto&){return scenario=="continuous"?std::uint64_t(frame+1):(changed?2ull:1ull);});
            maxUpdates=std::max(maxUpdates,schedule.stats().refreshed);
            if(scenario=="continuous" && frame>=8)check(schedule.stats().unready==0,"Continuously dirty shadow bank starved");
            p.execute(*shadows,renderCasters);
            if(frame>=20)check(schedule.stats().refreshed==(scenario=="continuous"?2u:0u),"Shadow refresh count mismatch");
        } else planned.execute(*shadows,renderCasters);
        draw(0,0,0,20,glm::value_ptr(vp),false);
        }
        r->endWorldSceneColorPass();r->endFrame();
    }
    r->shutdown();r.reset();
    check(std::filesystem::file_size(argv[4])>0,"Missing captured cache test");
    if(mode=="scheduled")check(maxUpdates==2&&dynamicPasses>=15&&dynamicPasses<=(scenario=="continuous"?48:40),"Shadow update budget/reuse failed");
    else check(dynamicPasses>=120,"Fresh caster pass skipped");
    if(mode=="cached"||mode=="scheduled")check(staticPasses>0&&staticPasses<=32,"Cache did not reuse warm entries");
    else check(staticPasses==dynamicPasses,"Uncached control dropped static casters");
    if(mode=="cached"&&(scenario=="source-off"||scenario=="replacement")) {
        const int banks=api=="vulkan"?2:1;
        check(staticPasses==banks*(scenario=="replacement"?9:8),"Unchanged lights redrawn after source set changed");
    }
    if(argc==6) {
        int w,h,c,rw,rh,rc;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[4],&w,&h,&c,3),stbi_image_free);
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> reference(stbi_load(argv[5],&rw,&rh,&rc,3),stbi_image_free);
        check(image&&reference&&w==rw&&h==rh,"Missing shadow reference");
        for(int i=0;i<w*h*3;++i)check(image.get()[i]==reference.get()[i],"Scheduled shadow differs from fresh control");
    }
    std::cout<<"PASS "<<api<<' '<<scenario<<' '<<mode<<" static="<<staticPasses<<" fresh="<<dynamicPasses<<'\n';
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
