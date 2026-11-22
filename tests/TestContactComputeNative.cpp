#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <filesystem>
#include <iostream>
#include <memory>
using namespace engine::render;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>void binary(const std::filesystem::path& path,const std::vector<T>& data){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(T));
    if(!out)throw std::runtime_error("Fixture export failed");
}
}
int main(int argc,char** argv)try{
    check(argc==4,"Expected API output-directory game-root");
    std::cout<<std::unitbuf;const std::string api=argv[1];
    const auto root=std::filesystem::path(argv[2]);std::filesystem::create_directories(root);
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Contact compute qualification",192,144,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),192,144,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),192,144,false);
    renderer->setWorldMaterialProfile(loadWorldMaterialProfile(argv[3],"config/render/world_materials.json"));
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    auto& contact=dynamic_cast<IWorldContactVisibility&>(*renderer);
    constexpr std::array<unsigned char,4> white{255,255,255,255},tilted{180,128,243,255};
    constexpr std::array<unsigned,6> triangles{0,2,1,0,3,2};
    std::map<std::uint64_t,WorldMotionMatrix> previousViews;
    std::uint64_t sequence=0;
    nlohmann::json records=nlohmann::json::array();
    const auto frame=[&](std::string name,int width,int height,unsigned quality,std::uint64_t id,std::uint64_t generation,
                        std::uint64_t number,bool reset,float shift,float oldShift,bool motion,bool mapped,bool suppress,bool leaf,float cameraShift,std::string lighting="",WorldContactResponse response={}){
        int liveWidth=0,liveHeight=0;SDL_GetWindowSize(window.getSDLWindow(),&liveWidth,&liveHeight);
        if(liveWidth!=width||liveHeight!=height){SDL_SetWindowSize(window.getSDLWindow(),width,height);renderer->onResize(width,height);}
        const glm::vec3 eye{3+cameraShift,3,5},target{0,.2f,0};
        const auto view=glm::lookAt(eye,target,glm::vec3(0,1,0));
        auto vp=glm::perspective(glm::radians(48.f),float(width)/height,.1f,100.f)*view;
        jitterWorldTemporalProjection(glm::value_ptr(vp),width,height,number);
        WorldMotionMatrix projection;std::copy_n(glm::value_ptr(vp),16,projection.begin());
        const auto previousVP=previousViews.contains(id)?previousViews.at(id):projection;
        WorldTemporalView temporal{id,generation,number,width,height,reset,1.f/60.f};
        temporal.cameraValid=true;temporal.cameraPosition={eye.x,eye.y,eye.z};temporal.viewProjection=projection;
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1.f);
        const auto draw=[&](bool capture){
            for(int object=0;object<3;++object){
                std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
                const std::array<glm::vec3,4> points=object==0?
                    std::array<glm::vec3,4>{{{-10,0,-10},{-10,0,10},{10,0,10},{10,0,-10}}}:
                    object==1?std::array<glm::vec3,4>{{{0,0,-3},{0,1.5,-3},{0,1.5,3},{0,0,3}}}:
                    std::array<glm::vec3,4>{{{-2,0,-2},{2,0,-2},{2,2,-2},{-2,2,-2}}};
                for(int j=0;j<4;++j){auto& v=vertices[j];v.x=points[j].x;v.y=points[j].y;v.z=points[j].z;
                    v.nx=object==1?1.f:0.f;v.ny=object==0?1.f:0.f;v.nz=object==2?1.f:0.f;
                    v.tx=object==1?0.f:1.f;v.tz=object==1?1.f:0.f;v.tw=v.weight0=1;v.r=v.g=v.b=.5f;v.a=1;
                    v.u=j>1?1.f:0.f;v.v=j==1||j==2?1.f:0.f;}
                IRenderBackend::WorldTextureData material;
                material.key=material.cacheKey="contact-compute-white";material.rgba=white.data();material.width=material.height=1;
                material.materialMode=2;material.environment.enabled=true;material.environment.ambientRadiance={.2f,.2f,.2f};
                material.roughnessFactor=.7f;material.metallicFactor=0;material.textureSrgb=0;
                if(!lighting.empty()) {
                    material.environment.ambientRadiance={0,0,0};
                    if(lighting=="ambient")material.environment.ambientRadiance={.2f,.2f,.2f};
                    else if(lighting=="emission") {
                        material.emissiveFactorR=.3f;material.emissiveFactorG=.2f;material.emissiveFactorB=.4f;
                    } else if(lighting=="point")material.environment.pointLights[0]={{0,3,2},12,{4,2,3}};
                    else {material.environment.sunDirection={-.4f,.8f,.4f};material.environment.sunRadiance={1,1,1};}
                    if(lighting=="specular"){material.metallicFactor=1;material.roughnessFactor=.45f;}
                }
                material.cameraPosX=eye.x;material.cameraPosY=eye.y;material.cameraPosZ=eye.z;
                if(object==1)material.modelMatrix[12]=shift;
                if(suppress&&object==1)material.materialFlags=4096;
                if(mapped&&object==0){
                    material.normalKey=material.normalCacheKey="contact-mapped";
                    material.normalRgba=tilted.data();material.normalWidth=material.normalHeight=1;
                }
                std::array<float,64> palette{};palette[0]=palette[4]=palette[51]=1;
                WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
                if(object==1)previous.model[12]=oldShift;
                if(leaf&&object==2){
                    material.materialMode=238;material.alphaMode=1;material.gpuSkinning=1;
                    material.skinMatrixCount=4;material.skinMatrices=palette.data();material.materialFlags=2;
                    material.materialTimeSec=float(number)/30;material.materialRect0V=.8f;
                    material.materialRect1V=eye.x;material.materialRect1W=eye.y;material.materialRect1H=eye.z;
                    material.normalKey=material.normalCacheKey="contact-leaf-normal";
                    material.normalRgba=white.data();material.normalWidth=material.normalHeight=1;
                    material.metallicRoughnessKey=material.metallicRoughnessCacheKey="contact-leaf-orm";
                    material.metallicRoughnessRgba=white.data();material.metallicRoughnessWidth=material.metallicRoughnessHeight=1;
                    previous.matrixCount=4;previous.deformation.assign(palette.begin(),palette.end());
                    previous.materialParameters[0]=float(number-1)/30;previous.materialParameters[1]=2;
                    previous.materialParameters[5]=.8f;
                }
                WorldMotionSample sample{&previous,unsigned(object+1),motion};
                if(capture)check(surface.setWorldSurfaceMotion(previousVP,{&sample,1}),"motion packet");
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,triangles.data(),6,&material,projection.data(),width,height);
            }
        };
        check(surface.beginWorldSurfaceBuffer(projection.data(),width,height),"surface begin");draw(true);
        check(surface.endWorldSurfaceBuffer(),"surface end");
        check(surface.captureWorldSurfaceMotion(),"surface readback");
        check(surface.beginWorldTemporalScene(temporal),"temporal begin");
        check(surface.resolveWorldSurfaceContact(packWorldContactVisibility(projection.data(),width,height,.8f,.015f,quality,response)),"contact resolve");
        check(contact.captureWorldContactVisibility(),"contact readback");
        draw(false);check(surface.resolveWorldTemporalScene(),"temporal scene");
        check(surface.captureWorldTemporalScene(),"consumer radiance readback");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        auto data=contact.readWorldContactVisibility();auto motionData=surface.readWorldSurfaceMotion();auto color=surface.readWorldTemporalScene();
        check(data.width==width&&data.height==height,"contact output extent");
        for(const auto& plane:data.planes)for(const auto& pixel:plane)for(float value:pixel)
            check(std::isfinite(value),"nonfinite contact output");
        const auto dir=root/(std::to_string(sequence++)+"-"+name);std::filesystem::create_directories(dir);
        for(int i=0;i<8;++i)binary(dir/("plane-"+std::to_string(i)+".bin"),data.planes[i]);
        binary(dir/"color.bin",color.color);binary(dir/"motion.bin",motionData.velocity);binary(dir/"normal.bin",motionData.normal);binary(dir/"identity.bin",motionData.identity);
        nlohmann::json record{{"name",name},{"width",width},{"height",height},{"quality",quality},{"view",id},{"generation",generation},{"frame",number},
            {"reset",reset},{"shift",shift},{"oldShift",oldShift},{"validMotion",motion},{"mapped",mapped},{"suppressed",suppress},{"leaf",leaf},
            {"camera",{eye.x,eye.y,eye.z}},{"target",{target.x,target.y,target.z}},{"projection",projection},{"parameters",data.parameters},
            {"texelBytes",data.texelBytes},{"directory",dir.generic_string()}};
        std::ofstream(dir/"metadata.json")<<record.dump(2);records.push_back(record);
        previousViews[id]=projection;std::cout<<api<<' '<<name<<" frame "<<number<<" history="<<data.parameters[9][2]<<'\n';
        return color;
    };
    for(unsigned q=0;q<3;++q){
        for(unsigned f=1;f<=4;++f)frame("quality"+std::to_string(q),192,144,q,10+q,1,f,f==1,0,0,f>1,false,false,false,0);
    }
    for(unsigned f=1;f<=5;++f)frame("moving",192,144,1,20,1,f,f==1,.12f*f,.12f*(f-1),f>1,true,false,false,.015f*f);
    frame("cut",192,144,1,20,1,6,true,.72f,.60f,true,true,false,false,2);
    frame("skip",192,144,1,20,1,8,false,.72f,.72f,true,true,false,false,2);
    frame("generation",192,144,1,20,2,9,false,.72f,.72f,true,true,false,false,2);
    frame("invalid-motion",192,144,1,20,2,10,false,.72f,.72f,false,true,false,false,2);
    frame("odd-resize",191,143,1,20,2,11,false,.72f,.72f,true,true,false,false,2);
    frame("other-view",191,143,1,30,1,1,false,0,0,false,false,false,false,0);
    frame("other-view",191,143,1,30,1,2,false,0,0,true,false,false,false,0);
    frame("return-view",191,143,1,20,2,12,false,.72f,.72f,true,true,false,false,2);
    for(unsigned f=1;f<=3;++f)frame("suppressed",192,144,2,40,1,f,f==1,0,0,f>1,false,true,false,0);
    for(unsigned f=1;f<=3;++f)frame("leaf",192,144,2,50,1,f,f==1,0,0,f>1,false,false,true,0);
    // Retire/recreate resources with a new generation; reuse cannot resurrect history.
    surface.releaseWorldTemporalView(50);
    frame("retired-view",192,144,2,50,2,4,true,0,0,false,false,false,true,0);
    nlohmann::json consumers=nlohmann::json::array();
    for(const std::string mode:{"direct","point","specular","ambient","emission","leaf"}) {
        const auto take=[&](bool enabled) {
            WorldContactResponse response;response.strengths={0,0,0,0};
            if(mode=="specular")response.strengths[3]=enabled?.5f:0.f;
            else if(mode=="ambient")response.strengths[0]=enabled?1.2f:0.f;
            else response.strengths[2]=enabled?1.f:0.f;
            auto image=frame("consumer-"+mode+(enabled?"-on":"-off"),192,144,2,70,1,1,true,0,0,false,false,false,mode=="leaf",0,mode,response);
            surface.releaseWorldTemporalView(70);return image;
        };
        const auto off=take(false),on=take(true);double sum=0,maxChange=0;unsigned darker=0,brighter=0;
        for(std::size_t i=0;i<off.color.size();++i)for(unsigned c=0;c<3;++c) {
            const double delta=off.color[i][c]-on.color[i][c];
            sum+=delta;maxChange=std::max(maxChange,std::abs(delta));
            if(delta>.0002)++darker;if(delta<-.0002)++brighter;
        }
        consumers.push_back({{"mode",mode},{"darker",darker},{"brighter",brighter},{"sum",sum},{"max",maxChange}});
        std::cout<<"CONSUMER "<<mode<<" darker="<<darker<<" brighter="<<brighter<<" sum="<<sum<<std::endl;
        if(mode=="emission")check(maxChange==0,"contact changed emission");
        else check(darker>10 && sum>0 && brighter==0,"contact consumer lobe/leaf response");
        std::cout<<"CONSUMER "<<mode<<" darker="<<darker<<" brighter="<<brighter<<" sum="<<sum<<'\n';
    }
    std::ofstream(root/"consumer-report.json")<<consumers.dump(2);
    std::ofstream(root/"records.json")<<records.dump(2);
    renderer->shutdown();std::cout<<"CONTACT NATIVE EXPORT PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
