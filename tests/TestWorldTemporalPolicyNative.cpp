#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
constexpr int W=128,H=96;
struct Settings {
    float distance=2,rangeNear=0;
    WorldTemporalMaterial category=WorldTemporalMaterial::Geometry;
    bool sky=false,motionValid=true,reset=false;
    float yaw=0,translation=0,jitter=0,transparent=0;
    int pattern=0,boundary=0;
    float foregroundMotion=0;
};
}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==2,"Expected native API");
    const std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Temporal policy",W,H,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),W,H,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),W,H,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    const auto projection=glm::perspective(glm::radians(60.f),float(W)/H,.1f,1000.f);
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<unsigned,6> corners{0,2,1,0,3,2};
    const auto frame=[&](std::uint64_t id,std::uint64_t number,Settings settings) {
        WorldTemporalView view{id,1,number,W,H,settings.reset,1.f/60,1};
        view.cameraValid=true;view.cameraPosition={settings.translation,0,0};
        auto camera=projection*glm::rotate(glm::mat4(1),settings.yaw,glm::vec3(0,1,0))*
            glm::translate(glm::mat4(1),glm::vec3(-settings.translation,0,0));
        camera[2][0]+=settings.jitter;
        std::copy_n(glm::value_ptr(camera),16,view.viewProjection.begin());
        std::vector<IRenderBackend::WorldMeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        // Deliberately controlled clip geometry: projection constants independently
        // interpret its stored depth as the stated physical distance.
        for(int x=0;x<W;++x) {
            const float distance=settings.boundary>0?(x<settings.boundary?2.f:10.f):
                settings.rangeNear>0 && (x/2)%2?settings.rangeNear:settings.distance;
            auto clip=projection*glm::vec4(0,0,-distance,1);
            const auto base=std::uint32_t(vertices.size());
            for(int corner=0;corner<4;++corner) {
                IRenderBackend::WorldMeshVertex v{};
                v.x=2.f*(x+(corner==1||corner==2?1:0))/W-1;v.y=corner<2?-1.f:1.f;
                v.z=clip.z/clip.w;v.nz=1;v.tx=v.tw=1;v.a=1;
                v.r=v.g=v.b=(x+settings.pattern)%2?0.f:1.f;
                vertices.push_back(v);
            }
            for(auto index:corners)indices.push_back(base+index);
        }
        IRenderBackend::WorldTextureData material;
        material.key=material.cacheKey="temporal-policy";material.rgba=white.data();
        material.width=material.height=1;material.textureSrgb=0;material.materialMode=2;
        WorldMotionPose pose;pose.temporalMaterial=settings.category;
        WorldMotionSample sample{&pose,0x11223344u,settings.motionValid};
        renderer->beginFrame(0,0,0,0);renderer->beginWorldSceneColorPass(W,H);
        check(surface.beginWorldSurfaceBuffer(worldMotionIdentity.data(),W,H),"surface begin");
        if(!settings.sky) {
            if(settings.boundary>0)for(int x=0;x<W;++x) {
                pose.temporalMaterial=x<settings.boundary?WorldTemporalMaterial::Character:WorldTemporalMaterial::Geometry;
                pose.model[12]=x<settings.boundary?settings.foregroundMotion:0;
                check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&sample,1}),"silhouette motion bind");
                renderer->drawWorldIndexedMeshTextured(vertices.data()+x*4,4,corners.data(),6,
                    &material,worldMotionIdentity.data(),W,H);
            } else {
                check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&sample,1}),"motion bind");
                renderer->drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
                    &material,worldMotionIdentity.data(),W,H);
            }
        }
        check(surface.endWorldSurfaceBuffer(),"surface end");
        check(surface.beginWorldTemporalScene(view),"temporal begin");
        // A procedural sky uses the same composed color target but writes no
        // surface/depth record. This tests real sky-history initialization.
        material.materialMode=255;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
            &material,worldMotionIdentity.data(),W,H);
        if(settings.transparent>0) {
            // An alpha-masked effect card has empty texels in its right half.
            // Its rectangle must not invalidate the uncovered background.
            constexpr std::array<unsigned char,32> coverage{
                0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
            auto effect=material;effect.key=effect.cacheKey="temporal-reactive-card";
            effect.rgba=coverage.data();effect.width=8;effect.height=1;effect.alphaMode=2;
            effect.wrapS=effect.wrapT=33071;
            std::array<IRenderBackend::WorldMeshVertex,4> card{};
            for(int i=0;i<4;++i) {
                auto& v=card[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;
                v.z=-.1f;v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;
                v.r=v.g=v.b=1;v.a=settings.transparent;v.nz=v.tx=v.tw=1;
            }
            renderer->drawWorldIndexedMeshTextured(card.data(),4,corners.data(),6,
                &effect,worldMotionIdentity.data(),W,H);
        }
        check(surface.resolveWorldTemporalScene()&&surface.captureWorldTemporalScene(),"temporal resolve/readback");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        auto result=surface.readWorldTemporalScene();
        check(result.color.size()==W*H && result.mask.size()==W*H/4,"readback dimensions");
        for(const auto& pixel:result.color)for(float v:pixel)check(std::isfinite(v),"nonfinite reconstruction");
        return result;
    };
    const auto mask=[](const WorldTemporalImage& i){return i.mask[(H/4)*(W/2)+W/4];};
    const auto color=[](const WorldTemporalImage& i){return i.color[(H/2)*W+W/2][0];};
    std::uint64_t id=1;
    Settings s;
    frame(id,1,s);s.pattern=1;
    auto ordinary=frame(id,2,s);
    const float minimum=1.f-std::pow(.2f,1.f/60);
    const float expected=(1-minimum)/(1+minimum);
    std::cout<<"ordinary "<<color(ordinary)<<" expected "<<expected<<" mask "<<mask(ordinary)[2]<<'\n';
    check(mask(ordinary)[2]<.001f && std::abs(color(ordinary)-expected)<.004f,"ordinary reconstruction arithmetic");
    surface.releaseWorldTemporalView(id++);
    s={};s.category=WorldTemporalMaterial::ReducedHistory;frame(id,1,s);s.pattern=1;
    auto reduced=frame(id,2,s);
    const float reducedExpected=(1-minimum-.5f)/(1+minimum+.5f);
    check(std::abs(color(reduced)-reducedExpected)<.004f,"reduced-history class not consumed");
    surface.releaseWorldTemporalView(id++);
    s={};frame(id,1,s);s.distance=5;auto depth=frame(id,2,s);
    std::cout<<"near depth mask "<<mask(depth)[2]<<'\n';
    check(std::abs(mask(depth)[2]-.6f)<.004f,"near depth rejection is not the traced ratio");
    surface.releaseWorldTemporalView(id++);
    s={};s.category=WorldTemporalMaterial::Character;frame(id,1,s);
    s.category=WorldTemporalMaterial::Geometry;s.distance=5;
    auto actor=frame(id,2,s);check(mask(actor)[2]>.999f,"vacated character did not strengthen rejection");
    surface.releaseWorldTemporalView(id++);
    s={};frame(id,1,s);s.distance=4;s.rangeNear=1;
    auto sloped=frame(id,2,s);check(mask(sloped)[2]<.002f,"local depth range rejected an overlapping surface");
    surface.releaseWorldTemporalView(id++);
    s={};s.distance=100;frame(id,1,s);s.distance=300;
    auto distant=frame(id,2,s);check(mask(distant)[2]<.002f,"near-depth heuristic affected distant surface");
    surface.releaseWorldTemporalView(id++);
    s={};s.sky=true;frame(id,1,s);s.pattern=1;s.translation=12;
    auto sky=frame(id,2,s);
    std::cout<<"sky "<<color(sky)<<" mask "<<mask(sky)[0]<<','<<mask(sky)[1]<<','<<mask(sky)[2]<<'\n';
    check(std::abs(mask(sky)[0])<.0001f && std::abs(mask(sky)[1])<.0001f &&
        mask(sky)[2]<.001f && std::abs(color(sky)-expected)<.004f,"sky translation or accumulation");
    s.yaw=.05f;s.pattern=0;auto rotated=frame(id,3,s);
    check(std::abs(mask(rotated)[0])>.02f && mask(rotated)[2]<.001f,"sky camera rotation missing");
    s.reset=true;s.pattern=1;
    auto cut=frame(id,4,s);check(mask(cut)[2]>.999f && color(cut)<.001f,"camera cut retained sky history");
    surface.releaseWorldTemporalView(id++);
    s={};s.sky=true;frame(id,1,s);s.sky=false;s.motionValid=false;
    auto appearing=frame(id,2,s);check(mask(appearing)[2]>.999f,"geometry appearance borrowed sky history");
    surface.releaseWorldTemporalView(id++);
    s={};s.boundary=W/2;frame(id,1,s);
    s.boundary=W/4;s.foregroundMotion=.5f;auto silhouette=frame(id,2,s);
    const auto at=[&](int x){return silhouette.mask[(H/4)*(W/2)+x/2];};
    check(at(44)[2]>.999f && at(96)[2]<.001f && at(16)[2]<.001f,
        "moving silhouette did not isolate disocclusion");
    check(std::abs(at(30)[0]-.25f)<.0003f && std::abs(at(96)[0])<.0003f,
        "nearest-depth motion did not follow the moving foreground");
    surface.releaseWorldTemporalView(id++);
    // Projection jitter is already in both view matrices: initialize sky motion
    // with exactly the difference, never subtract or add it a second time.
    s={};s.sky=true;frame(id,1,s);s.jitter=.004f;
    auto jittered=frame(id,2,s);
    check(std::abs(mask(jittered)[0]-.002f)<.00003f && mask(jittered)[2]<.001f,"sky jitter convention");
    surface.releaseWorldTemporalView(id++);
    s={};s.transparent=.1f;auto card=frame(id,1,s);
    const auto inside=(H/2)*W+W/4,outside=(H/2)*W+W*3/4;
    std::cout<<"card alpha "<<card.color[inside][3]<<","<<card.color[outside][3]<<" color "<<card.color[inside][0]<<'\n';
    constexpr float coverageAlpha=.1f;
    check(std::abs(card.color[inside][3]-2*coverageAlpha)<.002f && card.color[outside][3]<.001f,
        "effect coverage rejected the whole card");
    s.transparent=0;s.pattern=1;auto vanished=frame(id,2,s);
    // First pixel was .9; stored reactive coverage .2 blends its compressed
    // history with zero. Applying the coverage scale twice gives a wrong result.
    const float previous=1-coverageAlpha;
    const float compressed=previous/(1+previous)*(1-2*coverageAlpha),reactiveExpected=compressed/(1-compressed);
    std::cout<<"removed card "<<vanished.color[inside][0]<<" expected "<<reactiveExpected<<'\n';
    check(std::abs(vanished.color[inside][0]-reactiveExpected)<.006f &&
        std::abs(vanished.color[outside][0]-expected)<.004f && vanished.color[inside][3]<.001f,
        "departed effect coverage was lost, doubled or persisted");
    surface.releaseWorldTemporalView(id++);
    renderer->shutdown();renderer.reset();std::cout<<api<<" temporal material/depth/sky policy PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
