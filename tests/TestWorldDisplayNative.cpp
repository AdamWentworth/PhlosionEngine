#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include <SDL.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include <numeric>
#include "fixtures/WorldDisplayReference.h"
using namespace engine::render;
namespace {
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void close(float actual,float expected,float eps,const char* s){
    if(std::abs(actual-expected)>eps*std::max(1.f,std::abs(expected))) {
        std::cerr<<s<<" actual="<<actual<<" expected="<<expected<<'\n';throw std::runtime_error(s);
    }
}
}
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==3,"renderer and output directory required");
    const std::string api=argv[1];check(api=="d3d12"||api=="vulkan","invalid API");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    int width=320,height=240;
    Window window("HDR display qualification",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    auto& radiance=dynamic_cast<IWorldSceneRadiance&>(*renderer);
    auto& bloom=dynamic_cast<IWorldBloom&>(*renderer);
    WorldDisplaySettings settings;
    constexpr std::array<std::uint32_t,6> indices{0,2,1,0,3,2};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){auto& v=vertices[i];v.x=i==0||i==3?-1.f:1.f;v.y=i<2?-1.f:1.f;
        v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;v.r=v.g=v.b=v.a=1;v.nz=v.tx=v.tw=v.weight0=1;}
    unsigned serial=0;
    const auto frame=[&](WorldTemporalView view,std::array<float,3> color,int pattern=0,float bloomGain=1.f){
        view.width=width;view.height=height;
        constexpr std::array<unsigned char,4> white{255,255,255,255};
        IRenderBackend::WorldTextureData material;material.key=material.cacheKey="display-white";
        material.rgba=white.data();material.width=material.height=1;material.materialMode=2;material.textureSrgb=0;
        if(pattern==7){
            material.emissiveKey=material.emissiveCacheKey="display-emission";
            material.emissiveRgba=white.data();material.emissiveWidth=material.emissiveHeight=1;
            material.emissiveFactorR=material.emissiveFactorG=material.emissiveFactorB=1;
        }
        WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
        WorldMotionSample motion{&previous,1,true};
        renderer->beginFrame(0,0,0,0);renderer->beginWorldSceneColorPass(width,height);
        radiance.setWorldSceneRadianceExposure(.6f);radiance.setWorldDisplaySettings(settings);bloom.setWorldBloomStrength(bloomGain);
        check(surface.beginWorldSurfaceBuffer(worldMotionIdentity.data(),width,height),"surface begin");
        check(surface.setWorldSurfaceMotion(worldMotionIdentity,{&motion,1}),"motion");
        if(pattern!=4)renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&material,worldMotionIdentity.data(),width,height);
        check(surface.endWorldSurfaceBuffer(),"surface end");check(surface.beginWorldTemporalScene(view),"temporal begin");
        material.materialMode=255;if(pattern==4)material.alphaMode=2;
        if(pattern>=9&&pattern<=11){
            material.materialMode=2;material.metallicFactor=1;material.roughnessFactor=.4f;
            material.environment.enabled=true;material.environment.sunDirection={0,.5f,.8660254f};
            material.environment.sunRadiance={1,1,1};
            material.environment.globalSpecularTint=pattern==9?std::array<float,3>{1,1,1}:
                pattern==10?std::array<float,3>{.2f,.5f,.8f}:std::array<float,3>{0,0,0};
        }
        if(pattern==6){material.alphaMode=2;material.blendMode=3;}
        std::vector<IRenderBackend::WorldMeshVertex> mesh;std::vector<std::uint32_t> tris;
        for(int x=0;x<width;++x){
            auto c=color;
            if(pattern==12){const float ramp=std::clamp(.35f+.012f*(x+.5f-width*.5f-view.jitterPixels[0]),.01f,.9f);c={ramp,ramp,ramp};}
            if(pattern==1&&x>int(width*.98f))c={1000,1000,1000};
            if(pattern==2)c=x==width/2?std::array<float,3>{20,5,14}:std::array<float,3>{0,0,0};
            if(pattern==3)c=x<width/2?std::array<float,3>{8,.1f,2}:std::array<float,3>{.1f,2,8};
            auto base=std::uint32_t(mesh.size());
            for(int i=0;i<4;++i){auto v=vertices[i];v.x=2.f*(x+(i==1||i==2?1:0))/width-1.f;v.r=c[0];v.g=c[1];v.b=c[2];mesh.push_back(v);}
            for(auto i:indices)tris.push_back(base+i);
        }
        for(int copy=0;copy<(pattern==6?32:1);++copy)
            renderer->drawWorldIndexedMeshTextured(mesh.data(),mesh.size(),tris.data(),tris.size(),&material,worldMotionIdentity.data(),width,height);
        if(settings.heatHaze.strength>0)check(surface.resolveWorldHeatHaze(),"heat haze resolve");
        check(surface.resolveWorldTemporalScene(),"resolve");check(surface.captureWorldTemporalScene(),"capture");
        renderer->endWorldSceneColorPass();renderer->endFrame();if(!renderer->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldTemporalScene();check(image.display.size()==std::size_t(width*height),"display output");
        check(image.meter.size()==65536&&image.exposure.size()==1&&image.histogram.size()==512&&image.displayLut.size()==110592,"diagnostic dimensions");
        for(const auto* data:{&image.display,&image.meter,&image.exposure,&image.displayLut})
            for(const auto& p:*data)for(float f:p)check(std::isfinite(f),"nonfinite display data");
        // Double-precision bloom/ACES oracle, including compact HDR rounding.
        // This catches bloom settings delivered after the display pass (VK regression).
        if(serial==1) {
            constexpr float expected[]{.449723109f,.450151465f,.449315147f};
            for(int c=0;c<3;++c)close(image.display[(height/2)*width+width/2][c],expected[c],.002f,"bloom oracle");
        }
        // Independent integer histogram from the actual meter; detects GPU grouping,
        // fixed-point packing, fractional bins, dropped/duplicated samples and overflow.
        std::array<std::uint64_t,512> histogram{};
        for(std::size_t i=4;i<image.meter.size();++i){
            const double bin=std::clamp((std::log2(std::max(double(image.meter[i][0]),1e-30))+8.)*511./28.,0.,511.);
            const auto low=std::min(std::size_t(bin),std::size_t(510));
            const auto high=std::uint64_t(65536.*(bin-low));histogram[low]+=65536-high;histogram[low+1]+=high;
        }
        std::uint64_t total=0;
        for(unsigned i=0;i<512;++i){
            auto actual=std::uint64_t(image.histogram[i][0])+(std::uint64_t(image.histogram[i][1])<<16);
            total+=actual;
            check(std::abs(double(actual)-double(histogram[i]))<=double(65536)*16,"histogram bin mismatch");
        }
        check(total==65532ull*65536,"histogram total");
        double low=settings.lowPercentile*65532.,high=settings.highPercentile*65532.,sum=0,count=0;
        for(unsigned i=0;i<512;++i){double n=double(histogram[i])/65536,skip=std::min(n,low);n-=skip;low-=skip;high-=skip;n=std::min(n,high);high-=n;sum+=(i+.5)*n;count+=n;}
        const double expected=std::clamp(std::exp2(sum/count*28./511.-8.),.125*std::exp2(double(settings.minEV)),.125*std::exp2(double(settings.maxEV)));
        close(image.exposure[0][2],float(expected),.0002f,"trimmed luminance");
        std::ofstream data(root/(std::to_string(serial)+".bin"),std::ios::binary);
        for(const auto* v:{&image.display,&image.displayLut,&image.meter,&image.histogram,&image.exposure,&image.color})
            data.write(reinterpret_cast<const char*>(v->data()),std::streamsize(v->size()*sizeof((*v)[0])));
        std::ofstream meta(root/(std::to_string(serial)+".txt"));
        meta<<width<<' '<<height<<' '<<view.identity<<' '<<view.frame<<' '<<view.deltaSeconds<<' '<<pattern<<'\n';
        std::cout<<"frame "<<serial++<<" view "<<view.identity<<" measured "<<image.exposure[0][2]<<" adapted "<<image.exposure[0][0]<<" gain "<<image.exposure[0][1]<<'\n';
        return image;
    };
    WorldTemporalView view{101,1,1,width,height,false,1.f/60.f};
    view.cameraValid=true;
    auto a=frame(view,{.18f,.18f,.18f});close(a.exposure[0][0],a.exposure[0][2],1e-6f,"initial exposure");
    close(a.exposure[0][1],2.f/(9.6f*a.exposure[0][0]),1e-5f,"exposure calibration");
    for(const auto& ref:displayReferences) {
        const auto actual=a.displayLut[ref.g*2304+ref.b*48+ref.r];
        for(int channel=0;channel<3;++channel)close(actual[channel],ref.color[channel],.0012f,"independent ACES LUT fixture");
    }
    ++view.frame;auto b=frame(view,{1,1,1});
    close(b.exposure[0][0],a.exposure[0][0]+(b.exposure[0][2]-a.exposure[0][0])*(1-std::exp(-5.f/60)),1e-5f,"dark adaptation");
    ++view.frame;auto c=frame(view,{.01f,.01f,.01f});
    close(c.exposure[0][0],b.exposure[0][0]+(c.exposure[0][2]-b.exposure[0][0])*(1-std::exp(-8.f/60)),1e-5f,"bright adaptation");
    ++view.frame;view.reset=true;auto cut=frame(view,{.18f,.18f,.18f},1);close(cut.exposure[0][0],cut.exposure[0][2],1e-6f,"cut");
    check(std::abs(cut.exposure[0][2]-a.exposure[0][2])<.01f,"bright outlier trim");
    ++view.frame;view.reset=false;settings.manualBlend=1;settings.manualExposure=.5f;
    auto impulse=frame(view,{0,0,0},2);close(impulse.exposure[0][1],1,1e-5f,"manual blend");
    ++view.frame;auto colors=frame(view,{0,0,0},3);
    view.identity=102;view.frame=1;settings={};auto isolated=frame(view,{.05f,.05f,.05f});close(isolated.exposure[0][0],isolated.exposure[0][2],1e-6f,"view isolation");
    surface.releaseWorldTemporalView(101);
    view.identity=101;view.generation=2;view.frame=1;auto returned=frame(view,{.4f,.4f,.4f});
    close(returned.exposure[0][0],returned.exposure[0][2],1e-6f,"retired view return");
    ++view.frame;
    settings.grade={{{.01f,-.02f,.03f,0},{.9f,1.1f,1.2f,0},{1.02f,.97f,1.05f,0},{.01f,.02f,-.01f,.03f},
        {.03f,-.02f,.01f,.435f},{.05f,-.03f,.02f,.1f},{-.03f,.01f,.04f,.45f},{1.1f,.7f,.2f,0}}};
    auto grade=frame(view,{0,0,0},3,0);
    check(grade.displayLut!=returned.displayLut,"grade invalidation");
    for(const auto& ref:displayGradeReferences) {
        const auto actual=grade.displayLut[ref.g*2304+ref.b*48+ref.r];
        for(int channel=0;channel<3;++channel)close(actual[channel],ref.color[channel],.0012f,"independent graded LUT fixture");
    }

    WorldDisplaySettings bad=settings;bad.lowPercentile=bad.highPercentile;
    bool rejected=false;try{radiance.setWorldDisplaySettings(bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid display setting accepted");
    ++view.frame;view.deltaSeconds=0;auto pause=frame(view,{8,8,8});
    close(pause.exposure[0][0],grade.exposure[0][0],1e-6f,"paused adaptation");
    ++view.frame;view.deltaSeconds=1.f/60.f;view.reset=true;settings.minEV=settings.maxEV=0;
    auto bounded=frame(view,{0,0,0});close(bounded.exposure[0][2],.125f,1e-6f,"EV bounds");
    ++view.frame;settings={};settings.skyWeight=.25f;view.reset=true;
    auto sky=frame(view,{.2f,.2f,.2f},4);
    close(sky.meter[32768][0],.05f,.0001f,"sky weighting");
    settings.skyWeight=1;
    ++view.frame;view.reset=false;settings.velocityDamping=1;view.cameraPosition[0]=1;
    auto moving=frame(view,{1,1,1});
    close(moving.exposure[0][0],sky.exposure[0][0]+(moving.exposure[0][2]-sky.exposure[0][0])*
        (1-std::exp(-5.f/60))*std::exp(-.6f),1e-5f,"camera damping");
    width=321;height=243;SDL_SetWindowSize(window.getSDLWindow(),width,height);renderer->onResize(width,height);++view.frame;settings={};
    auto resized=frame(view,{.08f,.08f,.08f});
    close(resized.exposure[0][0],resized.exposure[0][2],1e-6f,"resize exposure");
    ++view.frame;view.reset=true;auto hdr=frame(view,{1,.25f,.5f},6);
    check(hdr.color[width*height/2][0]>31.5f,"HDR input clipped before display");
    check(hdr.exposure[0][2]>8,"HDR metering clipped");
    ++view.frame;settings.emissiveWeight=.25f;auto emissive=frame(view,{.2f,.2f,.2f},7);
    close(emissive.meter[32768][0],.05f,.0001f,"emissive weighting");
    ++view.frame;settings={};settings.centerWeight=2;settings.manualBlend=.4f;settings.manualExposure=.3f;settings.compensationEV=-.5f;
    auto centered=frame(view,{0,0,0},3);
    close(centered.exposure[0][1],((1.f/(9.6f*centered.exposure[0][0]))*.6f+.3f*.4f)*std::exp2(-.5f),.00001f,"partial manual blend");
    ++view.frame;view.reset=false;view.deltaSeconds=1.f/120.f;view.cameraPosition[0]+=.5f;
    settings={};settings.velocityDamping=1;
    auto fastCamera=frame(view,{1,1,1});
    close(fastCamera.exposure[0][0],centered.exposure[0][0]+(fastCamera.exposure[0][2]-centered.exposure[0][0])*
        (1-std::exp(-5.f/120))*std::exp(-.6f),1e-5f,"120 Hz camera damping");
    // Storm authoring uses -3: exercise signed metering on both backends,
    // then the exact origin at a 128-pixel viewport (no 0 * infinity).
    for(float importance:{-3.f,-8.f,0.f,8.f}) {
        ++view.frame;view.reset=true;settings={};settings.centerWeight=importance;
        auto signedWeight=frame(view,{.18f,.18f,.18f});
        close(signedWeight.meter[32768][0],.18f,.0002f,"signed uniform metering");
    }
    width=height=128;SDL_SetWindowSize(window.getSDLWindow(),width,height);
    renderer->onResize(width,height);++view.frame;settings.centerWeight=-3;
    auto origin=frame(view,{.18f,.18f,.18f});
    close(origin.meter[127*256+127][0],.18f,.0002f,"exact-origin signed metering");
    width=320;height=240;SDL_SetWindowSize(window.getSDLWindow(),width,height);
    renderer->onResize(width,height);view.reset=true;view.cameraPosition={0,0,500};
    WorldHeatHazeNoise noise;noise.key="test-heat-constant";noise.width=noise.height=noise.layers=2;
    noise.rgba.assign(2*2*2*4,255);radiance.setWorldHeatHazeNoise(noise);
    // The renderer owns its copy; no plugin-created deleter or transient bytes.
    noise.rgba.clear();
    settings={};settings.manualBlend=1;settings.manualExposure=.5f;
    ++view.frame;auto undistorted=frame(view,{0,0,0},3,0);
    settings.heatHaze={.4f,60,500,1,1,0};
    ++view.frame;auto distorted=frame(view,{0,0,0},3,0);
    const int sample=119*width+159;
    const float shift=.2f;
    for(int channel=0;channel<3;++channel){
        const auto before=undistorted.color[sample][channel],next=undistorted.color[sample+1][channel];
        close(distorted.color[sample][channel],before*(1-shift)+next*shift,.004f,"subpixel heat distortion");
    }
    ++view.frame;view.deltaSeconds=0;auto heatPaused=frame(view,{0,0,0},3,0);
    check(heatPaused.color==distorted.color,"Paused heat haze changed");
    view.cameraPosition={0,0,20};++view.frame;
    auto nearHeat=frame(view,{0,0,0},3,0);
    check(nearHeat.color==undistorted.color,"Heat haze changed near foreground");
    // At 100 m the tentative sample is nearer than its origin and below the
    // independent 192 m guard: it must revert, despite nonzero amplitude.
    view.cameraPosition={0,0,100};++view.frame;
    auto guarded=frame(view,{0,0,0},3,0);
    for(int channel=0;channel<3;++channel)
        close(guarded.color[sample][channel],undistorted.color[sample][channel],.0001f,"Heat foreground guard");
    settings.heatHaze.strength=0;++view.frame;
    auto heatOff=frame(view,{0,0,0},3,0);
    check(heatOff.color==undistorted.color,"Disabled heat retained distortion");
    view.cameraPosition={0,0,3};view.deltaSeconds=1.f/60.f;
    ++view.frame;auto whiteSpecular=frame(view,{.5f,.5f,.5f},9,0);
    ++view.frame;auto tintedSpecular=frame(view,{.5f,.5f,.5f},10,0);
    ++view.frame;auto zeroSpecular=frame(view,{.5f,.5f,.5f},11,0);
    const auto pixel=120*width+160;
    constexpr float expectedTint[]{.2f,.5f,.8f};
    for(int channel=0;channel<3;++channel){
        const auto full=whiteSpecular.color[pixel][channel]-zeroSpecular.color[pixel][channel];
        check(full>.001f,"Specular fixture has no directional response");
        close((tintedSpecular.color[pixel][channel]-zeroSpecular.color[pixel][channel])/full,
              expectedTint[channel],.008f,"Directional specular tint");
    }
    std::cout<<"PASS heat distance/amplitude/foreground/pause/disable and directional specular tint\n";
    // Encode a stationary ramp sampled on eight displaced pixel grids. Its
    // displayed position must not follow the projection's antialiasing jitter.
    settings={};settings.manualBlend=1;settings.manualExposure=.5f;
    view.reset=true;view.jitterPixels={};++view.frame;
    const auto stable=frame(view,{0,0,0},12,0);
    float maximumDrift=0,rawDrift=0;
    for(unsigned phase=1;phase<=8;++phase) {
        view.jitterPixels=worldTemporalJitter(phase);++view.frame;
        const auto displaced=frame(view,{0,0,0},12,0);
        for(int x=width/2-10;x<=width/2+10;++x) {
            const auto index=height/2*width+x;
            maximumDrift=std::max(maximumDrift,std::abs(displaced.display[index][0]-stable.display[index][0]));
            rawDrift=std::max(rawDrift,std::abs(displaced.color[index][0]-stable.color[index][0]));
        }
    }
    check(rawDrift>.003f,"Jitter fixture did not displace radiance");
    check(maximumDrift<.0008f,"Projection jitter leaked into displayed stationary image");
    std::cout<<"PASS stationary presentation jitter: raw "<<rawDrift<<" display "<<maximumDrift<<"\\n";
    std::cout<<"display native PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
