#include <functional>
#pragma once

// Generic world-renderer adapter for a CPU-owned glTF scene snapshot.
//
// The adapter binds the asset's owned vertices, indices, five decoded map roles
// and scalar factors through the existing cached textured world-mesh draw path.
// It never constructs Model, never creates its own GPU resources and retains no
// renderer pointer. Published assets own bounded CPU preparation caches.
// Borrowed pointers are valid for the duration of the call;
// the caller owns the asset and pose until it returns. Owner-thread use only.

#include "GltfSceneAsset.h"
#include "engine/render/WorldMotionHistory.h"

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"

#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <optional>

namespace engine::render::gltf::scene {

struct GltfSceneDrawOptions {
    std::array<float, 16> viewProjectionMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    // Whole-scene instance placement; composed after each node's global
    // transform.
    std::array<float, 16> instanceTransform{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    // Optional primary-view pose capture; prewarm and transparent draws do not submit.
    WorldMotionHistory* motionHistory = nullptr;
    std::uint64_t motionObject = 0;
    WorldTemporalMaterial temporalMaterial = WorldTemporalMaterial::Geometry;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    std::array<float, 3> cameraPosition{0.0f, 7.0f, 9.0f};
    std::array<float, 3> cameraForward{0.0f, -0.6139406f, -0.7893522f};
    std::array<float, 3> cameraTarget{0.0f, -1.0f, 0.0f};
    // The shared world material path; 2 selects the composed PBR material.
    std::uint8_t materialMode = 2u;
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
    bool translucent = false;
    // Upload the same geometry/materials without submitting a visible draw.
    // May be called outside a frame on the renderer's owner thread.
    bool prewarmOnly = false;
    // Optional conservative static-primitive culling. Supply the actual pass
    // matrix (a light's matrix for shadows), in engine [-w,w] clip convention.
    // Skinning and custom vertex materials bypass it; prewarm always uploads.
    std::optional<std::array<float, 16>> cullingViewProjection;
    bool cullSkinnedShadow=false;
    // Omit primitives attached directly to these nodes. Asset data, hierarchy
    // and animation palettes remain intact; the span is borrowed for this call.
    std::span<const int> excludedNodes;
    // Optional per-material factors for animated material inputs. Empty uses
    // authored factors; otherwise supply one linear RGBA value per material.
    std::span<const std::array<float, 4>> baseColorFactors;
    // Optional linear RGB emission (0..65504) per material, replacing authored
    // factors while retaining emissive textures. Empty preserves authored values.
    std::span<const std::array<float, 3>> emissiveFactors;
    std::span<const float> probeEmissionChannels;
    // Optional explicit GPU ownership. Create/release this renderer-owned scope
    // through IWorldResourceScope. Zero retains the legacy unowned cache path.
    engine::render::WorldResourceScopeId resourceScope = 0u;
    engine::render::WorldEnvironmentLighting environment;
};

struct GltfSceneDrawStats {
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t skinnedDraws = 0u;
    std::uint32_t paletteDraws = 0u;
    std::uint32_t paletteMatrices = 0u;
    std::uint32_t materialBinds = 0u;
    std::uint32_t textureRoles = 0u;
    // Already integrated from this view's captured standard surfaces.
    std::uint32_t composedDraws = 0u;
    std::uint64_t composedTriangles = 0u;
    std::uint32_t culledDraws = 0u;
    std::uint64_t culledTriangles = 0u;
};

struct GltfScenePreparationStats {
    std::uint64_t instanceBuilds = 0, instanceReuses = 0;
    std::uint64_t visibilityBuilds = 0, visibilityReuses = 0;
    std::uint64_t transformEvaluations = 0, visibilityTests = 0;
    std::size_t instanceRecords = 0, visibilityRecords = 0;
    std::size_t retainedInstanceBytes = 0, retainedVisibilityBytes = 0;
};
struct GltfSceneMaterialPreparationStats {
    std::uint64_t builds = 0, reuses = 0, materialEvaluations = 0, viewMaterialPreparations = 0;
    std::size_t records = 0, retainedBytes = 0;
};
GltfSceneMaterialPreparationStats gltfSceneMaterialPreparationStats(const GltfSceneAsset& asset);
GltfScenePreparationStats gltfScenePreparationStats(const GltfSceneAsset& asset);

// Submits opaque/masked primitives, then blended ones, in authored order within
// each pass. Returns false with a diagnostic
// before submitting anything when the pose does not belong to this asset
// generation or any submission input is inconsistent; the caller's statistics
// are only written on success. Native allocation failures in a managed scope
// are reported after submission; they are not transactional draw rollback.
bool drawGltfSceneAsset(IRenderBackend& renderer,
                        const GltfSceneAsset& asset,
                        const GltfScenePose& pose,
                        const GltfSceneDrawOptions& options,
                        GltfSceneDrawStats& stats,
                        std::string* error = nullptr);


namespace detail { struct GltfCollectedView; }
// A family owns collected immutable assets and pose/placement snapshots. Prepare
// a view only after its pass is admitted by the CPU plan; unused shadows/probe faces do no work.
// Camera/material inputs are consumed at prepareView; motion and renderer state
// remain live until draw returns. All public calls stay on the render owner thread.
struct GltfSceneViewRequest {
    std::size_t object = 0;
    GltfSceneDrawOptions options;
};
struct GltfSceneCollectionStats {
    std::uint64_t objects=0, views=0, requests=0;
    std::uint64_t selectionBuilds=0, selectionReuses=0, parallelBatches=0;
};
class GltfSceneCollectedView {
public:
    bool draw(IRenderBackend&,std::size_t request,GltfSceneDrawStats&,std::string* error=nullptr) const;
    bool drawWithMotion(IRenderBackend&,std::size_t request,WorldMotionHistory*,
        GltfSceneDrawStats&,std::string* error=nullptr) const;
    // Resume an immutable selection in bounded ordered batches. At least one
    // visible primitive advances even when it alone exceeds the triangle budget.
    bool drawBudgeted(IRenderBackend&,std::size_t request,std::size_t& cursor,
        std::size_t proxyBudget,std::uint64_t triangleBudget,bool& complete,
        GltfSceneDrawStats&,std::string* error=nullptr) const;
    std::size_t size() const;
private:
    friend class GltfSceneViewFamily;
    std::shared_ptr<const detail::GltfCollectedView> data_;
};
// Keep this scheduler with the presentation owner so helper threads join before
// plugin unload. A family without one owns its temporary scheduler if needed.
class GltfSceneCollectionScheduler {
public:
    GltfSceneCollectionScheduler();
    ~GltfSceneCollectionScheduler();
    // Owner may collect cached assets; independent must touch only its own CPU
    // state. Both complete (including on exceptions) before any view preparation.
    // Do not recursively prepare views or use this scheduler from either job.
    void collectIndependent(const std::function<void()>& owner,const std::function<void()>& independent);
    GltfSceneCollectionScheduler(const GltfSceneCollectionScheduler&)=delete;
    GltfSceneCollectionScheduler& operator=(const GltfSceneCollectionScheduler&)=delete;
private:
    friend class GltfSceneViewFamily;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class GltfSceneViewFamily {
public:
    explicit GltfSceneViewFamily(GltfSceneCollectionScheduler* scheduler=nullptr);
    ~GltfSceneViewFamily();
    GltfSceneViewFamily(const GltfSceneViewFamily&)=delete;
    GltfSceneViewFamily& operator=(const GltfSceneViewFamily&)=delete;
    // Placement is fixed for this family. Recollect after any pose/placement
    // change. The collected record keeps the original published asset alive.
    bool collect(std::shared_ptr<const GltfSceneAsset>,const GltfScenePose&,
        const std::array<float,16>& placement,std::size_t& object,std::string* error=nullptr);
    // Owns all input spans. Visibility jobs read immutable data, join before
    // publication and retain authored request/primitive order. No GPU calls.
    bool prepareView(std::span<const GltfSceneViewRequest>,GltfSceneCollectedView&,
        std::string* error=nullptr);
    // Snapshot admitted view inputs, prepare their independent selections in one
    // joined batch, then consume in pass order. Motion is bound at submission.
    bool planView(std::span<const GltfSceneViewRequest>,std::size_t& view,std::string* error=nullptr);
    bool preparePlannedViews(std::string* error=nullptr,const std::function<void()>& independent={});
    void prepareDeformations(IRenderBackend&);
    // Immutable collected versions; unknown/skinned bounds remain conservative.
    std::uint64_t shadowContentRevision(std::span<const std::size_t>,const std::array<float,16>&) const;
    GltfSceneCollectedView plannedView(std::size_t view) const;
    GltfSceneCollectionStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::render::gltf::scene
