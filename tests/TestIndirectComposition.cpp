#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
constexpr int width=512,height=320;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
using Color=std::array<double,3>;
Color display(Color rgb) {
    constexpr double input[3][3]={{.59719,.35458,.04823},{.076,.90834,.01566},{.0284,.13383,.83777}};
    constexpr double output[3][3]={{1.60475,-.53108,-.07367},{-.10208,1.10813,-.00605},{-.00327,-.07276,1.07602}};
    Color film{},result{};
    for(int i=0;i<3;++i) {
        double v=0;for(int j=0;j<3;++j)v+=input[i][j]*rgb[j];
        film[i]=(v*(v+.0245786)-.000090537)/(v*(.983729*v+.432951)+.238081);
    }
    for(int i=0;i<3;++i) {
        double v=0;for(int j=0;j<3;++j)v+=output[i][j]*film[j];
        v=std::clamp(v,0.,1.);result[i]=255*(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055);
    }
    return result;
}
// Expanded polynomial from the published integral fit, independent of shader Horner form.
double visibility(double ao,double albedo) {
    return std::clamp(std::max(ao,(2.0404*albedo-.3324)*std::pow(ao,3)
        +(.6417-4.7951*albedo)*ao*ao+(2.7552*albedo+.6903)*ao),0.,1.);
}
}
int main(int argc,char** argv) try {
    check(argc==4,"Expected API, mode, image path");
    const std::string api=argv[1],mode=argv[2];
    check(mode=="bounce"||mode=="direct"||mode=="emission","Unknown indirect composition case");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
    std::filesystem::remove(argv[3]);
#if defined(_MSC_VER)
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    _putenv_s("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
#else
    setenv("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3],1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_FRAME","2",1);
    setenv("PHLOSION_BACKEND_SCREENSHOT_DEFER","0",1);
#endif
    Window window("Indirect composition",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if(api=="d3d12")renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown API");
    auto* radiance=dynamic_cast<IWorldSceneRadiance*>(renderer.get());
    auto* probes=dynamic_cast<IWorldDiffuseProbes*>(renderer.get());
    check(radiance&&probes,"Missing indirect interfaces");
    WorldDiffuseProbeUniforms packet{};
    packet[0]={-2,-2,-2,1};packet[1]={4,4,4,1};packet[2]={2,2,2,0};
    for(int p=0;p<8;++p) {
        const int at=3+p*int(kDiffuseProbeStride);
        const float sh=3.14159265f/.282094792f;
        packet[at]={sh,sh,sh,1};
        for(int f=0;f<6;++f)packet[at+9+f]={100,10000,0,0};
    }
    constexpr std::array<Color,4> colors{{{.1,.1,.1},{.5,.5,.5},{.8,.8,.8},{.8,.2,.05}}};
    constexpr unsigned char aoBytes[]{0,64,128,255};
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<std::array<unsigned char,4>,4> aoTextures{};
    std::array<std::string,4> keys;
    for(int c=0;c<4;++c){aoTextures[c]={aoBytes[c],aoBytes[c],aoBytes[c],255};keys[c]="indirect-ao-"+std::to_string(c);}
    for(int frame=0;frame<4;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        radiance->setWorldSceneRadianceExposure(.6f);
        if(mode=="bounce")probes->setWorldDiffuseProbes(packet);
        for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
            const float x=-.75f+col*.5f,y=.75f-row*.5f;
            std::array<IRenderBackend::WorldMeshVertex,4> v{};
            for(int j=0;j<4;++j){v[j].x=x+(j==0||j==3?-.2f:.2f);v[j].y=y+(j<2?-.2f:.2f);
                v[j].nz=1;v[j].tx=1;v[j].r=float(colors[row][0]);v[j].g=float(colors[row][1]);v[j].b=float(colors[row][2]);v[j].a=1;}
            IRenderBackend::WorldTextureData t;
            t.key=t.cacheKey="indirect-white";t.rgba=white.data();t.width=t.height=1;t.textureSrgb=0;
            t.materialMode=2;t.metallicFactor=0;t.roughnessFactor=1;
            t.occlusionKey=t.occlusionCacheKey=keys[col].c_str();t.occlusionRgba=aoTextures[col].data();t.occlusionWidth=t.occlusionHeight=1;
            t.cameraPosX=x;t.cameraPosY=y;t.cameraPosZ=3;
            t.environment.enabled=true;t.environment.exposure=.6f;
            if(mode=="direct"){t.environment.sunDirection={0,0,1};t.environment.sunRadiance={3.14159265f,3.14159265f,3.14159265f};}
            if(mode=="emission") {t.emissiveKey=t.emissiveCacheKey="indirect-white";t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
                t.emissiveFactorR=.4f;t.emissiveFactorG=.4f;t.emissiveFactorB=.4f;}
            renderer->drawWorldIndexedMeshTextured(v.data(),4,indices.data(),6,&t,identity.data(),width,height);
        }
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(pixels&&w==width&&h==height,"Missing native composition image");
    double maxError=0;
    for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
        Color expected{};
        for(int k=0;k<3;++k)expected[k]=mode=="bounce"?.96*colors[row][k]*visibility(aoBytes[col]/255.,colors[row][k]):
            mode=="direct"?.96*colors[row][k]+.01:.4;
        expected=display(expected);
        for(int k=0;k<3;++k) {
            const auto index=((row*80+40)*width+col*128+64)*3+k;
            maxError=std::max(maxError,std::abs(pixels.get()[index]-expected[k]));
        }
    }
    std::cout<<api<<' '<<mode<<" max display error="<<maxError<<"/255\n";
    check(maxError<2,"Native material differs from independent indirect/direct/emission oracle");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
