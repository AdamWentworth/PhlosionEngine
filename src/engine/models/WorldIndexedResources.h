#pragma once
#include "engine/render/IWorldPreparedResources.h"
#include "engine/render/WorldMaterialBindingCache.h"
#include <unordered_map>

namespace engine::models::batches {
// Owner-thread native bindings only. The scene admits/releases the scope;
// live view, deformation and material scalars always accompany each draw.
class WorldIndexedResources {
    using Token=render::WorldPreparedResourceToken;
    struct StringHash {
        using is_transparent=void;
        std::size_t operator()(std::string_view value)const{return std::hash<std::string_view>{}(value);}
    };
    render::IWorldPreparedResources* owner_=nullptr;
    std::uint64_t scope_=0;
    std::unordered_map<std::string,Token,StringHash,std::equal_to<>> geometry_;
    render::WorldMaterialBindingCache<Token> materials_;
public:
    static constexpr std::size_t capacity=1024;
    void bind(render::IWorldPreparedResources* owner,std::uint64_t scope) {
        if(owner_!=owner || scope_!=scope) {
            geometry_.clear();materials_.clear();owner_=owner;scope_=scope;
        }
    }
    bool draw(const char* key,const IRenderBackend::WorldMeshVertex* vertices,std::size_t vertexCount,
        const std::uint32_t* indices,std::size_t indexCount,const IRenderBackend::WorldTextureData& texture,
        const float* matrix,int width,int height) {
        if(!owner_ || !scope_ || !key || !*key)return false;
        const auto identity=render::worldMaterialBindingIdentity(&texture,scope_);
        if(!identity)return false;
        auto it=geometry_.find(std::string_view(key));
        if(it==geometry_.end()) {
            if(geometry_.size()>=capacity)return false;
            it=geometry_.emplace(key,Token{}).first;
        }
        auto& geometry=it->second;
        if(!owner_->worldPreparedGeometryValid(geometry))
            geometry=owner_->prepareWorldGeometry(key,vertices,vertexCount,indices,indexCount);
        if(!geometry)return false;
        const auto* found=materials_.find(*identity);
        Token material=found?*found:Token{};
        if(!owner_->worldPreparedMaterialValid(material)) {
            // Do not repeatedly allocate unretained tokens when the CPU cache fills.
            if(!found && materials_.size()>=materials_.capacity)return false;
            material=owner_->prepareWorldMaterial(texture);
            if(!material)return false;
            materials_.insert(*identity,material);
        }
        return owner_->drawWorldPreparedIndexed(geometry,material,texture,matrix,width,height);
    }
};
} // namespace engine::models::batches
