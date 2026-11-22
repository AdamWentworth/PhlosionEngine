#include "engine/assets/ResourceBank.h"
#include "engine/render/WorldLayerMaterialPackage.h"
#include "engine/core/IAssetStore.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace engine::render {
namespace {
using Json=nlohmann::json;
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
std::string digest(const std::vector<std::uint8_t>& bytes){
    std::uint64_t hash=14695981039346656037ull;
    for(auto b:bytes){hash^=b;hash*=1099511628211ull;}
    std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;return s.str();
}
std::vector<std::uint8_t> read(const std::filesystem::path& path,std::size_t limit,const engine::IAssetStore* store){
    std::vector<std::uint8_t> bytes;
    if(store){
        std::string error;require(store->readBytes(path.generic_string(),bytes,&error),("Layer package read: "+error).c_str());
        require(bytes.size()<=limit,"Layer package payload budget");
    }else{
        std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"Layer package missing file");
        const auto size=file.tellg();require(size>=0&&std::uint64_t(size)<=limit,"Layer package payload budget");
        bytes.resize(std::size_t(size));file.seekg(0);require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)),"Layer package short read");
    }
    return bytes;
}
std::filesystem::path child(const std::filesystem::path& directory,const std::string& name){
    // All prepared payloads are siblings. Reject traversal, absolute and ADS paths
    // equally for filesystem and packed virtual stores.
    require(!name.empty()&&name!="."&&name!=".."&&name.find_first_of("/\\:") == std::string::npos,
        "Layer package payload must be a sibling filename");
    return directory/name;
}
template<class T,std::size_t N> std::array<T,N> array(const Json& j){
    require(j.is_array()&&j.size()==N,"Layer package array extent");
    std::array<T,N> v{};for(std::size_t i=0;i<N;++i)v[i]=j.at(i).get<T>();return v;
}
}
bool loadWorldLayerMaterialPackage(const std::string& path,
    std::shared_ptr<const WorldLayerMaterialPackage>& out,std::string* error,const engine::IAssetStore* store){
    try{
        if(!store){
            if(auto* bank=engine::assets::currentResourceBank()){
                const auto canonical=engine::assets::canonicalResourcePath(path);
                out=bank->acquire<WorldLayerMaterialPackage>("layer-package:"+canonical,
                    [canonical](engine::assets::ResourceLoadContext& context){
                        const std::filesystem::path file(canonical);
                        engine::assets::ResourceFileStore files(context,file.parent_path());
                        std::shared_ptr<const WorldLayerMaterialPackage> value;std::string problem;
                        if(!loadWorldLayerMaterialPackage(file.filename().generic_string(),value,&problem,&files))
                            throw std::runtime_error(problem);
                        const auto packed=packWorldLayerMaterialSet(*value->materials);
                        return engine::assets::ResourceValue<WorldLayerMaterialPackage>{value,
                            packed.textureBytes+packed.words.size()*sizeof(std::uint32_t)};
                    });
                if(error)error->clear();return true;
            }
        }
        const std::filesystem::path manifest(path);
        const auto bytes=read(manifest,16u<<20,store);const auto j=Json::parse(bytes);
        require(j.at("version")==1,"Layer package version");
        auto set=std::make_shared<WorldLayerMaterialSet>();set->identity="layer-package/"+digest(bytes);
        const auto& textures=j.at("textures");require(textures.is_array()&&!textures.empty()&&textures.size()<=kWorldLayerTextureLimit,"Layer package texture count");
        std::uint64_t total=0;
        for(const auto& t:textures){
            auto payload=read(child(manifest.parent_path(),t.at("file").get<std::string>()),256u<<20,store);
            total+=payload.size();require(total<=256u<<20,"Layer package total texture budget");
            const auto hash=digest(payload);require(hash==t.at("fnv1a64").get<std::string>(),"Layer package texture identity mismatch");
            WorldLayerTexture texture;texture.srgb=t.at("srgb").get<bool>();
            texture.identity=set->identity+"/"+std::to_string(set->textures.size())+"/"+hash;
            const auto& mips=t.at("mips");require(mips.is_array()&&!mips.empty()&&mips.size()<=15,"Layer package mip count");
            std::size_t offset=0;
            for(const auto& mip:mips){
                const auto extent=array<std::uint32_t,2>(mip);
                require(extent[0]&&extent[1]&&extent[0]<=16384&&extent[1]<=16384,"Layer package mip dimensions");
                const std::uint64_t size=std::uint64_t(extent[0])*extent[1]*4;
                require(offset<=payload.size()&&size<=payload.size()-offset,"Layer package mip truncated");
                WorldLayerTextureMip image;image.width=extent[0];image.height=extent[1];
                image.rgba.assign(payload.begin()+offset,payload.begin()+offset+std::size_t(size));offset+=std::size_t(size);
                texture.mips.push_back(std::move(image));
            }
            require(offset==payload.size(),"Layer package trailing texture data");set->textures.push_back(std::move(texture));
        }
        const auto& materials=j.at("materials");require(materials.is_array()&&!materials.empty()&&materials.size()<=256,"Layer package material count");
        auto result=std::make_shared<WorldLayerMaterialPackage>();
        for(const auto& m:materials){
            WorldLayerMaterial material;
            const auto& layers=m.at("layers");require(layers.is_array()&&!layers.empty()&&layers.size()<=kWorldLayerLimit,"Layer package layer count");
            for(const auto& l:layers){
                WorldLayerRecord layer;
                layer.tintOpacity=array<float,4>(l.at("tintOpacity"));layer.roughnessRemap=array<float,4>(l.at("roughnessRemap"));
                layer.metalnessRemap=array<float,4>(l.at("metalnessRemap"));layer.colorMaskRemap=array<float,4>(l.at("colorMaskRemap"));
                layer.detail=array<float,4>(l.at("detail"));layer.microOffset=array<float,4>(l.at("microOffset"));
                layer.layerOffsetNormal=array<float,4>(l.at("layerOffsetNormal"));layer.textures=array<std::uint32_t,4>(l.at("textures"));
                material.layers.push_back(layer);
            }
            material.terrain=m.at("terrain").get<bool>();material.maskTexture=m.at("maskTexture").get<std::uint32_t>();
            material.macroColor=m.value("macroColor",kWorldLayerNoTexture);material.globalNormal=m.value("globalNormal",kWorldLayerNoTexture);
            material.tilingMap=m.value("tilingMap",kWorldLayerNoTexture);
            if(m.contains("tiling"))material.tiling=array<float,16>(m.at("tiling"));
            if(m.contains("uvScale"))material.uvScale=array<float,2>(m.at("uvScale"));
            if(m.contains("selectedLayers"))material.selectedLayers=array<std::uint32_t,2>(m.at("selectedLayers"));
            material.normalStrength=m.value("normalStrength",1.6f);
            material.surfaceAspect=m.value("surfaceAspect",1.f);
            material.normalYSign=m.value("normalYSign",1.f);
            material.globalNormalIntensity=m.value("globalNormalIntensity",1.f);
            material.terrainDistanceFade=m.value("terrainDistanceFade",true);
            if(m.contains("globalNormalUvScale"))material.globalNormalUvScale=array<float,2>(m.at("globalNormalUvScale"));
            if(m.contains("globalNormalUvBias"))material.globalNormalUvBias=array<float,2>(m.at("globalNormalUvBias"));
            if(m.contains("normalGradient"))material.normalGradient=array<float,2>(m.at("normalGradient"));
            if(m.contains("microGradient"))material.microGradient=array<float,2>(m.at("microGradient"));
            const auto& masks=m.at("masks");auto& mask=material.masks;
            mask.highResolution=array<std::uint32_t,2>(masks.at("highResolution"));mask.lowResolution=array<std::uint32_t,2>(masks.at("lowResolution"));
            mask.atlasExtent=array<std::uint32_t,2>(masks.at("atlasExtent"));mask.tileSize=masks.at("tileSize").get<std::uint32_t>();
            const auto headers=[&](const Json& values,std::vector<WorldLayerMaskTile>& target){
                require(values.is_array()&&values.size()<=(1u<<20),"Layer mask tile budget");
                for(const auto& value:values){auto v=array<std::uint32_t,2>(value);target.push_back({v[0],v[1]});}
            };
            headers(masks.at("high"),mask.high);headers(masks.at("low"),mask.low);
            require(masks.at("records").is_array()&&masks.at("records").size()<=(1u<<22),"Layer mask record budget");
            mask.records=masks.at("records").get<std::vector<std::uint32_t>>();
            require(material.maskTexture<set->textures.size(),"Layer mask texture missing");
            const auto& atlas=set->textures[material.maskTexture].mips.front();
            require(atlas.width==mask.atlasExtent[0]&&atlas.height==mask.atlasExtent[1],"Layer mask atlas dimensions");
            for(std::size_t i=0;i<atlas.rgba.size();i+=4)mask.atlas.push_back(atlas.rgba[i]);
            set->materials.push_back(std::move(material));
            const auto& names=m.at("bindings");require(names.is_array()&&!names.empty()&&names.size()<=256,"Layer material binding count");
            for(const auto& name:names){
                const auto key=name.get<std::string>();require(!key.empty()&&key.size()<=512,"Layer material binding name");
                require(result->bindings.emplace(key,std::uint32_t(set->materials.size())).second,"Duplicate layered material binding");
            }
        }
        (void)packWorldLayerMaterialSet(*set);result->materials=std::move(set);out=std::move(result);if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
}
