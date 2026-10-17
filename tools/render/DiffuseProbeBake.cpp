#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) try {
    using namespace engine::render;
    if(argc!=4) throw std::runtime_error("Usage: PhlosionDiffuseProbeBake scene.glb request.json output.json");
    std::ifstream request(argv[2]);if(!request) throw std::runtime_error("Cannot open bake request");
    nlohmann::json j;request>>j;
    if(j.at("schema")!="phlosion-diffuse-bake-v1") throw std::runtime_error("Unsupported bake request");
    DiffuseProbeBakeSettings settings;auto& g=settings.grid;
    j.at("grid").at("origin").get_to(g.origin);j.at("grid").at("spacing").get_to(g.spacing);
    j.at("grid").at("counts").get_to(g.counts);j.at("grid").at("fade_distance").get_to(g.fadeDistance);
    settings.rayCount=j.value("rays",2048);settings.maxDistance=j.value("max_distance",40.f);
    settings.areaSamplesU=j.value("area_samples_u",32);settings.areaSamplesV=j.value("area_samples_v",2);
    std::vector<WorldRectLight> lights;
    for(const auto& l:j.at("sources")) {
        WorldRectLight light;l.at("position").get_to(light.position);l.at("half_u").get_to(light.halfU);
        l.at("half_v").get_to(light.halfV);l.at("range").get_to(light.range);light.radiance={1,1,1};lights.push_back(light);
    }
    std::shared_ptr<const gltf::scene::GltfSceneAsset> asset;std::string error;
    if(!gltf::scene::loadGltfSceneAsset(argv[1],asset,&error)) throw std::runtime_error(error);
    const auto transport=bakeDiffuseProbes(*asset,lights,settings);
    const auto path=std::filesystem::path(argv[3]);const auto temporary=path.string()+".tmp";
    {std::ofstream output(temporary,std::ios::binary);output<<diffuseProbeTransportJson(transport).dump()<<'\n';
        if(!output) throw std::runtime_error("Cannot write diffuse transport");}
    // Same-directory rename after a complete write; no incomplete cache can load.
    std::filesystem::rename(temporary,path);
    std::cout<<"Baked "<<transport.validity.size()<<" probes, "<<lights.size()<<" sources, scene "<<transport.sceneIdentity<<'\n';
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
