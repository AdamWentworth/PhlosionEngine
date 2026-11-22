#include "engine/render/WorldIndirectLighting.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include "engine/render/WorldLayerMaterialPackage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
int main(int argc,char** argv) try {
    using namespace engine::render;
    if(argc!=4)throw std::runtime_error("Usage: PhlosionIndirectBake scene.glb request.json output.gi OR --repack input.gi output.gi");
    if(std::string_view(argv[1])=="--repack") {
        if(std::filesystem::exists(argv[3]))throw std::runtime_error("Repack output must be a new file");
        const auto begin=std::chrono::steady_clock::now();
        const auto source=readIndirectTransport(argv[2]);
        const auto loaded=std::chrono::steady_clock::now();
        writeIndirectTransport(argv[3],source);
        const auto written=std::chrono::steady_clock::now();
        const auto restored=readIndirectTransport(argv[3]);
        const auto finished=std::chrono::steady_clock::now();
        const auto a=packIndirectTransport(source),b=packIndirectTransport(restored);
        if(source.sceneIdentity!=restored.sceneIdentity || a.rows.size()!=b.rows.size() ||
           a.outputRows!=b.outputRows || std::memcmp(a.rows.data(),b.rows.data(),a.rows.size()*sizeof(IndirectRow)))
            throw std::runtime_error("Repack changed the GPU transport payload");
        const auto ms=[](auto x,auto y){return std::chrono::duration<double,std::milli>(y-x).count();};
        std::cout<<nlohmann::json{{"source_read_ms",ms(begin,loaded)},{"packed_read_ms",ms(written,finished)},
            {"source_bytes",std::filesystem::file_size(argv[2])},{"packed_bytes",std::filesystem::file_size(argv[3])},
            {"gpu_bytes",a.rows.size()*sizeof(IndirectRow)},{"gpu_bitwise_equal",true},{"scene",source.sceneIdentity}}.dump()<<'\n';
        return 0;
    }
    nlohmann::json j;std::ifstream in(argv[2]);in>>j;
    if(j.at("schema")!="phlosion-indirect-bake-v1")throw std::runtime_error("Unknown indirect bake request");
    IndirectBakeSettings s;
    auto vec=[](const nlohmann::json& j){const auto a=j.get<std::array<float,3>>();return glm::vec3(a[0],a[1],a[2]);};
    s.origin=vec(j.at("origin"));s.spacing=vec(j.at("spacing"));j.at("counts").get_to(s.counts);
    s.faceResolution=j.value("face_resolution",64u);s.maxDistance=j.value("max_distance",24.f);
    s.fadeDistance=j.value("fade_distance",4.f);s.surfelSpacing=j.value("surfel_spacing",.25f);
    if(j.contains("terrain")) {
        const auto& d=j.at("terrain");s.terrainResolution=d.at("resolution").get<unsigned>();
        const auto a=d.at("origin").get<std::array<float,2>>();s.terrainOrigin={a[0],a[1]};s.terrainSpacing=d.value("spacing",16.f);
    }
    std::shared_ptr<const gltf::scene::GltfSceneAsset> asset;std::string error;
    gltf::scene::GltfSceneLoadOptions options;
    const auto sibling=[&](const std::string& value){
        const std::filesystem::path relative(value);
        if(relative.empty()||relative.is_absolute()||value.find(':')!=std::string::npos)
            throw std::runtime_error("Invalid bake sidecar path");
        for(const auto& part:relative)if(part=="..")throw std::runtime_error("Bake sidecar traversal");
        return std::filesystem::path(argv[1]).parent_path()/relative;
    };
    if(j.contains("layered_material_package"))
        if(!loadWorldLayerMaterialPackage(sibling(j.at("layered_material_package").get<std::string>()).string(),options.layeredMaterials,&error))
            throw std::runtime_error(error);
    if(j.contains("static_lod_package")){
        const auto path=sibling(j.at("static_lod_package").get<std::string>());
        if(std::filesystem::file_size(path)>4u*1024u*1024u)throw std::runtime_error("LOD sidecar budget");
        nlohmann::json lod;std::ifstream input(path);input>>lod;
        if(lod.at("version")!=1||lod.at("groups").size()>2048)throw std::runtime_error("Invalid LOD sidecar");
        for(const auto& group:lod.at("groups")){
            gltf::scene::GltfSceneLodSource source;group.at("nodes").get_to(source.nodes);
            group.at("distances").get_to(source.distances);options.lodGroups.push_back(std::move(source));
        }
    }
    if(!gltf::scene::loadGltfSceneAsset(argv[1],asset,&error,options))throw std::runtime_error(error);
    s.materialEmissionChannels.assign(asset->materials.size(),-1);
    if(j.contains("emission_channels"))for(std::size_t i=0;i<asset->materials.size();++i) {
        auto f=j.at("emission_channels").find(asset->materials[i].name);
        if(f!=j.at("emission_channels").end())s.materialEmissionChannels[i]=f->get<int>();
    }
    for(const auto& m:asset->materials)s.materialEmissionFactors.push_back(vec(nlohmann::json(m.emissiveFactor)));
    if(j.contains("emission_factors"))for(std::size_t i=0;i<asset->materials.size();++i) {
        auto f=j.at("emission_factors").find(asset->materials[i].name);
        if(f!=j.at("emission_factors").end())s.materialEmissionFactors[i]=vec(*f);
    }
    const auto data=bakeIndirectTransport(*asset,s);writeIndirectTransport(argv[3],data);
    const auto gpu=packIndirectTransport(data);
    std::cout<<nlohmann::json{{"scene",data.sceneIdentity},{"surfels",data.surfels.size()},
        {"bricks",data.bricks.size()},{"probes",data.probes.size()},{"tetrahedra",data.tetrahedra.size()},
        {"factors",data.factors.size()},{"immutable_bytes",gpu.rows.size()*16},{"working_bytes",gpu.outputRows*16*2}}.dump()<<std::endl;
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
