#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/models/WorldIndexedBatches.h"
#include <SDL.h>
#include <iostream>
#include <memory>
using namespace engine::render;
using namespace engine::models::batches;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
constexpr int W=320,H=240;
}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==2,"Expected native API");std::string api=argv[1];
    check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Motion producers",W,H,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),W,H,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),W,H,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    std::vector<WorldIndexedBatch> batches(1);auto& batch=batches[0];
    batch.motionRevision=1;batch.motionPart=3;batch.materialMode=2;
    batch.temporalMaterial=WorldTemporalMaterial::Character;
    batch.textureWidth=batch.textureHeight=1;batch.ownedTextureRgba={255,255,255,255};
    batch.textureKey="motion-producer";batch.textureCacheKey=batch.textureKey;
    batch.indices={0,2,1,0,3,2};batch.vertices.resize(4);
    for(int i=0;i<4;++i) {
        auto& v=batch.vertices[i];v.x=i==0||i==3?-.14f:.14f;v.y=i<2?-.2f:.2f;
        v.r=v.g=v.b=v.a=1;v.nz=v.tx=v.tw=v.weight0=1;
    }
    batch.instances.resize(2);batch.motionInstances={11,29};
    batch.instances[0].modelMatrix=worldMotionIdentity;batch.instances[0].modelMatrix[12]=-.45f;
    batch.instances[1].modelMatrix=worldMotionIdentity;batch.instances[1].modelMatrix[12]=.45f;
    WorldMotionHistory history;WorldMotionView view;view.identity=3;view.generation=1;view.width=W;view.height=H;
    const auto frame=[&](bool prepared) {
        ++view.frame;
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(W,H);
        check(surface.beginWorldSurfaceBuffer(worldMotionIdentity.data(),W,H),"surface begin");
        WorldMotionCapture capture(history,view);
        if(prepared) {
            PreparedWorldIndexedSubmission submission(*renderer,batches,false);
            submission.submit(*renderer,worldMotionIdentity.data(),W,H,nullptr,nullptr,nullptr,&history,91,nullptr);
        } else submitWorldIndexedBatches(*renderer,batches,worldMotionIdentity.data(),W,H,nullptr,nullptr,nullptr,&history,91);
        check(surface.endWorldSurfaceBuffer()&&surface.captureWorldSurfaceMotion(),"surface capture");
        capture.commit();renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        return surface.readWorldSurfaceMotion();
    };
    const auto pixel=[](float x){return (H/2)*W+int((x*.5f+.5f)*W);};
    const auto verify=[&](const auto& image,float x,std::uint32_t id,float dx,bool valid) {
        const auto q=pixel(x);const auto& v=image.velocity[q];
        std::cout<<"frame "<<view.frame<<" x "<<x<<" id "<<image.identity[q]<<" motion "<<v[0]<<','<<v[1]<<','<<v[3]<<'\n';
        check(image.identity[q]==id && image.temporalMaterial[q]==1,"authored instance/class identity changed");
        check(v[3]==float(valid),"producer validity");
        if(valid)check(std::abs(v[0]-dx)<.0003f&&std::abs(v[1])<.0003f,"producer motion arithmetic");
    };
    auto first=frame(false);auto left=first.identity[pixel(-.45f)],right=first.identity[pixel(.45f)];
    check(left&&right&&left!=right,"instances share identity");verify(first,-.45f,left,0,false);
    std::swap(batch.instances[0],batch.instances[1]);std::swap(batch.motionInstances[0],batch.motionInstances[1]);
    for(auto& instance:batch.instances)instance.modelMatrix[12]+=.1f;
    batch.geometryCacheKey="motion-producer-geometry";
    auto reordered=frame(true);verify(reordered,-.35f,left,-.05f,true);verify(reordered,.55f,right,-.05f,true);
    // A single-instance submission replaces the base transform and palette.
    batch.instances.resize(1);batch.motionInstances.resize(1);batch.instances[0].modelMatrix[12]+=.1f;
    batch.modelMatrix[12]=.2f;batch.gpuSkinning=1;batch.skinMatrixCount=1;
    batch.skinMatrices.assign(worldMotionIdentity.begin(),worldMotionIdentity.end());batch.skinMatrices[12]=.4f;
    auto single=frame(false);verify(single,.65f,right,-.05f,true);
    batch.instances[0].modelMatrix[12]+=.1f;
    auto singleAgain=frame(true);verify(singleAgain,.75f,right,-.05f,true);
    // CPU-deformed vertices must own N-1 positions, not reuse the current mesh.
    history.release(view.identity);batch.instances.clear();batch.motionInstances.clear();
    batch.gpuSkinning=0;batch.skinMatrixCount=0;batch.skinMatrices.clear();batch.modelMatrix=worldMotionIdentity;
    batch.cpuDeformedMotion=true;batch.geometryCacheKey.clear();
    auto cpuFirst=frame(false);auto cpuId=cpuFirst.identity[pixel(0)];
    for(auto& v:batch.vertices)v.x+=.08f;
    auto cpu=frame(true);verify(cpu,.08f,cpuId,-.04f,true);
    batch.vertices.push_back(batch.vertices.back());
    auto topology=frame(false);auto newId=topology.identity[pixel(.08f)];
    check(newId && newId!=cpuId,"topology change retained stale identity");verify(topology,.08f,newId,0,false);
    batch.temporalMaterial=WorldTemporalMaterial::Geometry;
    auto category=frame(true);
    check(category.velocity[pixel(.08f)][3]==0 && category.temporalMaterial[pixel(.08f)]==0,"class change reused history");
    batch.alphaMode=2;
    auto transparent=frame(false);check(transparent.identity[pixel(.08f)]==0,"transparent card overwrote opaque motion");
    renderer->shutdown();renderer.reset();std::cout<<api<<" motion producer PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
