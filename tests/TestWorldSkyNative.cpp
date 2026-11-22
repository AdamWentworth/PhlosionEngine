#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/AtmosphereSky.h"
#include "engine/render/IWorldClouds.h"
#include <SDL.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include "data/sky_oracle.h"
using namespace engine::render;
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==2||argc==3,"Expected API");std::string api=argv[1];
    check(api=="d3d12"||api=="vulkan","Unknown API");SDL_SetMainReady();
    struct Quit{~Quit(){SDL_Quit();}}quit;constexpr int w=129,h=97;
    Window window("Sky numerical qualification",w,h,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> backend;
    if(api=="vulkan")backend=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),w,h,false);
    else backend=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),w,h,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*backend);auto& scopes=dynamic_cast<IWorldResourceScope&>(*backend);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*backend);
    const auto owner=scopes.createWorldResourceScope(64*1024*1024);check(owner!=0,"Sky resource scope");
    AtmosphereSky sky;sky.prewarm(*backend,owner);unsigned frame=0;double worst=0;
    for(const auto& row:skyOracle) {
        if(row[2]!=.7||row[1]!=.1||row[3]!=.4)continue;
        const double cosAz=(row[4]-std::sin(row[2])*row[3])/(std::cos(row[2])*std::sqrt(1-row[3]*row[3]));
        check(std::abs(cosAz)<1,"Invalid oracle ray");
        const glm::vec3 ray(float(std::sqrt(1-row[3]*row[3])*cosAz),float(row[3]),float(std::sqrt(1-row[3]*row[3])*std::sqrt(1-cosAz*cosAz)));
        const auto vp=glm::perspective(glm::radians(60.f),float(w)/h,.05f,1000.f)*glm::lookAt(glm::vec3(0),ray,glm::vec3(0,1,0));
        WorldTemporalView view{31,1,++frame,w,h,true};view.cameraValid=true;
        std::copy_n(glm::value_ptr(vp),16,view.viewProjection.begin());
        WorldEnvironmentLighting env;env.enabled=true;env.exposure=1;
        env.sunDirection={float(std::cos(row[2])),float(std::sin(row[2])),0};
        env.sunAngularRadius=env.sunHaloStrength=env.moonAngularRadius=env.starIntensity=env.cloudCoverage=0;
        env.sky.turbidity=float(row[0]);env.sky.groundReflectance={.1f,.1f,.1f};
        backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(w,h);radiance.setWorldSceneRadianceExposure(1);
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),w,h),"Sky empty depth");
        check(surface.endWorldSurfaceBuffer(),"Sky depth close");check(surface.beginWorldTemporalScene(view),"Sky temporal begin");
        sky.draw(*backend,owner,env,{0,0,0},glm::value_ptr(vp),w,h);
        check(surface.resolveWorldTemporalScene(),"Sky resolve");check(surface.captureWorldTemporalScene(),"Sky capture");
        backend->endWorldSceneColorPass();backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldTemporalScene();check(image.color.size()==w*h,"Sky readback size");
        const auto color=image.color[(h/2)*w+w/2];
        for(unsigned c=0;c<3;++c) {
            const double expected=row[c+5]*100,error=std::abs(color[c]-expected)/std::max(expected,.001);
            worst=std::max(worst,error);
            std::cout<<"T="<<row[0]<<" channel="<<c<<" actual="<<color[c]<<" expected="<<expected<<"\n";
            check(std::isfinite(color[c])&&error<.025,"Native sky differs from public independent reference");
        }
    }

    check(frame==5,"Sky oracle coverage");
    auto& clouds=dynamic_cast<IWorldClouds&>(*backend);
    std::array<unsigned char,4> white{255,255,255,255};
    const auto render=[&](WorldEnvironmentLighting env,glm::vec3 direction,bool prepared=false) {
        const auto vp=glm::perspective(glm::radians(60.f),float(w)/h,.05f,1000.f)*
            glm::lookAt(glm::vec3(0),direction,glm::vec3(0,0,1));
        WorldTemporalView view{32,1,++frame,w,h,true};view.cameraValid=true;
        std::copy_n(glm::value_ptr(vp),16,view.viewProjection.begin());
        backend->beginFrame(0,0,0,1);backend->beginWorldSceneColorPass(w,h);radiance.setWorldSceneRadianceExposure(1);
        if(prepared) {
            WorldCloudView c;c.owner=owner;c.view=view;c.quality=0;c.environment=env;
            c.weather.key="sky-qualification-white";c.weather.rgba=white.data();c.weather.width=c.weather.height=1;c.weather.textureSrgb=0;
            c.noise=c.weather;clouds.prepareWorldClouds(c);
        }
        check(surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),w,h),"Celestial depth begin");
        check(surface.endWorldSurfaceBuffer(),"Celestial depth end");check(surface.beginWorldTemporalScene(view),"Celestial temporal");
        sky.draw(*backend,owner,env,{0,0,0},glm::value_ptr(vp),w,h);
        check(surface.resolveWorldTemporalScene()&&surface.captureWorldTemporalScene(),"Celestial capture");
        backend->endWorldSceneColorPass();backend->endFrame();if(!backend->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldTemporalScene();check(image.color.size()==w*h,"Celestial image size");
        for(const auto& px:image.color)for(unsigned c=0;c<3;++c)check(std::isfinite(px[c])&&px[c]>=0,"Celestial finite HDR");
        clouds.clearWorldClouds();return image.color;
    };
    const auto difference=[](const auto& a,const auto& b) {
        double total=0;for(std::size_t i=0;i<a.size();++i)for(unsigned c=0;c<3;++c)total+=std::abs(a[i][c]-b[i][c]);return total;
    };
    WorldEnvironmentLighting celestial;celestial.enabled=true;celestial.exposure=1;
    celestial.sunDirection={0,1,0};celestial.sunRadiance={10,10,10};celestial.sunAngularRadius=0;
    const glm::vec3 up(0,1,0);
    auto base=render(celestial,up);celestial.sunAngularRadius=.02f;
    const auto solar=render(celestial,up);
    check(solar[(h/2)*w+w/2][0]>base[(h/2)*w+w/2][0]+1,"Solar disk missing in scene-linear output");
    const auto packedDirect=packWorldEnvironment(celestial);
    celestial.sky.celestialColorWeight=1;celestial.sky.solarDiskColor={1,8,2};
    const auto coloredSolar=render(celestial,up);
    check(coloredSolar[(h/2)*w+w/2][1]>coloredSolar[(h/2)*w+w/2][0]*2,
          "Independent sky sun color ignored");
    check(packWorldEnvironment(celestial)==packedDirect,"Sky color changed direct illumination");
    celestial.sunRadiance={100,200,300};
    check(difference(render(celestial,up),coloredSolar)<.05,"Direct sun leaks into independent sky disk");
    celestial.sunRadiance={10,10,10};celestial.sky.celestialColorWeight=0;
    celestial.sky.indirect=true;celestial.sky.indirectTurbidity=celestial.sky.turbidity;
    check(difference(render(celestial,up),base)<.05,"Indirect sky must exclude the solar disk");
    celestial.sky.indirect=false;celestial.sunAngularRadius=0;celestial.sunDirection={0,-1,0};celestial.sunRadiance={0,0,0};
    celestial.sky.domeColor={.1f,.1f,.1f};celestial.sky.skylightColor={.2f,.3f,.4f};
    base=render(celestial,up);celestial.moonDirection={0,1,0};celestial.moonRadiance={1,1,1};celestial.moonAngularRadius=.02f;
    const auto moon=render(celestial,up);check(difference(moon,base)>.1,"Textured moon missing");
    celestial.moonAngularRadius=0;celestial.moonRadiance={0,0,0};celestial.starIntensity=1;
    const auto stars=render(celestial,up);check(difference(stars,base)>.1,"Night stars/galaxy missing");
    celestial.timeOfDayHours=8;check(difference(render(celestial,up),stars)>.1,"Stars do not follow the sky clock");
    celestial.starIntensity=0;celestial.sky.cirrusOpacity=.8f;
    const auto cirrus=render(celestial,up,true);
    celestial.sky.skylightColor={.8f,.3f,.4f};
    check(difference(render(celestial,up,true),cirrus)>.1,"Cirrus ignores prepared skylight color");
    celestial.sky.cirrusShadowFalloff=0;render(celestial,up,true);
    celestial.sky.cirrusOpacity=0;
    check(difference(render(celestial,up,true),render(celestial,up,false))<.05,"Sky model binding changed the clear dome");
    std::cout<<"PASS celestial HDR, indirect exclusion, night rotation and prepared cirrus lighting\n";
    check(scopes.requestWorldResourceScopeRelease(owner),"Sky release");
    backend->shutdown();std::cout<<"PASS "<<api<<" analytic sky, max relative error="<<worst<<"\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
