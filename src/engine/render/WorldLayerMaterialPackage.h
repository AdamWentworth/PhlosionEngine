#pragma once
#include "engine/render/WorldLayeredMaterial.h"
#include <unordered_map>
namespace engine { class IAssetStore; }
namespace engine::render {
struct WorldLayerMaterialPackage {
    std::shared_ptr<const WorldLayerMaterialSet> materials;
    // Exact glTF material names; one-based indices, zero remains ordinary PBR.
    std::unordered_map<std::string,std::uint32_t> bindings;
};
bool loadWorldLayerMaterialPackage(const std::string& path,
    std::shared_ptr<const WorldLayerMaterialPackage>& out,std::string* error=nullptr,
    const engine::IAssetStore* store=nullptr);
}
