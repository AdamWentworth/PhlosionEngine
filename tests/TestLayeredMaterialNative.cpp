#include "engine/core/Environment.h"
#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/IWorldLayeredMaterials.h"
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/render/WorldLayeredSampling.h"
#include "engine/render/WorldMaterialProfile.h"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace engine::render;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
double linear(unsigned byte){const double c=double(byte)/255;return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
WorldLayerTexture texture(const char* key,std::array<std::uint8_t,4> bytes,bool srgb=false){
    WorldLayerTexture t;t.identity=key;t.srgb=srgb;t.mips.push_back({1,1,{bytes.begin(),bytes.end()}});return t;
}
std::shared_ptr<WorldLayerMaterialSet> materialSet(bool replacement){
    auto set=std::make_shared<WorldLayerMaterialSet>();set->identity=replacement?"replacement":"layered-native";
    set->textures={texture("layer-color-a",replacement?std::array<std::uint8_t,4>{16,220,64,255}:std::array<std::uint8_t,4>{204,64,32,255},true),
        texture("layer-color-b",{32,96,220,255},true),texture("layer-normal",{128,128,255,255}),
        texture("layer-data-a",{96,0,0,255}),texture("layer-data-b",{192,0,0,255}),
        texture("layer-black",{0,0,0,255}),texture("layer-micro",{128,128,255,255}),
        texture("layer-macro",{180,180,180,255},true)};
    WorldLayerTexture atlas;atlas.identity="layer-mask";atlas.mips.push_back({8,4,std::vector<std::uint8_t>(128,255)});
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)atlas.mips[0].rgba[(y*8+x)*4]=128;
    for(unsigned size=4;size;size/=2)atlas.mips.push_back({size,std::max(1u,size/2),std::vector<std::uint8_t>(size*std::max(1u,size/2)*4,255)});
    set->textures.push_back(atlas);
    WorldLayerMaterial m;m.layers.resize(1);auto& a=m.layers[0];a.layerOffsetNormal[2]=0;
    a.textures={6,0u|(2u<<16),3u|(5u<<16),0};m.maskTexture=8;
    m.masks.highResolution={4,2};m.masks.lowResolution={2,1};m.masks.atlasExtent={8,4};m.masks.tileSize=2;
    m.masks.high={{0,1},{1,1}};m.masks.low={{2,1}};m.masks.records={0,0,0};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)m.masks.atlas.push_back(x<4?128:255);
    set->materials.push_back(m);
    auto two=m;two.layers.push_back(a);two.layers[1].textures={6,1u|(2u<<16),4u|(5u<<16),0};two.layers[1].tintOpacity[3]=.75f;
    two.masks.high={{0,1},{1,3}};two.masks.low={{3,3}};two.masks.records={0,0,1,0,0};
    set->materials.push_back(two);
    auto terrain=m;terrain.terrain=true;terrain.macroColor=7;terrain.layers[0].metalnessRemap={0,.5f,1,0};
    set->materials.push_back(terrain);
    set->textures.push_back(texture("layer-global-normal",{160,96,255,255}));
    auto meshGlobal=m;meshGlobal.globalNormal=9;meshGlobal.globalNormalIntensity=.7f;meshGlobal.normalYSign=-1;
    meshGlobal.layers[0].layerOffsetNormal[2]=.5f;
    set->materials.push_back(meshGlobal);
    auto terrainGlobal=terrain;terrainGlobal.globalNormal=9;terrainGlobal.terrainDistanceFade=false;
    terrainGlobal.layers[0].layerOffsetNormal[2]=.8f;
    set->materials.push_back(terrainGlobal);
    // A deliberately nonconstant color map makes sampler wrapping and UV
    // orientation observable; complete lower mips are owned by this texture.
    WorldLayerTexture pattern;pattern.identity="layer-pattern";pattern.srgb=true;
    pattern.mips={{2,2,{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255}},
                  {1,1,{188,188,188,255}}};
    set->textures.push_back(pattern);
    auto textured=m;textured.layers[0].textures[1]=10u|(2u<<16);textured.normalYSign=-1;
    set->materials.push_back(textured);
    set->textures.push_back(texture("layer-strong-normal",{172,96,255,255}));
    auto projected=textured;projected.terrain=true;projected.terrainDistanceFade=false;
    projected.layers[0].detail[0]=128;projected.layers[0].textures[1]=10u|(11u<<16);projected.layers[0].layerOffsetNormal[2]=.7f;
    projected.layers[0].metalnessRemap={0,.5f,1,0};
    set->materials.push_back(projected);
    auto distant=projected;distant.terrainDistanceFade=true;set->materials.push_back(distant);
    (void)packWorldLayerMaterialSet(*set);return set;
}
}
int main(int argc,char** argv)try{
    check(argc==3||argc==4,"Expected API, optional project profile root, and JSON path");
    const char* output=argv[argc-1];const std::string api=argv[1];
    check(api=="d3d12"||api=="vulkan","API");
    SDL_SetMainReady();struct Quit{~Quit(){SDL_Quit();}}quit;
    Window window("Layered material native bindings",128,128,api=="vulkan"?Window::GraphicsApi::Vulkan:Window::GraphicsApi::Native,false,false);
    std::unique_ptr<IRenderBackend> renderer;
    if(api=="vulkan")renderer=std::make_unique<VulkanRenderBackend>(window.getSDLWindow(),128,128,false);
    else renderer=std::make_unique<D3D12RenderBackend>(window.getSDLWindow(),128,128,false);
    auto& r=*renderer;
    auto profile=argc==4?loadWorldMaterialProfile(argv[2],"config/render/world_materials.json"):WorldMaterialProfile{};
    profile.textureMipDrop=unsigned(std::stoul(engine::env::get("PHLOSION_TEST_TEXTURE_MIP_DROP").value_or("0")));
    profile.maxAnisotropy=unsigned(std::stoul(engine::env::get("PHLOSION_TEST_ANISOTROPY").value_or("16")));
    r.setWorldMaterialProfile(profile);
    const auto reference=[&](const WorldLayerMaterialSet& original) {
        auto copy=original;
        for(unsigned i=0;i<copy.textures.size();++i) {
            if(std::any_of(copy.materials.begin(),copy.materials.end(),[&](const auto& m){return m.maskTexture==i||m.tilingMap==i;}))continue;
            auto& mips=copy.textures[i].mips;
            mips.erase(mips.begin(),mips.begin()+std::min(std::size_t(profile.textureMipDrop),mips.size()-1));
        }return copy;
    };
    auto& scopes=dynamic_cast<IWorldResourceScope&>(r);
    auto& layered=dynamic_cast<IWorldLayeredMaterials&>(r);auto& surface=dynamic_cast<IWorldSurfaceBuffer&>(r);
    auto source=materialSet(false);auto owner=scopes.createWorldResourceScope(16u<<20);
    check(scopes.beginWorldResourceScope(owner),"Prewarm owner");
    layered.bindWorldLayeredMaterials(owner,source);
    check(source.use_count()==1,"Renderer retained plugin-owned host data");
    check(scopes.endWorldResourceScope(),"Prewarm scope exit");
    nlohmann::json report={{"api",api},{"mip_drop",profile.textureMipDrop},{"anisotropy",profile.maxAnisotropy},{"cases",nlohmann::json::array()}};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(unsigned i=0;i<4;++i){auto& v=vertices[i];v.x=i==0||i==3?-2.f:2.f;v.z=i<2?-2.f:2.f;
        v.u=i==0||i==3?0.f:1.f;v.v=i<2?0.f:1.f;v.ny=v.tx=v.tw=v.r=v.g=v.b=v.a=1;}
    constexpr std::uint32_t indices[]{0,1,2,0,2,3};constexpr std::array<std::uint8_t,4> white{255,255,255,255};
    const auto vp=glm::ortho(-2.f,2.f,-2.f,2.f,.1f,10.f)*glm::lookAt(glm::vec3(0,3,0),glm::vec3(0),glm::vec3(0,0,-1));
    for(unsigned frame=0;frame<18;++frame){
        const bool replacement=frame>=9;const unsigned material=frame%9;
        if(frame==9){check(scopes.requestWorldResourceScopeRelease(owner),"Retire layered owner");owner=scopes.createWorldResourceScope(16u<<20);source=materialSet(true);}
        r.beginFrame(0,0,0,1);r.beginWorldSceneColorPass(128,128);
        check(scopes.beginWorldResourceScope(owner),"Begin layered owner");
        WorldClusteredLightingView lighting;lighting.environment.enabled=true;
        check(replacement?surface.beginWorldSurfaceCompositionCapture(glm::value_ptr(vp),128,128,lighting):
            surface.beginWorldSurfaceBuffer(glm::value_ptr(vp),128,128),"Begin surface");

        layered.bindWorldLayeredMaterials(owner,source);
        if(frame==0||frame==9) {
            const auto reduced=reference(*source);std::uint64_t textureBytes=0;
            for(const auto& t:reduced.textures)for(const auto& mip:t.mips)textureBytes+=mip.rgba.size();
            WorldResourceScopeStats uploaded;check(scopes.getWorldResourceScopeStats(owner,uploaded),"Upload counts");
            const auto expected=textureBytes+packWorldLayerMaterialSet(*source).words.size()*8;
            check(uploaded.retained.payloadBytes==expected,"Texture quality retained the wrong mip payload");
            report["upload_bytes"].push_back(uploaded.retained.payloadBytes);
        }
        WorldResourceScopeStats before,after;check(scopes.getWorldResourceScopeStats(owner,before),"Layered counts");
        layered.bindWorldLayeredMaterials(owner,source);check(scopes.getWorldResourceScopeStats(owner,after),"Repeated counts");
        check(before.retained.payloadBytes==after.retained.payloadBytes&&before.retained.textures==after.retained.textures,"Repeated binding uploaded again");
        bool rejected=false;try{layered.bindWorldLayeredMaterials(owner,materialSet(replacement));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Mutable material set admitted into existing owner");
        layered.bindWorldLayeredMaterials(owner,source);
        IRenderBackend::WorldTextureData t;t.key=t.cacheKey="layered-native-white";t.rgba=white.data();t.width=t.height=1;t.textureSrgb=0;
        t.normalScale=0;t.materialMode=2;t.layeredMaterialIndex=material==3?0u:(material<3?material+1u:material);t.metallicFactor=0;t.roughnessFactor=.8f;
        t.environment.enabled=true;t.cameraPosX=t.cameraPosZ=0;t.cameraPosY=material==8?150.f:3.f;
        t.cameraForwardX=t.cameraForwardZ=0;t.cameraForwardY=-1;
        for(auto& vertex:vertices){vertex.tx=material==4?0.f:1.f;vertex.tw=1;}
        r.beginWorldIndexedBatchSubmission();
        r.drawWorldIndexedMeshTexturedCached(material==4?"layered-native-derived-tangent":"layered-native-plane",vertices.data(),4,indices,6,&t,glm::value_ptr(vp),128,128);
        r.endWorldIndexedBatchSubmission();
        check(surface.endWorldSurfaceBuffer(),"End surface");check(surface.captureWorldSurfaceMotion(),"Readback request");
        check(scopes.endWorldResourceScope(),"End layered owner");
        r.endWorldSceneColorPass();r.endFrame();if(!r.handlesPresentation())window.swapBuffers();
        const auto data=surface.readWorldSurfaceMotion();check(data.width==128&&data.height==128&&!data.albedo.empty(),"Missing surface readback");
        nlohmann::json row={{"frame",frame},{"material",material},{"replacement",replacement},{"samples",nlohmann::json::array()}};
        for(unsigned y:{32u,64u,96u})for(unsigned x:{32u,96u}){
            const auto at=y*128+x;
            row["samples"].push_back({{"pixel",{x,y}},{"albedo",data.albedo[at]},{"normal",data.normal[at]},{"material",data.material[at]}});
        }
        report["cases"].push_back(row);std::ofstream(output)<<report.dump(2)<<'\n';
        for(unsigned y:{32u,64u,96u})for(unsigned x:{32u,96u}){
            const auto at=y*128+x;
            if(material>=4){
                const float u=(float(x)+.5f)/128,v=(float(y)+.5f)/128;
                auto expected=sampleWorldLayerMaterial(reference(*source),material,{u,v},{4*u-2,0,4*v-2},{0,1,0},{1,0,0,material==4?-1.f:1.f});
                if(material==8)expected.normal={0,1,0};
                for(unsigned c=0;c<3;++c){
                    check(std::abs(double((data.albedo[at]>>(10*c))&1023u)/1023-std::sqrt(expected.color[c]))<.0021,"Native/CPU textured color");
                    check(std::abs(double((data.normal[at]>>(10*c))&1023u)/1023-(expected.normal[c]*.5+.5))<.0021,"Native/CPU composed normal");
                }
                check(std::abs(double((data.material[at]>>24u)&255u)/255-expected.roughness)<.004,"Native/CPU roughness");
                continue;
            }
            const double weight=material==1?.75*(x<64?128.0/255:1.0):0;
            const std::array<unsigned,3> a=replacement?std::array<unsigned,3>{16,220,64}:std::array<unsigned,3>{204,64,32},b{32,96,220};
            for(unsigned c=0;c<3;++c){
                double expected=material==3?1.0:linear(a[c])*(1-weight)+linear(b[c])*weight;
                if(material==2)expected=std::min(1.0,expected*2*linear(180));
                // Surface storage is RGB10 sqrt-colour, independent of lighting.
                check(std::abs(double((data.albedo[at]>>(10*c))&1023u)/1023-std::sqrt(expected))<.0021,"Layered surface colour mismatch");
                check(std::abs(double((data.normal[at]>>(10*c))&1023u)/1023-(c==1?1.0:.5))<.0021,"Layered flat normal mismatch");
            }
            const double rough=material==3?.8:(96.0*(1-weight)+192.0*weight)/255;
            check(std::abs(double((data.material[at]>>0u)&255u)/255)<.004,"Layered dielectric became metal");
            check(std::abs(double((data.material[at]>>8u)&255u)/255-(material==2?.02:.04))<.004,"Layered dielectric F0");
            check(std::abs(double((data.material[at]>>24u)&255u)/255-rough)<.004,"Layered roughness mismatch");

        }
    }
    check(scopes.requestWorldResourceScopeRelease(owner),"Final layered retirement");r.beginFrame(0,0,0,1);r.endFrame();
    check(scopes.getWorldResourceStats().liveScopes==0,"Layered scope leaked");
    layered.bindWorldLayeredMaterials(0,nullptr);
    renderer->shutdown();renderer.reset();std::ofstream(output)<<report.dump(2)<<'\n';
    std::cout<<"PASS native layered colour, sparse high/low masks, priority, terrain specular, fallback, reuse and retirement "<<api<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
