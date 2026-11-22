#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include "engine/render/IWorldRainMap.h"
#include <SDL.h>
#include <iostream>
#include <memory>
#include <numeric>
using namespace engine::render;
void check(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
int main(int argc,char** argv)try{
    check(argc==2,"Expected d3d12 or vulkan");const std::string api=argv[1];check(api=="d3d12"||api=="vulkan","Invalid API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Falling rain numerical qualification",256,256,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> r;
    if(api=="vulkan")r=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
    else r=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*r);auto& maps=dynamic_cast<IWorldRainMap&>(*r);
    const auto owner=scopes.createWorldResourceScope(128ull*1024*1024);
    check(scopes.beginWorldResourceScope(owner),"Rain owner");maps.prewarmWorldRainMap(owner,128);check(scopes.endWorldResourceScope(),"Rain owner finish");
    const auto begin=[&]{r->beginFrame(0,0,0,1);r->beginWorldSceneColorPass(256,256);};
    const auto end=[&]{r->endWorldSceneColorPass();r->endFrame();if(!r->handlesPresentation())window.swapBuffers();};
    WorldDirectionalShadowView map;map.enabled=true;map.resolution=128;map.depthBias=map.normalOffset=0;
    const auto overhead=glm::ortho(-5.f,5.f,-5.f,5.f,0.f,16000.f)*glm::lookAt(glm::vec3(0,8000,0),glm::vec3(0,0,0),glm::vec3(0,0,-1));
    std::copy_n(glm::value_ptr(overhead),16,map.viewProjection.begin());
    const auto roof=[&](float height,bool masked){
        std::array<IRenderBackend::WorldMeshVertex,4> v{};
        for(unsigned i=0;i<4;++i){v[i].x=i==0||i==3?-4.f:4.f;v[i].z=i<2?4.f:-4.f;v[i].y=height;
            v[i].u=i==0||i==3?0.f:1.f;v[i].v=i<2?1.f:0.f;v[i].ny=v[i].tx=v[i].tw=v[i].r=v[i].g=v[i].b=v[i].a=1;}
        constexpr std::array<std::uint32_t,6> index{0,1,2,0,2,3};
        const unsigned char pixels[]{255,255,255,0,255,255,255,255},white[]{255,255,255,255};
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey=masked?"rain-mask-fixture":"rain-roof-fixture";t.rgba=masked?pixels:white;t.width=masked?2:1;t.height=1;t.materialMode=2;t.alphaMode=masked?1:0;t.alphaCutoff=.5;
        r->drawWorldIndexedMeshTextured(v.data(),4,index.data(),6,&t,map.viewProjection.data(),128,128);
    };
    begin();for(unsigned layer=0;layer<2;++layer){
        check(maps.beginWorldRainMap(owner,map,true),"Begin rain map");roof(layer?6.f:3.f,layer==0);
        check(maps.endWorldRainMap()&&maps.publishWorldRainMap(owner,layer),"Publish rain map");
    }
    check(maps.captureWorldRainMap(owner),"Rain-map capture");end();const auto first=maps.readWorldRainMap();
    check(first.resolution==128&&first.depth.size()==32768,"Map readback dimensions");
    const auto height=[&](const auto& image,unsigned x,unsigned y,unsigned cascade){return (1.0-double(image.depth[(cascade*128+y)*128+x]))*16000.0-8000.0;};
    check(std::abs(height(first,96,64,0)-3)<.02,"Opaque part of masked roof missing");
    check(first.depth[64*128+32]>.999,"Masked hole blocked rain");
    check(std::abs(height(first,64,64,1)-6)<.02,"Second cascade missing");
    begin();check(maps.beginWorldRainMap(owner,map,true),"Pending begin");roof(9,false);check(maps.endWorldRainMap(),"Pending end");
    check(maps.captureWorldRainMap(owner),"Pending capture");end();const auto pending=maps.readWorldRainMap();
    check(first.depth==pending.depth,"Incomplete replacement leaked to published rain map");
    begin();check(maps.beginWorldRainMap(owner,map,false),"Continue pending");check(maps.endWorldRainMap()&&maps.publishWorldRainMap(owner,0),"Complete pending");
    check(maps.captureWorldRainMap(owner),"Completed capture");end();const auto finalMap=maps.readWorldRainMap();
    check(std::abs(height(finalMap,64,64,0)-9)<.02,"Pending depth was lost");
    check(std::abs(height(finalMap,64,64,1)-6)<.02,"Publishing near corrupted far");
    std::cout<<"PASS opaque/masked shelter, independent cascades and atomic publication\n";
    auto& lighting=dynamic_cast<IWorldPrecipitationLighting&>(*r);
    WorldPrecipitationLightingView u;u.owner=owner;u.view.identity=100;u.view.generation=1;u.view.frame=1;
    u.view.width=u.view.height=256;u.view.cameraValid=true;u.view.cameraPosition={0,-300,0};u.quality=0;u.range=64;u.absorption=1.5f;
    const auto vp=glm::perspective(glm::radians(60.f),1.f,.05f,500.f)*glm::lookAt(glm::vec3(0,-300,0),glm::vec3(0,-300,-1),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(vp),16,u.view.viewProjection.begin());
    auto& env=u.environment;env.enabled=true;env.hazeDensity=.02f;env.localHazeAlbedo=.5f;env.ambientRadiance={.4f,.2f,.1f};
    bool opaquePlane=false;
    const auto run=[&](){
        begin();
        if(opaquePlane) {
            check(scopes.beginWorldResourceScope(owner),"Depth fixture owner");
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            for(unsigned i=0;i<4;++i) {
                auto& v=vertices[i];v.x=i==0||i==3?-100.f:100.f;v.y=i<2?-400.f:-200.f;v.z=-5;
                v.r=v.g=v.b=v.a=v.nz=v.tx=v.tw=1;
            }
            constexpr std::uint32_t indices[]{0,1,2,0,2,3};
            const unsigned char white[]{255,255,255,255};
            IRenderBackend::WorldTextureData material;material.key=material.cacheKey="rain-opaque-depth-fixture";
            material.width=material.height=1;material.rgba=white;material.materialMode=2;
            r->drawWorldIndexedMeshTextured(vertices.data(),4,indices,6,&material,u.view.viewProjection.data(),256,256);
            check(scopes.endWorldResourceScope(),"Depth fixture finish");
        }
        const auto output=lighting.updateWorldPrecipitationLighting(u);
        check(!output.texture.empty()&&output.depth[3]==1,"Missing rain volume publication");
        check(scopes.beginWorldResourceScope(owner),"Published weather binding owner");
        std::array<IRenderBackend::WorldMeshVertex,3> vertices{};
        for(auto& vertex:vertices)vertex.r=vertex.g=vertex.b=vertex.a=vertex.nz=vertex.tx=vertex.tw=1;
        vertices[0].x=-1;vertices[0].y=-1;vertices[1].x=1;vertices[1].y=-1;vertices[2].y=1;
        const unsigned char white[]{255,255,255,255};const std::uint32_t indices[]{0,1,2};
        const auto mapKey=maps.worldRainMapTexture(owner);
        IRenderBackend::WorldTextureData material;material.key=material.cacheKey="published-weather-binding";material.rgba=white;
        material.width=material.height=1;material.emissiveKey=output.texture.c_str();material.environmentKey=mapKey.c_str();
        const glm::mat4 identity(1);
        r->drawWorldIndexedMeshTextured(vertices.data(),3,indices,3,&material,glm::value_ptr(identity),256,256);
        check(scopes.endWorldResourceScope(),"Published weather binding finish");
        WorldResourceScopeStats resources;check(scopes.getWorldResourceScopeStats(owner,resources),"Published weather owner missing");
        if(resources.status!=WorldResourceScopeStatus::Ready)throw std::runtime_error("Published weather binding: "+resources.error);
        check(lighting.captureWorldPrecipitationLighting(),"Volume capture");end();return lighting.readWorldPrecipitationLighting();
    };
    auto baseline=run();check(!baseline.history&&baseline.identity==100,"First history admitted");
    for(const auto& v:baseline.lighting){
        for(unsigned c=0;c<3;++c)check(std::abs(v[c]-double(env.ambientRadiance[c])*.01)<.00002,"Uniform medium source radiance");
        check(std::abs(v[3]-.03)<.00003,"Uniform extinction");
    }
    double transmission=1;std::array<double,3> energy{};
    double previous=64*std::pow(.5/baseline.depth,2);
    for(unsigned z=0;z<baseline.depth;++z){
        const double next=64*std::pow((z+1.5)/baseline.depth,2),step=next-previous;previous=next;
        const auto& l=baseline.lighting[z*baseline.width*baseline.height];
        const double t=std::exp(-l[3]*step);transmission*=t;
        for(unsigned c=0;c<3;++c)energy[c]+=l[c]*(1-t)/l[3]*t;
        const auto& result=baseline.scattering[z*baseline.width*baseline.height];
        for(unsigned c=0;c<3;++c)check(std::abs(result[c]-energy[c])<.0003,"Independent scattering integral");
        check(std::abs(result[3]-transmission)<.001,"Independent Beer-Lambert transmission");
    }
    ++u.view.frame;env.ambientRadiance[0]=.8f;const auto accumulated=run();
    check(accumulated.history,"Ordinary camera history lost");
    check(std::abs(accumulated.lighting[0][0]-(baseline.lighting[0][0]*.95+.008*.05))<.00002,"History 95 percent reconstruction");
    ++u.view.frame;u.view.reset=true;const auto reset=run();check(!reset.history&&std::abs(reset.lighting[0][0]-.008)<.00002,"Cut retained old lighting");
    u.view.reset=false;++u.view.frame;env.timeOfDayHours+=2;check(!run().history,"Time-of-day cut retained history");
    ++u.view.frame;env.ambientRadiance={0,0,0};u.view.reset=true;
    auto& pink=env.pointLights[0];pink.position={-2,-300,-5};pink.radius=8;pink.intensity={6,0,3};pink.participation=WorldLightFog;
    auto& blue=env.pointLights[1];blue.position={2,-300,-5};blue.radius=8;blue.intensity={0,0,6};blue.participation=WorldLightFog;
    const auto spatial=run();double leftRed=0,rightRed=0,leftBlue=0,rightBlue=0;
    for(unsigned z=0;z<spatial.depth;++z)for(unsigned y=0;y<spatial.height;++y)for(unsigned x=0;x<spatial.width;++x){
        const auto& value=spatial.lighting[(z*spatial.height+y)*spatial.width+x];for(float v:value)check(std::isfinite(v),"Nonfinite volume");
        if(x<spatial.width/2){leftRed+=value[0];leftBlue+=value[2];}else{rightRed+=value[0];rightBlue+=value[2];}
    }
    check(leftRed>rightRed*1.2&&rightBlue>leftBlue*1.05,"Local rain illumination lost spatial color");
    env.localHazeAnisotropy=.8f;const auto anisotropic=run();
    check(anisotropic.lighting==spatial.lighting,"Local fog lighting incorrectly uses the sun phase function");
    ++u.view.frame;pink.participation=WorldLightSurfaces;blue.participation=WorldLightParticles;const auto excluded=run();
    for(const auto& v:excluded.lighting)check(v[0]==0&&v[1]==0&&v[2]==0,"A light excluded from fog entered its shared precipitation volume");
    ++u.view.frame;env.localHazeAnisotropy=0;env.sunRadiance={.8f,.4f,.2f};
    const auto directional=run();
    for(const auto& v:directional.lighting)for(unsigned c=0;c<3;++c)
        check(std::abs(v[c]-double(env.sunRadiance[c])*(std::acos(-1.)/4)*.01)<.00002,
            "Independent isotropic directional scattering");
    ++u.view.frame;env.sunRadiance={0,0,0};env.ambientRadiance={.4f,.2f,.1f};opaquePlane=true;
    const auto occluded=run();std::size_t rejected=0,visible=0;
    for(unsigned z=0;z<occluded.depth;++z)for(unsigned y=0;y<occluded.height;++y)for(unsigned x=0;x<occluded.width;++x) {
        const auto& value=occluded.lighting[(z*occluded.height+y)*occluded.width+x];
        if(value[3]<-900)++rejected;else if(value[3]>.02f)++visible;
        if(z==occluded.depth-1)check(value[3]<-900,"Opaque five-metre depth did not reject far froxels");
    }
    check(rejected>occluded.lighting.size()/2&&visible>0,"Opaque depth rejection removed the wrong interval");
    opaquePlane=false;
    for(unsigned quality=1;quality<4;++quality){++u.view.frame;u.quality=quality;const auto q=run();check(!q.history&&q.depth==precipitationDimensions(256,256,quality)[2],"Preset failed to resize/invalidate volume");}
    ++u.view.frame;u.quality=0;u.view.reset=true;env.ambientRadiance={.4f,.2f,.1f};
    WorldFogBox box;box.position={0,-300,-30};box.halfRight={1000,0,0};box.halfUp={0,1000,0};box.halfForward={0,0,1000};
    box.density=.04f;box.albedo={.25f,.25f,.25f};box.absorption=2;box.ambientScale=.5f;
    u.medium.boxes={box};const auto boxed=run();
    for(const auto& v:boxed.lighting) {
        check(std::abs(v[3]-.08)<.0001,"Local box extinction");
        for(unsigned c=0;c<3;++c)check(std::abs(v[c]-env.ambientRadiance[c]*.005)<.00002,"Independent local box medium");
    }
    // Two overlapping media: the rotated high-priority box is half blended and
    // half faded at this view. The lower-priority full box receives the remainder.
    auto back=box;back.density=.01f;back.albedo={.75f,.75f,.75f};back.absorption=.5f;back.ambientScale=.8f;
    auto edge=box;const float diagonal=std::sqrt(.5f);
    edge.halfRight={diagonal*1e6f,0,diagonal*1e6f};edge.halfForward={-diagonal*1e6f,0,diagonal*1e6f};
    edge.halfUp={0,1e6f,0};edge.position={diagonal*750000,-300,diagonal*750000};
    edge.density=.08f;edge.blendRange=edge.densityFalloff=.5f;edge.priority=5;
    u.medium.boxes={back,edge};const auto blended=run();
    for(const auto& v:blended.lighting) {
        check(std::abs(v[3]-.03125)<.00006,"Oriented priority/edge density blend");
        for(unsigned c=0;c<3;++c)check(std::abs(v[c]-env.ambientRadiance[c]*.008125)<.00001,"Oriented medium albedo/ambient blend");
    }
    env.ambientRadiance={0,0,0};pink.participation=WorldLightFog;pink.channels=1;
    box.channels=2;u.medium.boxes={box};const auto wrongChannel=run();
    for(const auto& v:wrongChannel.lighting)check(v[0]==0&&v[1]==0&&v[2]==0,"Box light channel leaked");
    u.medium.boxes[0].channels=1;const auto matchingChannel=run();
    double red=0;for(const auto& v:matchingChannel.lighting)red+=v[0];
    check(red>1,"Box light channel rejected its light");
    pink.participation=WorldLightSurfaces;env.ambientRadiance={.4f,.2f,.1f};
    u.medium.boxes.clear();
    WorldFogParticle particle;particle.position={0,-300,-30};particle.radius=10000;particle.density=.2f;particle.falloff=0;particle.albedo={.2f,.4f,.6f};
    u.medium.particles={particle};const auto particles=run();
    const double total=.2+.02*.8;
    for(const auto& v:particles.lighting) {
        check(std::abs(v[3]-total*1.5)<.0003,"Particle extinction");
        for(unsigned c=0;c<3;++c) {
            const double albedo=particle.albedo[c]*std::sqrt(.2)+.5*.02*.8/total;
            check(std::abs(v[c]-env.ambientRadiance[c]*albedo*total)<.00004,"Source particle edge-bleed expression");
        }
    }
    u.medium.particles.clear();u.distant=true;opaquePlane=true;
    const auto narrow=glm::perspective(glm::radians(1.f),1.f,.05f,5000.f)*glm::lookAt(glm::vec3(0,-300,0),glm::vec3(0,-300,-1),glm::vec3(0,1,0));
    std::copy_n(glm::value_ptr(narrow),16,u.view.viewProjection.begin());
    // The distant pass must ignore local density/light emitters and near opaque depth.
    u.medium.boxes={box};u.medium.particles={particle};pink.participation=WorldLightFog;
    const auto distant=run();
    check(distant.width==baseline.width/2&&distant.height==baseline.height/2&&distant.depth==baseline.depth/2,"Distant half-resolution grid");
    for(const auto& v:distant.lighting) {
        check(std::abs(v[3]-.03)<.0001,"Distant local density/depth contamination");
        for(unsigned c=0;c<3;++c)check(std::abs(v[c]-env.ambientRadiance[c]*.01)<.00003,"Distant global-only scattering");
    }
    u.view.reset=false;++u.view.frame;check(run().history,"Distant temporal history missing");
    ++u.medium.revision;++u.view.frame;check(!run().history,"Fog authoring revision retained stale history");
    opaquePlane=false;u.distant=false;u.medium={};u.view.reset=true;
    std::copy_n(glm::value_ptr(vp),16,u.view.viewProjection.begin());
    env.pointLights={};env.rectLights={};env.ambientRadiance={0,0,0};
    // A downward-facing capsule must light both longitudinal halves. Using the
    // tube axis as its cone direction produces a false plane through the middle.
    auto& tube=env.rectLights[0];tube.position={0,-299,-5};tube.range=32;
    tube.halfU={4,0,0};tube.halfV={0,0,.1f};tube.radiance={2,0,0};
    tube.response=WorldRectLightResponse::FiniteSource;tube.source.axis={1,0,0};
    tube.source.length=8;tube.source.cone=true;
    const auto capsule=run();double tubeLeft=0,tubeRight=0;
    for(unsigned z=0;z<8;++z)for(unsigned y=0;y<capsule.height/2;++y)for(unsigned x=0;x<capsule.width;++x) {
        const auto value=capsule.lighting[(z*capsule.height+y)*capsule.width+x][0];
        (x<capsule.width/2?tubeLeft:tubeRight)+=value;
    }
    check(tubeLeft>0&&tubeRight>tubeLeft*.8&&tubeRight<tubeLeft*1.25,"Capsule fog cone used its longitudinal axis");
    env.rectLights={};auto& tail=env.pointLights[0];tail.position={0,-300,-100};tail.radius=256;tail.intensity={1,0,0};
    u.medium.localLightScale=1000;const auto uncut=run();
    const double q=10000./(256.*256.),expectedTail=std::pow(1-q*q,2)/10000.*.01*1000;
    check(std::abs(uncut.lighting[0][0]-expectedTail)<.000003,"Fog inherited a surface dim-tail cutoff");
    u.medium.localLightScale=1;
    std::cout<<"PASS capsule outward cone and independent uncut dim-tail attenuation\n";
    std::cout<<"PASS local boxes, density particles, independent distant volume and revision rejection\n";
    std::cout<<"PASS independent ambient/directional scattering, opaque depth rejection, spatial colors, participation, history cuts and all preset layouts\n";
    check(scopes.requestWorldResourceScopeRelease(owner),"Owner release");begin();end();
    check(scopes.getWorldResourceStats().liveScopes==0,"Precipitation owner leaked");
    r->shutdown();std::cout<<"PASS "<<api<<" falling-rain native qualification\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
