#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldWetSurfaces.h"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace engine::render;
void check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
int main(int argc,char** argv)try {
    check(argc==3,"Expected API and output JSON");
    const std::string api=argv[1];check(api=="vulkan"||api=="d3d12","API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Wet surface numerics",128,128,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),128,128,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),128,128,false);
    auto& r=*renderer;auto& scope=dynamic_cast<IWorldResourceScope&>(r);
    auto& maps=dynamic_cast<IWorldRainMap&>(r);auto& wet=dynamic_cast<IWorldWetSurfaces&>(r);
    auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(r);
    const auto owner=scope.createWorldResourceScope(96ull*1024*1024);
    check(scope.beginWorldResourceScope(owner),"Prepare owner");maps.prewarmWorldRainMap(owner,2048);check(scope.endWorldResourceScope(),"Prepare owner close");
    constexpr std::array<unsigned char,4> drops{128,179,204,230},leaks{180,110,128,255},ripples{150,115,178,255},white{255,255,255,255};
    constexpr std::uint32_t indices[]{0,1,2,0,2,3};
    const std::array names{"dry","damp","puddle","drop","full","slope","wall","metal","emissive","emissive-soft","character","weapon","vehicle","roof","interior","car","water","drying","distant","pattern-a","pattern-b","pattern-c","wall-pattern-a","wall-pattern-b","interior-edge","car-edge","water-shallow","roof-edge"};
    nlohmann::json report={{"api",api},{"cases",nlohmann::json::array()}};
    WorldRainMap planner;
    for(unsigned index=0;index<names.size();++index) {
        const std::string mode=names[index];const bool wall=mode.starts_with("wall");const bool pattern=mode.find("pattern")!=std::string::npos;
        std::array<std::vector<unsigned char>,3> patterns;
        std::array<IRenderBackend::WorldTextureMipLevel,4> levels;
        std::vector<unsigned char> fields(8*24*4);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            if(mode=="interior"||(mode=="interior-edge"&&x>=4))for(unsigned c=0;c<4;++c)fields[(y*24+x)*4+c]=255;
            if(mode=="car"||(mode=="car-edge"&&x>=4)) {
                const auto encode=[](float h){return unsigned(std::round((h+64)/128*65535));};
                const auto a=(y*24+x+8)*4;const unsigned top=encode(4),bottom=encode(-1);
                fields[a]=static_cast<std::uint8_t>(top&255);fields[a+1]=static_cast<std::uint8_t>(top>>8);fields[a+2]=static_cast<std::uint8_t>(bottom&255);fields[a+3]=static_cast<std::uint8_t>(bottom>>8);
            }
            if(mode=="water"||mode=="water-shallow") {
                const auto a=(y*24+x+16)*4;const unsigned h=unsigned(std::round((mode=="water-shallow"?64.f:66.f)/128*65535));
                fields[a]=static_cast<std::uint8_t>(h&255);fields[a+1]=static_cast<std::uint8_t>(h>>8);fields[a+2]=255;
            }
        }
        WorldWetSurfaceView view;view.owner=owner;view.view={101,1,index+1,128,128,false};
        view.view.cameraValid=true;view.view.cameraPosition=wall?std::array<float,3>{0,0,3}:std::array<float,3>{0,mode=="distant"?150.f:3.f,0};
        auto eye=glm::make_vec3(view.view.cameraPosition.data());
        auto vp=glm::ortho(-2.f,2.f,-2.f,2.f,.05f,300.f)*glm::lookAt(eye,glm::vec3(0),wall?glm::vec3(0,1,0):glm::vec3(0,0,-1));
        std::copy_n(glm::value_ptr(vp),16,view.view.viewProjection.begin());
        if(mode=="weapon")view.weaponDepthMaximum=1;
        view.interior={-4,-4,1,8};view.interiorHeight={-8,1,32,0};view.vehicle={-6,-6,12,0};view.water={-64,-64,128,-64};
        std::array<std::string,4> keys{"wet-test-drops","wet-test-leaks","wet-test-ripples","wet-test-fields-"+mode};
        if(pattern)for(unsigned i=0;i<3;++i) {
            const unsigned size=i==2?16:8;patterns[i].resize(size*size*4);
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
                const unsigned at=(y*size+x)*4;
                patterns[i][at]=(x*29+y*7+i*11)%256;
                patterns[i][at+1]=(x*13+y*31+63+i*17)%256;
                patterns[i][at+2]=(x*19+y*23+91+i*31)%256;
                patterns[i][at+3]=static_cast<unsigned char>(110+(x*17+y*11)%146);
            }
        }
        for(unsigned i=0;i<4;++i) {
            if(pattern&&i<3)keys[i]+="-"+mode;
            auto& t=view.textures[i];t.key=t.cacheKey=keys[i].c_str();t.width=i==3?24:1;t.height=i==3?8:1;
            t.rgba=i==0?drops.data():i==1?leaks.data():i==2?ripples.data():fields.data();
            if(pattern&&i<3){t.width=t.height=i==2?16:8;t.rgba=patterns[i].data();}
            t.textureSrgb=0;t.wrapS=t.wrapT=i<3?10497:33071;t.linearMipSampling=i<3?2:1;
            levels[i]={t.rgba,t.width,t.height};t.mipLevels=&levels[i];t.mipLevelCount=1;
        }
        check(scope.beginWorldResourceScope(owner),"Mask owner");
        for(const auto& t:view.textures)r.prewarmWorldTextureData(&t);
        check(scope.endWorldResourceScope(),"Mask owner close");
        WorldEnvironmentLighting environment;environment.enabled=environment.surfaceWeatherEnabled=true;
        if(mode=="roof-edge")environment.weatherShelters[0]={{-4,-4,0,4},2};
        environment.surfaceWetness=mode=="dry"?0:mode=="damp"?.3f:mode=="drop"?.2f:1.f;
        environment.puddleAmount=mode=="dry"||mode=="damp"||mode=="drop"?0.f:1.f;
        environment.rainIntensity=mode=="dry"||mode=="damp"||mode=="puddle"||mode=="drying"?0.f:1.f;
        environment.precipitationSeconds=mode=="drop"||mode=="pattern-c"?1.3f:mode.ends_with("pattern-b")?.8f:.37f;
        const auto plane=[&](const float* matrix,float elevation,bool roof) {
            std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
            for(unsigned j=0;j<4;++j) {
                auto& v=vertices[j];const float x=j==0||j==3?-4.f:4.f,z=j<2?-4.f:4.f;
                v.x=roof&&mode=="roof-edge"?std::min(x,0.f):x;v.y=elevation;v.z=z;v.ny=1;v.tx=v.tw=1;v.r=v.g=v.b=v.a=1;
                if(!roof&&wall){v.y=-z;v.z=0;v.ny=0;v.nz=1;}
                if(!roof&&mode=="slope"){v.y=z*.2679491924f;v.ny=.9659258263f;v.nz=-.2588190451f;}
            }
            IRenderBackend::WorldTextureData t;t.key=t.cacheKey="wet-test-white";t.rgba=white.data();t.width=t.height=1;
            t.materialMode=2;t.normalScale=0;t.metallicFactor=mode=="metal"?.8f:0;t.roughnessFactor=.8f;t.textureSrgb=0;
            t.vertexColorMulR=.5f;t.vertexColorMulG=.4f;t.vertexColorMulB=.3f;
            t.environment=roof?WorldEnvironmentLighting{}:environment;
            t.cameraPosX=eye.x;t.cameraPosY=eye.y;t.cameraPosZ=eye.z;
            if((mode=="emissive"||mode=="emissive-soft")&&!roof) {
                t.emissiveKey=t.emissiveCacheKey="wet-test-white";t.emissiveRgba=white.data();t.emissiveWidth=t.emissiveHeight=1;
                t.emissiveFactorR=t.emissiveFactorG=t.emissiveFactorB=mode=="emissive-soft"?.05f:1.f;
            }
            r.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices,6,&t,matrix,128,128);
        };
        r.beginFrame(0,0,0,1);r.beginWorldSceneColorPass(128,128);
        check(scope.beginWorldResourceScope(owner),"Draw owner");
        for(const auto& job:planner.plan({0,3,0},index+1))planner.execute(maps,owner,job,
            [&](std::size_t&,std::size_t,std::uint64_t,bool& complete){if(mode=="roof"||mode=="roof-edge")plane(job.view.viewProjection.data(),2,true);complete=true;},[]{});
        view.cascades=planner.published();view.rainMap=maps.worldRainMapTexture(owner);
        if(index>1) {
            auto stale=view;stale.view.frame=index;
            bool rejected=false;try{wet.bindWorldWetSurfaces(&stale);}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"Stale wet view was accepted");
        }
        wet.bindWorldWetSurfaces(&view);
        check(surface.beginWorldSurfaceBuffer(view.view.viewProjection.data(),128,128),"Surface begin");
        WorldMotionPose pose;pose.temporalMaterial=mode=="character"?WorldTemporalMaterial::Character:mode=="vehicle"?WorldTemporalMaterial::Vehicle:WorldTemporalMaterial::Geometry;
        WorldMotionSample motion{&pose,11,false};
        check(surface.setWorldSurfaceMotion(view.view.viewProjection,{&motion,1}),"Surface class");
        plane(view.view.viewProjection.data(),0,false);
        check(surface.endWorldSurfaceBuffer(),"Surface end");
        check(surface.captureWorldSurfaceMotion(),"Surface capture");
        check(scope.endWorldResourceScope(),"Draw owner close");
        r.endWorldSceneColorPass();r.endFrame();if(!r.handlesPresentation())window.swapBuffers();
        const auto data=surface.readWorldSurfaceMotion();
        check(data.width==128&&data.height==128&&!data.material.empty(),"Surface readback");
        nlohmann::json row={{"case",mode},{"frame",view.view.frame},{"camera",view.view.cameraPosition},{"rain",environment.rainIntensity},
            {"moisture",environment.surfaceWetness},{"puddles",environment.puddleAmount},{"time",environment.precipitationSeconds},
            {"samples",nlohmann::json::array()}};
        for(unsigned y:{32u,64u,96u})for(unsigned x:{32u,64u,96u}) {
            const auto at=y*128+x;
            row["samples"].push_back({{"pixel",{x,y}},{"albedo",data.albedo[at]},{"normal",data.normal[at]},
                                     {"material",data.material[at]},{"metadata",data.identityMetadata[at]},{"id",data.identity[at]}});
        }
        if(mode=="roof-edge") {
            // Same plane/material across a half-roof: dampness must vary through
            // many pixels, never turn into a single hard light-looking rectangle.
            const auto red=[](std::uint32_t packed){const float v=float(packed&1023u)/1023.f;return v*v;};
            float largest=0;unsigned gradients=0;
            for(unsigned x=17;x<112;++x) {
                const float a=red(data.albedo[64*128+x-1]),b=red(data.albedo[64*128+x]);
                largest=std::max(largest,std::abs(a-b));
                if(std::abs(a-b)>.0005f)++gradients;
            }
            check(gradients>15,"Shelter edge remained binary");
            check(largest<.04f,"Shelter edge has a hard material discontinuity");
            check(red(data.albedo[64*128+16])>red(data.albedo[64*128+112])+.05f,"Deep shelter lost dry material");
            row["largest_edge_step"]=largest;row["gradient_pixels"]=gradients;
        }
        report["cases"].push_back(row);
    }
    // Rebinding an owner after release must fail, never retain borrowed images.
    check(scope.requestWorldResourceScopeRelease(owner),"Owner release");
    r.beginFrame(0,0,0,1);r.endFrame();
    check(scope.getWorldResourceStats().liveScopes==0,"Wet owner leaked");
    renderer->shutdown();renderer.reset();
    std::ofstream(argv[2])<<report.dump(2)<<'\n';
    std::cout<<"PASS native wet material readbacks "<<api<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
