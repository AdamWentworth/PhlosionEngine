#include "engine/assets/ResourceBank.h"
#include "engine/render/WorldLayerMaterialPackage.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include "engine/render/gltf/GltfSceneLod.h"
#include "engine/render/WorldLayeredSampling.h"
#include <fstream>
#include "engine/core/IAssetStore.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <map>
#include <algorithm>
#include <cmath>
using namespace engine::render;
using namespace engine::render::gltf::scene;
using Json=nlohmann::json;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Store final:engine::IAssetStore {
    std::map<std::string,std::vector<std::uint8_t>> files;
    bool readBytes(const std::string& p,std::vector<std::uint8_t>& b,std::string*)const override{
        const auto it=files.find(p);if(it==files.end())return false;b=it->second;return true;
    }
    bool readText(const std::string& p,std::string& s,std::string* error)const override{
        std::vector<std::uint8_t>b;if(!readBytes(p,b,error))return false;s.assign(b.begin(),b.end());return true;
    }
    bool exists(const std::string& p)const override{return files.contains(p);}
    void json(const std::string& p,const Json& j){auto s=j.dump();files[p]={s.begin(),s.end()};}
};
std::string hash(const std::vector<std::uint8_t>& bytes){
    std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}
    std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return s.str();
}
}
int main(int argc,char** argv)try{
    if(argc==2){
        const std::filesystem::path root(argv[1]);std::string error;
        GltfSceneLoadOptions options;
        check(loadWorldLayerMaterialPackage((root/"layered-materials/materials.json").string(),options.layeredMaterials,&error),error);
        Json lod;std::ifstream input(root/"scene-lods.json");input>>lod;
        for(const auto& group:lod.at("groups"))options.lodGroups.push_back({group.at("nodes").get<std::vector<std::string>>(),group.at("distances").get<std::vector<float>>()});
        std::shared_ptr<const GltfSceneAsset> asset;
        check(loadGltfSceneAsset((root/"station.glb").string(),asset,&error,options),error);
        check(asset->lodGroups.size()==108,"Station rock instance count");
        check(asset->layeredMaterials->materials.size()==3,"Station layer material count");
        for(const auto* name:{"Ground | ochre hardpan sand and gravel","Road | asphalt","Road | shoulder","Road | forecourt"})
            check(options.layeredMaterials->bindings.at(name)==1,"Road/ground material boundary mismatch");
        const auto sample=[&](float x,float y){
            return sampleWorldLayerMaterial(*asset->layeredMaterials,1,{(x+500)/1000,(500+y)/1000},
                {x,0,-y},{0,1,0},{1,0,0,1});
        };
        for(const float x:{-400.f,-50.f,0.f,50.f,400.f}){
            const float roadY=10.5f+2.8f*std::tanh(x/45.f);
            check(sample(x,roadY).dominantLayer>=5,"Road mask is misregistered from geometry");
            check(sample(x,roadY-6).dominantLayer<4,"Road coverage leaked outside footprint");
        }
        GltfScenePose pose;check(sampleGltfScenePose(asset,0,kBindPoseClip,pose,&error),error);
        for(const auto camera:{std::array<float,3>{0,3,0},std::array<float,3>{500,5,-500}}){
            std::vector<int> excluded;appendGltfSceneLodExclusions(*asset,pose,camera,1,excluded);
            check(excluded.size()==216,"Each rock must retain one LOD");
        }
        std::cout<<Json{{"materials",asset->materials.size()},{"layered_materials",asset->layeredMaterials->materials.size()},
            {"lod_instances",asset->lodGroups.size()},{"texture_bytes",asset->stats.retainedTextureBytes},{"primitives",asset->primitives.size()}}.dump()<<'\n';
        return 0;
    }
    check(argc==1,"Unexpected test arguments");
    Store store;store.files["scene/c.rgba"]={128,128,128,255};store.files["scene/n.rgba"]={128,128,255,255};
    store.files["scene/a.rgba"]=std::vector<std::uint8_t>(84,255);
    Json textures=Json::array();
    for(const auto& name:{"c","n","a"}){
        const std::string file=std::string(name)+".rgba";
        textures.push_back({{"file",file},{"srgb",name==std::string("c")},{"fnv1a64",hash(store.files.at("scene/"+file))},
            {"mips",name==std::string("a")?Json{{4,4},{2,2},{1,1}}:Json{{1,1}}}});
    }
    WorldLayerRecord layer;layer.textures={1,1u<<16,1u|(1u<<16),0};
    Json record={{"tintOpacity",layer.tintOpacity},{"roughnessRemap",layer.roughnessRemap},
        {"metalnessRemap",layer.metalnessRemap},{"colorMaskRemap",layer.colorMaskRemap},
        {"detail",layer.detail},{"microOffset",layer.microOffset},{"layerOffsetNormal",layer.layerOffsetNormal},{"textures",layer.textures}};
    Json material={{"bindings",{"terrain"}},{"terrain",true},{"maskTexture",2},{"layers",Json::array({record})},
        {"masks",{{"highResolution",{2,2}},{"lowResolution",{1,1}},{"atlasExtent",{4,4}},{"tileSize",2},
        {"high",{{0,1}}},{"low",{{1,1}}},{"records",{0,0}}}}};
    Json document={{"version",1},{"textures",textures},{"materials",Json::array({material})}};
    store.json("scene/materials.json",document);
    std::shared_ptr<const WorldLayerMaterialPackage> package;std::string error;
    check(loadWorldLayerMaterialPackage("scene/materials.json",package,&error,&store),error);
    check(package->bindings.at("terrain")==1&&package->materials->textures.size()==3,"Package load");
    const auto value=sampleWorldLayerMaterial(*package->materials,1,{.5f,.5f},{-17,0,-29},{0,1,0},{1,0,0,1});
    const float expectedColor=std::pow((128.f/255+.055f)/1.055f,2.4f);
    check(glm::length(value.color-glm::vec3(expectedColor))<1e-6f,"CPU material color-space conversion");
    check(std::abs(value.roughness-128.f/255)<1e-6f&&std::abs(value.specular-.04f*128.f/255)<1e-6f&&value.metalness==0,"CPU terrain material channels");
    check(value.dominantLayer==0&&value.dominantWeight==1&&value.selectedWeight==1,"CPU layer ownership");
    const auto published=package;
    const auto reject=[&](Json bad,const char* why){
        store.json("scene/materials.json",bad);
        check(!loadWorldLayerMaterialPackage("scene/materials.json",package,&error,&store),why);
        check(package==published&&!error.empty(),"Failed package replaced snapshot");
    };
    auto bad=document;bad["textures"][0]["file"]="../c.rgba";reject(bad,"Traversal accepted");
    bad=document;bad["textures"][0]["file"]="c.rgba:stream";reject(bad,"ADS accepted");
    bad=document;bad["textures"][0]["fnv1a64"]="0000000000000000";reject(bad,"Corruption accepted");
    bad=document;bad["textures"][2]["mips"]={{4,4}};reject(bad,"Trailing payload accepted");
    bad=document;bad["materials"][0]["masks"]["records"]={0xffffffffu,0};reject(bad,"Invalid mask accepted");
    bad=document;bad["materials"].push_back(material);reject(bad,"Duplicate binding accepted");
    bad=document;bad["materials"][0]["layers"][0]["textures"][1]=12345;reject(bad,"Invalid texture accepted");
    bad=document;bad["materials"][0]["layers"][0]["detail"][0]=-1;reject(bad,"Invalid layer accepted");
    bad=document;bad["version"]=2;reject(bad,"Unknown version accepted");
    store.json("scene/materials.json",document);

    const float positions[]{0,0,0,1,0,0,0,0,1};
    auto& binary=store.files["scene/triangle.bin"];binary.resize(sizeof(positions));std::memcpy(binary.data(),positions,sizeof(positions));
    Json gltf={{"asset",{{"version","2.0"}}},{"buffers",{{{"uri","triangle.bin"},{"byteLength",sizeof(positions)}}}},
        {"bufferViews",{{{"buffer",0},{"byteLength",sizeof(positions)}}}},
        {"accessors",{{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,0,1}}}}},
        {"materials",{{{"name","terrain"},{"pbrMetallicRoughness",{{"metallicFactor",0},{"roughnessFactor",1}}}}}},
        {"meshes",{{{"primitives",{{{"attributes",{{"POSITION",0}}},{"material",0}}}}}}},
        {"nodes",{{{"mesh",0}}}},{"scenes",{{{"nodes",{0}}}}},{"scene",0}};
    store.json("scene/triangle.gltf",gltf);
    GltfSceneLoadOptions options;options.layeredMaterials=package;
    std::shared_ptr<const GltfSceneAsset> asset;
    check(loadGltfSceneAsset(store,"scene/triangle.gltf",asset,&error,options),error);
    check(asset->layeredMaterials==package->materials&&asset->materials[0].layeredMaterialIndex==1,"Layered glTF binding lost");
    check(asset->materials[0].baseColor.width==1&&!asset->materials[0].hasBaseColorMap,"Baked material still bound");
    check(asset->stats.retainedTextureBytes==100,"Layered glTF retained texture accounting");
    {
        engine::assets::ResourceBank bank;
        auto resident=bank.acquire<WorldLayerMaterialPackage>("test-layer-package",
            [package](engine::assets::ResourceLoadContext&) {
                return engine::assets::ResourceValue<WorldLayerMaterialPackage>{
                    std::make_shared<const WorldLayerMaterialPackage>(*package),100};
            });
        std::weak_ptr<const WorldLayerMaterialPackage> lifetime=resident;
        GltfSceneLoadOptions pinnedOptions;pinnedOptions.layeredMaterials=resident;
        std::shared_ptr<const GltfSceneAsset> pinnedScene;
        check(loadGltfSceneAsset(store,"scene/triangle.gltf",pinnedScene,&error,pinnedOptions),error);
        resident.reset();pinnedOptions.layeredMaterials.reset();bank.trim(true);
        check(!lifetime.expired()&&bank.stats().residentBytes==100,
            "Live layered scene lost its separately accounted package owner");
        pinnedScene.reset();bank.trim(true);
        check(lifetime.expired()&&!bank.stats().residentBytes,
            "Unused layered package did not release after its final scene");
    }
    const auto old=asset;
    auto wrong=std::make_shared<WorldLayerMaterialPackage>(*package);wrong->bindings={{"missing",1}};options.layeredMaterials=wrong;
    check(!loadGltfSceneAsset(store,"scene/triangle.gltf",asset,&error,options)&&asset==old,"Missing binding published");
    options.layeredMaterials=package;
    gltf["nodes"]=Json::array({Json{{"name","near"},{"mesh",0}},Json{{"name","middle"},{"mesh",0}},Json{{"name","far"},{"mesh",0}}});
    gltf["scenes"][0]["nodes"]={0,1,2};store.json("scene/lod.gltf",gltf);
    options.lodGroups={{{"near","middle","far"},{10,20}}};
    std::shared_ptr<const GltfSceneAsset> lodAsset;
    check(loadGltfSceneAsset(store,"scene/lod.gltf",lodAsset,&error,options),error);
    check(lodAsset->primitives.size()==3&&lodAsset->primitives[0].geometryKey==lodAsset->primitives[1].geometryKey&&
        lodAsset->primitives[1].geometryKey==lodAsset->primitives[2].geometryKey,"Static instances duplicated GPU geometry identity");
    GltfScenePose pose;check(sampleGltfScenePose(lodAsset,0,kBindPoseClip,pose,&error),error);
    for(const auto [distance,selected]:std::array<std::pair<float,int>,5>{{{0.f,0},{9.99f,0},{10.f,1},{20.f,2},{1000.f,2}}}){
        std::vector<int> excluded;appendGltfSceneLodExclusions(*lodAsset,pose,{0,distance,0},1,excluded);
        check(excluded.size()==2&&std::find(excluded.begin(),excluded.end(),selected)==excluded.end(),"LOD boundary or distant disappearance");
    }
    std::vector<int> excluded;appendGltfSceneLodExclusions(*lodAsset,pose,{0,12,0},1.5f,excluded);
    check(std::find(excluded.begin(),excluded.end(),0)==excluded.end(),"LOD preset distance scale");
    gltf["nodes"][1]["scale"]={2,3,4};
    gltf["nodes"][1]["translation"]={0,5,0};
    store.json("scene/lod.gltf",gltf);
    check(loadGltfSceneAsset(store,"scene/lod.gltf",lodAsset,&error,options),error);
    check(lodAsset->lodGroups[0].boundsMin.size()==3&&lodAsset->lodGroups[0].boundsMax.size()==3,"Missing lower-level bounds");
    check(sampleGltfScenePose(lodAsset,0,kBindPoseClip,pose,&error),error);
    excluded.clear();appendGltfSceneLodExclusions(*lodAsset,pose,{0,12,0},1,excluded);
    check(std::find(excluded.begin(),excluded.end(),0)==excluded.end(),"Transformed lower LOD omitted from union bounds");
    // A coarse level can extend beyond the original, even without a transform.
    auto expanded=std::make_shared<GltfSceneAsset>(*lodAsset);
    expanded->lodGroups[0].boundsMax[2][1]=8;
    excluded.clear();appendGltfSceneLodExclusions(*expanded,pose,{0,16,0},1,excluded);
    check(std::find(excluded.begin(),excluded.end(),0)==excluded.end(),"Expanded coarse LOD omitted from bounds");
    options.lodGroups[0].distances={20,10};
    const auto savedLod=lodAsset;
    check(!loadGltfSceneAsset(store,"scene/lod.gltf",lodAsset,&error,options)&&lodAsset==savedLod,"Invalid LOD published");
    store.files.clear();check(asset->layeredMaterials->textures[0].mips[0].rgba[0]==128,"Snapshot retained file dependency");
    std::cout<<"PASS prepared layer package integrity, bounds, failure-preserved snapshots and glTF ownership\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
