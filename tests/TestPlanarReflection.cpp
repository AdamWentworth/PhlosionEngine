#include "engine/render/LegacyOpenGL.h"
#include "engine/render/WorldPlanarCapture.h"
#include "engine/render/IWorldLocalHaze.h"
#include <random>
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace engine::render;
namespace {
void check(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr int width=512,height=320;
constexpr std::array<unsigned char,4> white{255,255,255,255};
constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};

void scissorContracts() {
    std::mt19937 random(417);
    std::uniform_real_distribution<float> unit(0,1);
    unsigned samples=0,reduced=0;
    for(unsigned test=0;test<200;++test) {
        const float plane=(unit(random)-.5f)*20;
        const glm::vec3 eye((unit(random)-.5f)*20,plane+.15f+unit(random)*20,(unit(random)-.5f)*20);
        const float yaw=unit(random)*6.28f,pitch=(unit(random)-.5f)*2.8f,roll=(unit(random)-.5f)*1.8f;
        const glm::vec3 direction(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch));
        const auto right=glm::normalize(glm::cross(direction,glm::vec3(0,1,0)));
        const auto up=glm::normalize(glm::cross(right,direction))*std::cos(roll)+right*std::sin(roll);
        const int w=test%19==0?16383:test%23==0?1:137+int(unit(random)*1800);
        const int h=test%19==0?1:test%23==0?16383:93+int(unit(random)*1000);
        auto projection=glm::perspective(.5f+unit(random)*1.5f,float(w)/h,.05f,30.f+unit(random)*800);
        // Off-center projections include temporal jitter and asymmetric cameras.
        projection[2][0]=(unit(random)-.5f)*.2f;projection[2][1]=(unit(random)-.5f)*.2f;
        if(test%7==0)projection=glm::ortho(-15.f,15.f,-10.f,10.f,.05f,200.f);
        const auto vp=projection*glm::lookAt(eye,eye+direction,up);
        auto capture=makeWorldPlanarReflectionView(glm::value_ptr(vp),{eye.x,eye.y,eye.z},plane,w,h);
        const auto scissor=worldPlanarCaptureScissor(capture);
        check(scissor[0]>=0 && scissor[1]>=0 && scissor[2]<=capture.width && scissor[3]<=capture.heightPixels &&
              scissor[0]<scissor[2] && scissor[1]<scissor[3],"Invalid planar scissor");
        if((scissor[2]-scissor[0])*(scissor[3]-scissor[1])<capture.width*capture.heightPixels)++reduced;
        const auto inverse=glm::inverse(glm::dmat4(vp));
        // Independent oracle: intersect primary pixel rays with receiver heights,
        // then check every extreme normal offset and filter tap's bilinear support.
        for(int y=0;y<=24;++y)for(int x=0;x<=32;++x) {
            const auto unproject=[&](double z) {
                const auto p=inverse*glm::dvec4(x/16.-1,y/12.-1,z,1);
                return glm::dvec3(p)/p.w;
            };
            const auto a=unproject(-1),b=unproject(1);
            if(std::abs(b.y-a.y)<1e-10)continue;
            for(double offset:{-.119999,0.,.119999}) {
                const double t=(plane+offset-a.y)/(b.y-a.y);
                if(t<0 || t>1)continue;
                auto p=a+(b-a)*t;p.y=plane;
                const auto clip=glm::dmat4(vp)*glm::dvec4(p,1);
                if(clip.w<=.00001)continue;
                const glm::dvec2 base(.5+.5*clip.x/clip.w,.5-.5*clip.y/clip.w);
                for(int nx:{-1,1})for(int nz:{-1,1}) {
                    const auto uv=base+glm::dvec2(nx,nz)*.025;
                    if(uv.x<=0 || uv.x>=1 || uv.y<=0 || uv.y>=1)continue;
                    const glm::dvec2 extent(capture.width,capture.heightPixels);
                    const auto center=uv*extent;
                    const double blur=.02*.55*.55*1.182426*capture.heightPixels;
                    for(int tx:{-1,1})for(int ty:{-1,1}) {
                        const auto margin=.5*extent/double(std::min(capture.width,capture.heightPixels));
                        const auto texel=glm::clamp(center+glm::dvec2(tx,ty)*blur,margin,extent-margin)-glm::dvec2(.5);
                        for(int dx:{0,1})for(int dy:{0,1}) {
                            const int px=int(std::floor(texel.x))+dx,py=int(std::floor(texel.y))+dy;
                            if(px<0||py<0||px>=capture.width||py>=capture.heightPixels)continue;
                            check(px>=scissor[0] && px<scissor[2] && py>=scissor[1] && py<scissor[3],
                                  "Planar scissor excludes a possible receiver sample");
                            if(nx==-1 && nz==-1 && tx==-1 && ty==-1 && dx==0 && dy==0) {
                                const int lowW=(capture.width+3)/4,lowH=(capture.heightPixels+3)/4;
                                const auto lowBounds=worldPlanarHazeScissor(scissor,capture,lowW,lowH);
                                const int cellX=int(std::floor((px+.5)*lowW/capture.width-.5));
                                const int cellY=int(std::floor((py+.5)*lowH/capture.heightPixels-.5));
                                for(int hx:{0,1})for(int hy:{0,1}) {
                                    const int lx=std::clamp(cellX+hx,0,lowW-1),ly=std::clamp(cellY+hy,0,lowH-1);
                                    check(lx>=lowBounds[0] && lx<lowBounds[2] && ly>=lowBounds[1] && ly<lowBounds[3],
                                          "Haze scissor excludes an upsample tap");
                                    const int depthX=int((lx+.5)*capture.width/lowW),depthY=int((ly+.5)*capture.heightPixels/lowH);
                                    check(depthX>=scissor[0] && depthX<scissor[2] && depthY>=scissor[1] && depthY<scissor[3],
                                          "Haze upsample reads uncaptured depth");
                                }
                            }
                            ++samples;
                        }
                    }
                }
            }
        }
    }
    check(samples>100000 && reduced>30,"Scissor oracle did not exercise enough receivers/reduced views");
    WorldPlanarReflectionView invalid;invalid.width=257;invalid.heightPixels=161;
    check(worldPlanarCaptureScissor(invalid)==std::array<int,4>{0,0,257,161},"Singular view must fail open");
    const auto vp=glm::perspective(glm::radians(60.f),1.6f,.1f,100.f)*glm::lookAt(glm::vec3(0,3,0),glm::vec3(0,4,0),glm::vec3(0,0,1));
    const auto empty=makeWorldPlanarReflectionView(glm::value_ptr(vp),{0,3,0},0,512,320);
    check(worldPlanarCaptureScissor(empty)==std::array<int,4>{0,0,1,1},"Empty receiver domain should render one unused pixel");
    std::cout<<"Scissor oracle: "<<samples<<" bilinear samples; "<<reduced<<" reduced views\n";
}

void cpu() {
    scissorContracts();
    const auto vp=glm::perspective(glm::radians(55.f),1.6f,.1f,100.f)*glm::lookAt(glm::vec3(2,3,7),glm::vec3(0,0,0),glm::vec3(0,1,0));
    auto reflected=makeWorldPlanarReflectionView(glm::value_ptr(vp),{2,3,7},.25f,513,321);
    check(reflected.width==257&&reflected.heightPixels==161,"Odd reflection extent lost pixels");
    check(reflected.cameraPosition==std::array<float,3>{2,-2.5f,7},"Camera reflection plane mismatch");
    check(std::abs(packWorldPlanarReflection(reflected)[4][2]-161.f/257.f)<1e-6f,"Reflection filter aspect mismatch");
    auto matrix=glm::make_mat4(reflected.viewProjection.data());
    for (auto point : {glm::vec4(0,.25f,0,1),glm::vec4(3,.25f,-8,1)})
        check(glm::length(vp*point-matrix*point)<1e-5f,"Points on reflection plane changed projection");
    check(glm::length(matrix*glm::vec4(1,2,3,1)-vp*glm::vec4(1,-1.5f,3,1))<1e-5f,"Virtual object projection mismatch");
    auto twice=makeWorldPlanarReflectionView(reflected.viewProjection.data(),reflected.cameraPosition,.25f,512,320);
    for (int i=0;i<16;++i) check(std::abs(twice.viewProjection[i]-glm::value_ptr(vp)[i])<1e-5f,"Reflection is not involutory");
    bool rejected=false; try {makeWorldPlanarReflectionView(nullptr,{0,0,0},0,1,1);} catch (const std::invalid_argument&) {rejected=true;}
    check(rejected,"Null reflection matrix accepted");
}
}
int main(int argc,char** argv) try {
    cpu();
    if (argc==1) {std::cout<<"PASS planar projection contracts\n";return 0;}
    check(argc==4,"Expected API, mode, image path");
    const std::string api=argv[1],mode=argv[2];
    const bool haze=mode=="haze";
    const bool emissive=mode=="emissive"||mode=="emissive-rough"||mode=="emissive-off";
    check(haze||emissive||mode=="on"||mode=="off"||mode=="offscreen"||mode=="reversed"||mode=="dry"||mode=="clipped"||mode=="occluded"||mode=="reset"||mode=="extent-change","Unknown reflection test");
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
    Window window("Planar reflection contracts",width,height,api=="opengl"?Window::GraphicsApi::OpenGL:
        api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if (api=="opengl") {
        check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");
        renderer=engine::render::createLegacyOpenGLBackend();renderer->onResize(width,height);
    } else if (api=="vulkan") renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else if (api=="d3d12") renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    else throw std::runtime_error("Unknown API");
    auto* planar=dynamic_cast<IWorldPlanarReflection*>(renderer.get());
    auto* radiance=dynamic_cast<IWorldSceneRadiance*>(renderer.get());
    check(planar&&radiance,"Reflection interfaces absent");
    const glm::vec3 camera(0,2,6);
    const auto vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,60.f)*glm::lookAt(camera,glm::vec3(0,0,-1),glm::vec3(0,1,0));
    const float objectY=mode=="offscreen"?4.5f:1.5f;
    const auto reflected=makeWorldPlanarReflectionView(glm::value_ptr(vp),{camera.x,camera.y,camera.z},0,width,height);
    std::array<unsigned char,16*16*4> sign{};
    for (int y=0;y<16;++y) for(int x=0;x<16;++x) {
        const auto at=(y*16+x)*4;
        sign[at]=sign[at+1]=sign[at+2]=(x<3||y<3||y>=13||(y>=6&&y<10))?255:0;
        sign[at+3]=255;
    }
    WorldEnvironmentLighting hazeEnvironment;
    hazeEnvironment.enabled=true;hazeEnvironment.hazeDensity=.035f;hazeEnvironment.localHazeAlbedo=.9f;
    hazeEnvironment.rectLights[0]={{0,3,0},8,{2,0,0},{0,0,.2f},{30,0,0}};
    const auto draw=[&](bool floor,bool reverse,int blocker=0) {
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for (int i=0;i<4;++i) {
            auto& v=vertices[i];
            v.x=(i==0||i==3)?-1.f:1.f;
            v.y=floor?0.f:objectY+(i<2?-.6f:.6f);
            v.z=floor?(i<2?8.f:-12.f):-1.f;
            if (blocker) {
                v.x*=.7f; v.y=(blocker<0?-.5f:.75f)+(i<2?-.25f:.25f);
                v.z=blocker<0?3.f:.5f;
            }
            if (floor) v.x*=12;
            v.u=(i==0||i==3)?0.f:1.f;v.v=i<2?0.f:1.f;
            v.ny=floor?1.f:0.f;v.nz=floor?0.f:1.f;v.tx=1;v.a=1;
            v.r=floor?.04f:1.f;v.g=v.b=floor?.04f:.005f;
            if (blocker) {v.r=.005f;v.g=blocker>0?1.f:.005f;v.b=blocker<0?1.f:.005f;}
        }
        auto order=indices;if(reverse) for(int i=0;i<6;i+=3) std::swap(order[i+1],order[i+2]);
        IRenderBackend::WorldTextureData t;
        t.key=t.cacheKey="planar-white";t.rgba=white.data();t.width=t.height=1;
        t.textureSrgb=0;t.materialMode=2;t.metallicFactor=0;t.roughnessFactor=floor?.16f:1.f;
        t.environment.enabled=true;t.environment.ambientRadiance={.5f,.5f,.5f};
        t.environment.sunRadiance={0,0,0};t.environment.hazeDensity=0;t.environment.exposure=.6f;
        t.environment.surfaceWeatherEnabled=floor;t.environment.waterSurface=floor;
        t.environment.surfaceWetness=mode=="dry"?0.f:1.f;
        if(emissive) {
            t.environment.ambientRadiance={0,0,0};
            if(floor) t.roughnessFactor=mode=="emissive-rough"?.4f:.06f;
            else {
                t.emissiveFactorR=mode=="emissive-off"?0.f:3.f;
                t.emissiveKey=t.emissiveCacheKey="planar-sign";
                t.emissiveRgba=sign.data();t.emissiveWidth=t.emissiveHeight=16;
                t.emissiveWrapS=t.emissiveWrapT=33071;
            }
        }
        if(mode=="offscreen"&&!floor) {
            // A calibrated luminous target keeps the off-screen visibility
            // oracle above its existing contrast threshold with physical DFG.
            t.emissiveFactorR=1.f;
            t.emissiveKey=t.emissiveCacheKey="planar-offscreen-emitter";
            t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
        }
        t.cameraPosX=camera.x;t.cameraPosY=camera.y;t.cameraPosZ=camera.z;
        renderer->drawWorldIndexedMeshTextured(vertices.data(),4,order.data(),6,&t,glm::value_ptr(vp),width,height);
    };
    for(int frame=0;frame<4;++frame) {
        renderer->beginFrame(0,0,0,1);renderer->beginWorldSceneColorPass(width,height);
        radiance->setWorldSceneRadianceExposure(.6f);
        check(!planar->endWorldPlanarReflection(),"Prior view reflection capture leaked");
        if (mode!="off" && (mode!="reset" || frame==0)) {
            auto capture=reflected;
            if (mode=="extent-change" && frame%2==0) {capture.width=width;capture.heightPixels=height;}
            check(planar->beginWorldPlanarReflection(capture),"Reflection begin failed");
            check(!planar->beginWorldPlanarReflection(reflected),"Nested reflection accepted");
            draw(false,mode=="reversed");
            if (mode=="clipped"||mode=="occluded") draw(false,false,mode=="clipped"?-1:1);
            if(haze)drawWorldLocalHaze(*renderer,hazeEnvironment,capture.cameraPosition,capture.viewProjection.data(),capture.width,capture.heightPixels);
            check(planar->endWorldPlanarReflection(),"Reflection end failed");
        }
        draw(true,false);draw(false,mode=="reversed");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation()) window.swapBuffers();
    }
    renderer->shutdown();renderer.reset();
    int w=0,h=0,c=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(argv[3],&w,&h,&c,3),stbi_image_free);
    check(image&&w==width&&h==height,"Missing reflection image");
    const auto projected=vp*glm::vec4(0,-objectY,-1,1);
    const int x=int((projected.x/projected.w*.5f+.5f)*width);
    const int y=int((.5f-projected.y/projected.w*.5f)*height);
    check(x>3&&x<width-3&&y>3&&y<height-3,"Oracle reflection outside viewport");
    int red=0,green=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
        const auto at=((y+dy)*width+x+dx)*3;red+=image.get()[at];green+=image.get()[at+1];
    }
    const bool active=mode!="off"&&mode!="dry"&&mode!="reset"&&mode!="emissive-off";
    std::cout<<api<<' '<<mode<<" reflection pixel="<<x<<','<<y<<" red="<<red/25.f<<" green="<<green/25.f<<'\n';
    check(mode=="occluded"?green>red+250:active?red>green+250:std::abs(red-green)<75,"Reflected object absent or dry/disabled receiver reflects");
    if(mode=="offscreen") {
        auto real=vp*glm::vec4(0,objectY-.6f,-1,1);
        check(real.y/real.w>1,"Offscreen oracle object remains visible in main view");
    }
    std::cout<<"PASS planar reflection\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
