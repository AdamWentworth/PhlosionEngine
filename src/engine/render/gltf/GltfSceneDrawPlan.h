#include "GltfSceneCulling.h"
#pragma once
#include "GltfSceneRenderer.h"
#include "engine/render/IWorldPreparedResources.h"
#include <memory>
#include <vector>

namespace engine::render::gltf::scene::detail {
struct GltfNativeResources {
    const IWorldPreparedResources* owner=nullptr;
    WorldResourceScopeId scope=0;
    std::vector<WorldPreparedResourceToken> geometry,materials;
    std::size_t bytes=0;
};
struct GltfPreparedMaterials {
    struct Values {
        std::array<float,4> color;
        std::array<float,3> emission;
        float probeChannel;
    };
    std::array<float,4> tint;
    std::vector<std::array<float,4>> colors;
    std::vector<std::array<float,3>> emissions;
    std::vector<float> channels;
    std::vector<Values> values;
    std::size_t bytes = 0;
};
struct GltfPreparedInstance {
    std::uint64_t id = 0;
    std::array<float,16> placement{};
    std::vector<glm::mat4> globals;
    std::vector<std::vector<glm::mat4>> palettes;
    std::vector<WorldDeformation> deformations;
    std::vector<GltfSceneBounds> skinnedBounds;
    std::vector<std::array<float,16>> models;
    std::size_t bytes = 0;
};
struct GltfPreparedSelection {
    struct Draw { std::size_t primitive; bool culled; };
    std::uint64_t instance = 0;
    std::optional<std::array<float,16>> view;
    std::vector<int> excluded;
    bool prewarm = false, translucent = false, standard = false, cullSkinnedShadow=false;
    std::vector<Draw> draws;
    std::size_t bytes = 0;
};
// Owner-thread preparation; returned records are immutable and independently
// owned, including across cache eviction. Native resource handles are checked
// by the current renderer before use; no native allocation/pointer is owned here.
class GltfSceneDrawPlan {
public:
    const GltfSceneAsset* owner;
    std::uint64_t generation;
    struct Material {
        backend::WorldTextureData texture;
        std::array<backend::WorldTextureMipLevel,5> levels;
    };
    std::vector<Material> materials;
    std::vector<std::size_t> authored, ordered;
    GltfScenePreparationStats stats;
    GltfSceneMaterialPreparationStats materialStats;
    std::shared_ptr<GltfNativeResources> nativeResources(const IWorldPreparedResources*,WorldResourceScopeId);

    std::shared_ptr<const GltfPreparedSelection> findSelection(
        const GltfPreparedInstance&,const GltfSceneDrawOptions&);
    std::shared_ptr<const GltfPreparedSelection> buildSelection(const GltfSceneAsset&,
        const GltfPreparedInstance&,const GltfSceneDrawOptions&) const;
    void retainSelection(std::shared_ptr<const GltfPreparedSelection>);
    static constexpr std::size_t materialCapacity = 4, materialBudget = 1u<<20;
    bool prepareMaterials(const GltfSceneAsset&, const GltfSceneDrawOptions&,
        std::shared_ptr<const GltfPreparedMaterials>&, std::string* error);
    static constexpr std::size_t instanceCapacity = 4, visibilityCapacity = 16;
    static constexpr std::size_t instanceBudget = 8u<<20, visibilityBudget = 2u<<20;
    explicit GltfSceneDrawPlan(const GltfSceneAsset& asset);
    GltfSceneDrawPlan(const GltfSceneDrawPlan&) = delete;
    bool prepareInstance(const GltfSceneAsset& asset, const GltfScenePose& pose,
        const GltfSceneDrawOptions& options,
        std::shared_ptr<const GltfPreparedInstance>& result, std::string* error);
    std::shared_ptr<const GltfPreparedSelection> select(const GltfSceneAsset& asset,
        const GltfPreparedInstance& instance, const GltfSceneDrawOptions& options);
private:
    std::vector<std::shared_ptr<GltfNativeResources>> nativeResources_;
    std::size_t nativeBytes_=0;
    std::vector<std::shared_ptr<const GltfPreparedMaterials>> materialInputs_;
    std::uint64_t nextInstance_ = 1;
    std::vector<std::shared_ptr<const GltfPreparedInstance>> instances_;
    WorldDeformationSet deformations_;
    std::vector<std::shared_ptr<const GltfPreparedSelection>> selections_;
};
struct GltfSceneDrawPlanAccess {
    static std::shared_ptr<GltfSceneDrawPlan> get(const GltfSceneAsset&);
    static void set(GltfSceneAsset&, std::shared_ptr<GltfSceneDrawPlan>);
};
// Called before publishing a const snapshot. Mutating a published asset is not
// supported. Mutable/manual copies use a freshly validated plan on each call.
bool publishGltfSceneDrawPlan(GltfSceneAsset&, std::string* error);
std::shared_ptr<GltfSceneDrawPlan> acquireGltfSceneDrawPlan(const GltfSceneAsset&, std::string* error);
}
