#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldParticleLighting.h"
#include "engine/render/IWorldPrecipitationLighting.h"
#include <SDL.h>
#include <iostream>
#include <memory>
using namespace engine::render;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(int argc,char** argv)try {
    check(argc==2,"Expected d3d12 or vulkan");const std::string api=argv[1];
    check(api=="d3d12"||api=="vulkan","Invalid API");SDL_SetMainReady();
    struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Rain impact numerical qualification",256,256,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> r;
    if(api=="vulkan")r=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),256,256,false);
    else r=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),256,256,false);
    auto& scopes=dynamic_cast<IWorldResourceScope&>(*r);auto& lighting=dynamic_cast<IWorldParticleLighting&>(*r);
    WorldParticleLightingView u;u.owner=scopes.createWorldResourceScope(64ull*1024*1024);
    u.view.identity=17;u.view.generation=1;u.view.cameraValid=true;u.view.width=u.view.height=256;
    u.view.cameraPosition={0,0,0};u.view.frame=1;
    const auto vp=glm::perspective(glm::radians(60.f),1.f,.05f,100.f);
    std::copy_n(glm::value_ptr(vp),16,u.view.viewProjection.begin());
    // Tiny fixed-normal quad gives four independently computable samples.
    std::array<WorldParticleQuad,1> quads;
    quads[0].corners={glm::vec3(-.01f,-.01f,-4),glm::vec3(.01f,-.01f,-4),glm::vec3(.01f,.01f,-4),glm::vec3(-.01f,.01f,-4)};
    quads[0].billboard=false;u.quads=quads;auto& env=u.environment;env.enabled=true;
    env.sunRadiance={0,0,0};env.ambientRadiance={.4f,.2f,.1f};env.pointLights={};env.rectLights={};
    WorldParticleLightingResult output;bool opaque=false,depthMismatch=false,sunShadow=false,localShadow=false,spotShadow=false;
    auto& directional=dynamic_cast<IWorldDirectionalShadow&>(*r);auto& locals=dynamic_cast<IWorldLocalShadow&>(*r);
    directional.prewarmWorldDirectionalShadow(2048);locals.prewarmWorldLocalShadows();
    const auto drawBlocker=[&](const WorldDirectionalShadowView& shadow) {
        std::array<IRenderBackend::WorldMeshVertex,4> v{};
        for(unsigned i=0;i<4;++i){v[i].x=i==0||i==3?-1.f:1.f;v[i].y=i<2?-1.f:1.f;v[i].z=-3;
            v[i].r=v[i].g=v[i].b=v[i].a=v[i].nz=v[i].tx=v[i].tw=1;}
        const unsigned char white[]{255,255,255,255};const std::uint32_t indices[]{0,1,2,0,2,3};
        IRenderBackend::WorldTextureData m;m.key=m.cacheKey="particle-shadow-blocker";m.rgba=white;m.width=m.height=1;m.materialMode=2;
        r->drawWorldIndexedMeshTextured(v.data(),4,indices,6,&m,shadow.viewProjection.data(),shadow.resolution,shadow.resolution);
    };
    const auto begin=[&]{
        r->beginFrame(0,0,0,1);r->beginWorldSceneColorPass(256,256);directional.clearWorldDirectionalShadow();locals.clearWorldLocalShadows();
        if(sunShadow) {
            WorldDirectionalShadowView shadow;shadow.enabled=true;shadow.resolution=2048;shadow.depthBias=shadow.normalOffset=0;
            const auto matrix=glm::ortho(-2.f,2.f,-2.f,2.f,0.f,8.f);
            std::copy_n(glm::value_ptr(matrix),16,shadow.viewProjection.begin());
            check(directional.beginWorldDirectionalShadow(shadow),"Directional shadow admission");drawBlocker(shadow);
            check(directional.endWorldDirectionalShadow(),"Directional shadow end");
        }
        if(localShadow||spotShadow) {
            const unsigned source=spotShadow?16:0;
            auto shadow=worldLocalShadowView(env,source);check(shadow.enabled,"Local shadow view");
            check(locals.beginWorldLocalShadow(source,shadow),"Local shadow admission");drawBlocker(shadow);check(locals.endWorldLocalShadow(),"Local shadow end");
        }
    };
    const auto end=[&]{r->endWorldSceneColorPass();r->endFrame();if(!r->handlesPresentation())window.swapBuffers();};
    const auto run=[&] {
        begin();
        if(opaque) {
            std::array<IRenderBackend::WorldMeshVertex,4> v{};
            for(unsigned i=0;i<4;++i){v[i].x=i==0||i==3?-10.f:10.f;v[i].y=i<2?-10.f:10.f;v[i].z=-2;
                v[i].r=v[i].g=v[i].b=v[i].a=v[i].nz=v[i].tx=v[i].tw=1;}
            const unsigned char white[]{255,255,255,255};const std::uint32_t ix[]{0,1,2,0,2,3};
            IRenderBackend::WorldTextureData m;m.key=m.cacheKey="particle-occluder";m.rgba=white;m.width=m.height=1;m.materialMode=2;
            check(scopes.beginWorldResourceScope(u.owner),"Depth fixture owner");
            r->drawWorldIndexedMeshTextured(v.data(),4,ix,6,&m,u.view.viewProjection.data(),256,256);
            check(scopes.endWorldResourceScope(),"Depth fixture owner finish");
            WorldPrecipitationLightingView fog;fog.owner=u.owner;fog.view=u.view;fog.environment=u.environment;fog.quality=0;fog.range=64;
            if(depthMismatch)++fog.view.identity;
            dynamic_cast<IWorldPrecipitationLighting&>(*r).updateWorldPrecipitationLighting(fog);
        }
        output=lighting.updateWorldParticleLighting(u);
        check(output.sharedDepth==(opaque&&!depthMismatch),"Shared depth view admission");
        check(!output.texture.empty()&&output.tiles==1&&output.texels==4,"Small quad allocation");
        // Exercise the ordinary material descriptor route, not just readback.
        check(scopes.beginWorldResourceScope(u.owner),"Material owner");
        std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
        for(unsigned i=0;i<4;++i){auto& v=vertices[i];const auto p=quads[0].corners[i];
            v.x=p.x;v.y=p.y;v.z=p.z;v.r=v.g=v.b=v.a=v.nz=v.tx=v.tw=1;}
        applyParticleLightingCoordinates(vertices,output.coordinates);
        const unsigned char white[]{255,255,255,255};const std::uint32_t indices[]{0,1,2,0,2,3};
        IRenderBackend::WorldTextureData m;m.key=m.cacheKey="particle-lighting-binding";m.rgba=white;m.width=m.height=1;
        m.emissiveKey=output.texture.c_str();
        r->drawWorldIndexedMeshTextured(vertices.data(),4,indices,6,&m,u.view.viewProjection.data(),256,256);
        check(scopes.endWorldResourceScope(),"Material owner finish");
        check(lighting.captureWorldParticleLighting(),"Particle lighting capture");end();
        auto result=lighting.readWorldParticleLighting();check(result.identity==u.view.identity&&result.frame==u.view.frame,"View identity");
        check(result.bytes==8ull*1024*1024&&result.lighting.size()==1024*1024,"Lighting allocation");
        ++u.view.frame;return result;
    };
    const auto pixels=[&](const auto& image) {
        std::array<std::array<float,4>,4> result;
        for(unsigned i=0;i<4;++i){const auto uv=output.coordinates[0][i];result[i]=image.lighting[unsigned(uv.y*1024)*1024+unsigned(uv.x*1024)];}
        return result;
    };
    const double pi=std::acos(-1.);
    const auto ambient=pixels(run());
    for(const auto& v:ambient)for(unsigned c=0;c<3;++c)
        check(std::abs(v[c]*64-env.ambientRadiance[c]/pi)<.00015,"Independent ambient / pi, HDR packing");
    env.ambientRadiance={0,0,0};
    auto& point=env.pointLights[0];point.position={0,0,-2};point.radius=8;point.intensity={3,0,6};
    point.participation=WorldLightParticles|WorldLightDiffuse;
    const auto pointImage=pixels(run());
    for(unsigned i=0;i<4;++i) {
        const glm::dvec3 p=quads[0].corners[i],delta=glm::dvec3(0,0,-2)-p;
        const double d2=glm::dot(delta,delta),nl=delta.z/std::sqrt(d2);
        const double wrap=(nl+.65)/(1.65*1.65),q=d2/64,atten=std::pow(1-q*q,2)/d2;
        for(unsigned c=0;c<3;++c)check(std::abs(pointImage[i][c]*64-point.intensity[c]*wrap*atten/pi)<.0002,"Independent inverse-square wrapped point response");
    }
    point.participation=WorldLightDiffuse|WorldLightSurfaces;
    for(const auto& v:pixels(run()))check(v[0]==0&&v[1]==0&&v[2]==0,"Non-particle light leaked");
    point.participation=WorldLightDiffuse|WorldLightParticles;point.channels=2;env.lightChannels=1;
    for(const auto& v:pixels(run()))check(v[0]==0&&v[1]==0&&v[2]==0,"Particle light channel leaked");
    point.channels=1;point.position={0,0,-6};
    const auto back=pixels(run());
    for(unsigned i=0;i<4;++i) {
        const glm::dvec3 p=quads[0].corners[i],delta=glm::dvec3(0,0,-6)-p,view=glm::normalize(-p),l=glm::normalize(delta);
        const double nl=l.z,wrapped=std::clamp((nl+.65)/(1.65*1.65),0.,1.);
        const double a=wrapped+(-nl*.5)*std::clamp(-glm::dot(view,l),0.,1.);
        const double d2=glm::dot(delta,delta),q=d2/64,atten=std::pow(1-q*q,2)/d2;
        for(unsigned c=0;c<3;++c)check(std::abs(back[i][c]*64-point.intensity[c]*a*atten/pi)<.0003,"Independent particle back-lighting");
    }
    env.pointLights={};env.sunRadiance={2,1,.5f};env.sunDirection={0,0,1};
    const auto sun=pixels(run());
    for(const auto& v:sun)for(unsigned c=0;c<3;++c)
        check(std::abs(v[c]*64-env.sunRadiance[c]/(1.65*pi))<.0005,"Independent wrapped directional response");
    sunShadow=true;
    for(const auto& v:pixels(run()))check(v[0]<.000001&&v[1]<.000001&&v[2]<.000001,"Directional shadow did not suppress particle lighting");
    sunShadow=false;
    env.sunRadiance={0,0,0};
    auto& rect=env.rectLights[0];rect.position={0,0,-2};rect.halfU={1,0,0};rect.halfV={0,-.1f,0};rect.range=8;
    rect.radiance={3,0,6};rect.participation=WorldLightParticles|WorldLightDiffuse;
    const auto area=pixels(run());check(area[0][0]>0&&area[0][2]>area[0][0]*1.9f,"Finite rectangle particle response");
    rect.participation|=WorldLightShadows;localShadow=true;
    const auto shadowed=pixels(run());
    for(unsigned i=0;i<4;++i)check(shadowed[i][0]<area[i][0]*.05f,"Local moment shadow did not suppress particle lighting");
    localShadow=false;
    const auto savedRect=rect;rect={};
    auto& spot=env.pointLights[0];spot.position={0,0,-2};spot.radius=8;spot.intensity={3,0,6};
    spot.source.axis={0,0,-1};spot.source.cone=true;spot.source.radius=.1f;
    const auto spotLit=pixels(run());check(spotLit[0][0]>0,"Spot particle control absent");
    spotShadow=true;const auto spotDark=pixels(run());spotShadow=false;
    for(unsigned i=0;i<4;++i)check(spotDark[i][0]<spotLit[i][0]*.05f,"Spot moment shadow did not reach particles");
    env.pointLights={};rect=savedRect;
    rect.halfV={0,.1f,0};
    for(const auto& v:pixels(run()))check(v[0]==0&&v[1]==0&&v[2]==0,"One-sided rectangle backside lit");
    env.rectLights={};env.ambientRadiance={.4f,.2f,.1f};
    for(unsigned quality=0;quality<4;++quality){u.quality=quality;const auto v=pixels(run());check(v==ambient,"Preset changed uniform illumination");}
    u.view.identity=18;check(pixels(run())==ambient,"Second view contaminated");
    u.view.identity=17;env.ambientRadiance={.8f,.4f,.2f};const auto changed=pixels(run());
    for(unsigned c=0;c<3;++c)check(std::abs(changed[0][c]-ambient[0][c]*2)<.00002,"Dynamic lighting stale");
    opaque=true;const auto hidden=pixels(run());
    for(const auto& v:hidden)check(v[0]==0&&v[1]==0&&v[2]==0&&v[3]==0,"Shared conservative depth did not reject hidden splash");
    depthMismatch=true;const auto mismatch=pixels(run());
    check(mismatch[0][0]>0&&mismatch[0][3]==1,"Foreign-view depth rejected splash");
    opaque=false;depthMismatch=false;
    check(pixels(run())[0][3]==1,"Old-frame depth rejected splash");
    // A broad billboard with a coarse light tile crosses a nearby source.
    // Its finite texel footprint must prevent a one-frame inverse-square flash.
    env.ambientRadiance={0,0,0};env.pointLights={};env.rectLights={};
    quads[0].billboard=true;quads[0].lightingMipBias=8;
    quads[0].corners={glm::vec3(-1,-1,-4),glm::vec3(1,-1,-4),glm::vec3(1,1,-4),glm::vec3(-1,1,-4)};
    auto& crossing=env.pointLights[0];crossing.radius=8;crossing.intensity={3,0,6};
    crossing.participation=WorldLightParticles|WorldLightDiffuse;
    float previous=-1;
    for(float offset:{-.02f,-.01f,0.f,.01f,.02f}) {
        crossing.position={1+offset,1,-3.98f};
        const auto image=pixels(run());const auto& v=image[2];
        for(unsigned c=0;c<3;++c)check(std::isfinite(v[c])&&v[c]>=0&&v[c]*64<6,"Billboard source crossing flash");
        check(v[2]>0&&std::abs(v[2]-v[0]*2)<.0001,"Filtered light must preserve source hue");
        if(previous>=0)check(std::abs(v[2]*64-previous)<.6f,"Billboard source crossing discontinuity");
        previous=v[2]*64;
    }
    check(scopes.requestWorldResourceScopeRelease(u.owner),"Scope release");begin();end();
    check(scopes.getWorldResourceStats().liveScopes==0,"Particle view owner leaked");
    r->shutdown();std::cout<<"PASS "<<api<<" particle lighting: independent ambient, direct/back lighting, particle/channel flags, rectangle hemisphere, presets, views, ordinary binding and owner release\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
