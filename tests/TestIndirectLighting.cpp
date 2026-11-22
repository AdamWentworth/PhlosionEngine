#include "engine/render/WorldIndirectLighting.h"
#include "engine/render/WorldPlacedProbeCache.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <iostream>
#include <stdexcept>
#include <random>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
using namespace engine::render;
namespace {
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
template<class F>void rejects(F f,const char* s){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,s);}
IndirectTransport lattice() {
    IndirectTransport t;t.sceneIdentity="owned-fixture";t.settings.origin={0,0,0};t.settings.spacing={1,1,1};
    t.settings.counts={3,3,3};t.settings.faceResolution=4;
    for(unsigned z=0;z<3;++z)for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x) {
        IndirectProbe p;p.position={x,y,z};p.sky.fill(1);t.probes.push_back(p);
    }
    buildIndirectTetrahedra(t);return t;
}
}
int main() try {
    auto t=lattice();validateIndirectTransport(t);
    check(t.tetrahedra.size()==48&&t.acceleration.size()==8,"Tetrahedron topology");
    IndirectAxes axes;for(unsigned a=0;a<6;++a)axes[a]=glm::vec3(float(a+1));
    check(glm::length(indirectAxisSample(axes,glm::normalize(glm::vec3(1,-1,0)))-glm::vec3(2.5f))<1e-5f,"Directional axis convention");
    std::vector<IndirectAxes> colors(t.probes.size());
    for(unsigned i=0;i<colors.size();++i)colors[i].fill(t.probes[i].position+glm::vec3(1,2,3));
    IndirectControls linear;linear.normalWeight=linear.shadowDistanceWeight=0;
    std::mt19937 random(317);std::uniform_real_distribution<float> point(0,2);
    for(unsigned i=0;i<1000;++i) {
        const glm::vec3 p(point(random),point(random),point(random));
        const auto result=evaluateIndirectTransport(t,colors,p,{0,1,0},linear);
        check(result.tetra>=0&&result.steps<=16,"Tetra traversal failed");
        check(glm::length(result.radiance-(p+glm::vec3(1,2,3)))<.001,"Independent linear field interpolation");
    }
    for(auto p:{glm::vec3(0),glm::vec3(1),glm::vec3(2),glm::vec3(0,1,2)}) {
        auto v=evaluateIndirectTransport(t,colors,p,{0,1,0},linear);
        check(v.weight>0&&glm::length(v.radiance-(p+glm::vec3(1,2,3)))<.001,"Tetra boundary");
    }
    check(evaluateIndirectTransport(t,colors,{-1,0,0},{0,1,0}).weight==0,"Outside grid leak");
    std::array<std::uint8_t,16> vis;vis.fill(255);vis[15]=0;
    const glm::vec3 p(.75f,.15f,.15f),a(1,0,0),b(1,1,0),c(1,0,1);
    check(indirectVisibleDistance(p,vis,{0,0,0},a,b,c,0)<1e-6f,"Unobstructed visibility");
    vis.fill(128);vis[15]=0;
    const float expected=glm::length(p)*(1-(128.f/255)/p.x);
    check(std::abs(indirectVisibleDistance(p,vis,{0,0,0},a,b,c,0)-expected)<1e-5f,"Reconstructed occluder distance");
    vis[15]=1;check(indirectVisibleDistance(p,vis,{0,0,0},a,b,c,0)==100,"Buried probe validity");
    auto bad=t;bad.tetrahedra[0].probes[0]=999;rejects([&]{validateIndirectTransport(bad);},"Invalid probe index accepted");
    bad=t;bad.tetrahedra[0].inverse[0].x+=1;rejects([&]{validateIndirectTransport(bad);},"Invalid inverse accepted");
    bad=t;bad.acceleration[0]=999;rejects([&]{validateIndirectTransport(bad);},"Invalid acceleration accepted");
    bad=t;bad.probes[0].sky[0]=2;rejects([&]{validateIndirectTransport(bad);},"Excess energy accepted");
    bad=t;bad.tetrahedra[0].neighbor[0]=0;rejects([&]{validateIndirectTransport(bad);},"Self-referencing tetra accepted");
    bad=t;bad.acceleration[0]=int(t.tetrahedra.size()-1);rejects([&]{validateIndirectTransport(bad);},"Wrong acceleration cell accepted");
    bad=t;bad.settings.materialEmissionChannels={0};rejects([&]{validateIndirectTransport(bad);},"Incomplete emission metadata accepted");
    bad=t;bad.settings.materialEmissionChannels={32};bad.settings.materialEmissionFactors={{1,1,1}};
    rejects([&]{validateIndirectTransport(bad);},"Invalid fixture channel accepted");
    auto packed=packIndirectTransport(t);check(packed.outputRows>0&&packed.rows.size()>100,"GPU payload missing");
    const auto path=std::filesystem::temp_directory_path()/"phlosion-indirect-fixture.gi";
    if(std::filesystem::exists(path))std::filesystem::remove(path);
    writeIndirectTransport(path,t);auto restored=readIndirectTransport(path);std::filesystem::remove(path);
    check(packIndirectTransport(restored).rows==packed.rows,"Cooked roundtrip changed payload");
    std::array<WorldIndirectLightingUpdate,9> jobs;
    auto shared=std::make_shared<IndirectTransport>(t);
    const PreparedIndirectTransport prepared(shared);
    check(prepared.dataFor(shared).rows==packed.rows,"Prepared transport changed lighting data");
    check(&prepared.dataFor(shared)==&prepared.dataFor(shared),"Prepared transport rebuilt on reuse");
    rejects([&]{prepared.dataFor(std::make_shared<IndirectTransport>(t));},"Mismatched prepared source accepted");
    rejects([&]{PreparedIndirectTransport missing(nullptr);},"Missing prepared source accepted");
    std::array<IndirectScheduleCandidate,9> candidates;
    for(unsigned i=0;i<jobs.size();++i) {
        auto& j=jobs[i];j.identity=i+1;j.revision=1;j.transport=shared;j.camera={1,1,1};
        candidates[i]={&j,0,0};
    }
    jobs[8].camera={1000,1,1};
    std::array<unsigned,9> counts{};
    for(unsigned frame=1;frame<=24;++frame) {
        const auto chosen=scheduleIndirectLighting(candidates,2);
        check(chosen.size()==2,"Relight budget not enforced");
        for(unsigned i:chosen){check(i<8,"Distant sector displaced a nearer resident");++counts[i];++candidates[i].generation;candidates[i].lastRelight=frame;}
    }
    for(unsigned i=0;i<8;++i)check(counts[i]==6,"Oldest-first scheduler starved a visible sector");
    for(unsigned i=0;i<jobs.size();++i) {
        jobs[i].viewProjection={1,0,0,0,0,1,0,0,0,0,1,0,1000,0,0,1};
        candidates[i].generation=5;
    }
    check(scheduleIndirectLighting(candidates,8).empty(),"Warm offscreen sectors consume relight budget");
    candidates[3].generation=4;
    check(scheduleIndirectLighting(candidates,1)==std::vector<unsigned>{3},"Cold offscreen sector was not prewarmed");
    candidates[3].generation=5;jobs[5].reset=true;
    check(scheduleIndirectLighting(candidates,1)==std::vector<unsigned>{5},"Weather cut did not refresh offscreen sector");
    jobs[5].reset=false;jobs[4].identity=jobs[0].identity;
    rejects([&]{scheduleIndirectLighting(candidates,2);},"Duplicate sector ownership accepted");
    WorldPlacedProbeCache cache;WorldLocalReflectionProbe probe;probe.resolution=128;
    probe.ambientContribution=false;probe.overrideIndirect=true;probe.reflectionDimming=.35f;
    const std::array<std::uint64_t,2> ids{0x1122334455667788ull,0x8877665544332211ull};
    for(auto id:ids)for(unsigned face=0;face<6;++face) {
        check(cache.begin(id,1,probe,face),"Coincident probe preparation");cache.finish();
    }
    const auto first=cache.select(ids);
    check(first[20]!=first[21],"Coincident probes lost distinct identities");
    check(first[20]==IndirectRow{float(0x7788),float(0x5566),float(0x3344),float(0x1122)},"Exact 64-bit probe identity");
    check((unsigned(first[55][2])&1536u)==1536u&&std::abs(first[55][3]-.35f)<1e-6f,"Indirect probe participation");
    for(unsigned generation=2;generation<=3;++generation)for(unsigned face=0;face<6;++face) {
        check(cache.begin(ids[0],generation,probe,face),"Probe refresh");cache.finish();
    }
    const auto refreshed=cache.select(ids);
    check(refreshed[7]==first[7]&&refreshed[24]!=first[24],"Reused atlas bank lost ambient invalidation");
    using namespace gltf::scene;
    GltfSceneAsset scene;scene.sourceIdentity="owned-floor";scene.nodes.resize(1);scene.materials.resize(1);
    scene.materials[0].metallicFactor=0;scene.materials[0].baseColorFactor={.5f,.25f,.125f,1};
    GltfScenePrimitive floor;floor.nodeIndex=0;floor.materialIndex=0;
    for(auto v:{glm::vec3(-4,0,-4),glm::vec3(4,0,-4),glm::vec3(4,0,4),glm::vec3(-4,0,4)}) {
        WorldMeshVertex vert;vert.x=v.x;vert.y=v.y;vert.z=v.z;vert.ny=vert.tx=vert.tw=1;floor.vertices.push_back(vert);
    }
    floor.indices={0,2,1,0,3,2};scene.primitives.push_back(floor);
    IndirectBakeSettings settings;settings.origin={-.5f,.5f,-.5f};settings.spacing={1,1,1};settings.counts={2,2,2};settings.faceResolution=8;
    auto bake=bakeIndirectTransport(scene,settings);
    check(!bake.surfels.empty()&&!bake.bricks.empty(),"No prepared floor samples");
    for(const auto& bakedProbe:bake.probes)check(bakedProbe.sky[2]==1&&bakedProbe.sky[3]==0&&bakedProbe.count[3]>0,"Sky and floor transport ownership");
    for(const auto& sample:bake.surfels)check(glm::length(sample.albedo-glm::vec3(.5f,.25f,.125f))<1e-5f,"Prepared linear albedo");
    // Exercise every nonempty v2 block, exact signed bits, and malformed assets.
    bake.settings.terrainResolution=4;bake.terrain.resize(16);
    bake.terrainMaterials.assign(16,IndirectTerrainMaterial{.37f,.02f,.65f,.8f,7});
    bake.settings.materialEmissionChannels={-1};bake.settings.materialEmissionFactors={{0,0,0}};
    bake.surfels[0].permanentShadow=true;
    writeIndirectTransport(path,bake);
    auto compact=readIndirectTransport(path);
    const auto packedBake=packIndirectTransport(bake),packedCompact=packIndirectTransport(compact);
    check(packedBake.rows.size()==packedCompact.rows.size() &&
          std::memcmp(packedBake.rows.data(),packedCompact.rows.data(),packedBake.rows.size()*sizeof(IndirectRow))==0,
          "Packed transport changed GPU bits");
    std::ifstream cooked(path,std::ios::binary);
    const auto document=nlohmann::json::from_cbor(cooked);cooked.close();
    check(compact.terrainMaterials.size()==16&&compact.terrainMaterials[0].dominantLayer==7&&
        compact.terrainMaterials[0].roughness==.37f,"Terrain material roundtrip");
    const auto& surfelBytes=document.at("surfels").get_binary();
    check(surfelBytes.size()==bake.surfels.size()*61 &&
          surfelBytes[56]==255 && surfelBytes[57]==255 && surfelBytes[58]==255 && surfelBytes[59]==255 && surfelBytes[60]==1,
          "Little endian signed channel/boolean layout");
    const auto save=[&](const nlohmann::json& doc){
        const auto bytes=nlohmann::json::to_cbor(doc);
        std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    };
    for(const char* block:{"terrain","terrain_materials","surfels","bricks","probes","factors","tetrahedra","acceleration"}) {
        auto corrupt=document;corrupt[block].get_binary().pop_back();save(corrupt);
        rejects([&]{readIndirectTransport(path);},"Truncated block accepted");
    }
    auto v2=document;v2["schema"]="phlosion-indirect-transport-v2";v2.erase("terrain_materials");save(v2);
    const auto oldTransport=readIndirectTransport(path);check(oldTransport.terrainMaterials.empty(),"Legacy terrain defaults");
    const auto oldRows=packIndirectTransport(oldTransport).rows;
    check(oldRows[unsigned(oldRows[8][2])]==IndirectRow{1,.04f,0,0},"Legacy terrain material fallback");
    auto corrupt=document;corrupt["surfels"].get_binary()[60]=2;save(corrupt);
    rejects([&]{readIndirectTransport(path);},"Noncanonical shadow flag accepted");
    corrupt=document;corrupt["probes"]=nlohmann::json::array();save(corrupt);
    rejects([&]{readIndirectTransport(path);},"Nonbinary block accepted");
    corrupt=document;corrupt["schema"]="phlosion-indirect-transport-v99";save(corrupt);
    rejects([&]{readIndirectTransport(path);},"Unknown schema accepted");
    corrupt=document;auto& xyz=corrupt["probes"].get_binary();xyz[0]=0;xyz[1]=0;xyz[2]=128;xyz[3]=127;save(corrupt);
    rejects([&]{readIndirectTransport(path);},"Nonfinite decoded probe accepted");
    // Independent legacy lattice document, with pre-terrain optional fields absent.
    auto vec=[](glm::vec3 v){return std::array{v.x,v.y,v.z};};
    nlohmann::json legacy={{"schema","phlosion-indirect-transport-v1"},{"scene",t.sceneIdentity},
        {"grid",{vec(t.settings.origin),vec(t.settings.spacing),t.settings.counts,4,0,24,4,.25}},
        {"surfels",nlohmann::json::array()},{"bricks",nlohmann::json::array()},{"factors",nlohmann::json::array()},
        {"probes",nlohmann::json::array()},{"tetrahedra",nlohmann::json::array()},{"acceleration",t.acceleration}};
    for(const auto& v:t.probes)legacy["probes"].push_back({vec(v.position),v.first,v.count,v.sky,v.volume});
    for(const auto& v:t.tetrahedra)legacy["tetrahedra"].push_back({v.probes,v.neighbor,
        std::array{vec(v.inverse[0]),vec(v.inverse[1]),vec(v.inverse[2])},v.visibility});
    save(legacy);check(packIndirectTransport(readIndirectTransport(path)).rows==packed.rows,"Legacy transport changed");
    std::filesystem::remove(path);
    const auto originalIdentity=indirectSceneIdentity(scene);
    auto& material=scene.materials[0];material.hasNormalMap=true;material.normal.width=material.normal.height=1;
    material.normal.rgba={191,128,238,255};material.normal.textureSrgb=0;material.normal.contentHash=42;
    check(indirectSceneIdentity(scene)!=originalIdentity,"Mapped normal did not invalidate prepared transport");
    auto mappedBake=bakeIndirectTransport(scene,settings);
    check(!mappedBake.surfels.empty(),"Mapped normal preparation lost geometry");
    const auto mappedNormal=glm::normalize(glm::vec3(191.f/255*2-1,238.f/255*2-1,-(128.f/255*2-1)));
    for(const auto& sample:mappedBake.surfels)
        check(glm::length(sample.normal-mappedNormal)<1e-4f,"Prepared surfels lost linear tangent-space normal");
    scene.nodes[0].defaultGlobal[0][0]=-1;
    mappedBake=bakeIndirectTransport(scene,settings);
    for(const auto& sample:mappedBake.surfels)
        check(glm::length(sample.normal-glm::vec3(-mappedNormal.x,mappedNormal.y,mappedNormal.z))<1e-4f,"Mirrored transform changed normal handedness");
    std::cout<<"PASS indirect axes, 1000 independent tetra samples, boundaries, visibility depths, invalid data, cooked roundtrip, real geometry transport\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
