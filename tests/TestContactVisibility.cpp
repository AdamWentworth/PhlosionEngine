#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <vector>
using namespace engine::render;
namespace {
int width=640,height=480;
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
glm::vec3 camera(3,3,5);
glm::mat4 viewProjection(){return glm::perspective(glm::radians(48.f),float(width)/height,.1f,30.f)*glm::lookAt(camera,glm::vec3(0,.2f,0),glm::vec3(0,1,0));}
glm::mat4 vp=viewProjection();
std::array<int,2> pixel(glm::vec3 p){auto c=vp*glm::vec4(p,1);return {int((c.x/c.w*.5f+.5f)*width),int((.5f-c.y/c.w*.5f)*height)};}
std::vector<unsigned char> capture(const std::string& api,const std::string& mode,bool enabled,const std::filesystem::path& path) {
#if defined(_WIN32)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str());
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",path.string().c_str(),1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Contact visibility",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<unsigned char,4> hole{255,255,255,0};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    int renderFrame=0;
    auto draw=[&] {
        for(int surface=0;surface<2;++surface) {
            if(surface && mode=="disoccluded" && renderFrame>0)continue;
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            const std::array<glm::vec3,4> points=surface==0 ? std::array<glm::vec3,4>{{{-4,0,-4},{-4,0,4},{4,0,4},{4,0,-4}}} :
                std::array<glm::vec3,4>{{{0,0,-3},{0,1.5,-3},{0,1.5,3},{0,0,3}}};
            for(int j=0;j<4;++j){auto& v=vertices[j];v.x=points[j].x;v.y=points[j].y;v.z=points[j].z;
                v.nx=surface?1.f:0.f;v.ny=surface?0.f:1.f;v.nz=0;v.tx=0;v.tz=1;v.tw=1;
                v.r=v.g=v.b=.5f;v.a=1;}
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey="contact-white";t.rgba=white.data();t.width=t.height=1;
            t.materialMode=mode=="pbr254"?254:2;t.textureSrgb=0;t.metallicFactor=0;t.roughnessFactor=1;
            if(mode=="glossy"||mode=="rough"||mode=="away") {t.metallicFactor=1;t.roughnessFactor=mode=="rough"?.65f:.16f;}
            t.cameraPosX=camera.x;t.cameraPosY=camera.y;t.cameraPosZ=camera.z;
            t.environment.enabled=true;t.environment.exposure=.6f;
            if(mode=="direct") {t.environment.sunDirection={0,1,0};t.environment.sunRadiance={1,1,1};}
            else if(mode=="emission"){t.emissiveFactorR=.3f;t.emissiveFactorG=.2f;t.emissiveFactorB=.4f;}
            else t.environment.ambientRadiance={.2f,.2f,.2f};
            if(surface && mode=="masked"){t.key=t.cacheKey="contact-hole";t.rgba=hole.data();t.alphaMode=1;}
            renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(vp),width,height);
        }
    };
    for(int frame=0;frame<4;++frame){
        renderFrame=frame;
        if(mode=="camera-cut" || mode=="motion") {
            camera={3.f+(mode=="camera-cut"?(frame<1?4.f:0.f):float(frame)*.02f),3,5};vp=viewProjection();
        }
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(.6f);
        if(mode=="camera-cut" && !enabled)dynamic_cast<IWorldContactVisibility&>(*renderer).clearWorldContactVisibility();
        if(mode=="surface") {
            if(enabled) {
                auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
                check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),width,height),"Surface contact begin failed");
                draw();check(surface.endWorldSurfaceBuffer(),"Surface contact end failed");
                check(surface.resolveWorldSurfaceContact(packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f)),"Surface contact resolve failed");
            } else drawWorldContactVisibility(*renderer,glm::value_ptr(vp),width,height,draw);
        } else if((enabled || mode=="camera-cut") && (mode!="reset" || frame==0))drawWorldContactVisibility(*renderer,glm::value_ptr(vp),width,height,draw);
        draw();renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    if(mode=="motion"){camera={3.02f,3,5};vp=viewProjection();}
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;auto* pixels=stbi_load(path.string().c_str(),&w,&h,&c,3);
    check(pixels&&w==width&&h==height,"Missing contact visibility image");
    std::vector<unsigned char> result(pixels,pixels+w*h*3);stbi_image_free(pixels);return result;
}
}
int main(int argc,char** argv)try {
    if(argc==1) {
        // The prepared depth payload uses integer chunks in RGBA16F.
        // Check the storage format independently across its entire 11-bit domain.
        for(unsigned value=0;value<2048;++value)
            check(glm::unpackHalf1x16(glm::packHalf1x16(float(value)))==float(value),"Contact depth chunk lost half precision");
        auto p=packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f);
        check(p[8][0]==width&&p[8][3]==-1,"Invalid contact packet");
        check(p[9][3]==1,"Perspective view did not enable buffered contact");
        for(int i=0;i<3;++i)check(std::abs(p[10][i]-camera[i])<.001f,"Incorrect reconstructed camera");
        check(p[22][0]==-1.f&&p[22][1]==.75f&&p[22][2]==.5f,"Contact leaf/grazing packet");
        check(p[23]==std::array<float,4>{1.2f,.95f,1.f,.5f},"Contact independent strengths");
        for(unsigned q=0;q<3;++q)check(packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f,q)[10][3]==float(q),"Contact quality packet");
        bool badQuality=false;try{packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f,3);}catch(const std::invalid_argument&){badQuality=true;}
        check(badQuality,"Invalid quality accepted");
        WorldContactResponse invalid;invalid.strengths[1]=std::numeric_limits<float>::quiet_NaN();
        bool badResponse=false;try{packWorldContactVisibility(glm::value_ptr(vp),width,height,.8f,.015f,2,invalid);}catch(const std::invalid_argument&){badResponse=true;}
        check(badResponse,"Invalid response accepted");
        WorldContactHistory history{p,8,17,23,true};
        check(history.accepts(p,9,17,23),"Consecutive unchanged view rejected history");
        check(!history.accepts(p,10,17,23),"Skipped frame retained history");
        check(!history.accepts(p,9,18,23)&&!history.accepts(p,9,17,24),"Different view/scene retained history");
        auto altered=p;altered[8][0]+=1;check(!history.accepts(altered,9,17,23),"Resize retained history");
        altered=p;altered[10][0]+=2;check(!history.accepts(altered,9,17,23),"Camera cut retained history");
        altered=p;altered[11][3]+=.000001f;check(history.accepts(altered,9,17,23),"Projection roundoff rejected history");
        altered=p;altered[11][3]+=.01f;check(!history.accepts(altered,9,17,23),"Field-of-view change retained history");
        altered=p;altered[13][0]*=-1;altered[13][1]*=-1;altered[13][2]*=-1;
        check(!history.accepts(altered,9,17,23),"Camera reversal retained history");
        history.valid=false;check(!history.accepts(p,9,17,23),"Explicit history reset ignored");
        bool rejected=false;try{packWorldContactVisibility(nullptr,width,height,.8f,.01f);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Null view accepted");return 0;
    }
    check(argc==4 || argc==6,"Expected API mode directory [width height]");
    if(argc==6) {
        width=std::stoi(argv[4]);height=std::stoi(argv[5]);
        check(width>=320&&height>=240&&width<=2048&&height<=2048,"Invalid contact test extent");
        vp=viewProjection();
    }
    const std::string api=argv[1],mode=argv[2];
    check(api=="d3d12"||api=="vulkan","Invalid API");
    check(mode=="surface"||mode=="contact"||mode=="pbr254"||mode=="direct"||mode=="emission"||mode=="masked"||mode=="reset"||mode=="glossy"||mode=="rough"||mode=="away"||mode=="disoccluded"||mode=="camera-cut"||mode=="motion","Invalid mode");
    if(mode=="away"){camera={-.1f,3.f,1.1f};vp=viewProjection();}
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    std::filesystem::path root=argv[3];std::filesystem::create_directories(root);
    auto off=capture(api,mode,false,root/"off.png"),on=capture(api,mode,true,root/"on.png");
    int maxDelta=0,changed=0;for(std::size_t i=0;i<off.size();++i){int d=int(off[i])-on[i];maxDelta=std::max(maxDelta,std::abs(d));if(std::abs(d)>3)++changed;}
    if(mode=="surface")check(maxDelta<=1,"Surface consumer exceeded 1/255 comparison tolerance");
    else if(mode=="direct"||mode=="emission"||mode=="reset"||mode=="camera-cut")check(maxDelta==0,"Direct/emission/view-reset pixels changed");
    else {
        auto patch=[&](glm::vec3 p){auto xy=pixel(p);double sum=0;for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)for(int c=0;c<3;++c){auto at=((xy[1]+y)*width+xy[0]+x)*3+c;sum+=int(off[at])-on[at];}return sum/75.;};
        const double nearDelta=patch({.12f,0,1}),farDelta=patch({1.6f,0,1});
        std::cout<<"near darkening="<<nearDelta<<" far="<<farDelta<<" changed channels="<<changed<<'\n';
        if(mode=="contact"||mode=="pbr254"||mode=="glossy"||mode=="rough"||mode=="motion")check(nearDelta>4&&std::abs(farDelta)<2,"Missing localized floor contact shading");
        else if(mode=="away")check(std::abs(nearDelta)<2&&std::abs(farDelta)<2,"Open reflection direction was occluded");
        else check(maxDelta<=2,"Masked-away wall or flat plane occluded the floor");
    }
    std::cout<<api<<' '<<mode<<" max delta="<<maxDelta<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
