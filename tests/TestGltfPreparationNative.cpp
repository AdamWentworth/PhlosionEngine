
#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/gltf/GltfSceneRenderer.h"
#include "engine/render/gltf/GltfSceneDrawPlan.h"
#include "gltf_import/GltfSceneTestSupport.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iostream>
#include <memory>
using namespace engine::render;
using namespace engine::render::gltf::scene;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
void capture(const std::string& api,const std::string& path,const std::string& document,bool retained,bool collected=false) {
    env("PHLOSION_BACKEND_SCREENSHOT_PATH",path);env("PHLOSION_BACKEND_SCREENSHOT_FRAME","6");
    env("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    Window window("Retained glTF preparation",320,180,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),320,180,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),320,180,false);
    std::shared_ptr<const GltfSceneAsset> asset;std::string error;
    check(loadGltfSceneAsset(document,asset,&error),error);
    GltfSceneDrawOptions options;options.surfaceWidth=320;options.surfaceHeight=180;
    const auto camera=glm::ortho(-2.f,2.f,-1.2f,1.05f,.1f,20.f)*
        glm::lookAt(glm::vec3(0,0,5),glm::vec3(0,0,0),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(camera),16,options.viewProjectionMatrix.begin());
    options.cullingViewProjection=options.viewProjectionMatrix;
    options.cameraPosition={0,0,5};options.cameraForward={0,0,-1};
    options.environment.enabled=true;options.environment.ambientRadiance={1,1,1};
    options.environment.sunRadiance={0,0,0};
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*renderer);
    auto& resources=dynamic_cast<IWorldPreparedResources&>(*renderer);
    options.resourceScope=scopes.createWorldResourceScope(8u<<20);
    WorldPreparedResourceToken savedGeometry,savedMaterial;
    for(unsigned frame=0;frame<7;++frame) {
        if(frame==2) {
            const auto temporary=scopes.createWorldResourceScope(1u<<20);
            check(scopes.requestWorldResourceScopeRelease(temporary),"Temporary scope release failed");
        }
        if(frame==3) {
            check(scopes.beginWorldResourceScope(options.resourceScope),"Scope admission failed");
            check(scopes.requestWorldResourceScopeRelease(options.resourceScope),"Live release failed");
            if(retained)check(!resources.worldPreparedGeometryValid(savedGeometry) &&
                !resources.worldPreparedMaterialValid(savedMaterial) &&
                !resources.drawWorldPreparedIndexed(savedGeometry,savedMaterial,{},options.viewProjectionMatrix.data(),320,180),
                "Release-pending resource handle remained usable");
            check(scopes.endWorldResourceScope(),"Scope end failed");
            options.resourceScope=scopes.createWorldResourceScope(8u<<20);
        }
        if(frame==4)check(loadGltfSceneAsset(document,asset,&error),error);
        GltfScenePose pose;check(sampleGltfScenePose(asset,float(frame==1?0:frame)/6.f,0,pose,&error),error);
        // Bring the authored distant static prop beside each skinned actor so
        // both kinds of geometry are verified in the final image.
        pose.global[gltf_scene_contract::AuthoredRig::kPropNode][3][0]=-2.f;
        pose.global[gltf_scene_contract::AuthoredRig::kPropNode][3][1]=-.9f;
        auto fresh=*asset;
        std::vector<std::array<float,3>> emission(asset->materials.size(),{.1f,float(frame)*.02f,.1f});
        options.emissiveFactors=emission;
        int excluded=asset->primitives[1].nodeIndex;
        options.excludedNodes=frame==2?std::span<const int>(&excluded,1):std::span<const int>{};
        options.environment.pointLights[0].position={0,1,3};
        options.environment.pointLights[0].radius=10;
        options.environment.pointLights[0].intensity={float(frame)*.1f,.4f,.8f};
        if(frame==0) {
            auto warm=options;warm.prewarmOnly=true;GltfSceneDrawStats warmed;
            check(drawGltfSceneAsset(*renderer,retained?*asset:fresh,pose,warm,warmed,&error),error);
        }
        renderer->beginFrame(0,0,0,1);
        if(retained && frame==2) {
            check(scopes.beginWorldResourceScope(options.resourceScope),"Scope admission after collection failed");
            check(!resources.drawWorldPreparedIndexed(savedGeometry,savedMaterial,{},options.viewProjectionMatrix.data(),320,180),
                "Collected generation accepted stale resources");
            check(scopes.endWorldResourceScope(),"Scope end failed");
        }
        GltfSceneViewFamily family;GltfSceneCollectedView view;
        if(collected) {
            std::vector<GltfSceneViewRequest> requests;
            for(unsigned instance=0;instance<2;++instance) {
                auto draw=options;draw.instanceTransform[12]=instance?.85f:-.85f;
                draw.cullSkinnedShadow=true;draw.cullingViewProjection=draw.viewProjectionMatrix;
                draw.tint=instance?std::array<float,4>{.3f,.7f,1,1}:std::array<float,4>{1,.3f,.5f,1};
                std::size_t id;
                check(family.collect(asset,pose,draw.instanceTransform,id,&error),error);
                requests.push_back({id,draw});
            }
            check(family.prepareView(requests,view,&error),error);
            WorldDeformationBatch batch(*renderer);
            family.prepareDeformations(*renderer);
            check(dynamic_cast<IWorldDeformationPreparation&>(*renderer).worldDeformationPreparationStats().preparedPalettes==0,
                  "collected glTF uploaded before frame commit");
            batch.commit();
            check(family.stats().objects==2 && family.stats().requests==2,"Collection count mismatch");
        }
        const auto preparedMaterialCount=gltfSceneMaterialPreparationStats(*asset).viewMaterialPreparations;
        for(unsigned repeat=0;repeat<8;++repeat)for(unsigned instance=0;instance<2;++instance) {
            options.instanceTransform[12]=instance?.85f:-.85f;
            options.tint=instance?std::array<float,4>{.3f,.7f,1,1}:std::array<float,4>{1,.3f,.5f,1};
            GltfSceneDrawStats stats;
            check(collected?view.draw(*renderer,instance,stats,&error):drawGltfSceneAsset(*renderer,retained?*asset:fresh,pose,options,stats,&error),error);
            if(frame==6)check(stats.drawCalls==2 && stats.skinnedDraws==1 && stats.culledDraws==0,
                "Final native fixture must draw both static and skinned geometry");
        }
        if(collected)check(gltfSceneMaterialPreparationStats(*asset).viewMaterialPreparations==preparedMaterialCount,"Collected draw rebuilt material state");
        if(retained && frame==0) {
            auto plan=detail::GltfSceneDrawPlanAccess::get(*asset);
            auto native=plan->nativeResources(&resources,options.resourceScope);
            savedGeometry=native->geometry[0];savedMaterial=native->materials[asset->primitives[0].materialIndex];
            check(savedGeometry && savedMaterial,"Normal glTF draw did not retain native bindings");
        }
        const auto upload=dynamic_cast<IWorldDeformationPreparation&>(*renderer).worldDeformationPreparationStats();
        check(upload.immediatePalettes==0,"glTF draw performed a lazy palette upload");
        if(retained && frame==1)check(upload.preparedPalettes==0,"Unchanged glTF pose uploaded again");
        else check(upload.preparedPalettes>0,"Changed glTF pose was not prepared");
        renderer->endFrame();
    }
    if(retained) {
        const auto native=resources.worldPreparedResourceStats();
        check(native.draws==208 && native.geometryPreparations>=6 && native.geometryPreparations<=8 &&
            native.materialPreparations>=6 && native.materialPreparations<=8 && native.staleDraws>=2,
            "Native resources did not survive views or recover from retirement");
        native.print(std::cout,api.c_str(),7);
        if(!collected) {
        const auto s=gltfScenePreparationStats(*asset);
        const auto m=gltfSceneMaterialPreparationStats(*asset);
        check(m.builds==6 && m.reuses==42 && m.materialEvaluations==6*asset->materials.size() &&
            m.records<=4 && m.retainedBytes<=(1u<<20),"Native material preparation did not reuse owned inputs");
        std::cout<<"material-builds="<<m.builds<<" material-reuses="<<m.reuses
            <<" material-evaluations="<<m.materialEvaluations<<" view-materials="<<m.viewMaterialPreparations<<'\n';

        check(s.instanceReuses>=42 && s.visibilityReuses>=42 && s.instanceRecords<=4,
            "Native renderer did not consume retained preparation");
        std::cout<<"instance-builds="<<s.instanceBuilds<<" instance-reuses="<<s.instanceReuses
            <<" visibility-builds="<<s.visibilityBuilds<<" visibility-reuses="<<s.visibilityReuses<<'\n';
    }
    }
    renderer->shutdown();renderer.reset();
}
}
int main(int argc,char** argv)try {
    check(argc==3,"Expected API and image path");
    const std::string api=argv[1],path=argv[2];check(api=="d3d12"||api=="vulkan","Invalid API");
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
    gltf_import_contract::FixtureDirectory directory("gltf-preparation-native");check(directory.ready(),directory.error());
    const auto source=gltf_scene_contract::makeValidSceneSource();
    const auto document=directory.file("scene.gltf");
    {std::ofstream f(document);f<<source.document.dump();check(f.good(),"document write failed");}
    const auto write=[&](const char* name,const auto& bytes){
        std::ofstream f(directory.file(name),std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());check(f.good(),"fixture write failed");
    };
    write("scene.bin",source.binary);write("base_color.png",gltf_scene_contract::authoredBaseColorPngBytes());
    const auto reference=path+".reference.png";
    capture(api,reference,document.string(),false);capture(api,path,document.string(),true);
    int w,h,c,rw,rh,rc;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(path.c_str(),&w,&h,&c,3),stbi_image_free);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> expected(stbi_load(reference.c_str(),&rw,&rh,&rc,3),stbi_image_free);
    check(image&&expected&&w==320&&h==180&&rw==w&&rh==h,"Missing native capture");
    check(std::equal(image.get(),image.get()+w*h*3,expected.get()),"Retained glTF differs from fresh preparation");
    check(std::count_if(image.get(),image.get()+w*h*3,[](unsigned char x){return x>30;})>300,
        "Native fixture contains no visible geometry");
    const auto collectionPath=path+".collection.png";
    capture(api,collectionPath,document.string(),true,true);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> collection(stbi_load(collectionPath.c_str(),&rw,&rh,&rc,3),stbi_image_free);
    check(collection && rw==w && rh==h && std::equal(collection.get(),collection.get()+w*h*3,expected.get()),
        "Collected scene differs from fresh/retained native submission");
    std::cout<<"PASS glTF retained preparation "<<api<<'\n';return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
