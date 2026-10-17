#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace engine::render;
using namespace engine::render::gltf::scene;
namespace {
void check(bool v,const char* reason) {if(!v) throw std::runtime_error(reason);}
template<class F> void rejects(F f,const char* reason) {bool caught=false;try {f();}catch(const std::exception&){caught=true;}check(caught,reason);}
void quad(GltfSceneAsset& a,float y,int material) {
    GltfScenePrimitive p;p.nodeIndex=0;p.materialIndex=material;
    for(const glm::vec2 xz:{glm::vec2(-10,-10),glm::vec2(10,-10),glm::vec2(10,10),glm::vec2(-10,10)}) {
        WorldMeshVertex v;v.x=xz.x;v.y=y;v.z=xz.y;v.ny=1;v.u=(xz.x+10)/20;v.v=(xz.y+10)/20;p.vertices.push_back(v);
    }
    p.indices={0,2,1,0,3,2};a.primitives.push_back(p);
}
float energy(const DiffuseProbeTransport& t) {float e=0;for(const auto& sh:t.transfer) for(const auto& rgb:sh) for(float c:rgb) e+=std::abs(c);return e;}
}
int main() try {
    constexpr float pi=3.14159265358979323846f;
    DiffuseSh uniform{};uniform[0]={pi/.282094792f,2*pi/.282094792f,3*pi/.282094792f};
    for(auto n:{glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,0,-1)})
        check(glm::length(evaluateDiffuseSh(uniform,n)-glm::vec3(pi,2*pi,3*pi))<1e-5f,"SH cosine energy/orientation");
    GltfSceneAsset a;a.sourceIdentity="public-diffuse-floor";a.nodes.resize(1);a.materials.resize(1);
    a.materials[0].metallicFactor=0;a.materials[0].baseColorFactor={.5f,.25f,.125f,1};quad(a,0,0);
    const WorldRectLight light{{0,3,0},20,{1,0,0},{0,0,1},{4,2,1}};
    DiffuseProbeBakeSettings settings;settings.grid.origin={-.5f,.5f,-.5f};settings.grid.spacing={1,1,1};
    settings.rayCount=512;settings.areaSamplesU=8;settings.areaSamplesV=8;settings.maxDistance=20;
    auto transport=bakeDiffuseProbes(a,std::span(&light,1),settings);
    check(energy(transport)>.1f,"No floor bounce");
    auto reversed=a;
    for(auto& p:reversed.primitives) for(std::size_t i=0;i<p.indices.size();i+=3) std::swap(p.indices[i+1],p.indices[i+2]);
    check(std::abs(energy(bakeDiffuseProbes(reversed,std::span(&light,1),settings))-energy(transport))<1e-5f,
        "Two-sided world winding changed diffuse transport");
    const auto down=evaluateDiffuseSh(transport.transfer[0],{0,-1,0}),up=evaluateDiffuseSh(transport.transfer[0],{0,1,0});
    check(down.x>up.x*5&&down.x>0,"Bounce direction not preserved");
    check(std::abs(down.x/down.y-2)<.001f&&std::abs(down.y/down.z-2)<.001f,"Linear reflectance tint not preserved");
    auto packet=relightDiffuseProbes(transport,std::span(&light,1));
    const auto received=sampleDiffuseProbes(packet,{0,1,0},{0,-1,0});check(received.x>0,"Probe interpolation lost energy");
    check(sampleDiffuseProbes(packet,{100,1,0},{0,-1,0})==glm::vec3(0),"Probe escaped volume");
    auto off=light;off.radiance={};check(relightDiffuseProbes(transport,std::span(&off,1))==WorldDiffuseProbeUniforms{},"Lights off retained bounce");
    auto red=light;red.radiance={4,0,0};auto blue=light;blue.radiance={0,2,1};
    const auto rp=relightDiffuseProbes(transport,std::span(&red,1)),bp=relightDiffuseProbes(transport,std::span(&blue,1));
    check(glm::length(sampleDiffuseProbes(rp,{0,1,0},{0,-1,0})+sampleDiffuseProbes(bp,{0,1,0},{0,-1,0})-received)<1e-5f,"Linear source additivity");
    auto moved=light;moved.position[0]+=.1f;
    rejects([&]{relightDiffuseProbes(transport,std::span(&moved,1));},"Moved source accepted stale transport");
    check(diffuseProbeTransportJson(diffuseProbeTransportFromJson(diffuseProbeTransportJson(transport)))==diffuseProbeTransportJson(transport),"Transport serialization changed basis");
    auto malformed=packet;malformed[2][0]=100000;
    rejects([&]{validateDiffuseProbeUniforms(malformed);},"Invalid grid reached renderer");
    malformed=packet;malformed[10][0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validateDiffuseProbeUniforms(malformed);},"NaN reached renderer");
    auto metal=a;metal.materials[0].metallicFactor=1;
    check(energy(bakeDiffuseProbes(metal,std::span(&light,1),settings))==0,"Metal bounced diffuse light");
    auto empty=a;empty.materials[0].alphaMode=1;empty.materials[0].baseColorFactor[3]=0;
    check(energy(bakeDiffuseProbes(empty,std::span(&light,1),settings))==0,"Masked hole bounced light");
    auto unlit=a;unlit.materials[0].unlit=true;
    check(energy(bakeDiffuseProbes(unlit,std::span(&light,1),settings))==0,"Unlit source counted as indirect diffuse");
    auto occluded=a;quad(occluded,2,0);
    check(energy(bakeDiffuseProbes(occluded,std::span(&light,1),settings))==0,"Light crossed opaque blocker");
    // With identical irradiance everywhere, an occluded probe neighborhood must
    // attenuate instead of normalizing tiny visibility weights back to full light.
    auto blocked=packet;
    for(std::size_t p=0;p<8;++p) for(std::size_t s=0;s<6;++s) blocked[3+p*kDiffuseProbeStride+9+s]={0,0,0,0};
    check(glm::length(sampleDiffuseProbes(blocked,{0,1,0},{0,-1,0}))<glm::length(received)*.01f,"Visibility normalization leaks through wall");
    auto tinted=a;tinted.materials[0].baseColor.rgba={255,128,0,255};tinted.materials[0].baseColor.width=1;tinted.materials[0].baseColor.height=1;
    tinted.materials[0].baseColorFactor={1,1,1,1};
    const auto texture=bakeDiffuseProbes(tinted,std::span(&light,1),settings);
    const auto rgb=evaluateDiffuseSh(texture.transfer[0],{0,-1,0});
    check(std::abs(rgb.y/rgb.x-.21586f)<.001f&&rgb.z==0,"Texture was not sampled in linear light");
    std::cout<<"PASS SH irradiance, real triangle visibility, masking, metal, linear textures, transport identity, source off/additivity, volume and leak controls\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
