#include "engine/render/LegacyOpenGL.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldSceneRadiance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL.h>
#include <iostream>
#include <memory>
#include "ScreenReflectionOracle.h"
using namespace engine::render;
namespace { void check(bool b,const char* text){if(!b)throw std::runtime_error(text);} }
int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;check(argc==2||argc==3||argc==4,"Expected API [secondary or project root [foliage]]");
    const std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();
    struct Quit{~Quit(){SDL_Quit();}}quit;
    int width=320,height=240;
    Window window("Temporal reflections",width,height,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),width,height,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),width,height,false);
    const bool secondary=argc==3 && std::string(argv[2])=="secondary";
    const bool foliage=argc==4 && std::string(argv[3])=="foliage";
    const bool subsurface=argc>=3 && !secondary && !foliage;
    if(subsurface || foliage)renderer->setWorldMaterialProfile(loadWorldMaterialProfile(argv[2],"config/render/world_materials.json"));
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(*renderer);
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const glm::vec3 eye{0,2,4},target{0,.3,-2};
    const auto viewMatrix=glm::lookAt(eye,target,glm::vec3(0,1,0));
    WorldTemporalReflectionView reflection;reflection.camera={eye.x,eye.y,eye.z};
    std::copy_n(glm::value_ptr(viewMatrix),16,reflection.worldToView.begin());
    WorldMotionImage lastSurface;
    std::string receiver;
    const std::array<float,64> leafPalette=[](){std::array<float,64> p{};p[0]=p[4]=p[51]=1;return p;}();
    const auto frame=[&](WorldTemporalView temporal,glm::vec3 wallColor,bool valid=true,float roughness=.12f,float previousShift=0.f,float currentShift=0.f,bool gradient=false) {
        temporal.width=width;temporal.height=height;temporal.deltaSeconds=1.f/60.f;
        const auto vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,100.f)*viewMatrix;
        WorldMotionMatrix projection;std::copy_n(glm::value_ptr(vp),16,projection.begin());
        renderer->beginFrame(0,0,0,0);renderer->beginWorldSceneColorPass(width,height);
        if(subsurface || foliage || !receiver.empty())dynamic_cast<IWorldSceneRadiance&>(*renderer).setWorldSceneRadianceExposure(1.f);
        const auto draw=[&](bool capture) {
            for(int plane=0;plane<2;++plane) {
                const std::array<glm::vec3,4> positions=plane==0?
                    std::array<glm::vec3,4>{{{-8,0,5},{8,0,5},{8,0,-8},{-8,0,-8}}}:
                    std::array<glm::vec3,4>{{{-8,0,-4},{8,0,-4},{8,6,-4},{-8,6,-4}}};
                std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
                for(int i=0;i<4;++i) {
                    auto& v=vertices[i];v.x=positions[i].x;v.y=positions[i].y;v.z=positions[i].z;
                    v.u=(i==1||i==2)?1.f:0.f;v.v=i>=2?1.f:0.f;
                    v.ny=plane==0?1.f:0.f;v.nz=plane==1?1.f:0.f;v.tx=v.tw=v.weight0=1;
                    const auto color=plane?(gradient?glm::vec3(i==0||i==3?1:0,0,i==0||i==3?0:1):wallColor):glm::vec3(.01f);v.r=color.x;v.g=color.y;v.b=color.z;v.a=1;
                }
                IRenderBackend::WorldTextureData material;
                material.key=material.cacheKey="reflection-fixture-white";material.rgba=white.data();
                material.width=material.height=1;material.materialMode=capture?2:255;
                material.textureSrgb=0;material.roughnessFactor=plane?1.f:roughness;material.metallicFactor=0;
                if(subsurface && plane==0) {
                    material.materialMode=33;material.normalScale=0;
                    material.metallicRoughnessKey=material.metallicRoughnessCacheKey="reflection-sss-roughness";
                    material.metallicRoughnessRgba=white.data();material.metallicRoughnessWidth=material.metallicRoughnessHeight=1;
                    material.environment.enabled=true;material.environment.ambientRadiance={.01f,.01f,.01f};
                }
                if(plane==0 && !receiver.empty()) {
                    material.materialMode=2;material.environment.enabled=true;
                    material.environment.ambientRadiance={.01f,.01f,.01f};
                    material.environment.sunRadiance={0,0,0};material.environment.moonRadiance={0,0,0};
                    material.metallicFactor=receiver=="metal"?1.f:0.f;
                    if(receiver=="normal") {
                        static constexpr std::array<unsigned char,4> tilt{190,128,238,255};
                        material.normalKey=material.normalCacheKey="reflection-tilt";
                        material.normalRgba=tilt.data();material.normalWidth=material.normalHeight=1;material.normalScale=1;
                    }
                    if(receiver=="wet") {
                        material.roughnessFactor=.7f;material.environment.surfaceWeatherEnabled=true;
                        material.environment.surfaceWetness=1;material.environment.puddleAmount=1;material.environment.surfaceAbsorption=0;
                    }
                    if(receiver.starts_with("puddle")) {
                        auto& weather=material.environment;
                        material.roughnessFactor=1;
                        weather.surfaceWeatherEnabled=true;weather.surfaceWetness=weather.puddleAmount=1;
                        weather.puddles[0]={{0,receiver=="puddle-height"?2.f:0.f,0},{30,30},{1,0}};
                        if(receiver=="puddle-dry")weather.surfaceWetness=weather.puddleAmount=0;
                        if(receiver=="puddle-disabled")weather.surfaceWeatherEnabled=false;
                        if(receiver=="puddle-covered")weather.weatherShelters[0]={{-40,-40,40,40},3};
                    }
                    if(receiver=="masked") {
                        static const auto mask=[](){std::array<unsigned char,8*8*4> a{};for(int j=0;j<64;++j){a[j*4]=a[j*4+1]=a[j*4+2]=255;a[j*4+3]=(j%8)<4?255:0;}return a;}();
                        material.key=material.cacheKey="reflection-mask";material.rgba=mask.data();material.width=material.height=8;
                        material.alphaMode=1;material.alphaCutoff=.5f;
                    }
                    if(receiver=="motion-only")material.materialMode=0;
                }
                if(foliage && plane==0) {
                    static constexpr std::array<unsigned char,4> leafNormal{128,128,255,255};
                    static constexpr std::array<unsigned char,4> leafSurface{0,115,0,255};
                    material.materialMode=238;material.alphaMode=1;material.gpuSkinning=1;
                    material.skinMatrixCount=4;material.skinMatrices=leafPalette.data();material.materialFlags=2;
                    material.normalKey=material.normalCacheKey="reflection-leaf-normal";
                    material.normalRgba=leafNormal.data();material.normalWidth=material.normalHeight=1;
                    material.metallicRoughnessKey=material.metallicRoughnessCacheKey="reflection-leaf-surface";
                    material.metallicRoughnessRgba=leafSurface.data();material.metallicRoughnessWidth=material.metallicRoughnessHeight=1;
                    material.environment.enabled=true;material.environment.ambientRadiance={.01f,.01f,.01f};
                    material.environment.sunRadiance=material.environment.moonRadiance={0,0,0};
                    material.materialRect1V=eye.x;material.materialRect1W=eye.y;material.materialRect1H=eye.z;
                    material.environment.reflectionResponseWeight=1;material.environment.reflectionAlbedoF90=true;
                }
                if(plane)material.modelMatrix[12]=currentShift;
                material.cameraPosX=eye.x;material.cameraPosY=eye.y;material.cameraPosZ=eye.z;
                material.cameraTargetX=target.x;material.cameraTargetY=target.y;material.cameraTargetZ=target.z;
                WorldMotionPose previous;previous.model=previous.instance=worldMotionIdentity;
                if(plane)previous.model[12]=previousShift;
                if(foliage && plane==0) {
                    previous.matrixCount=4;previous.deformation.assign(leafPalette.begin(),leafPalette.end());
                    previous.materialParameters[1]=2;
                }
                WorldMotionSample sample{&previous,std::uint32_t(plane+1),valid};
                if(capture)check(surface.setWorldSurfaceMotion(projection,{&sample,1}),"fixture motion");
                renderer->drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&material,projection.data(),width,height);
            }
        };
        check(surface.beginWorldSurfaceBuffer(projection.data(),width,height),"surface begin");draw(true);
        check(surface.endWorldSurfaceBuffer(),"surface end");
        check(surface.captureWorldSurfaceMotion(),"surface qualification capture");
        if(secondary) {
            // A secondary lit draw precedes primary history preparation. Its
            // descriptor bundle must not retain another frame's SSR images,
            // especially when the following primary view changes extent.
            WorldClusteredLightingView lighting;
            lighting.camera={eye.x,eye.y,eye.z};
            const auto forward=glm::normalize(target-eye);
            lighting.forward={forward.x,forward.y,forward.z};
            check(surface.resolveWorldClusteredLighting(lighting),"secondary lighting");
            draw(false);
        }
        check(surface.beginWorldTemporalScene(temporal),"temporal begin");
        const bool hadFeedback=surface.worldTemporalFeedback().valid;
        check(surface.resolveWorldTemporalReflection(reflection),"reflection resolve");
        check(!surface.resolveWorldTemporalReflection(reflection),"duplicate reflection accepted");
        draw(false);
        check(surface.resolveWorldTemporalScene(),"scene resolve");
        check(surface.captureWorldTemporalScene(),"capture");
        renderer->endWorldSceneColorPass();renderer->endFrame();
        if(!renderer->handlesPresentation())window.swapBuffers();
        auto image=surface.readWorldTemporalScene();
        lastSurface=surface.readWorldSurfaceMotion();
        check(image.reflection.size()==std::size_t(width*height),"reflection readback missing");
        check(image.reflectionDepthWidth==width/4&&image.reflectionDepthHeight==height/4,"quarter depth dimensions");
        // Independent plane/ray intersections verify reverse-Z compression,
        // gather footprint and native coordinate conventions, including odd sizes.
        const auto inverse=glm::inverse(vp);
        for(int quarterY:{image.reflectionDepthHeight/6,image.reflectionDepthHeight*3/4}) {
            const int quarterX=image.reflectionDepthWidth/2;
            const int bx=int(std::floor(float(quarterX)*width/image.reflectionDepthWidth-.5f));
            const int by=int(std::floor(float(quarterY)*height/image.reflectionDepthHeight-.5f));
            float nearest=0;
            for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx) {
                const float x=(std::clamp(bx+dx,0,width-1)+.5f)/width;
                const float y=(std::clamp(by+dy,0,height-1)+.5f)/height;
                auto a=inverse*glm::vec4(2*x-1,1-2*y,-1,1),b=inverse*glm::vec4(2*x-1,1-2*y,1,1);
                const glm::vec3 start=glm::vec3(a)/a.w,ray=glm::vec3(b)/b.w-start;
                for(int plane=0;plane<2;++plane) {
                    const float t=plane?(-4-start.z)/ray.z:-start.y/ray.y;
                    const auto hit=start+t*ray;
                    const bool inside=plane?(hit.y>=0&&hit.y<=6&&hit.x>=-8+currentShift&&hit.x<=8+currentShift):
                        (hit.x>=-8&&hit.x<=8&&hit.z>=-8&&hit.z<=5);
                    if(t>0&&inside){const auto clip=vp*glm::vec4(hit,1);nearest=std::max(nearest,.5f-.5f*clip.z/clip.w);}
                }
            }
            check(std::abs(image.reflectionDepth[quarterY*image.reflectionDepthWidth+quarterX]-std::sqrt(nearest))<3.f/65535,
                "quarter depth compression/intersection mismatch");
        }
        // Independently check ray confidence, including invalidation-driven ray count.
        if(hadFeedback && !temporal.reset && valid && currentShift==0 && previousShift==0 && receiver!="motion-only") {
            unsigned compared=0,different=0;double mean=0;
            for(int y=height/2;y<height-4;y+=3)for(int x=4;x<width-4;x+=3) {
                const double expected=reflection_oracle::confidence(x,y,image,lastSurface,glm::dmat4(vp),glm::dmat4(viewMatrix),glm::dvec3(eye),unsigned(temporal.frame),int(reflection.quality));
                if(expected<0)continue;
                const double error=std::abs(image.reflection[y*width+x][3]-expected);
                ++compared;mean+=error;if(error>2./255+.001)++different;
            }
            std::cout<<"confidence oracle compared "<<compared<<" different "<<different<<" mean "<<(compared?mean/compared:0)<<'\n';
            check(compared>100 && different*1000<compared*15 && mean/compared<.004,"independent reflection confidence mismatch");
        }
        unsigned hits=0;double r=0,g=0,b=0;
        for(const auto& pixel:image.reflection) {
            for(float channel:pixel)check(std::isfinite(channel)&&channel>=0,"nonfinite reflection");
            check(pixel[3]<=1,"confidence range");
            if(pixel[3]>.05){++hits;r+=pixel[0];g+=pixel[1];b+=pixel[2];}
        }
        for(float depth:image.reflectionDepth)check(std::isfinite(depth)&&depth>=0&&depth<=1,"compressed depth range");
        std::cout<<"frame "<<temporal.frame<<" tier "<<int(reflection.quality)<<" hits "<<hits<<" rgb "<<r<<' '<<g<<' '<<b<<'\n';
        return image;
    };
    const auto hits=[](const WorldTemporalImage& image) {std::size_t n=0;for(auto p:image.reflection)if(p[3]>.05)++n;return n;};
    const auto red=[](const WorldTemporalImage& image){double result=0;unsigned n=0;for(auto p:image.reflection)if(p[3]>.05){result+=p[0];++n;}return n?result/n:0.;};
    for(int tier=0;tier<4;++tier) {
        reflection.quality=WorldTemporalReflectionQuality(tier);
        WorldTemporalView temporal{std::uint64_t(tier+1),1,1,width,height};
        auto initial=frame(temporal,{1,0,0});
        check(hits(initial)==0,"first frame borrowed reflection history");
        ++temporal.frame;auto old=frame(temporal,{0,0,1});
        check(hits(old)>100,"no reflected surface hits");
        unsigned redHits=0,blueHits=0;
        for(auto p:old.reflection)if(p[3]>.05){if(p[0]>p[2]+.1)++redHits;if(p[2]>p[0]+.1)++blueHits;}
        check(redHits>100&&blueHits==0,"SSR did not consume completed N-1 color");
        if(subsurface || foliage) {
            unsigned redReceiver=0;
            for(std::size_t i=0;i<old.reflection.size();++i)
                if(old.reflection[i][3]>(foliage?.05f:.5f) && old.reflection[i][0]>.5 && old.color[i][0]>old.color[i][2]+.0001)
                    ++redReceiver;
            std::cout<<"subsurface reflected pixels "<<redReceiver<<'\n';
            check(redReceiver>100,"Specialized shader did not consume reflected radiance");
        }
        ++temporal.frame;temporal.reset=true;auto cut=frame(temporal,{1,0,0});
        check(hits(cut)==0,"camera cut reused reflection color");
        ++temporal.frame;temporal.reset=false;temporal.preExposure=2;
        auto exposed=frame(temporal,{2,0,0});
        if(foliage) {
            // A broad leaf lobe contains missed rays, so its average is not a
            // unit-radiance mirror. Compare the identical frame/sample pattern
            // against an independently seeded history at unit exposure.
            auto reference=temporal;reference.identity=1000+tier;
            reference.frame=temporal.frame-1;reference.preExposure=1;
            frame(reference,{1,0,0});++reference.frame;
            const auto unit=frame(reference,{2,0,0});
            unsigned compared=0;double error=0;
            for(std::size_t i=0;i<unit.reflection.size();++i)
                if(unit.reflection[i][3]>.05 && unit.reflection[i][0]>.05) {
                    error+=std::abs(exposed.reflection[i][0]/unit.reflection[i][0]-2.0);
                    ++compared;
                }
            std::cout<<"leaf exposure paired "<<compared<<" ratio error "<<(compared?error/compared:0)<<'\n';
            check(compared>100 && error/compared<.015,"leaf exposure ratio mismatch");
            surface.releaseWorldTemporalView(reference.identity);
        } else check(red(exposed)>1.3,"reflection exposure compensation missing");
        ++temporal.frame;auto missing=frame(temporal,{2,0,0},false);
        check(hits(missing)==0,"missing hit motion fabricated");
        ++temporal.frame;auto rough=frame(temporal,{2,0,0},true,1);
        if(!foliage)check(hits(rough)==0,"rough receiver exceeded tier cutoff");
        surface.releaseWorldTemporalView(temporal.identity);
    }
    reflection.quality=foliage?WorldTemporalReflectionQuality::High:WorldTemporalReflectionQuality::Medium;
    frame({20,1,1,width,height},{1,0,0},true,.12f,0,0,true);
    auto moved=frame({20,1,2,width,height},{1,0,0},true,.12f,0,2,true);
    frame({21,1,1,width,height},{1,0,0},true,.12f,0,0,true);
    auto stationaryVelocity=frame({21,1,2,width,height},{1,0,0},true,.12f,2,2,true);
    // Compare the same strongly hit pixels, normalizing out ray miss energy.
    // The wall's linear 16-unit red/blue gradient shifts two units in history.
    double delta=0;unsigned paired=0;
    for(std::size_t i=0;i<moved.reflection.size();++i) {
        const auto a=moved.reflection[i],b=stationaryVelocity.reflection[i];
        const float ea=a[0]+a[2],eb=b[0]+b[2];
        if(a[3]>.8&&b[3]>.8&&ea>.8&&eb>.8) {delta+=a[0]/ea-b[0]/eb;++paired;}
    }
    std::cout<<"hit motion gradient shift "<<(paired?delta/paired:0)<<" paired "<<paired<<'\n';
    check(paired>100&&std::abs(delta/paired-.125)<.025,"hit-object motion gradient reprojection");
    // Sample the roughness transition and large-cone path; the numerical oracle
    // checks the separate confidence while these colors check N-1 provenance.
    if(!foliage)for(float r:{.04f,.3f,.45f,.55f,.59f,.61f}) {
        const auto viewId=std::uint64_t(300+int(r*100));
        frame({viewId,1,63,width,height},{1,0,0},true,r);
        const auto sampled=frame({viewId,1,64,width,height},{0,0,1},true,r);
        unsigned invalidBlue=0;
        for(const auto& pixel:sampled.reflection)if(pixel[3]>.01 && pixel[2]>pixel[0]+.05)++invalidBlue;
        check(invalidBlue==0,"rough cone sampled current color");
        if(r>.6f)check(hits(sampled)==0,"roughness boundary retained screen hits");
        surface.releaseWorldTemporalView(viewId);
    }
    frame({390,1,1,width,height},{1,0,0});
    const auto outside=frame({390,1,2,width,height},{0,0,1},true,.12f,100,0);
    // The stationary floor can still be hit; only the displaced red wall is absent.
    for(const auto& pixel:outside.reflection)check(pixel[0]<.1f,"offscreen previous wall fabricated screen color");
    surface.releaseWorldTemporalView(390);
    if(!subsurface && !foliage) {
        unsigned viewId=100;
        for(const std::string name:{"dielectric","metal","normal","masked","wet","motion-only","puddle","puddle-height","puddle-dry","puddle-disabled","puddle-covered"}) {
            receiver=name;
            frame({++viewId,1,1,width,height},{1,0,0});
            const auto lit=frame({viewId,1,2,width,height},{0,0,1});
            unsigned receivers=0,redReceivers=0,tilted=0,masked=0,wet=0,metal=0;
            for(std::size_t i=0;i<lastSurface.identity.size();++i)if(lastSurface.identity[i]==1) {
                ++receivers;
                const auto m=lastSurface.material[i],n=lastSurface.normal[i];
                if(std::abs(float(n&1023)/1023*2-1)>.3f)++tilted;
                if(lastSurface.materialClass[i]==2)++masked;
                if((m>>24)<153)++wet;
                if((m&255)==255)++metal;
                if(lit.reflection[i][3]>.5 && lit.reflection[i][0]>.5 && lit.color[i][0]>lit.color[i][2]+.0001)++redReceivers;
            }
            std::cout<<"receiver "<<name<<" pixels "<<receivers<<" red "<<redReceivers<<" tilted "<<tilted<<" masked "<<masked<<" wet "<<wet<<" metal "<<metal<<'\n';
            check(receivers>100,"missing fixture receiver");
            if(name=="motion-only" || (name.starts_with("puddle-") && name!="puddle"))
                check(hits(lit)==0,"excluded puddle/unsupported receiver fabricated SSR");
            else check(redReceivers>50,"evaluated receiver did not consume SSR");
            if(name=="normal")check(tilted>100,"normal map not exported");
            if(name=="masked")check(masked>100,"masked receiver not exported");
            if(name=="wet"||name=="puddle")check(wet>100,"wet/puddle roughness not exported");
            if(name=="metal")check(metal>100,"metallic not exported");
            surface.releaseWorldTemporalView(viewId);
        }
        receiver.clear();
    }
    width=321;height=243;SDL_SetWindowSize(window.getSDLWindow(),width,height);renderer->onResize(width,height);
    auto resized=frame({21,1,3,width,height},{1,0,0});
    check(hits(resized)==0,"resized history reused");
    surface.releaseWorldTemporalView(21);
    auto recreated=frame({21,1,1,width,height},{1,0,0});
    check(hits(recreated)==0,"retired reflection history reused");
    auto oddHistory=frame({21,1,2,width,height},{1,0,0});
    check(hits(oddHistory)>100,"odd-size reflection history unusable");
    renderer->shutdown();renderer.reset();
    std::cout<<"PASS temporal reflection tiers/history/cuts/exposure/missing-motion/roughness/odd-size\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
