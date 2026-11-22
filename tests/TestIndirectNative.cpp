#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/WorldIndirectLighting.h"
#include <SDL.h>
#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/render/WorldSurfaceComposition.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
std::shared_ptr<IndirectTransport> fixture() {
    auto t=std::make_shared<IndirectTransport>();t->sceneIdentity="owned-native-fixture";
    t->settings.origin={0,0,0};t->settings.spacing={2,2,2};t->settings.counts={2,2,2};t->settings.faceResolution=4;
    for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        IndirectProbe p;p.position={2*x,2*y,2*z};p.sky.fill(.5f);
        p.count.fill(1);t->probes.push_back(p);
    }
    IndirectSurfel s;s.position={0,0,0};s.albedo={1,1,1};s.emission={.25f,.5f,1};s.emissionChannel=0;
    t->surfels.push_back(s);t->bricks.push_back({{0,0,0},0,1});t->factors.push_back({0,.5f});
    t->settings.terrainResolution=4;t->settings.terrainOrigin={-32,-32};t->settings.terrainSpacing=16;
    t->terrain.resize(16);for(auto& v:t->terrain){v.albedo={.5f,.25f,.125f};v.sky=.8f;}
    buildIndirectTetrahedra(*t);validateIndirectTransport(*t);return t;
}
}
int main(int argc,char** argv)try {
    check(argc>=2&&argc<=5,"Expected d3d12 or vulkan [consumer image [game root [first case]]]");
    std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Indirect transport numerical qualification",256,256,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
    auto& gi=dynamic_cast<IWorldIndirectLighting&>(*renderer);
    WorldIndirectLightingUpdate update;update.identity=101;update.revision=1;update.transport=fixture();
    update.environment.enabled=true;update.environment.sunDirection={0,1,0};
    glm::dvec3 previous(0),distant(0);double maxError=0;
    for(unsigned frame=0;frame<8;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
        const unsigned passes=frame==3?3:1;
        for(unsigned pass=0;pass<passes;++pass) {
            update.reset=frame==5;
            if(update.reset){previous=glm::dvec3(0);distant=glm::dvec3(0);}
            if(frame==6){++update.revision;previous=glm::dvec3(0);distant=glm::dvec3(0);}
            update.environment.ambientRadiance={.2f+frame*.01f,.4f,.6f};
            update.environment.sunRadiance={float(3.141592653589793*(pass+1)),0,0};
            update.emission[0]={1,.5f,.25f,0};
            check(gi.updateWorldIndirectLighting(update),"GI update refused");
            const glm::dvec3 sun(pass+1,0,0),emission(.25,.25,.25);
            const glm::dvec3 sky(update.environment.ambientRadiance[0],.4,.6);
            previous=(sun+previous*.5+emission)*.5+sky*.5;
            distant=glm::dvec3(.5,.25,.125)*(sun+distant*.5+sky*.8);
        }
        check(gi.captureWorldIndirectLighting(),"GI output capture refused");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        auto result=gi.readWorldIndirectLighting();
        check(result.identity==update.identity&&result.revision==update.revision&&result.probes==8&&result.bricks==1,"GI diagnostic identity");
        check(result.output.size()>=48,"Incomplete GI readback");
        for(unsigned i=0;i<48;++i)for(unsigned c=0;c<3;++c) {
            const double error=std::abs(result.output[i][c]-previous[c]);
            if(!std::isfinite(error))throw std::runtime_error("Nonfinite indirect output");
            maxError=std::max(maxError,error);
            if(error>.002){std::cerr<<"frame "<<frame<<" row "<<i<<" channel "<<c<<" expected "<<previous[c]<<" actual "<<result.output[i][c]<<'\n';throw std::runtime_error("Independent relighting recurrence mismatch");}
        }
        const auto terrain=packIndirectTransport(*update.transport).terrainOutput;
        for(unsigned i=0;i<16;++i)for(unsigned c=0;c<3;++c) {
            double error=std::abs(result.output[terrain+i*3+1][c]-distant[c]);maxError=std::max(maxError,error);
            check(error<.002,"Independent distant-ground recurrence mismatch");
        }
        std::cout<<api<<" frame="<<frame<<" generation="<<result.generation<<" radiance="<<result.output[0][0]<<','<<result.output[0][1]<<','<<result.output[0][2]<<'\n';
    }
    // Isolate source families instead of accepting agreement only for their sum.
    auto isolated=fixture();for(auto& p:isolated->probes)p.sky.fill(0);
    isolated->factors[0].weight=1;isolated->surfels[0].emission={0,0,0};isolated->surfels[0].channels=1;
    update.transport=isolated;update.revision=100;update.reset=true;update.controls.multibounce=0;
    update.environment={};update.environment.enabled=true;
    const auto readCase=[&](glm::dvec3 expected,const char* label) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
        check(gi.updateWorldIndirectLighting(update),"Isolated source update");
        check(gi.captureWorldIndirectLighting(),"Isolated source capture");
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
        const auto output=gi.readWorldIndirectLighting();
        for(unsigned i=0;i<48;++i)for(unsigned c=0;c<3;++c)
            if(std::abs(output.output[i][c]-expected[c])>std::max(.002,std::abs(expected[c])*.0002))
                throw std::runtime_error(std::string(label)+" independent lighting mismatch");
        std::cout<<"PASS "<<label<<'\n';
    };
    auto& point=update.environment.pointLights[0];point.position={0,2.125f,0};point.radius=4;
    point.intensity={float(4*3.141592653589793),float(8*3.141592653589793),0};point.channels=1;
    readCase({.87890625,1.7578125,0},"point quartic range and inverse square");
    point.channels=2;readCase({0,0,0},"surfel light-channel exclusion");
    point.channels=1;point.participation=WorldLightSurfaces;readCase({0,0,0},"GI participation exclusion");
    point.participation=WorldLightAll;point.radius=1;readCase({0,0,0},"local source outside influence");
    point.radius=4;point.position={0,.13f,0};point.intensity={.05f,0,0};
    readCase({0,0,0},"negligible-source admission threshold");
    point.intensity={3.14159265359f,0,0};
    readCase({10000,0,0},"inverse square near-source clamp");
    point={};
    auto& area=update.environment.rectLights[0];area.position={0,2.125f,0};area.range=4;area.halfU={.5f,0,0};area.halfV={0,0,1};
    area.radiance={float(2*3.141592653589793),0,float(2*3.141592653589793)};
    readCase({.87890625,0,.87890625},"rectangle GI proxy and radiance-area units");
    area.affectsDiffuseProbes=false;readCase({0,0,0},"rectangle diffuse transport exclusion");
    area={};update.environment.sunRadiance={3.14159265359f,0,0};
    readCase({1,0,0},"sun only");
    update.environment.sunDirection={0,-1,0};readCase({0,0,0},"backfacing sun");
    update.environment.sunDirection={0,1,0};
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->surfels[0].permanentShadow=true;update.transport=isolated;
    readCase({0,0,0},"permanent shadow");
    update.environment.sunRadiance={0,0,0};update.emission[0]={2,.5f,0,0};
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->surfels[0].emission={.5f,.25f,1};update.transport=isolated;
    readCase({1,.125,0},"animated emission channels");

    // Independent high-energy checks make the two different clamps observable.
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->surfels[0].emission={2000,4000,8000};
    update.transport=isolated;update.emission[0]={1,1,1,0};update.controls.multibounce=.5f;
    readCase({2000,4000,8000},"high-energy emission");
    update.reset=false;readCase({2500,4500,8500},"previous bounce limited to 1000 before gain");
    update.reset=true;update.controls.multibounce=0;
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->factors[0].weight=.5f;update.transport=isolated;
    update.environment.ambientRadiance={2000,4000,8000};
    readCase({1500,2500,4500},"missing-energy ambient limited before luminance reconstruction");
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->factors[0].weight=1;update.transport=isolated;
    update.environment.ambientRadiance={0,0,0};
    auto& shadow=dynamic_cast<IWorldDirectionalShadow&>(*renderer);shadow.prewarmWorldDirectionalShadow(128);
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->surfels[0].permanentShadow=false;isolated->surfels[0].emission={0,0,0};
    update.transport=isolated;update.environment.sunRadiance={3.14159265359f,0,0};
    for(unsigned step=0;step<5;++step) {
        WorldDirectionalShadowView lightView;lightView.enabled=true;lightView.resolution=128;lightView.depthBias=0;lightView.normalOffset=0;
        const float x=step==1||step==2?20.f:0.f;
        const auto vp=glm::ortho(-2.f,2.f,-2.f,2.f,.1f,10.f)*glm::lookAt(glm::vec3(x,4,0),glm::vec3(x,0,0),glm::vec3(0,0,-1));
        std::copy_n(glm::value_ptr(vp),16,lightView.viewProjection.begin());
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
        check(shadow.beginWorldDirectionalShadow(lightView),"Indirect sun shadow producer");
        if(step==0||step==4) {
            std::array<IRenderBackend::WorldMeshVertex,4> v{};
            for(unsigned i=0;i<4;++i){v[i].x=i==0||i==3?-2.f:2.f;v[i].y=1;v[i].z=i<2?2.f:-2.f;v[i].ny=v[i].tx=v[i].tw=v[i].r=v[i].g=v[i].b=v[i].a=1;}
            const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};const unsigned char white[]{255,255,255,255};
            IRenderBackend::WorldTextureData material;material.key=material.cacheKey="gi-shadow-blocker";material.rgba=white;
            material.width=material.height=1;material.materialMode=2;
            renderer->drawWorldIndexedMeshTextured(v.data(),4,indices.data(),6,&material,lightView.viewProjection.data(),128,128);
        }
        check(shadow.endWorldDirectionalShadow(),"Indirect sun shadow finish");
        update.reset=step==0||step==2;
        check(gi.updateWorldIndirectLighting(update),"Shadowed indirect update");check(gi.captureWorldIndirectLighting(),"Shadowed indirect capture");
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
        const auto output=gi.readWorldIndirectLighting();const float expected=step==2||step==3?1.f:0.f;
        for(unsigned i=0;i<48;++i)check(std::abs(output.output[i][0]-expected)<.002f,"Sun visibility cache/reset mismatch");
        std::cout<<"PASS sun shadow cache step "<<step<<'\n';
    }
    shadow.clearWorldDirectionalShadow();
    isolated=std::make_shared<IndirectTransport>(*isolated);isolated->surfels[0].emission={.5,.25,1};update.transport=isolated;
    update.environment.sunRadiance={0,0,0};update.reset=true;
    // A rejected overflow must leave all eight already-recorded views intact.
    update.emission[0]={1,1,1,0};
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    for(unsigned i=0;i<8;++i){update.identity=1000+i;check(gi.updateWorldIndirectLighting(update),"Resident view");}
    bool rejected=false;update.identity=2000;
    try{gi.updateWorldIndirectLighting(update);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Current-frame resource overflow must not evict a recorded view");
    check(gi.captureWorldIndirectLighting(),"Overflow preserved published view");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    check(gi.readWorldIndirectLighting().identity==1007,"Overflow changed publication");
    readCase({.5,.25,1},"next-frame safe eviction and reload");
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    check(gi.updateWorldIndirectLighting(update),"Valid request before rejected reset");
    auto invalidUpdate=update;invalidUpdate.reset=true;invalidUpdate.emission[0][0]=-1;
    bool invalidEnergy=false;
    try{gi.updateWorldIndirectLighting(invalidUpdate);}catch(const std::invalid_argument&){invalidEnergy=true;}
    check(invalidEnergy&&gi.captureWorldIndirectLighting(),"Invalid reset destroyed accepted indirect output");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    const auto retained=gi.readWorldIndirectLighting();
    check(retained.identity==update.identity&&retained.generation==1,"Rejected reset changed published generation");
    std::array<WorldIndirectLightingUpdate,4> jobs;
    for(unsigned i=0;i<jobs.size();++i){jobs[i]=update;jobs[i].identity=5000+i;jobs[i].reset=false;jobs[i].quality=0;}
    for(unsigned frame=0;frame<26;++frame) {
        const unsigned selected=frame<24?frame%4:2;
        if(frame==24)for(auto& job:jobs)job.viewProjection={1,0,0,0,0,1,0,0,0,0,1,0,1000,0,0,1};
        if(frame==25)jobs[2].reset=true;
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
        check(gi.prepareWorldIndirectLighting(jobs,jobs[selected].identity),"Native scheduled indirect preparation");
        check(gi.prepareWorldIndirectLighting(jobs,jobs[selected].identity),"Second view could not bind prepared indirect lighting");
        check(gi.captureWorldIndirectLighting(),"Scheduled publication capture");
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
        const auto result=gi.readWorldIndirectLighting();
        const unsigned expected=frame<24?frame/4+1:frame==24?6:1;
        check(result.identity==jobs[selected].identity&&result.generation==expected,"Native scheduler budget/fairness/prewarm/reset");
    }
    check(!gi.prepareWorldIndirectLighting(jobs,jobs[0].identity),"Scheduled update accepted outside a frame");
    jobs[2].reset=false;
    SDL_SetWindowSize(window.getSDLWindow(),321,243);renderer->onResize(321,243);
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(321,243);
    check(gi.bindWorldIndirectLighting(jobs[2]),"World-space GI was lost on viewport resize");
    check(gi.captureWorldIndirectLighting(),"Resized resident capture");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    check(gi.readWorldIndirectLighting().generation==1,"Resize unnecessarily relit world-space data");
    SDL_SetWindowSize(window.getSDLWindow(),256,256);renderer->onResize(256,256);

    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    jobs[2].reset=false;
    check(gi.bindWorldIndirectLighting(jobs[2]),"Resident bind before rejected request");
    jobs[1].controls.multibounce=2;
    bool invalidBatch=false;
    try{gi.prepareWorldIndirectLighting(jobs,jobs[2].identity);}catch(const std::invalid_argument&){invalidBatch=true;}
    check(invalidBatch&&!gi.captureWorldIndirectLighting(),"Failed preparation left unrelated publication active");
    jobs[1].controls.multibounce=.5f;
    check(gi.bindWorldIndirectLighting(jobs[2]),"Resident bind before missing consumer");
    check(!gi.prepareWorldIndirectLighting(jobs,999999)&&!gi.captureWorldIndirectLighting(),"Missing consumer retained unrelated binding");
    check(gi.bindWorldIndirectLighting(jobs[2]),"Resident bind before invalid tier");
    jobs[2].quality=4;
    check(!gi.prepareWorldIndirectLighting(jobs,jobs[2].identity)&&!gi.captureWorldIndirectLighting(),"Invalid tier retained unrelated binding");
    jobs[2].quality=0;
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    auto temporary=fixture();std::weak_ptr<const IndirectTransport> released=temporary;
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*renderer);
    auto ownership=jobs[2];ownership.identity=1414;ownership.transport=temporary;ownership.reset=false;
    ownership.resourceScope=scopes.createWorldResourceScope(1024*1024);
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    check(gi.updateWorldIndirectLighting(ownership),"Temporary transport update");
    ownership.transport.reset();temporary.reset();
    check(!released.expired(),"Live immutable input was not retained");
    check(gi.captureWorldIndirectLighting(),"Packed transport did not survive caller release");
    check(scopes.requestWorldResourceScopeRelease(ownership.resourceScope),"Indirect owner release");
    check(released.expired(),"Scope release retained plugin-owned transport");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    check(gi.readWorldIndirectLighting().identity==1414,"Released transport readback identity");
    // Outgoing and incoming scenes can share one frame, including Low's
    // single refresh allowance. Both new consumers must initialize.
    std::array<WorldIndirectLightingUpdate,2> transition;
    for(unsigned i=0;i<transition.size();++i) {
        transition[i]=update;transition[i].identity=6000+i;
        transition[i].reset=false;transition[i].quality=0;
    }
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    for(auto& scene:transition) {
        const std::span<const WorldIndirectLightingUpdate> request(&scene,1);
        check(gi.prepareWorldIndirectLighting(request,scene.identity),"Cold transition consumer missed exhausted refresh budget");
        check(gi.prepareWorldIndirectLighting(request,scene.identity),"Repeated transition view failed");
    }
    check(gi.captureWorldIndirectLighting(),"Incoming transition capture");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    const auto incoming=gi.readWorldIndirectLighting();
    check(incoming.identity==6001&&incoming.generation==1,"Incoming scene initialized more than once");
    renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
    check(gi.bindWorldIndirectLighting(transition[0]),"Outgoing transition publication lost");
    check(gi.captureWorldIndirectLighting(),"Outgoing transition capture");
    renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    const auto outgoing=gi.readWorldIndirectLighting();
    check(outgoing.identity==6000&&outgoing.generation==1,"Outgoing scene initialized more than once");

    gi.clearWorldIndirectLighting();
    renderer->shutdown();renderer.reset();
    update=WorldIndirectLightingUpdate{};update.identity=101;update.revision=1;update.transport=fixture();
    update.environment.enabled=true;update.environment.sunDirection={0,1,0};update.emission[0]={1,.5f,.25f,0};
    if(argc>=3)for(unsigned caseIndex=argc==5?unsigned(std::stoul(argv[4])):0u;caseIndex<(argc>=4?32u:12u);++caseIndex) {
        constexpr unsigned preparedModes[]{1,2,3,4,5,6,7,8,9,10,14,15,16,3,3};
        const bool prepared=caseIndex>=17 && caseIndex!=30;
        const unsigned mode=caseIndex>=17?preparedModes[caseIndex-17]:caseIndex;
        const std::string imagePath=caseIndex?std::string(argv[2])+".case"+std::to_string(caseIndex)+".png":argv[2];
#ifdef _WIN32
        _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",imagePath.c_str());
        _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
        _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#endif
        if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
        else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
        if(argc>=4)renderer->setWorldMaterialProfile(loadWorldMaterialProfile(argv[3],"config/render/world_materials.json"));
        auto& illumination=dynamic_cast<IWorldIndirectLighting&>(*renderer);
        update.controls.multibounce=0;update.controls.normalWeight=0;update.controls.shadowDistanceWeight=0;
        update.environment.ambientRadiance={.2f,.4f,.6f};update.environment.sunRadiance={3.14159265359f,0,0};update.camera={1,1,4};
        auto receiver=fixture();std::vector<IndirectAxes> colors;
        if(mode) {
            receiver->terrain.clear();receiver->settings.terrainResolution=0;
            receiver->settings.fadeDistance=(mode>=4&&mode<9)||mode==10?4.f:.1f;
            receiver->surfels.clear();receiver->bricks.clear();receiver->factors.clear();
            for(unsigned i=0;i<receiver->probes.size();++i) {
                auto& p=receiver->probes[i];p.sky.fill(0);p.first.fill(i);p.count.fill(1);
                IndirectSurfel surfel;surfel.position=p.position;surfel.emission=mode>=12?glm::vec3(.5,.25,.125):mode>=9?glm::vec3(0):glm::vec3(.125,.25,.375)+p.position*.25f;surfel.closestProbe=i;
                receiver->surfels.push_back(surfel);receiver->bricks.push_back({p.position,i,1});receiver->factors.push_back({i,1});
                IndirectAxes axes;axes.fill(surfel.emission);colors.push_back(axes);
            }
            if(mode==2)for(auto& tet:receiver->tetrahedra)for(unsigned i=0;i<4;++i) {
                tet.visibility[i].fill(tet.probes[i]==0?32:255);tet.visibility[i][15]=tet.probes[i]==0?1:0;
            }
            update.transport=receiver;update.environment.sunRadiance={0,0,0};update.environment.ambientRadiance={.125,.125,.125};
            update.camera={100,100,100};update.controls.shadowDistanceWeight=mode==2?.1f:0.f;
            // Camera position must not override the spatial GI blend. The
            // reflection case also samples the fade instead of only its centre.
            if(mode==8||mode==10)update.camera={1,1,4};
            update.volume=mode==3||mode==11?1u:0u;
            if(mode==11) {
                receiver->settings.terrainResolution=4;
                receiver->terrain=fixture()->terrain;
                for(auto& terrainPoint:receiver->terrain)terrainPoint.albedo={.01f,.02f,.03f};
            }
        } else {
            // This case isolates near-field relighting. Boundary blending is
            // qualified separately, without the retired camera-radius override.
            auto nearField=fixture();nearField->settings.fadeDistance=.1f;
            update.transport=nearField;update.volume=0;
        }
        if(mode==14||mode==15) {
            receiver->settings.terrainResolution=4;receiver->terrain=fixture()->terrain;
            update.volume=1;update.controls.distantControls[3]=0;
            update.controls.distantRange={0,1,0,mode==15?1.f:0.f};
        }
        auto& reflections=dynamic_cast<IWorldLocalReflection&>(*renderer);
        constexpr std::array<std::uint64_t,1> probeIds{0x1122334455667788ull};
        WorldLocalReflectionProbe probe;probe.resolution=128;probe.sampling=WorldLocalReflectionSampling::PreparedRadiance;
        probe.minimum={-8,-8,-8};probe.maximum={8,8,8};probe.fade=1;
        probe.reflectionDimming=mode==9?1.f:.35f;
        probe.overrideIndirect=mode==5;probe.ambientContribution=mode!=6;probe.receiverChannels=mode==7?0:255;
        update.environmentProbes=mode>=5&&mode<11?std::array<std::uint64_t,4>{probeIds[0],0,0,0}:std::array<std::uint64_t,4>{};
        static constexpr unsigned char white[]{255,255,255,255};
        const std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
        std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,-1,-1,-1,1};
        if(mode==16)matrix[12]-=10;
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(unsigned i=0;i<4;++i) {
            auto& v=vertices[i];v.x=i==0||i==3?.1f:1.9f;v.y=i<2?.1f:1.9f;v.z=1;v.nz=v.tx=v.tw=v.a=1;
            v.r=.5f;v.g=.25f;v.b=.1f;v.weight0=1;
            if(mode==16)v.x+=10;
        }
        if(prepared) {
            // Keep the original receiver plane/pixel mapping, but exercise the
            // supported perspective composition contract instead of orthographic diagnostics.
            const glm::vec3 camera(mode==16?11.f:1.f,1,4);
            const auto vp=glm::perspective(2.f*std::atan(1.f/3.f),1.f,.1f,20.f)*
                glm::lookAt(camera,camera+glm::vec3(0,0,-1),glm::vec3(0,1,0));
            std::copy_n(glm::value_ptr(vp),16,matrix.begin());
        }
        for(unsigned f=0;f<4;++f) {
            renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(256,256);
            dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(.6f);
            if(mode>=5&&f==0)for(unsigned face=0;face<6;++face) {
                check(reflections.beginWorldPlacedProbeFace(probeIds[0],1,probe,face,{}),"Indirect environment source");
                auto quad=vertices;
                for(unsigned i=0;i<4;++i){auto& v=quad[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;v.z=0;v.r=.75f;v.g=.25f;v.b=.5f;}
                IRenderBackend::WorldTextureData source;source.materialMode=255;source.depthTestEnabled=0;
                const auto identity=glm::mat4(1);
                renderer->drawWorldIndexedMeshTextured(quad.data(),4,indices.data(),6,&source,glm::value_ptr(identity),128,128);
                check(reflections.endWorldPlacedProbeFace(),"Indirect environment publication");
            }
            check(illumination.updateWorldIndirectLighting(update),"Consumer indirect update");
            // Isolate diffuse probe composition from the separately qualified
            // specular consumer while keeping the prepared GI snapshot intact.
            if(mode>=5)check(reflections.selectWorldPlacedProbes(mode>=9&&mode<11?std::span<const std::uint64_t>(probeIds):std::span<const std::uint64_t>{}),"Consumer probe selection");
            IRenderBackend::WorldTextureData t;t.rgba=white;t.width=t.height=1;t.key=t.cacheKey="indirect-consumer-white";t.textureSrgb=0;
            t.materialMode=2;t.alphaMode=mode==11?2:0;t.metallicFactor=mode>=9&&mode<11?1.f:0.f;t.roughnessFactor=.04f;t.cameraPosX=1;t.cameraPosY=1;t.cameraPosZ=4;
            std::array<float,64> leafPalette{};leafPalette[0]=leafPalette[4]=leafPalette[51]=1;
            static constexpr unsigned char normalMap[]{128,128,255,255},leafSurface[]{0,255,0,255};
            if(mode==12||mode==13) {
                t.materialMode=mode==12?238:33;t.normalScale=0;
                t.metallicRoughnessKey=t.metallicRoughnessCacheKey="gi-specialized-orm";
                t.metallicRoughnessRgba=mode==12?leafSurface:white;t.metallicRoughnessWidth=t.metallicRoughnessHeight=1;
                if(mode==12) {
                    t.alphaMode=1;t.gpuSkinning=1;t.skinMatrixCount=4;t.skinMatrices=leafPalette.data();t.materialFlags=2;
                    t.normalKey=t.normalCacheKey="gi-leaf-normal";t.normalRgba=normalMap;t.normalWidth=t.normalHeight=1;
                    t.materialRect1V=1;t.materialRect1W=1;t.materialRect1H=4;
                }
            }
            if(mode==16)t.cameraPosX+=10;
            t.environment.enabled=true;t.environment.exposure=.6f;
            if(caseIndex>=30){t.environment.ambientRadiance={.125f,.125f,.125f};illumination.clearWorldIndirectLighting();}
            if(prepared) {
                t.cameraForwardX=t.cameraForwardY=0;t.cameraForwardZ=-1;t.directionalShadowReceiver=1;
                auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
                WorldClusteredLightingView lighting;lighting.environment=t.environment;
                lighting.camera={t.cameraPosX,t.cameraPosY,t.cameraPosZ};lighting.forward={0,0,-1};
                const auto view=packWorldContactVisibility(matrix.data(),256,256,.8f,.015f);
                check(surface.beginWorldSurfaceCompositionCapture(matrix.data(),256,256,lighting),"GI receiver capture");
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,matrix.data(),256,256);
                check(surface.endWorldSurfaceBuffer(),"GI receiver surface publication");
                check(surface.resolveWorldClusteredLighting(lighting),"GI receiver preparation");
                if(f==1 && caseIndex==17) {
                    check(illumination.bindWorldIndirectLighting(update),"GI rebind after receiver preparation");
                    check(!surface.beginWorldSurfaceComposition(view,lighting),"Stale GI receiver accepted after field rebind");
                    check(surface.resolveWorldClusteredLighting(lighting),"GI receiver reprepare");
                }
                check(surface.beginWorldSurfaceComposition(view,lighting),"GI prepared composition");
                surface.endWorldSurfaceComposition();
            } else renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,matrix.data(),256,256);
            renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
        }
        renderer->shutdown();renderer.reset();
        int w=0,h=0,c=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(imagePath.c_str(),&w,&h,&c,3),stbi_image_free);
        check(image&&w==256&&h==256,"Indirect consumer image missing");
        if(caseIndex>=30) {
            // Clearing GI restores the existing environment SH path, whose response
            // differs from prepared environment irradiance. Compare to that actual
            // forward fallback instead of incorrectly expecting the GI equation.
            double error=0;
            check(image.get()[(128*256+128)*3]>0,"Missing-field fallback is black");
            if(caseIndex==31) {
                int cw=0,ch=0,cc=0;
                const auto path=std::string(argv[2])+".case30.png";
                std::unique_ptr<unsigned char,decltype(&stbi_image_free)> control(stbi_load(path.c_str(),&cw,&ch,&cc,3),stbi_image_free);
                check(control&&cw==256&&ch==256,"Missing-field control absent");
                for(int y=20;y<236;++y)for(int x=20;x<236;++x)for(int channel=0;channel<3;++channel)
                    error=std::max(error,double(std::abs(int(image.get()[(y*256+x)*3+channel])-int(control.get()[(y*256+x)*3+channel]))));
                check(error<3,"Prepared receiver changed the existing missing-field fallback");
            }
            std::cout<<"consumer case "<<caseIndex<<" maximum display error "<<error<<'\n';
            continue;
        }
        const double a[3][3]{{.59719,.35458,.04823},{.076,.90834,.01566},{.0284,.13383,.83777}};
        const double b[3][3]{{1.60475,-.53108,-.07367},{-.10208,1.10813,-.00605},{-.00327,-.07276,1.07602}};
        glm::dmat3 matrixA,matrixB;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){matrixA[col][row]=a[row][col];matrixB[col][row]=b[row][col];}
        const auto inverseA=glm::inverse(matrixA),inverseB=glm::inverse(matrixB);
        const auto decodeDisplay=[&](const unsigned char* pixel) {
            glm::dvec3 displayed;
            for(unsigned c=0;c<3;++c){double v=pixel[c]/255.;displayed[c]=v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
            auto film=inverseB*displayed;glm::dvec3 beforeFit;
            for(unsigned c=0;c<3;++c) {
                double low=0,high=100;
                for(unsigned iteration=0;iteration<50;++iteration) {
                    const double v=(low+high)*.5,fit=(v*(v+.0245786)-.000090537)/(v*(.983729*v+.432951)+.238081);
                    if(fit<film[c])low=v;else high=v;
                }
                beforeFit[c]=(low+high)*.5;
            }
            return inverseA*beforeFit;
        };
        int rw=0,rh=0,rc=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> reference(nullptr,stbi_image_free);
        if(mode==10) {
            const auto path=std::string(argv[2])+(prepared?".case25.png":".case9.png");
            reference.reset(stbi_load(path.c_str(),&rw,&rh,&rc,3));
            check(reference&&rw==256&&rh==256,"Reflection dimming control capture missing");
        }
        double error=0;
        for(int y:{32,96,160,224})for(int x:{32,96,160,224}) {
            glm::dvec3 incident(.725,.325,.425);
            if(mode) {
                const glm::vec3 p((x+.5f)/128,2-(y+.5f)/128,1);
                const auto nearby=evaluateIndirectTransport(*receiver,colors,p,{0,0,1},update.controls);
                const float alpha=mode==3?0.f:std::clamp(std::min({p.x,2-p.x,p.y,2-p.y,1.f})/receiver->settings.fadeDistance,0.f,1.f);
                incident=glm::mix(glm::dvec3(.125),glm::dvec3(nearby.radiance),double(alpha));
                if(mode==5)incident={.75,.25,.5};
                if(mode==11||mode==16)incident=glm::dvec3(.125);
                if(mode==14)incident=glm::dvec3(.075,.0625,.05625);
                if(mode==15)incident=glm::dvec3(.1);
                if(mode==8)incident=glm::dvec3(.75,.25,.5)*double(1-alpha)+incident*double(alpha);
            }
            if(mode==9){check(image.get()[(y*256+x)*3]>20,"Reflection dimming control has no reflected light");continue;}
            const double boundaryWeight=std::clamp(std::min({(x+.5)/128.,2-(x+.5)/128.,(y+.5)/128.,2-(y+.5)/128.,1.})/4.,0.,1.);
            const glm::dvec3 radiance=mode==10?decodeDisplay(reference.get()+(y*256+x)*3)*(1.-.65*boundaryWeight):incident*glm::dvec3(.5,.25,.1)*(mode==12?1.0:.96);
            double film[3];
            for(unsigned i=0;i<3;++i){double v=0;for(unsigned j=0;j<3;++j)v+=a[i][j]*radiance[j];film[i]=(v*(v+.0245786)-.000090537)/(v*(.983729*v+.432951)+.238081);}
            for(unsigned i=0;i<3;++i) {
                double v=0;for(unsigned j=0;j<3;++j)v+=b[i][j]*film[j];v=std::clamp(v,0.,1.);
                const auto expected=255*(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055);
                const auto actual=image.get()[(y*256+x)*3+i];const double pixelError=std::abs(actual-expected);
                error=std::max(error,pixelError);
                if(pixelError>3)std::cerr<<"case "<<mode<<" pixel "<<x<<','<<y<<" channel "<<i<<" expected "<<expected<<" actual "<<int(actual)<<'\n';
            }
        }
        std::cout<<"consumer case "<<caseIndex<<" maximum display error "<<error<<'\n';
        check(error<3,"Standard surface indirect interpolation/environment composition differs from independent expectation");
    }
    std::cout<<"PASS native sky/sun/emission/bounce, same-frame snapshots, reset and revision; max error="<<maxError<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
