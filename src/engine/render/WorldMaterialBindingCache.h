#pragma once
#include "engine/render/RenderBackendTypes.h"
#include "engine/render/IWorldDepthControl.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::render {
// Texture cache identities are immutable. Own their text in retained entries;
// never use caller pointer identity or retain borrowed texture/mip pointers.
template<class Text> struct WorldMaterialBindingIdentity {
    std::array<Text,8> textures{};
    std::array<std::array<int,6>,8> sampling{};
    std::uint64_t scope=0;
    unsigned linearMipSampling=0;
};
using WorldMaterialBindingView=WorldMaterialBindingIdentity<std::string_view>;
using WorldMaterialBindingKey=WorldMaterialBindingIdentity<std::string>;

inline std::optional<WorldMaterialBindingView> worldMaterialBindingIdentity(
    const backend::WorldTextureData* t,std::uint64_t scope) {
    // Unowned/anonymous inputs and frame-owned depth snapshots keep resolving
    // normally. Managed allocation failures must never become cached fallbacks.
    if(!t || !scope || IWorldDepthControl::isSnapshotTexture(t->lightProjectionKey))return {};
    WorldMaterialBindingView out;out.scope=scope;out.linearMipSampling=t->linearMipSampling;
    const auto role=[&](unsigned i,const char* key,const char* cache,const unsigned char* pixels,
                       int width,int height,int wrapS,int wrapT,bool srgb,unsigned mips) {
        if(!pixels || width<=0 || height<=0)return true;
        if(!key || !key[0])return false;
        out.textures[i]=cache && cache[0]?cache:key;
        out.sampling[i]={width,height,wrapS,wrapT,srgb?1:0,static_cast<int>(mips)};
        return true;
    };
    if(!role(0,t->key,t->cacheKey,t->rgba,t->width,t->height,t->wrapS,t->wrapT,t->textureSrgb,t->mipLevelCount) ||
       !role(1,t->normalKey,t->normalCacheKey,t->normalRgba,t->normalWidth,t->normalHeight,t->normalWrapS,t->normalWrapT,t->normalTextureSrgb,t->normalMipLevelCount) ||
       !role(2,t->metallicRoughnessKey,t->metallicRoughnessCacheKey,t->metallicRoughnessRgba,t->metallicRoughnessWidth,t->metallicRoughnessHeight,t->metallicRoughnessWrapS,t->metallicRoughnessWrapT,t->metallicRoughnessTextureSrgb,t->metallicRoughnessMipLevelCount) ||
       !role(3,t->occlusionKey,t->occlusionCacheKey,t->occlusionRgba,t->occlusionWidth,t->occlusionHeight,t->occlusionWrapS,t->occlusionWrapT,t->occlusionTextureSrgb,t->occlusionMipLevelCount) ||
       !role(4,t->emissiveKey,t->emissiveCacheKey,t->emissiveRgba,t->emissiveWidth,t->emissiveHeight,t->emissiveWrapS,t->emissiveWrapT,t->emissiveTextureSrgb,t->emissiveMipLevelCount) ||
       !role(5,t->environmentKey,t->environmentCacheKey,t->environmentRgba,t->environmentWidth,t->environmentHeight,t->environmentWrapS,t->environmentWrapT,t->environmentTextureSrgb,t->environmentMipLevelCount) ||
       !role(6,t->lightProjectionKey,t->lightProjectionCacheKey,t->lightProjectionRgba,t->lightProjectionWidth,t->lightProjectionHeight,t->lightProjectionWrapS,t->lightProjectionWrapT,t->lightProjectionTextureSrgb,t->lightProjectionMipLevelCount) ||
       !role(7,t->projectedShadowKey,t->projectedShadowCacheKey,t->projectedShadowRgba,t->projectedShadowWidth,t->projectedShadowHeight,t->projectedShadowWrapS,t->projectedShadowWrapT,t->projectedShadowTextureSrgb,0u))return {};
    return out;
}

struct WorldMaterialBindingHash {
    using is_transparent=void;
    template<class Text> std::size_t operator()(const WorldMaterialBindingIdentity<Text>& k) const {
        std::size_t h=std::hash<std::uint64_t>{}(k.scope);
        const auto add=[&](std::size_t v){h^=v+0x9e3779b9u+(h<<6)+(h>>2);};
        add(k.linearMipSampling);
        for(unsigned i=0;i<8;++i) {
            add(std::hash<std::string_view>{}(k.textures[i]));
            for(int v:k.sampling[i])add(std::hash<int>{}(v));
        }
        return h;
    }
};
struct WorldMaterialBindingEqual {
    using is_transparent=void;
    template<class A,class B> bool operator()(const WorldMaterialBindingIdentity<A>& a,
                                              const WorldMaterialBindingIdentity<B>& b) const {
        if(a.scope!=b.scope || a.linearMipSampling!=b.linearMipSampling || a.sampling!=b.sampling)return false;
        for(unsigned i=0;i<8;++i)if(std::string_view(a.textures[i])!=std::string_view(b.textures[i]))return false;
        return true;
    }
};
// CPU lookup only; native texture/descriptor caches still own allocation,
// admission and lifetime. Clear before retiring any referenced native resource.
template<class Value> class WorldMaterialBindingCache {
public:
    static constexpr std::size_t capacity=1024;
    const Value* find(const WorldMaterialBindingView& view) const {
        const auto it=entries_.find(view);return it==entries_.end()?nullptr:&it->second;
    }
    void insert(const WorldMaterialBindingView& view,const Value& value) {
        if(entries_.size()>=capacity && entries_.find(view)==entries_.end())return;
        WorldMaterialBindingKey key;key.scope=view.scope;key.linearMipSampling=view.linearMipSampling;key.sampling=view.sampling;
        for(unsigned i=0;i<8;++i)key.textures[i]=view.textures[i];
        entries_.insert_or_assign(std::move(key),value);
    }
    void clear(){entries_.clear();}
    std::size_t size()const{return entries_.size();}
private:
    std::unordered_map<WorldMaterialBindingKey,Value,WorldMaterialBindingHash,WorldMaterialBindingEqual> entries_;
};
} // namespace engine::render
