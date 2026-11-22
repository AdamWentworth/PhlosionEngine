#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
using namespace engine::render;
namespace {
constexpr int width=640,height=480;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
glm::vec3 camera(0,1.5f,6);
glm::mat4 vp, orientation(1);
float sourceX=3.5f;
float sourceZ=2.f;
bool prepared=false;
glm::vec3 oriented(glm::vec3 p){return glm::vec3(orientation*glm::vec4(p,1));}
std::array<int,2> pixel(glm::vec3 p){auto c=vp*glm::vec4(oriented(p),1);return {int((c.x/c.w*.5f+.5f)*width),int((.5f-c.y/c.w*.5f)*height)};}
std::vector<unsigned char> capture(const std::string& api,const std::string& mode,bool enabled,const std::filesystem::path& path,bool animate=true) {
    std::filesystem::remove(path);
#if defined(_WIN32)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str());
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","4");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str(),1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","4",1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Local reflection contracts",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& local=dynamic_cast<IWorldLocalReflection&>(*renderer);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*renderer);
    auto& screen=dynamic_cast<IWorldScreenReflection&>(*renderer);
    constexpr std::array<unsigned char,4> white{255,255,255,255},hole{255,255,255,0};
    constexpr std::array<std::uint32_t,12> order{0,1,2,0,2,3,2,1,0,3,2,0};
    WorldLocalReflectionProbe probe;
    if(prepared)probe.sampling=WorldLocalReflectionSampling::PreparedRadiance;
    probe.position={0,1.5f,1};probe.minimum={-6,-4,-1};probe.maximum={6,7,2};probe.fade=.1f;
    if(mode.starts_with("receiver-grazing")){
        probe.resolution=mode.ends_with("-low")?64:256;probe.position={camera.x,camera.y,camera.z};
        probe.minimum={-12,-40,-1};probe.maximum={40,40,4};
    }
    if(mode=="receiver-hidden")probe.position={-5,1.5f,1};
    if(mode=="outside"){probe.minimum[2]=.1f;probe.position[2]=1.1f;}
    {
        auto lo=probe.minimum,hi=probe.maximum;glm::vec3 minimum(1e20f),maximum(-1e20f);
        for(int i=0;i<8;++i){auto p=oriented({i&1?hi[0]:lo[0],i&2?hi[1]:lo[1],i&4?hi[2]:lo[2]});minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);}
        std::copy_n(glm::value_ptr(minimum),3,probe.minimum.begin());std::copy_n(glm::value_ptr(maximum),3,probe.maximum.begin());
    }
    auto draw=[&](const float* matrix,const std::array<float,3>& eye,int w,int h) {
        for(int item=0;item<(mode=="blocked"||mode=="masked"||mode=="receiver-hidden"?3:2);++item) {
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            const glm::vec3 center=item==0?glm::vec3(0,1.5,0):item==1?glm::vec3(sourceX,1.5,sourceZ):mode=="receiver-hidden"?glm::vec3(-1,1.5f,.5f):glm::vec3(2.975f,1.5f,.8f);
            float half=item==0?(mode.starts_with("receiver-grazing")?8.f:mode=="parallax"||mode.starts_with("grazing")||mode.starts_with("motion")||mode=="moving"||mode=="removed"?4.5f:3.5f):item==1?(mode.starts_with("receiver-grazing")?32.f:.3f):.45f;
            for(int j=0;j<4;++j){auto& v=vertices[j];v.x=center.x+(j==0||j==3?-half:half);v.y=center.y+(j<2?-half:half);v.z=center.z;
                v.nz=1;v.tx=1;v.tw=1;v.r=v.g=v.b=item==0?.8f:.01f;v.a=1;v.u=j==0||j==3?0.f:1.f;v.v=j<2?0.f:1.f;
                auto p=oriented({v.x,v.y,v.z});v.x=p.x;v.y=p.y;v.z=p.z;
                auto n=glm::mat3(orientation)*glm::vec3(0,0,1);v.nx=n.x;v.ny=n.y;v.nz=n.z;
                auto tangent=glm::mat3(orientation)*glm::vec3(1,0,0);v.tx=tangent.x;v.ty=tangent.y;v.tz=tangent.z;
            }
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey="screen-white";t.rgba=white.data();t.width=t.height=1;
            if(item==2 && mode=="masked"){t.key=t.cacheKey="screen-hole";t.rgba=hole.data();t.alphaMode=1;t.alphaCutoff=.5f;}
            t.textureSrgb=0;t.normalScale=0;t.materialMode=mode=="pbr254"?254:2;t.metallicFactor=item==0?1.f:0.f;
            t.roughnessFactor=item==0?(mode=="rough"?.5f:.08f):1.f;
            t.cameraPosX=eye[0];t.cameraPosY=eye[1];t.cameraPosZ=eye[2];
            t.environment.enabled=true;t.environment.ambientRadiance={.03f,.03f,.03f};t.environment.exposure=.6f;
            if(item==1 && mode!="source-off" && sourceX<20.f) {
                t.emissiveFactorR=.02f;t.emissiveFactorG=4.f;t.emissiveFactorB=3.f;
                t.emissiveKey=t.emissiveCacheKey="screen-emission";t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
            }
            renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),12,&t,matrix,w,h);
        }
    };
    for(int frame=0;frame<4;++frame){
        if(animate && (mode=="moving"||mode=="removed"))sourceX=frame<3?3.2f:mode=="removed"?30.f:4.1f;
        if(mode=="camera") {
            camera=oriented({-.2f*frame,1.5f,6});
            vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,oriented({0,1.5,0}),glm::mat3(orientation)*glm::vec3(0,1,0));
        }
        if(mode.starts_with("motion")) {
            float offset=.1f*(std::stoi(mode.substr(6))-8);
            camera=oriented({offset,1.5f,6});
            vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,oriented({offset,1.5,0}),glm::mat3(orientation)*glm::vec3(0,1,0));
        }
        if(mode.starts_with("grazing-view")) {
            const float offset=.1f*(std::stoi(mode.substr(12))-4);
            camera={offset,1.5f,6};probe.position={camera.x,camera.y,camera.z};
            vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(offset,1.5f,0),glm::vec3(0,1,0));
        }
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);radiance.setWorldSceneRadianceExposure(.6f);
        check(!local.endWorldLocalReflectionFace(),"Capture leaked between views");
        if(enabled && (mode!="reset"||frame==0)){
            check(local.beginWorldLocalReflection(probe),"Probe begin failed");
            check(!local.beginWorldLocalReflection(probe),"Nested capture accepted");
            for(int face=0;face<(worldLocalReflectionCaptureViewCount(probe)-(mode=="partial"?1:0));++face){
                const auto v=makeWorldLocalReflectionFace(probe,face);
                draw(v.viewProjection.data(),v.position,v.resolution,v.resolution);
                check(local.endWorldLocalReflectionFace(),"Probe face failed");
            }
            if(mode=="partial")local.clearWorldLocalReflection();
            check(!local.endWorldLocalReflectionFace(),"Excess capture face accepted");
        }
        if(enabled && mode.starts_with("handoff")) {
            check(screen.beginWorldScreenReflection(packWorldScreenReflection(glm::value_ptr(vp),width,height)),"Combined screen source begin failed");
            draw(glm::value_ptr(vp),{camera.x,camera.y,camera.z},width,height);
            check(screen.endWorldScreenReflection(),"Combined screen source end failed");
        }
        draw(glm::value_ptr(vp),{camera.x,camera.y,camera.z},width,height);renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* data=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(data&&w==width&&h==height,"Missing local reflection image");
    std::vector<unsigned char> result(data,data+w*h*3);stbi_image_free(data);return result;
}
}
int main(int argc,char** argv)try {
    vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,1.5,0),glm::vec3(0,1,0));
    if(argc==1){
        WorldLocalReflectionProbe probe;
        for(int face=0;face<worldLocalReflectionViewCount;++face){
            auto f=makeWorldLocalReflectionFace(probe,face);
            auto c=glm::make_mat4(f.viewProjection.data())*glm::vec4(glm::make_vec3(f.position.data())+glm::make_vec3(f.forward.data()),1);
            check(std::abs(c.x)<1e-5&&std::abs(c.y)<1e-5&&c.w>0,"Probe face orientation");
        }
        for(bool preparedAtlas:{false,true})for(int base:{16,256,512}) {
            std::vector<bool> occupied(16*base*base,false);
            const auto mark=[&](int face,int level) {
                int size=base>>level;auto offset=worldLocalReflectionAtlasOffset(face,size,level,base);
                check(offset[0]>=0 && offset[1]>=0 && offset[0]+size<=4*base && offset[1]+size<=4*base,"Reflection atlas overflow");
                for(int y=offset[1];y<offset[1]+size;++y)for(int x=offset[0];x<offset[0]+size;++x) {
                    auto at=y*4*base+x;check(!occupied[at],"Reflection atlas regions overlap");occupied[at]=true;
                }
            };
            for(int face=0;face<(preparedAtlas?6:worldLocalReflectionViewCount);++face)mark(face,0);
            for(int level=1;level<=std::min(5,int(std::log2(base)));++level)
                for(int face=0;face<6;++face) {
                    mark(face,level);if(preparedAtlas)mark(face+12,level);
                }
        }
        check(worldLocalReflectionCaptureViewCount(probe)==12,"Traced capture count");
        probe.sampling=WorldLocalReflectionSampling::PreparedRadiance;
        check(worldLocalReflectionCaptureViewCount(probe)==6,"Prepared capture count");
        bool excess=false;
        try{makeWorldLocalReflectionFace(probe,6);}catch(const std::invalid_argument&){excess=true;}
        check(excess,"Prepared capture accepted a geometry face");
        probe.position[0]=20;
        check(packWorldLocalReflection(probe)[0][0]==20,"Capture outside receiver influence rejected");
        bool rejected=false;probe.position[0]=std::numeric_limits<float>::quiet_NaN();
        try{packWorldLocalReflection(probe);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Nonfinite capture accepted");
        rejected=false;probe.position[0]=0;probe.minimum[0]=probe.maximum[0];
        try{packWorldLocalReflection(probe);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Empty influence bounds accepted");return 0;
    }
    check(argc==4,"Expected API mode output directory");
    const std::string api=argv[1];std::string mode=argv[2];
    prepared=mode.starts_with("prepared-");if(prepared)mode.erase(0,9);check(api=="d3d12"||api=="vulkan","Invalid API");
    check(mode.starts_with("receiver-grazing")||mode=="receiver-hidden"||mode=="mirror"||mode=="pbr254"||mode=="rough"||mode=="reset"||mode=="source-off"||mode=="camera"||mode=="outside"||mode=="partial"||mode=="seam"||mode=="blocked"||mode=="masked"||mode=="parallax"||mode=="moving"||mode=="removed"||mode.starts_with("grazing")||mode.starts_with("handoff")||mode.starts_with("motion")||mode.starts_with("face"),"Invalid mode");
    if(prepared)check(mode=="mirror"||mode=="rough"||mode=="reset"||mode=="outside"||mode=="partial"||mode=="seam"||mode.starts_with("face"),
        "Prepared mode needs an emitter on the projection box wall");
    if(mode.starts_with("handoff"))sourceX=2.6f+.1f*std::stoi(mode.substr(7));
    if(mode=="parallax"){sourceZ=1.2f;sourceX=4.f;}
    if(mode.starts_with("receiver-grazing")){sourceZ=2.f;sourceX=2.f;}
    if(mode.starts_with("grazing")){sourceZ=1.2f;sourceX=4.5f;}
    if(mode.starts_with("motion")){sourceZ=1.2f;sourceX=4.f;}
    if(mode=="seam")sourceX=1.f;
    if(mode.starts_with("face")) {
        const int face=std::stoi(mode.substr(4));check(face>=0&&face<6,"Bad face");
        const std::array<glm::vec3,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
        const glm::vec3 axis=face==1?glm::vec3(0,1,0):glm::cross(glm::vec3(1,0,0),directions[face]);
        const auto rotation=face==0?glm::mat4(1):glm::rotate(glm::mat4(1),glm::radians(face==1?180.f:90.f),axis);
        const glm::vec3 pivot(0,1.5f,1);orientation=glm::translate(glm::mat4(1),pivot)*rotation*glm::translate(glm::mat4(1),-pivot);
    }
    camera=oriented({0,1.5,6});
    if(mode.starts_with("receiver-grazing"))camera={-6,1.5f,.25f};
    vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,oriented({0,1.5,0}),glm::mat3(orientation)*glm::vec3(0,1,0));
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    const std::filesystem::path root=argv[3];std::filesystem::create_directories(root);
    auto off=capture(api,mode,false,root/"off.png"),on=capture(api,mode,true,root/"on.png");
    // A vertical z=0 mirror reflects the emitter at z=+2 to z=-2, independent
    // of the tracing implementation. Its real projection is a separate control.
    const float expectedSourceX=mode=="removed"?3.2f:sourceX;
    auto reflected=pixel({expectedSourceX,1.5f,-sourceZ}),real=pixel({expectedSourceX,1.5f,sourceZ});
    auto patch=[&](auto xy,int channel){double sum=0;for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){auto at=((xy[1]+y)*width+xy[0]+x)*3+channel;sum+=int(on[at])-off[at];}return sum/25.;};
    const auto green=patch(reflected,1),direct=mode=="seam"?patch(real,1):0.0;
    if(!mode.starts_with("receiver-grazing") && mode!="seam" && !mode.starts_with("handoff"))check(real[0]>width+3,"Oracle emitter is still in view");
    int maxDelta=0;for(std::size_t i=0;i<on.size();++i)maxDelta=std::max(maxDelta,std::abs(int(on[i])-off[i]));
    std::cout<<api<<' '<<mode<<" reflected green delta="<<green<<" real delta="<<direct<<" max="<<maxDelta<<'\n';
    if(mode=="reset"||mode=="outside"||mode=="partial")check(maxDelta==0,"Disabled/rough view changed");
    else if(mode=="source-off"||mode=="blocked"||mode=="removed"||mode=="receiver-hidden")check(green<3,"Absent/blocked reflection retained bright source");
    else if(mode!="rough")check(green>20,"Missing emitter at analytic vertical mirror projection");
    check(std::abs(direct)<=1,"Direct source changed during reflection composition");
    if(mode=="moving"||mode=="removed") {
        auto current=capture(api,mode,true,root/"current-pose.png",false);
        check(on==current,"Previous source pose leaked into the current reflection");
    }
    if(mode=="parallax"||mode=="moving"||mode.starts_with("grazing")||mode.starts_with("motion")||mode.starts_with("handoff")) {
        double total=0,cx=0,cy=0;int litPixels=0;
        for(int y=reflected[1]-25;y<=reflected[1]+25;++y)
            for(int x=reflected[0]-30;x<=reflected[0]+30;++x) {
                const auto at=(y*width+x)*3+1;const int delta=int(on[at])-off[at];
                if(delta>40){total+=delta;cx+=delta*(x+.5);cy+=delta*(y+.5);++litPixels;}
            }
        check(total>0,"No visible reflected source");cx/=total;cy/=total;
        auto projected=vp*glm::vec4(oriented({sourceX,1.5f,-sourceZ}),1);
        const double expectedX=(projected.x/projected.w*.5+.5)*width;
        const double expectedY=(.5-projected.y/projected.w*.5)*height;
        std::cout<<"centroid="<<cx<<','<<cy<<" expected="<<expectedX<<','<<expectedY<<" lit="<<litPixels<<'\n';
        check(std::abs(cx-expectedX)<2.5 && std::abs(cy-expectedY)<2.5,"Reflection moved away from analytic projection");
        check(litPixels>650 && litPixels<1550,"Reflection footprint collapsed or spread during camera movement");
    }
    if(mode=="rough") {
        int softened=0;
        for(int y=reflected[1]-20;y<=reflected[1]+20;++y)
            for(int x=reflected[0]-22;x<=reflected[0]+22;++x) {
                const auto at=(y*width+x)*3+1;const int delta=int(on[at])-off[at];
                if(delta>green*.1 && delta<green*.9)++softened;
            }
        check(green>1 && softened>16,"Rough reflection retained a hard mirror edge");
    }
    if(mode.starts_with("receiver-grazing")) {
        // A broad source covers all these mirror rays. Use world-space receiver
        // points to isolate receiver rejection from source silhouette sampling.
        int missing=0,total=0;
        for(int y=0;y<20;++y)for(int x=0;x<20;++x){
            auto xy=pixel({-3.7f+1.4f*(x+.5f)/20.f,1.1f+.8f*(y+.5f)/20.f,0});
            auto at=(xy[1]*width+xy[0])*3+1;++total;if(int(on[at])-off[at]<40)++missing;
        }
        std::cout<<"Interior missing="<<missing<<'/'<<total<<'\n';
        check(total>100 && missing==0,"Visible receiver lost reflection in interior bands");
    }
    std::cout<<"PASS local reflection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
