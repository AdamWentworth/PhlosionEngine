#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace engine::render;
constexpr int width=512,height=320;
// Independent scalar reference: neutral RGB remains neutral through ACES matrices.
double displayByte(double radiance,double exposure) {
    double x=radiance*exposure/.6;
    x=std::clamp((x*(x+.0245786)-.000090537)/(x*(.983729*x+.4329510)+.238081),0.,1.);
    return 255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055);
}
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
int main(int argc,char** argv) try {
    if(argc!=4)return 2;
    const std::string api=argv[1],mode=argv[2];
    require(mode=="ramp"||mode=="bloom"||mode=="feedback","Unknown radiance case");
    SDL_SetMainReady();struct Quit {~Quit(){SDL_Quit();}} quit;
    std::filesystem::create_directories(std::filesystem::path(argv[3]).parent_path());
    std::filesystem::remove(argv[3]);
    const auto environment=[](const char* key,const char* value) {
#if defined(_MSC_VER)
        _putenv_s(key,value);
#else
        setenv(key,value,1);
#endif
    };
    environment("PHLOSION_BACKEND_SCREENSHOT_PATH",argv[3]);
    environment("PHLOSION_BACKEND_SCREENSHOT_FRAME","2");
    environment("PHLOSION_BACKEND_SCREENSHOT_DEFER","0");
    Window window("Scene radiance contracts",width,height,api=="opengl"?Window::GraphicsApi::OpenGL:
        api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="opengl") {
        require(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");
        renderer=std::make_unique<OpenGLRenderBackend>();renderer->onResize(width,height);
    } else if(api=="vulkan") renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if(api=="d3d12") renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown renderer");
    auto* radiance=dynamic_cast<IWorldSceneRadiance*>(renderer.get());
    auto* bloom=dynamic_cast<IWorldBloom*>(renderer.get());
    require(radiance&&bloom,"Missing scene post interfaces");
    for(float invalid : {-1.f,129.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        bool rejected=false;
        try {radiance->setWorldSceneRadianceExposure(invalid);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid exposure accepted");
    }
    constexpr std::array<unsigned char,4> black{0,0,0,255},white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    constexpr std::array<float,4> strengths{.25f,1.f,4.f,16.f};
    const float exposure=mode=="bloom"?.6f:.12f;
    for(int frame=0;frame<4;++frame) {
        radiance->setWorldSceneRadianceExposure(2.f);
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        require(radiance->worldSceneRadianceExposure()==0,"World pass retained prior exposure");
        radiance->setWorldSceneRadianceExposure(exposure);
        bloom->setWorldBloomStrength(mode=="bloom"?.65f:0.f);
        for(int row=0;row<5;++row)for(int col=0;col<4;++col) {
            const float x=-.75f+col*.5f,y=.8f-row*.4f;
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            for(int v=0;v<4;++v) {
                auto& point=vertices[static_cast<std::size_t>(v)];
                point.x=x+((v==0||v==3)?-.16f:.16f);point.y=y+(v<2?-.1f:.1f);
                point.z=0;point.nz=1;point.tx=1;point.r=point.g=point.b=1;
                point.a=row>=2?.5f:1.f;
            }
            IRenderBackend::WorldTextureData texture;
            texture.key=texture.cacheKey="radiance-black";texture.rgba=black.data();texture.width=texture.height=1;
            texture.emissiveKey=texture.emissiveCacheKey="radiance-white";texture.emissiveRgba=white.data();
            texture.emissiveWidth=texture.emissiveHeight=1;texture.materialMode=2;texture.normalScale=0;
            texture.metallicFactor=0;texture.environment.enabled=true;texture.environment.exposure=exposure;
            texture.cameraPosY=0;texture.cameraPosZ=1;texture.cameraForwardY=0;texture.cameraForwardZ=-1;
            const float energy=strengths[static_cast<std::size_t>(col)]*((row==1||row==3)?.5f:1.f);
            texture.emissiveFactorR=texture.emissiveFactorG=texture.emissiveFactorB=energy;
            texture.alphaMode=row==0?0:2;texture.blendMode=row==1?1:row==3?2:0;
            texture.dualSourceBlendEnabled=row==4?1:0;
            for(int layer=0;layer<(row==0?1:2);++layer)
                renderer->drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
                    &texture,identity.data(),width,height);
        }
        if(mode=="feedback") {
            ScreenFeedbackFrame feedback;feedback.sequence=1;feedback.width=width;feedback.height=height;
            feedback.seed=frame==0;feedback.maskKey="radiance-feedback-mask";feedback.maskRgba=white.data();
            feedback.maskWidth=feedback.maskHeight=1;
            renderer->endWorldSceneColorPass(feedback);
        } else renderer->endWorldSceneColorPass();
        renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,channels=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load(argv[3],&w,&h,&channels,3),stbi_image_free);
    require(pixels&&w==width&&h==height,"Missing radiance image");
    double error=0;
    for(int row=0;row<5;++row)for(int col=0;col<4;++col) {
        const auto expected=displayByte(strengths[static_cast<std::size_t>(col)]*(row>=2?.75:1.),exposure);
        const int x=64+col*128,y=32+row*64;
        for(int c=0;c<3;++c)error=std::max(error,std::abs(pixels.get()[(y*width+x)*3+c]-expected));
    }
    std::cout<<"Maximum display byte error: "<<error<<'\n';
    require(error<=3,"Radiance/clamping, blend-before-map or display transform mismatch");
    if(mode=="bloom") {
        const auto halo=[&](int col){return pixels.get()[(14*width+64+col*128)*3];};
        require(halo(0)<=1&&halo(2)>halo(1)+8&&halo(3)>halo(2)+8,"Bloom lost source intensity above 1");
    }
    std::cout<<"PASS "<<api<<' '<<mode<<" emission >1, opaque/alpha/additive/premultiplied/dual-source composition, reset; max error="<<error<<'\n';
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
