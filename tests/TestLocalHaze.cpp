#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldLocalShadow.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
int width=320,height=200;
constexpr std::array<unsigned char,4> white{255,255,255,255};
constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
void cpu() {
    IRenderBackend::WorldTextureData pass;pass.materialMode=247;
    pass.materialRect0U=321;pass.materialRect0V=201;pass.materialRect0W=81;pass.materialRect0H=51;
    const auto packed=engine::render::d3d12_internal::makeWorldPsConstants(&pass,1,2);
    check(packed.materialRect0U==321&&packed.materialRect0V==201&&packed.materialRect0W==81&&packed.materialRect0H==51,
        "D3D12 material packing overwrote haze viewport dimensions");
    WorldEnvironmentLighting e;e.localHazeAlbedo=.9f;e.localHazeAnisotropy=.25f;
    check(packWorldEnvironment(e)==WorldEnvironmentUniforms{},"Disabled environment leaked haze");
    e.enabled=true;
    check(packWorldEnvironment(e)[18]==std::array<float,4>{0,0,.9f,.25f},"Haze requires wet material");
    e.surfaceWeatherEnabled=true;e.waterSurface=true;e.surfaceAbsorption=.6f;
    check(packWorldEnvironment(e)[18]==std::array<float,4>{.6f,1,.9f,.25f},"Haze overwrote weather channels");
    e.localHazeAlbedo=5;e.localHazeAnisotropy=-1;
    check(packWorldEnvironment(e)[18][2]==1&&packWorldEnvironment(e)[18][3]==-.8f,"Unbounded haze energy/phase");
    e.localHazeAlbedo=std::numeric_limits<float>::quiet_NaN();
    check(packWorldEnvironment(e)[18][2]==0,"Nonfinite scattering reached shader");
}
// Independent area quadrature and fine view integration in a homogeneous
// medium, including phase variation across the source instead of its centroid.
double reference(double wallZ,double g) {
    double sum=0;constexpr int steps=4000,nu=80,nv=8;
    const double ds=(6-wallZ)/steps,da=1.6/(nu*nv),sigma=.035;
    for(int s=0;s<steps;++s) {
        const double z=6-(s+.5)*ds,d2=9+z*z;
        if(d2>=64) continue;
        const double cutoff=1-d2*d2/(64*64);
        double omega=0;
        for(int u=0;u<nu;++u) for(int v=0;v<nv;++v) {
            const double x=-2+(u+.5)*4/nu,dz=-.2+(v+.5)*.4/nv-z;
            const double r2=x*x+9+dz*dz;
            const double cosine=-dz/std::sqrt(r2);
            const double denominator=1+g*g-2*g*cosine;
            const double phase=(1-g*g)/(4*3.141592653589793*denominator*std::sqrt(denominator));
            omega+=3*da*phase/(r2*std::sqrt(r2));
        }
        sum+=omega*cutoff*cutoff*std::exp(-sigma*(6-z+std::sqrt(d2)))*sigma*.9*ds;
    }
    return sum*30;
}
std::array<double,3> display(std::array<double,3> rgb) {
    for(auto& x:rgb) x/=.6;
    const auto dot=[&](std::array<double,3> row){return row[0]*rgb[0]+row[1]*rgb[1]+row[2]*rgb[2];};
    rgb={dot({.59719,.35458,.04823}),dot({.076,.90834,.01566}),dot({.0284,.13383,.83777})};
    for(auto& x:rgb) x=(x*(x+.0245786)-.000090537)/(x*(.983729*x+.432951)+.238081);
    rgb={dot({1.60475,-.53108,-.07367}),dot({-.10208,1.10813,-.00605}),dot({-.00327,-.07276,1.07602})};
    for(auto& x:rgb) {x=std::clamp(x,0.,1.);x=255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055);}
    return rgb;
}
}
int main(int argc,char** argv) try {
    cpu();if(argc==1){std::cout<<"PASS local haze packing and disabled contracts\n";return 0;}
    check(argc==4,"Expected API, mode, output");const std::string api=argv[1];
    std::string mode=argv[2];const bool reduced=mode.starts_with("reduced-");if(reduced)mode=mode.substr(8);
    check(mode=="on"||mode=="off"||mode=="zero-density"||mode=="source-off"||mode=="sky"||mode=="back"||mode=="blocked"||mode=="foreground"||mode=="blue"||mode=="both"||mode=="odd"||mode=="edge"||(reduced&&mode=="repeat"),"Unknown haze case");
    if(mode=="odd"){width=321;height=201;}
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}} quit;
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());std::filesystem::remove(argv[3]);
#if defined(_MSC_VER)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");_putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3],1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Local haze contracts",width,height,api=="opengl"?Window::GraphicsApi::OpenGL:
        api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="opengl") {check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");
        renderer=std::make_unique<OpenGLRenderBackend>();renderer->onResize(width,height);}
    else if(api=="vulkan") renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if(api=="d3d12") renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown API");
    auto* shadows=dynamic_cast<IWorldLocalShadow*>(renderer.get());
    auto* hdr=dynamic_cast<IWorldSceneRadiance*>(renderer.get());check(shadows&&hdr,"Missing interfaces");
    shadows->prewarmWorldLocalShadows();
    WorldEnvironmentLighting e;e.enabled=true;e.exposure=1;e.hazeDensity=mode=="zero-density"?0.f:.035f;
    e.localHazeAlbedo=mode=="off"?0.f:.9f;
    e.localHazeDeferred=reduced;
    e.localHazeAnisotropy=mode=="both"?.25f:mode=="blue"?-.25f:0.f;
    e.rectLights[0]={{0,3,0},8,{2,0,0},{0,0,.2f},{30,0,0}};
    if(mode=="source-off") e.rectLights[0].radiance={};
    if(mode=="sky") {e.skyDraw=true;e.skyIntensity=0;e.sunAngularRadius=0;e.nightSkyRadiance={};}
    if(mode=="back") e.rectLights[0].halfV[2]=-.2f;
    if(mode=="blue") e.rectLights[0].radiance={0,0,30};
    if(mode=="both") {e.rectLights[1]=e.rectLights[0];e.rectLights[1].radiance={0,0,30};}
    const auto vp=glm::perspective(glm::radians(50.f),float(width)/height,.05f,50.f)*
        glm::lookAt(glm::vec3(0,0,6),glm::vec3(0),glm::vec3(0,1,0));
    const auto draw=[&](bool blocker,const float* matrix,bool foregroundEdge=false) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(int i=0;i<4;++i) {auto& v=vertices[i];v.x=(i==0||i==3)?-12.f:12.f;
            v.y=blocker?1.5f:(i<2?-10.f:10.f);v.z=blocker?(i<2?20.f:-20.f):(mode=="foreground"?5.9f:-4.f);
            if(foregroundEdge){v.x=std::max(v.x,0.f);v.z=5.9f;}
            v.a=1;v.ny=blocker?1.f:0.f;v.nz=blocker?0.f:1.f;v.tx=1;}
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="haze-black";t.rgba=white.data();t.width=t.height=1;
        t.textureSrgb=0;t.materialMode=0;t.vertexColorMulR=t.vertexColorMulG=t.vertexColorMulB=0;
        t.environment=e;t.cameraPosX=t.cameraPosY=0;t.cameraPosZ=6;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,matrix,width,height);
    };
    for(int frame=0;frame<4;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);hdr->setWorldSceneRadianceExposure(1);
        drawWorldRectShadows(*shadows,e,[&](const auto& view){if(mode=="blocked") draw(true,view.viewProjection.data());});
        draw(false,glm::value_ptr(vp));
        if(mode=="edge")draw(false,glm::value_ptr(vp),true);
        if(reduced)drawWorldLocalHaze(*renderer,e,{0,0,6},glm::value_ptr(vp),width,height);
        if(mode=="repeat")drawWorldLocalHaze(*renderer,e,{0,0,6},glm::value_ptr(vp),width,height);
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation()) window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();int w=0,h=0,c=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(pixels&&w==width&&h==height,"Missing haze image");
    if(mode=="edge") {
        for(int y=height/2-10;y<height/2+10;++y)for(int x=width/2;x<width/2+20;++x)
            check(pixels.get()[(y*width+x)*3]<4,"Distant haze leaked across foreground silhouette");
        check(pixels.get()[(height/2*width+width/2-8)*3]>20,"Depth-aware composition removed background haze");
        std::cout<<"PASS sharp foreground silhouette\n";return 0;
    }
    double rgb[3]{};for(int y=height/2-2;y<=height/2+2;++y)for(int x=width/2-2;x<=width/2+2;++x)
        for(int channel=0;channel<3;++channel) rgb[channel]+=pixels.get()[(y*width+x)*3+channel]/25.;
    const bool active=mode=="on"||mode=="sky"||mode=="foreground"||mode=="blue"||mode=="both"||mode=="odd"||mode=="repeat";
    const double radiance=active?reference(mode=="foreground"?5.9:mode=="sky"?-20:-4,e.localHazeAnisotropy)*(mode=="repeat"?2:1):0;
    const auto expected=display({mode=="blue"?0:radiance,0,mode=="blue"||mode=="both"?radiance:0});
    std::cout<<api<<' '<<mode<<" RGB "<<rgb[0]<<','<<rgb[1]<<','<<rgb[2]
        <<" reference "<<expected[0]<<','<<expected[1]<<','<<expected[2]<<'\n';
    for(int channel=0;channel<3;++channel) {
        check(std::abs(rgb[channel]-expected[channel])<3.5,"Haze differs from independent area/volume integration or leaks through blocker");
    }
    std::cout<<"PASS local haze\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
