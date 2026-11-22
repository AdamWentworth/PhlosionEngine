#include "GltfSceneDrawPlan.h"
#include "GltfSceneCulling.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace engine::render::gltf::scene::detail {
namespace {
using backend::WorldTextureData;
std::array<float,16> toArray(const glm::mat4& m) {
    std::array<float,16> result;
    std::copy(&m[0][0],&m[0][0]+16,result.begin()); return result;
}
bool finiteMatrix(const std::array<float,16>& m) {
    return std::all_of(m.begin(),m.end(),[](float v){return std::isfinite(v);});
}
template<class T> bool sameBytes(const std::vector<T>& a,const std::vector<T>& b) {
    return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(T))==0);
}
WorldTextureData materialTemplate(const GltfSceneMaterial& material) {
    WorldTextureData texture;

    texture.key = material.baseColor.key.c_str();
    texture.cacheKey = material.baseColor.cacheKey.c_str();
    texture.rgba = material.baseColor.rgba.data();
    texture.width = material.baseColor.width;
    texture.height = material.baseColor.height;
    texture.wrapS = material.baseColor.wrapS;
    texture.wrapT = material.baseColor.wrapT;
    texture.textureSrgb = material.baseColor.textureSrgb;

    texture.normalKey = material.normal.key.c_str();
    texture.normalCacheKey = material.normal.cacheKey.c_str();
    texture.normalRgba = material.normal.rgba.data();
    texture.normalWidth = material.normal.width;
    texture.normalHeight = material.normal.height;
    texture.normalWrapS = material.normal.wrapS;
    texture.normalWrapT = material.normal.wrapT;
    texture.normalTextureSrgb = material.normal.textureSrgb;

    texture.metallicRoughnessKey = material.metallicRoughness.key.c_str();
    texture.metallicRoughnessCacheKey =
        material.metallicRoughness.cacheKey.c_str();
    texture.metallicRoughnessRgba = material.metallicRoughness.rgba.data();
    texture.metallicRoughnessWidth = material.metallicRoughness.width;
    texture.metallicRoughnessHeight = material.metallicRoughness.height;
    texture.metallicRoughnessWrapS = material.metallicRoughness.wrapS;
    texture.metallicRoughnessWrapT = material.metallicRoughness.wrapT;
    texture.metallicRoughnessTextureSrgb =
        material.metallicRoughness.textureSrgb;

    texture.occlusionKey = material.occlusion.key.c_str();
    texture.occlusionCacheKey = material.occlusion.cacheKey.c_str();
    texture.occlusionRgba = material.occlusion.rgba.data();
    texture.occlusionWidth = material.occlusion.width;
    texture.occlusionHeight = material.occlusion.height;
    texture.occlusionWrapS = material.occlusion.wrapS;
    texture.occlusionWrapT = material.occlusion.wrapT;
    texture.occlusionTextureSrgb = material.occlusion.textureSrgb;

    texture.emissiveKey = material.emissive.key.c_str();
    texture.emissiveCacheKey = material.emissive.cacheKey.c_str();
    texture.emissiveRgba = material.emissive.rgba.data();
    texture.emissiveWidth = material.emissive.width;
    texture.emissiveHeight = material.emissive.height;
    texture.emissiveWrapS = material.emissive.wrapS;
    texture.emissiveWrapT = material.emissive.wrapT;
    texture.emissiveTextureSrgb = material.emissive.textureSrgb;

    texture.alphaCutoff = material.alphaCutoff;
    texture.depthTestEnabled = 1u;
    texture.normalScale = material.normalScale;
    texture.metallicFactor = material.metallicFactor;
    texture.roughnessFactor = material.roughnessFactor;
    texture.occlusionStrength = material.occlusionStrength;
    texture.emissiveFactorR = material.emissiveFactor[0];
    texture.emissiveFactorG = material.emissiveFactor[1];
    texture.emissiveFactorB = material.emissiveFactor[2];
    return texture;
}
}
std::shared_ptr<GltfSceneDrawPlan> GltfSceneDrawPlanAccess::get(const GltfSceneAsset& asset) {
    const auto& p=asset.drawPlan_.value;
    return p && p->owner==&asset && p->generation==asset.generation?p:nullptr;
}
void GltfSceneDrawPlanAccess::set(GltfSceneAsset& asset,std::shared_ptr<GltfSceneDrawPlan> p) {
    asset.drawPlan_.value=std::move(p);
}
GltfSceneDrawPlan::GltfSceneDrawPlan(const GltfSceneAsset& asset):owner(&asset),generation(asset.generation) {
    materials.resize(asset.materials.size());
    for(std::size_t i=0;i<materials.size();++i) {
        const auto& material=asset.materials[i];auto& record=materials[i];
        record.texture=materialTemplate(material);
        const GltfSceneTexture* roles[]={&material.baseColor,&material.normal,&material.metallicRoughness,&material.occlusion,&material.emissive};
        const backend::WorldTextureMipLevel** pointers[]={&record.texture.mipLevels,&record.texture.normalMipLevels,
            &record.texture.metallicRoughnessMipLevels,&record.texture.occlusionMipLevels,&record.texture.emissiveMipLevels};
        std::uint32_t* counts[]={&record.texture.mipLevelCount,&record.texture.normalMipLevelCount,
            &record.texture.metallicRoughnessMipLevelCount,&record.texture.occlusionMipLevelCount,&record.texture.emissiveMipLevelCount};
        for(unsigned r=0;r<5;++r)if(roles[r]->linearWithoutMipmaps) {
            record.levels[r]={roles[r]->rgba.data(),roles[r]->width,roles[r]->height};
            *pointers[r]=&record.levels[r];*counts[r]=1;
        }
    }
    authored.resize(asset.primitives.size());std::iota(authored.begin(),authored.end(),std::size_t{0});
    for(const auto& p:asset.primitives) {
        if(p.materialIndex<0 || std::size_t(p.materialIndex)>=asset.materials.size() ||
           p.nodeIndex<0 || std::size_t(p.nodeIndex)>=asset.nodes.size() ||
           p.vertices.empty() || p.indices.empty() ||
           (p.skinIndex>=0 && std::size_t(p.skinIndex)>=asset.skins.size()))
            throw std::invalid_argument("glTF primitive has invalid material/node/skin or empty geometry");
    }
    ordered.reserve(authored.size());
    for(unsigned pass=0;pass<2;++pass)for(auto i:authored)
        if((asset.materials[asset.primitives[i].materialIndex].alphaMode==2u)==(pass!=0))ordered.push_back(i);
}
std::shared_ptr<GltfNativeResources> GltfSceneDrawPlan::nativeResources(
    const IWorldPreparedResources* backend,WorldResourceScopeId scope) {
    for(const auto& r:nativeResources_)if(r->owner==backend && r->scope==scope)return r;
    auto r=std::make_shared<GltfNativeResources>();r->owner=backend;r->scope=scope;
    r->geometry.resize(authored.size());r->materials.resize(materials.size());
    r->bytes=(r->geometry.capacity()+r->materials.capacity())*sizeof(WorldPreparedResourceToken);
    constexpr std::size_t budget=2u<<20;
    if(r->bytes<=budget) {
        while(!nativeResources_.empty() && (nativeResources_.size()>=4 || nativeBytes_+r->bytes>budget)) {
            nativeBytes_-=nativeResources_.front()->bytes;nativeResources_.erase(nativeResources_.begin());
        }
        nativeBytes_+=r->bytes;nativeResources_.push_back(r);
    }
    return r;
}
bool GltfSceneDrawPlan::prepareMaterials(const GltfSceneAsset& asset,
    const GltfSceneDrawOptions& options, std::shared_ptr<const GltfPreparedMaterials>& result,
    std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    const auto count=asset.materials.size();
    if(!options.baseColorFactors.empty() && options.baseColorFactors.size()!=count)
        return fail("glTF material factor count does not match the asset");
    if(!options.emissiveFactors.empty() && options.emissiveFactors.size()!=count)
        return fail("glTF emissive factor count does not match the asset");
    if(!options.probeEmissionChannels.empty() && options.probeEmissionChannels.size()!=count)
        return fail("Probe emitter channels must match materials");
    const auto equal=[]<class T>(const std::vector<T>& owned,std::span<const T> input) {
        return owned.size()==input.size() &&
            (owned.empty() || std::memcmp(owned.data(),input.data(),input.size_bytes())==0);
    };
    for(const auto& p:materialInputs_) {
        if(p->tint==options.tint && equal(p->colors,options.baseColorFactors) &&
           equal(p->emissions,options.emissiveFactors) && equal(p->channels,options.probeEmissionChannels)) {
            ++materialStats.reuses;result=p;return true;
        }
    }
    for(std::size_t i=0;i<4;++i) if(const float v=options.tint[i];
        !std::isfinite(v) || v<0 || v>(i==3?1.f:2.f))
        return fail("glTF scene RGB tint must be in [0,2], alpha in [0,1]");
    for(const auto& color:options.baseColorFactors)for(float v:color)
        if(!std::isfinite(v) || v<0 || v>1)return fail("glTF material factors must be finite and normalized");
    for(const auto& color:options.emissiveFactors)for(float v:color)
        if(!std::isfinite(v) || v<0 || v>65504)
            return fail("glTF emissive factors must be finite nonnegative radiance within 65504");
    for(float v:options.probeEmissionChannels)if(!std::isfinite(v))
        return fail("Probe emitter channels must be finite");
    auto p=std::make_shared<GltfPreparedMaterials>();
    p->tint=options.tint;
    p->colors.assign(options.baseColorFactors.begin(),options.baseColorFactors.end());
    p->emissions.assign(options.emissiveFactors.begin(),options.emissiveFactors.end());
    p->channels.assign(options.probeEmissionChannels.begin(),options.probeEmissionChannels.end());
    p->values.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        auto color=p->colors.empty()?asset.materials[i].baseColorFactor:p->colors[i];
        for(unsigned c=0;c<4;++c)color[c]*=p->tint[c];
        p->values.push_back({color,p->emissions.empty()?asset.materials[i].emissiveFactor:p->emissions[i],
            p->channels.empty()?0.f:p->channels[i]});
    }
    p->bytes=p->colors.capacity()*sizeof(p->colors[0])+p->emissions.capacity()*sizeof(p->emissions[0])+
        p->channels.capacity()*sizeof(float)+p->values.capacity()*sizeof(p->values[0]);
    ++materialStats.builds;materialStats.materialEvaluations+=count;
    if(p->bytes<=materialBudget) {
        while(!materialInputs_.empty() && (materialInputs_.size()>=materialCapacity ||
              materialStats.retainedBytes+p->bytes>materialBudget)) {
            materialStats.retainedBytes-=materialInputs_.front()->bytes;
            materialInputs_.erase(materialInputs_.begin());
        }
        materialInputs_.push_back(p);materialStats.retainedBytes+=p->bytes;
        materialStats.records=materialInputs_.size();
    }
    result=std::move(p);return true;
}
bool GltfSceneDrawPlan::prepareInstance(const GltfSceneAsset& asset,const GltfScenePose& pose,
    const GltfSceneDrawOptions& options,std::shared_ptr<const GltfPreparedInstance>& result,std::string* error) {
    const auto fail=[&](std::string message){if(error)*error=std::move(message);return false;};
    for(const auto& p:instances_) {
        if(std::memcmp(p->placement.data(),options.instanceTransform.data(),sizeof(p->placement)) ||
           !sameBytes(p->globals,pose.global) || p->palettes.size()!=pose.palettes.size())continue;
        bool match=true;
        for(std::size_t i=0;i<p->palettes.size();++i)if(!sameBytes(p->palettes[i],pose.palettes[i])){match=false;break;}
        if(match){++stats.instanceReuses;result=p;return true;}
    }
    auto prepared=std::make_shared<GltfPreparedInstance>();
    prepared->models.resize(asset.nodes.size());std::vector<bool> transformed(asset.nodes.size());
    // Preflight every submission before the first draw is issued, so a failure
    // can never leave a partially drawn frame.
    for (std::size_t index = 0u; index < asset.primitives.size(); ++index) {
        const GltfScenePrimitive& primitive = asset.primitives[index];
        if (primitive.materialIndex < 0 ||
            static_cast<std::size_t>(primitive.materialIndex) >=
                asset.materials.size()) {
            return fail("primitive " + std::to_string(index) +
                        " has no supported material binding");
        }
        if (primitive.nodeIndex < 0 ||
            static_cast<std::size_t>(primitive.nodeIndex) >= pose.global.size()) {
            return fail("primitive " + std::to_string(index) +
                        " references an out-of-range node");
        }
        if (primitive.vertices.empty() || primitive.indices.empty()) {
            return fail("primitive " + std::to_string(index) +
                        " has no submittable geometry");
        }
        const auto node = static_cast<std::size_t>(primitive.nodeIndex);
        if (!transformed[node]) {
            prepared->models[node] = gltfSceneMultiplyMatrices(options.instanceTransform,
                toArray(pose.global[node]));
            transformed[node] = true;
            ++stats.transformEvaluations;
        }
        const auto& composed = prepared->models[node];
        if (!finiteMatrix(composed)) return fail("composed scene transform is not finite");
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(pose.global[static_cast<std::size_t>(
                        primitive.nodeIndex)][column][row])) {
                    return fail("primitive " + std::to_string(index) +
                                " has a non-finite node transform");
                }
            }
        }
        if (primitive.skinIndex >= 0) {
            if (static_cast<std::size_t>(primitive.skinIndex) >=
                asset.skins.size()) {
                return fail("primitive " + std::to_string(index) +
                            " references an out-of-range skin");
            }
            const std::size_t expected =
                asset.skins[static_cast<std::size_t>(primitive.skinIndex)]
                    .joints.size();
            const std::vector<glm::mat4>& palette = pose.palettes[index];
            if (expected == 0u || expected > kMaxSkinJoints || palette.size() != expected) {
                return fail("primitive " + std::to_string(index) +
                            " needs " + std::to_string(expected) +
                            " palette matrices but the pose has " +
                            std::to_string(palette.size()));
            }
            for (const glm::mat4& matrix : palette) {
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        if (!std::isfinite(matrix[column][row])) {
                            return fail("primitive " + std::to_string(index) +
                                        " has a non-finite palette matrix");
                        }
                    }
                }
            }
        } else if (!pose.palettes[index].empty()) {
            return fail("primitive " + std::to_string(index) +
                        " is not skinned but the pose carries a palette");
        }
    }

    prepared->skinnedBounds.resize(asset.primitives.size());
    for(std::size_t i=0;i<asset.primitives.size();++i) {
        const auto& p=asset.primitives[i];
        if(p.skinIndex>=0)prepared->skinnedBounds[i]=gltfSceneSkinnedBounds(p,
            glm::make_mat4(prepared->models[p.nodeIndex].data()),pose.palettes[i]);
    }
    prepared->id=nextInstance_++;
    prepared->placement=options.instanceTransform;prepared->globals=pose.global;prepared->palettes=pose.palettes;
    prepared->bytes=prepared->models.size()*sizeof(prepared->models[0])+prepared->globals.size()*sizeof(glm::mat4)
        +prepared->palettes.size()*sizeof(std::vector<glm::mat4>);
    for(const auto& p:prepared->palettes)prepared->bytes+=p.size()*sizeof(glm::mat4);
    prepared->deformations.resize(prepared->palettes.size());
    for(std::size_t i=0;i<prepared->palettes.size();++i)if(const auto& p=prepared->palettes[i];!p.empty())
        prepared->deformations[i]=deformations_.publish(i,{reinterpret_cast<const float*>(p.data()),p.size()*16u});
    prepared->bytes+=prepared->deformations.size()*sizeof(WorldDeformation)+prepared->skinnedBounds.size()*sizeof(GltfSceneBounds);
    for(const auto& v:prepared->deformations)if(v)prepared->bytes+=v->values().size_bytes();
    ++stats.instanceBuilds;
    if(prepared->bytes<=instanceBudget) {
        while(!instances_.empty() && (instances_.size()>=instanceCapacity ||
              stats.retainedInstanceBytes+prepared->bytes>instanceBudget)) {
            stats.retainedInstanceBytes-=instances_.front()->bytes;instances_.erase(instances_.begin());
        }
        instances_.push_back(prepared);stats.retainedInstanceBytes+=prepared->bytes;
        stats.instanceRecords=instances_.size();
    }
    result=std::move(prepared);return true;
}
std::shared_ptr<const GltfPreparedSelection> GltfSceneDrawPlan::findSelection(
    const GltfPreparedInstance& instance,const GltfSceneDrawOptions& options) {
    const bool standard=options.materialMode==2u;
    for(const auto& s:selections_) {
        if(s->instance==instance.id && s->view==options.cullingViewProjection && s->cullSkinnedShadow==options.cullSkinnedShadow &&
           s->prewarm==options.prewarmOnly && s->translucent==options.translucent && s->standard==standard &&
           s->excluded.size()==options.excludedNodes.size() &&
           std::equal(s->excluded.begin(),s->excluded.end(),options.excludedNodes.begin())) {
            ++stats.visibilityReuses;return s;
        }
    }
    return {};
}
std::shared_ptr<const GltfPreparedSelection> GltfSceneDrawPlan::buildSelection(
    const GltfSceneAsset& asset,const GltfPreparedInstance& instance,const GltfSceneDrawOptions& options) const {
    const bool standard=options.materialMode==2u;
    auto s=std::make_shared<GltfPreparedSelection>();s->instance=instance.id;s->view=options.cullingViewProjection;
    s->prewarm=options.prewarmOnly;s->translucent=options.translucent;s->standard=standard;s->cullSkinnedShadow=options.cullSkinnedShadow;
    s->excluded.assign(options.excludedNodes.begin(),options.excludedNodes.end());
    std::vector<bool> excluded(asset.nodes.size());for(int node:options.excludedNodes)excluded[node]=true;
    s->draws.reserve(asset.primitives.size());
    for(auto i:options.translucent?authored:ordered) {
        const auto& p=asset.primitives[i];if(excluded[p.nodeIndex])continue;
        bool culled=false;
        if(!options.prewarmOnly && options.cullingViewProjection && standard) {
            culled=options.cullSkinnedShadow&&p.skinIndex>=0
                ?gltfSceneBoundsOutsideView(instance.skinnedBounds.at(i),*options.cullingViewProjection)
                :gltfScenePrimitiveOutsideView(p,gltfSceneMultiplyMatrices(*options.cullingViewProjection,instance.models[p.nodeIndex]));
        }
        s->draws.push_back({i,culled});
    }
    s->bytes=s->draws.capacity()*sizeof(GltfPreparedSelection::Draw)+s->excluded.size()*sizeof(int);
    return s;
}
void GltfSceneDrawPlan::retainSelection(std::shared_ptr<const GltfPreparedSelection> s) {
    if(!s->prewarm && s->view && s->standard)stats.visibilityTests+=s->draws.size();
    ++stats.visibilityBuilds;
    if(s->bytes<=visibilityBudget) {
        while(!selections_.empty() && (selections_.size()>=visibilityCapacity ||
              stats.retainedVisibilityBytes+s->bytes>visibilityBudget)) {
            stats.retainedVisibilityBytes-=selections_.front()->bytes;selections_.erase(selections_.begin());
        }
        selections_.push_back(s);stats.retainedVisibilityBytes+=s->bytes;stats.visibilityRecords=selections_.size();
    }
}
std::shared_ptr<const GltfPreparedSelection> GltfSceneDrawPlan::select(const GltfSceneAsset& asset,
    const GltfPreparedInstance& instance,const GltfSceneDrawOptions& options) {
    if(auto s=findSelection(instance,options))return s;
    auto s=buildSelection(asset,instance,options);retainSelection(s);return s;
}
std::shared_ptr<GltfSceneDrawPlan> acquireGltfSceneDrawPlan(const GltfSceneAsset& asset,std::string* error) {
    if(auto p=GltfSceneDrawPlanAccess::get(asset))return p;
    try{return std::make_shared<GltfSceneDrawPlan>(asset);}
    catch(const std::invalid_argument& e){if(error)*error=e.what();return {};}
}
bool publishGltfSceneDrawPlan(GltfSceneAsset& asset,std::string* error) {
    auto p=acquireGltfSceneDrawPlan(asset,error);if(!p)return false;
    GltfSceneDrawPlanAccess::set(asset,std::move(p));return true;
}
}
namespace engine::render::gltf::scene {
GltfSceneMaterialPreparationStats gltfSceneMaterialPreparationStats(const GltfSceneAsset& asset) {
    const auto p=detail::GltfSceneDrawPlanAccess::get(asset);return p?p->materialStats:GltfSceneMaterialPreparationStats{};
}
GltfScenePreparationStats gltfScenePreparationStats(const GltfSceneAsset& asset) {
    const auto p=detail::GltfSceneDrawPlanAccess::get(asset);return p?p->stats:GltfScenePreparationStats{};
}
}
