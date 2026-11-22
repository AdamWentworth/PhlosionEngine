#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/AreaLightLut.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"
#include <SDL.h>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
using namespace engine::render;
namespace {
constexpr int width=512,height=320;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double half(std::uint16_t bits) {
    const int exponent=(bits>>10)&31, mantissa=bits&1023;
    return std::ldexp(exponent?1.+mantissa/1024.:mantissa/1024.,exponent?exponent-15:-14)*(bits&32768?-1:1);
}
std::array<double,2> dfg(double roughness,double nv) {
    double x=std::clamp(roughness,.04,1.)*63,y=std::sqrt(1-std::clamp(nv,0.,1.))*63;
    const int ix=int(x),iy=int(y);x-=ix;y-=iy;
    std::array<double,2> moments{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i)for(int c=0;c<2;++c) {
        const int offset=(std::min(iy+j,63)*128+64+std::min(ix+i,63))*4+c;
        moments[c]+=half(areaLightLut[offset])*(i?x:1-x)*(j?y:1-y);
    }
    return {moments[0]-moments[1],moments[1]};
}
double display(double radiance) {
    double x=std::clamp((radiance*(radiance+.0245786)-.000090537)/
        (radiance*(.983729*radiance+.432951)+.238081),0.,1.);
    return 255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055);
}
}
int main(int argc,char** argv) try {
    if(argc==1){
        WorldEnvironmentLighting e;
        e.reflectionResponse={1.2f,1.3f,.2f,3};e.reflectionResponseWeight=.5f;e.reflectionAlbedoF90=true;
        check(packWorldReflectionResponse(e)==WorldReflectionResponseUniforms{},"Disabled environment leaked response");
        e.enabled=true;
        const auto packed=packWorldReflectionResponse(e);
        check(packed[0]==e.reflectionResponse&&packed[1]==std::array<float,4>{.5f,1,0,0},"Response packing lost controls");
        backend::WorldTextureData material;material.environment=e;
        check(vulkan_backend::makeWorldViewState(&material).reflectionResponse==packed,"View cache lost response");
        for(int i=0;i<6;++i){auto bad=e;
            if(i<4)bad.reflectionResponse[i]=std::numeric_limits<float>::quiet_NaN();
            if(i==4)bad.reflectionResponseWeight=std::numeric_limits<float>::infinity();
            if(i==5)bad.reflectionResponse[0]=-1;
            check(packWorldReflectionResponse(bad)==WorldReflectionResponseUniforms{},"Invalid reflection controls reached shader");
        }
        e.reflectionResponseWeight=0;
        check(packWorldReflectionResponse(e)==WorldReflectionResponseUniforms{},"Zero weight failed neutral opt-out");
        e.reflectionResponseWeight=2;e.reflectionResponse[3]=9;
        check(packWorldReflectionResponse(e)[1][0]==1&&packWorldReflectionResponse(e)[0][3]==4,"Reflection controls unbounded");
        std::cout<<"Reflection response contracts PASS\n";return 0;
    }
    check(argc==4,"Expected API, mode and image path");
    const std::string api=argv[1],mode=argv[2];
    check(api=="vulkan"||api=="d3d12","Unknown API");
    const bool authored=mode.find("authored")!=std::string::npos;
    const bool weighted=mode.find("weight")!=std::string::npos;
    const bool occluded=mode.find("occluded")!=std::string::npos;
    check(authored||mode=="energy"||mode=="energy254"||mode=="grazing"||mode=="grazing254"||
          mode=="wet"||mode=="wet254"||mode=="grazing-wet"||mode=="grazing-wet254"||
          mode=="direct"||mode=="direct254","Unknown response case");
    const bool wet=mode.find("wet")!=std::string::npos;
    const bool direct=mode.starts_with("direct");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    const std::filesystem::path path=argv[3];std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
#if defined(_WIN32)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3],1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Reflection response",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& local=dynamic_cast<IWorldLocalReflection&>(*renderer);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*renderer);
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<unsigned char,4> gray{128,128,128,255};
    constexpr std::array<std::array<float,4>,4> responseControls{{{1.2f,1.3f,0,1},{1.2f,1.3f,0,3},{1,1,.2f,0},{1,1,0,0}}};
    constexpr std::array<std::uint32_t,12> indices{0,1,2,0,2,3,2,1,0,3,2,0};
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    constexpr std::array<float,16> floorView{1,0,0,0,0,0,1,0,0,-1,0,0,0,0,0,1};
    constexpr std::array<float,4> roughness{.04f,.08f,.16f,.42f};
    constexpr std::array<float,4> reflectance{.04f,.5f,1.f,.04f};
    const float cosine=mode.starts_with("grazing")?.2f:1.f;
    // Rotate wet cases onto a horizontal floor so their full weather response
    // is tested with the same view angles and pixel positions as dry cases.
    auto orient=[&](glm::vec3 p){return wet?glm::vec3(p.x,p.z,-p.y):p;};
    auto material=[&](){
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="response-white";
        t.rgba=white.data();t.width=t.height=1;t.textureSrgb=0;t.normalScale=0;
        t.materialMode=mode.ends_with("254")?254:2;t.environment.enabled=true;t.environment.exposure=.6f;
        return t;
    };
    auto quad=[&](glm::vec3 center,glm::vec3 u,glm::vec3 v,float color,
                  IRenderBackend::WorldTextureData t,const float* matrix,int w,int h){
        center=orient(center);u=orient(u);v=orient(v);
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        const auto normal=glm::normalize(glm::cross(u,v));
        const auto tangent=glm::normalize(u);
        for(int j=0;j<4;++j){auto p=center+u*(j==0||j==3?-1.f:1.f)+v*(j<2?-1.f:1.f);
            auto& a=vertices[j];a.x=p.x;a.y=p.y;a.z=p.z;
            a.nx=normal.x;a.ny=normal.y;a.nz=normal.z;a.tx=tangent.x;a.ty=tangent.y;a.tz=tangent.z;
            a.r=a.g=a.b=color;a.a=1;a.u=j==0||j==3?0.f:1.f;a.v=j<2?0.f:1.f;}
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),12,&t,matrix,w,h);
    };
    WorldLocalReflectionProbe probe;probe.position=wet?std::array<float,3>{0,1,0}:std::array<float,3>{0,0,1};
    probe.minimum={-25,-25,-25};probe.maximum={25,25,25};probe.fade=.1f;
    for(int frame=0;frame<4;++frame){
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);radiance.setWorldSceneRadianceExposure(.6f);
        if(!direct){
        check(local.beginWorldLocalReflection(probe),"Cannot start unit-radiance capture");
        for(int face=0;face<worldLocalReflectionViewCount;++face){
            const auto view=makeWorldLocalReflectionFace(probe,face);
            auto t=material();t.cameraPosX=view.position[0];t.cameraPosY=view.position[1];t.cameraPosZ=view.position[2];
            t.emissiveKey=t.emissiveCacheKey="response-white";t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
            t.emissiveFactorR=t.emissiveFactorG=t.emissiveFactorB=1;
            // A closed unit-emission box supplies constant incoming radiance.
            // Include the receiver plane so its capture visibility is valid.
            for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
                glm::vec3 center(0),u(0),v(0);center[axis]=20.f*sign;u[(axis+1)%3]=20;v[(axis+2)%3]=20;
                quad(center,u,v,0,t,view.viewProjection.data(),view.resolution,view.resolution);
            }
            quad({0,0,0},{20,0,0},{0,20,0},0,t,view.viewProjection.data(),view.resolution,view.resolution);
            check(local.endWorldLocalReflectionFace(),"Cannot finish unit-radiance face");
        }
        }
        for(int row=0;row<4;++row)for(int col=0;col<4;++col){
            const float x=-.75f+.5f*col,y=.75f-.5f*row;auto t=material();
            const auto camera=orient({x+10*std::sqrt(1-cosine*cosine),y,10*cosine});
            t.cameraPosX=camera.x;t.cameraPosY=camera.y;t.cameraPosZ=camera.z;
            t.metallicFactor=row==3?0.f:1.f;t.roughnessFactor=roughness[col];
            t.environment.surfaceWeatherEnabled=wet;t.environment.surfaceWetness=wet?1.f:0.f;
            t.environment.surfaceAbsorption=0;
            if(authored){
                t.environment.reflectionResponse=responseControls[row];
                t.environment.reflectionResponseWeight=weighted?float(col)/3:1;
                t.environment.reflectionAlbedoF90=true;
            }
            if(occluded){t.occlusionKey=t.occlusionCacheKey="response-gray";t.occlusionRgba=gray.data();t.occlusionWidth=t.occlusionHeight=1;}
            if(direct){t.environment.sunDirection={0,0,1};t.environment.sunRadiance={1,1,1};}
            quad({x,y,0},{.2f,0,0},{0,.2f,0},row==3?(authored?.001f:.3f):reflectance[row],t,wet?floorView.data():identity.data(),width,height);
        }
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(pixels&&w==width&&h==height,"Missing response image");
    double error=0,legacyError=0;
    for(int row=0;row<4;++row)for(int col=0;col<4;++col){
        const int px=64+col*128,py=40+row*80;
        const int actual=pixels.get()[(py*width+px)*3];
        if(direct){
            // Reflection smoothness must not alter the established direct
            // highlights. The .42 patch verifies that this is a lit response.
            const int reference=pixels.get()[(py*width+320)*3];
            check(actual>5,"Direct-light control is dark");
            if(col<3)error=std::max(error,double(std::abs(actual-reference)));
            else check(reference-actual>12,"Direct roughness control lost its highlight");
            continue;
        }
        const double dx=10*std::sqrt(1-cosine*cosine)-1./width,dy=1./height,dz=10*cosine;
        const double nv=dz/std::sqrt(dx*dx+dy*dy+dz*dz);
        // Rain must not roughen a polished material; rough receivers keep
        // their ground-texture floor.
        double r=roughness[col];
        if(wet)r=std::min(r,std::max(.16,r*.55));
        double legacyRoughness=std::max(double(roughness[col]),.16);
        if(wet)legacyRoughness=std::max(.16,legacyRoughness*.55);
        const auto response=dfg(r,nv),legacy=dfg(legacyRoughness,nv);
        double reflected=reflectance[row]*response[0]+response[1];
        const double ao=occluded?128./255:1;
        if(authored){
            const double weight=weighted?double(col)/3:1;
            if(weight>0){
                const double q=weight*ao;
                const auto controls=responseControls[row];
                const double f90=1+weight*((row==3?.15:1)-1);
                reflected=std::clamp((1+(controls[0]-1)*q)*response[0]*reflectance[row]+
                    (1+(controls[1]-1+controls[3])*q)*response[1]*f90+controls[2]*q,0.,1.);
            }
        }
        const double visibility=std::clamp(std::pow(nv+ao,std::exp2(-16*r-1))-1+ao,0.,1.);
        const double expected=display(reflected*visibility);
        const double previous=display(.44*(reflectance[row]*legacy[0]+legacy[1]));
        error=std::max(error,std::abs(actual-expected));legacyError=std::max(legacyError,std::abs(actual-previous));
        std::cout<<"f0="<<reflectance[row]<<" roughness="<<roughness[col]<<" actual="<<actual<<" unit="<<expected<<" legacy="<<previous<<'\n';
    }
    std::cout<<api<<' '<<mode<<(direct?" direct-error=":" unit-error=")<<error<<" legacy-error="<<legacyError<<"/255\n";
    check(direct?error<=1:error<2,direct?"Reflection change altered direct highlights":"Unit incoming radiance lost energy or authored smoothness");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
