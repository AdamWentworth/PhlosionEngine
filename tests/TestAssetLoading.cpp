#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/gltf/GltfSceneRenderer.h"
#include "engine/assets/ResourceBank.h"
#include "gltf_import/GltfSceneTestSupport.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
using namespace engine::render;
using namespace engine::render::gltf::scene;
using namespace engine::assets;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
void write(const std::filesystem::path& path,const auto& bytes){
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    check(f.good(),"fixture write failed");
}
void cpu(const std::filesystem::path& document,std::vector<std::uint8_t> binary){
    std::shared_ptr<const GltfSceneAsset> legacy;std::string legacyError;
    check(loadGltfSceneAsset(document.string(),legacy,&legacyError),legacyError);
    ResourceBank bank;ResourceBankScope context(bank);
    auto first=requestGltfSceneAsset(bank,document.string());
    auto joined=requestGltfSceneAsset(bank,document.string());
    auto a=first.get<GltfSceneAsset>(),b=joined.get<GltfSceneAsset>();
    check(a==b&&bank.stats().loads==1,"Concurrent imports did not join immutable snapshot");
    check(a->sourceIdentity==legacy->sourceIdentity && a->primitives.size()==legacy->primitives.size() &&
        a->materials.size()==legacy->materials.size() && a->animations.size()==legacy->animations.size(),
        "Prepared import changed asset content identity");
    for(std::size_t i=0;i<a->primitives.size();++i) {
        const auto& x=a->primitives[i];const auto& y=legacy->primitives[i];
        check(x.indices==y.indices && x.vertices.size()==y.vertices.size() &&
            !std::memcmp(x.vertices.data(),y.vertices.data(),x.vertices.size()*sizeof(WorldMeshVertex)),
            "Prepared geometry differs from uncached import");
    }
    for(std::size_t i=0;i<a->materials.size();++i)
        check(a->materials[i].baseColor.rgba==legacy->materials[i].baseColor.rgba &&
            a->materials[i].baseColorFactor==legacy->materials[i].baseColorFactor &&
            a->materials[i].roughnessFactor==legacy->materials[i].roughnessFactor,
            "Prepared material differs from uncached import");
    first.cancel();joined.cancel();
    std::string error;std::shared_ptr<const GltfSceneAsset> c;
    check(loadGltfSceneAsset((document.parent_path()/"."/document.filename()).string(),c,&error),error);
    check(c==a,"Normalized path did not reuse snapshot");
    GltfSceneLoadOptions restricted;restricted.maxVertices=1;
    check(!loadGltfSceneAsset(document.string(),c,&error,restricted)&&c==a,"Variant limit or atomic failure lost");
    const auto path=document.parent_path()/"scene.bin";const auto time=std::filesystem::last_write_time(path);
    float x;std::memcpy(&x,binary.data(),4);x+=.25f;std::memcpy(binary.data(),&x,4);
    write(path,binary);std::filesystem::last_write_time(path,time);
    check(loadGltfSceneAsset(document.string(),c,&error),error);
    check(c!=a&&c->generation!=a->generation,"External buffer changed at same size/time was not reloaded");
    check(c->primitives.front().vertices.front().x!=a->primitives.front().vertices.front().x,
        "Changed geometry did not reach imported snapshot");
    const auto stable=c;
    write(path,std::vector<std::uint8_t>{1});
    check(!loadGltfSceneAsset(document.string(),c,&error)&&c==stable,"Corrupt replacement damaged active snapshot");
    check(bank.stats().retiredBytes>0,"Old live scene escaped accounting");
    std::cout<<"PASS CPU scene dependencies, typed variants, joined preparation and failed replacement\n";
}
void capture(const std::string& api,const std::string& path,const std::string& document,bool share) {
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path);env("PHLOSION_BACKEND_SCREENSHOT_FRAME","5");
    env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    Window window("Immutable GPU residency",320,180,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),320,180,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),320,180,false);
    ResourceBank bank;
    std::unique_ptr<ResourceBankScope> context;
    if(share)context=std::make_unique<ResourceBankScope>(bank);
    std::shared_ptr<const GltfSceneAsset> asset;std::string error;
    check(loadGltfSceneAsset(document,asset,&error),error);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*renderer);
    const auto create=[&]{
        const auto id=scopes.createWorldResourceScope(16u<<20);check(id!=0,"Scope allocation");
        if(share)check(scopes.bindWorldResourceAssetGroup(id,asset->generation,32u<<20),"Group binding");
        return id;
    };
    auto a=create(),b=create();
    GltfSceneDrawOptions options;options.surfaceWidth=320;options.surfaceHeight=180;
    const auto camera=glm::ortho(-3.f,3.f,-1.2f,1.05f,.1f,20.f)*
        glm::lookAt(glm::vec3(0,0,5),glm::vec3(0,0,0),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(camera),16,options.viewProjectionMatrix.begin());
    options.cullingViewProjection=options.viewProjectionMatrix;
    options.cameraPosition={0,0,5};options.cameraForward={0,0,-1};
    options.environment.enabled=true;options.environment.ambientRadiance={1,1,1};options.environment.sunRadiance={0,0,0};
    GltfScenePose pose;check(sampleGltfScenePose(asset,.6f,0,pose,&error),error);
    pose.global[gltf_scene_contract::AuthoredRig::kPropNode][3][0]=-2.f;
    pose.global[gltf_scene_contract::AuthoredRig::kPropNode][3][1]=-.9f;
    const auto draw=[&](WorldResourceScopeId id,float x,bool warm){
        auto o=options;o.resourceScope=id;o.instanceTransform[12]=x;o.prewarmOnly=warm;
        GltfSceneDrawStats stats;check(drawGltfSceneAsset(*renderer,*asset,pose,o,stats,&error),error);
    };
    draw(a,-.8f,true);const auto one=scopes.getWorldResourceStats().allocated;
    draw(b,.8f,true);const auto two=scopes.getWorldResourceStats().allocated;
    check(one.textures>0&&one.geometryBuffers>0,"Empty residency fixture");
    if(share)check(two.textures==one.textures&&two.geometryBuffers==one.geometryBuffers,
        "Compatible consumers uploaded immutable resources twice");
    else check(two.textures==2*one.textures&&two.geometryBuffers==2*one.geometryBuffers,
        "Isolated control did not contain two resource copies");
    const auto initialBytes=two.payloadBytes;
    std::cout<<(share?"shared":"isolated")<<" prewarm textures="<<two.textures<<" geometry="<<two.geometryBuffers<<" bytes="<<two.payloadBytes<<'\n';
    std::uint64_t stableTextures=0;
    for(unsigned frame=0;frame<6;++frame){
        if(frame==2){
            check(scopes.requestWorldResourceScopeRelease(a),"First consumer release");a=create();
        }
        if(frame==3){
            check(scopes.requestWorldResourceScopeRelease(a)&&scopes.requestWorldResourceScopeRelease(b),"Last consumer release");
            a=create();b=create();
        }
        renderer->beginFrame(0,0,0,1);
        draw(a,-.8f,false);draw(b,.8f,false);
        renderer->endFrame();
        const auto frameResources=scopes.getWorldResourceStats();
        WorldResourceScopeStats detail;scopes.getWorldResourceScopeStats(b,detail);
        if(detail.assetGroup)scopes.getWorldResourceScopeStats(detail.assetGroup,detail);
        std::cout<<"frame="<<frame<<" textures="<<frameResources.allocated.textures<<" meshes="<<frameResources.allocated.geometryBuffers
            <<" group_textures="<<detail.retained.textures<<" group_meshes="<<detail.retained.geometryBuffers<<'\n';
        // The first real draw also creates immutable lighting lookup inputs.
        // Returning a consumer must not allocate again after that warm frame.
        if(frame==1)stableTextures=frameResources.allocated.textures;
        if(share&&frame==2)check(frameResources.allocated.textures==stableTextures,
            "Surviving consumer failed to keep shared texture resident");
    }
    std::uint64_t expectedFailures=0;
    if(share) {
        const auto denied=scopes.createWorldResourceScope(16u<<20);
        check(scopes.bindWorldResourceAssetGroup(denied,asset->generation+1000000,1),"Pressure owner setup");
        bool failed=false;
        try{draw(denied,0,true);}catch(const std::exception&){failed=true;}
        WorldResourceScopeStats rejected;
        check(failed&&scopes.getWorldResourceScopeStats(denied,rejected)&&
            rejected.status==WorldResourceScopeStatus::BudgetExceeded&&!rejected.error.empty(),
            "GPU pressure was not diagnosed at admission");
        expectedFailures=scopes.getWorldResourceStats().allocationFailures;
        check(expectedFailures>0,"Pressure rejection was not counted");
        check(scopes.requestWorldResourceScopeRelease(denied),"Failed owner release");
        renderer->beginFrame(0,0,0,1);draw(a,-.8f,false);draw(b,.8f,false);renderer->endFrame();
    }
    check(scopes.requestWorldResourceScopeRelease(a)&&scopes.requestWorldResourceScopeRelease(b),"Final release");
    renderer->beginFrame(0,0,0,1);renderer->endFrame();
    const auto end=scopes.getWorldResourceStats();
    check(!end.liveScopes&&!end.retained.payloadBytes&&!end.pendingReleaseScopes&&
        end.allocated.payloadBytes==end.reclaimed.payloadBytes,"GPU resources did not retire after completion");
    check(end.allocationFailures==expectedFailures,"Unexpected GPU admission failures");
    std::cout<<(share?"shared":"isolated")<<" initial_payload_bytes="<<initialBytes
        <<" allocated_textures="<<end.allocated.textures<<" reclaimed_bytes="<<end.reclaimed.payloadBytes<<'\n';
    renderer->shutdown();
}
}
int main(int argc,char** argv)try{
    check(argc>=2,"Expected cpu or API and image path");
    gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
    gltf_import_contract::FixtureDirectory directory("asset-loading");check(directory.ready(),directory.error());
    auto source=gltf_scene_contract::makeValidSceneSource();const auto document=directory.file("scene.gltf");
    {std::ofstream f(document);f<<source.document.dump();check(f.good(),"document write");}
    write(directory.file("scene.bin"),source.binary);
    write(directory.file("base_color.png"),gltf_scene_contract::authoredBaseColorPngBytes());
    if(std::string(argv[1])=="cpu"){cpu(document,source.binary);return 0;}
    check(argc==3,"Expected API and image path");const std::string api=argv[1],path=argv[2];
    check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    const auto reference=path+".reference.png";
    capture(api,reference,document.string(),false);capture(api,path,document.string(),true);
    int w,h,c,rw,rh,rc;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(path.c_str(),&w,&h,&c,3),stbi_image_free);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> expected(stbi_load(reference.c_str(),&rw,&rh,&rc,3),stbi_image_free);
    check(image&&expected&&w==320&&h==180&&rw==w&&rh==h,"Missing native capture");
    check(std::equal(image.get(),image.get()+w*h*3,expected.get()),"Shared residency changed rendered content");
    check(std::count_if(image.get(),image.get()+w*h*3,[](unsigned char x){return x>30;})>300,"Empty native fixture");
    std::cout<<"PASS GPU shared residency, surviving consumer, fenced retirement and exact image "<<api<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
