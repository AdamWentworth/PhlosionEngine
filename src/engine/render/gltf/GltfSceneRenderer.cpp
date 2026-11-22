#include "engine/render/WorldShadowSchedule.h"
#include "engine/render/gltf/GltfSceneCulling.h"
#include "engine/render/WorldSurfaceComposition.h"
#include "GltfSceneRenderer.h"
#include "GltfSceneDrawPlan.h"
#include "engine/render/IWorldDeformationPreparation.h"
#include "engine/render/IWorldLayeredMaterials.h"
#include "GltfSceneCulling.h"
#include "engine/render/WorldRecordingWorkers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <limits>
#include <vector>

namespace engine::render::gltf::scene {
namespace {

using engine::render::backend::WorldTextureData;


WorldTextureData makeTextureData(const GltfSceneMaterial& material,
                                 const WorldTextureData& materialTemplate,
                                 const detail::GltfPreparedMaterials::Values& values,
                                 const GltfSceneDrawOptions& options) {
    WorldTextureData texture = materialTemplate;
    texture.layeredMaterialIndex=material.layeredMaterialIndex;
    texture.materialMode = material.unlit ? 255u : options.materialMode;
    texture.alphaMode = options.translucent ? 2u : material.alphaMode;
    texture.emissiveFactorR = values.emission[0];
    texture.emissiveFactorG = values.emission[1];
    texture.emissiveFactorB = values.emission[2];
    texture.vertexColorMulR = values.color[0];
    texture.vertexColorMulG = values.color[1];
    texture.vertexColorMulB = values.color[2];
    texture.vertexColorMulA = values.color[3];
    texture.cameraPosX = options.cameraPosition[0];
    texture.cameraPosY = options.cameraPosition[1];
    texture.cameraPosZ = options.cameraPosition[2];
    texture.cameraForwardX = options.cameraForward[0];
    texture.cameraForwardY = options.cameraForward[1];
    texture.cameraForwardZ = options.cameraForward[2];
    texture.cameraTargetX = options.cameraTarget[0];
    texture.cameraTargetY = options.cameraTarget[1];
    texture.cameraTargetZ = options.cameraTarget[2];
    texture.environment = options.environment;
    texture.environment.surfaceAbsorption = std::clamp(material.roughnessFactor,0.f,1.f)*
        (1.f-std::clamp(material.metallicFactor,0.f,1.f));
    if (material.unlit || material.alphaMode!=0) texture.environment.surfaceWeatherEnabled=false;
    texture.materialTimeSec = values.probeChannel;
    return texture;
}

}  // namespace

namespace {
bool validateInstance(const GltfSceneAsset& asset,const GltfScenePose& pose,std::string* error) {
    const auto fail=[&](std::string message){if(error)*error=std::move(message);return false;};
    if (!gltfScenePoseMatchesAsset(asset, pose)) {
        return fail(
            "the pose was sampled from a different glTF scene asset "
            "generation; reload the pose before drawing");
    }
    if (pose.global.size() != asset.nodes.size()) {
        return fail("the pose has " + std::to_string(pose.global.size()) +
                    " global transforms for " +
                    std::to_string(asset.nodes.size()) + " nodes");
    }
    if (pose.palettes.size() != asset.primitives.size()) {
        return fail("the pose has " + std::to_string(pose.palettes.size()) +
                    " palettes for " +
                    std::to_string(asset.primitives.size()) + " primitives");
    }
    return true;
}
bool validateView(const GltfSceneAsset& asset,const GltfSceneDrawOptions& options,std::string* error) {
    const auto fail=[&](std::string message){if(error)*error=std::move(message);return false;};
    if (options.surfaceWidth <= 0 || options.surfaceHeight <= 0) {
        return fail("glTF scene draw needs positive surface dimensions");
    }
    for (const int node : options.excludedNodes) {
        if (node < 0 || static_cast<std::size_t>(node) >= asset.nodes.size())
            return fail("glTF excluded node is out of range");
    }
    if (asset.materials.empty() && !asset.primitives.empty())
        return fail("the asset has primitives but no materials");
    const auto finiteMatrix = [](const std::array<float, 16>& matrix) {
        for (const float value : matrix) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    };
    if (!finiteMatrix(options.viewProjectionMatrix) ||
        !finiteMatrix(options.instanceTransform)) {
        return fail("glTF scene draw matrices must be finite");
    }
    if (options.cullingViewProjection && !finiteMatrix(*options.cullingViewProjection))
        return fail("glTF culling matrix must be finite");
    for (const auto& vector : {options.cameraPosition, options.cameraForward, options.cameraTarget}) {
        for (const float value : vector) {
            if (!std::isfinite(value)) return fail("glTF scene camera must be finite");
        }
    }

    return true;
}
struct PreparedMaterialView {
    std::vector<std::size_t> indices;
    std::vector<WorldTextureData> values;
};
std::shared_ptr<const PreparedMaterialView> prepareMaterialView(const GltfSceneAsset& asset,
    const detail::GltfSceneDrawPlan& plan,const detail::GltfPreparedMaterials& materials,
    const detail::GltfPreparedSelection& selection,const GltfSceneDrawOptions& options) {
    auto result=std::make_shared<PreparedMaterialView>();
    result->indices.resize(asset.materials.size(),std::numeric_limits<std::size_t>::max());
    for(const auto& draw:selection.draws) {
        const auto material=asset.primitives[draw.primitive].materialIndex;
        auto& slot=result->indices[material];
        if(slot!=std::numeric_limits<std::size_t>::max())continue;
        slot=result->values.size();
        result->values.push_back(makeTextureData(asset.materials[material],plan.materials[material].texture,
            materials.values[material],options));
    }
    return result;
}
bool submitPrepared(IRenderBackend& renderer,const GltfSceneAsset& asset,
    const GltfSceneDrawOptions& options,
    const std::shared_ptr<detail::GltfSceneDrawPlan>& plan,
    const std::shared_ptr<const detail::GltfPreparedInstance>& instance,
    const std::shared_ptr<const detail::GltfPreparedMaterials>& materials,
    const std::shared_ptr<const detail::GltfPreparedSelection>& selection,
    GltfSceneDrawStats& stats,std::string* error,
    std::shared_ptr<const PreparedMaterialView> preparedMaterials={},
    std::size_t first=0,std::size_t last=std::numeric_limits<std::size_t>::max()) {
    const auto fail=[&](std::string message){if(error)*error=std::move(message);return false;};
    if (!options.prewarmOnly && options.motionHistory &&
        (!options.motionHistory->active() || !options.motionObject))
        return fail("glTF motion capture requires an active view and owner identity");

    const WorldClusteredLightingView* composedLighting=nullptr;
    if (!options.prewarmOnly && !options.motionHistory) {
        auto* surface=dynamic_cast<IWorldSurfaceBuffer*>(&renderer);
        composedLighting=surface?surface->worldSurfaceCompositionLighting():nullptr;
    }
    if(!preparedMaterials) {
        preparedMaterials=prepareMaterialView(asset,*plan,*materials,*selection,options);
        plan->materialStats.viewMaterialPreparations+=preparedMaterials->values.size();
    }
    constexpr auto missing=std::numeric_limits<std::size_t>::max();
    const auto& materialStates=preparedMaterials->values;
    std::vector<std::int8_t> composedMaterials(materialStates.size(),-1);
    const auto prepareMaterial=[&](std::size_t index) {
        const auto slot=preparedMaterials->indices.at(index);
        if(slot==missing)throw std::logic_error("Unprepared collected glTF material");
        if(composedMaterials[slot]<0)composedMaterials[slot]=composedLighting &&
            worldSurfaceCompositionMaterial(&materialStates[slot],*composedLighting);
        return slot;
    };

    IWorldResourceScope* scopeApi = nullptr;
    if (options.resourceScope != 0u) {
        scopeApi = dynamic_cast<IWorldResourceScope*>(&renderer);
        if (!scopeApi || !scopeApi->beginWorldResourceScope(options.resourceScope))
            return fail("renderer rejected the glTF scene resource scope");
    }
    struct EndScope {
        IWorldResourceScope* api;
        ~EndScope() { if (api) api->endWorldResourceScope(); }
    } endScope{scopeApi};
    if(auto* layered=dynamic_cast<IWorldLayeredMaterials*>(&renderer)){
        if(asset.layeredMaterials&&!options.resourceScope)return fail("Layered glTF requires an owned resource scope");
        layered->bindWorldLayeredMaterials(options.resourceScope,asset.layeredMaterials);
    }else if(asset.layeredMaterials)return fail("Renderer does not support layered glTF materials");
    // Preserve backend state once for this ordered group of primitives.
    if (!options.prewarmOnly) renderer.beginWorldIndexedBatchSubmission();
    struct EndBatch {
        IRenderBackend& renderer;
        bool active;
        ~EndBatch() { if (active) renderer.endWorldIndexedBatchSubmission(); }
    } endBatch{renderer,!options.prewarmOnly};
    auto* resources=options.resourceScope && detail::GltfSceneDrawPlanAccess::get(asset)
        ?dynamic_cast<IWorldPreparedResources*>(&renderer):nullptr;
    const auto native=resources?plan->nativeResources(resources,options.resourceScope):nullptr;
    const auto prepareResources=[&](std::size_t index,const WorldTextureData& texture) {
        const auto& primitive=asset.primitives[index];
        auto& geometry=native->geometry[index];
        auto& material=native->materials[primitive.materialIndex];
        if(!resources->worldPreparedGeometryValid(geometry))
            geometry=resources->prepareWorldGeometry(primitive.geometryKey.c_str(),
                primitive.vertices.data(),primitive.vertices.size(),primitive.indices.data(),primitive.indices.size());
        if(!resources->worldPreparedMaterialValid(material))
            material=resources->prepareWorldMaterial(texture);
    };
    std::vector<backend::WorldPreparedViewToken> preparedViews(asset.materials.size());
    GltfSceneDrawStats collected;
    // Establish opaque depth before drawing transparent surface overlays.
    // Preserve authored primitive order within each pass.
    for(std::size_t selectedIndex=first;selectedIndex<std::min(last,selection->draws.size());++selectedIndex) {
        const auto& selected=selection->draws[selectedIndex];
        const auto index=selected.primitive;
        const GltfScenePrimitive& primitive=asset.primitives[index];
        const bool transparent=options.translucent || asset.materials[primitive.materialIndex].alphaMode==2u;
        const auto composedSlot=composedLighting?prepareMaterial(primitive.materialIndex):missing;
        if (composedSlot!=missing && composedMaterials[composedSlot]) {
            ++collected.composedDraws;
            collected.composedTriangles += primitive.indices.size() / 3u;
            continue;
        }
        const std::vector<glm::mat4>& palette = instance->palettes[index];

        const auto& modelMatrix=instance->models[primitive.nodeIndex];
        if(selected.culled) {
            ++collected.culledDraws;collected.culledTriangles+=primitive.indices.size()/3u;continue;
        }
        const auto materialSlot=prepareMaterial(primitive.materialIndex);
        WorldTextureData texture=materialStates[materialSlot];
        texture.modelMatrix=modelMatrix;
        if (!options.prewarmOnly) {
            auto& view = preparedViews[primitive.materialIndex];
            if (!view) view = renderer.prepareWorldViewData(texture);
            texture.preparedView = view;
        }
        if (!palette.empty()) {
            texture.gpuSkinning = 1u;
            texture.deformation=instance->deformations[index];
            // Mode 0 binds one matrix per joint; the palette already contains
            // the inverse(meshGlobal) * jointGlobal * inverseBind product.
            texture.gpuSkinningMode = 0u;
            texture.skinMatrixCount =
                static_cast<std::uint32_t>(palette.size());
            texture.skinMatrices =
                reinterpret_cast<const float*>(palette.data());
            ++collected.paletteDraws;
            ++collected.skinnedDraws;
            collected.paletteMatrices +=
                static_cast<std::uint32_t>(palette.size());
        }

        WorldMotionSample motionSample;
        if (!options.prewarmOnly && !transparent && options.motionHistory) {
            WorldMotionObject motion;
            motion.identity = {options.motionObject, index};
            motion.revision = asset.generation;
            motion.temporalMaterial=options.temporalMaterial;
            motion.model = modelMatrix;
            motion.deformationVersion=texture.deformation;
            motion.materialMode = options.materialMode;
            motion.matrixCount = texture.skinMatrixCount;
            if (motion.matrixCount) motion.deformation = {texture.skinMatrices,
                static_cast<std::size_t>(motion.matrixCount) * 16};
            motion.materialParameters={texture.materialTimeSec,texture.materialFlags,0,0,
                texture.materialRect0U,texture.materialRect0V,texture.materialRect0W,texture.materialRect0H,
                texture.materialRect1U,texture.materialRect1V,texture.materialRect1W,texture.materialRect1H};
            motionSample=options.motionHistory->submit(motion);
        }
        WorldSurfaceMotionDraw motionDraw(renderer, motionSample.previous?options.motionHistory:nullptr,
            motionSample.previous?std::span<const WorldMotionSample>(&motionSample,1):std::span<const WorldMotionSample>{});
        if (options.prewarmOnly) {
            // Prewarm also prepares shader variants; a texture binding alone
            // cannot replace it, even when the immutable material is retained.
            renderer.prewarmWorldTextureData(&texture);
            if(native)prepareResources(index,texture);
            if(!native || !resources->worldPreparedGeometryValid(native->geometry[index]))
                renderer.prewarmWorldIndexedMeshCached(primitive.geometryKey.c_str(),
                    primitive.vertices.data(), primitive.vertices.size(),
                    primitive.indices.data(), primitive.indices.size());
        } else {
            bool submitted=false;
            if(native) {
                auto& geometry=native->geometry[index];
                auto& material=native->materials[primitive.materialIndex];
                if(geometry && material)
                    submitted=resources->drawWorldPreparedIndexed(geometry,material,texture,
                        options.viewProjectionMatrix.data(),options.surfaceWidth,options.surfaceHeight);
                if(!submitted) {
                    prepareResources(index,texture);
                    if(geometry && material)
                        submitted=resources->drawWorldPreparedIndexed(geometry,material,texture,
                            options.viewProjectionMatrix.data(),options.surfaceWidth,options.surfaceHeight);
                }
            }
            if(!submitted)renderer.drawWorldIndexedMeshTexturedCached(
            primitive.geometryKey.c_str(),
            primitive.vertices.data(),
            primitive.vertices.size(),
            primitive.indices.data(),
            primitive.indices.size(),
            &texture,
            options.viewProjectionMatrix.data(),
            options.surfaceWidth,
            options.surfaceHeight);
        }
        if (!options.prewarmOnly) ++collected.drawCalls;
        collected.triangles +=
            static_cast<std::uint64_t>(primitive.indices.size() / 3u);
        ++collected.materialBinds;
        collected.textureRoles += 5u;
    }

    if (scopeApi) {
        engine::render::WorldResourceScopeStats scope;
        if (!scopeApi->getWorldResourceScopeStats(options.resourceScope, scope) ||
            scope.status != engine::render::WorldResourceScopeStatus::Ready)
            return fail("glTF scene GPU allocation failed: " + scope.error);
    }
    stats = collected;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace
bool drawGltfSceneAsset(IRenderBackend& renderer,const GltfSceneAsset& asset,
    const GltfScenePose& pose,const GltfSceneDrawOptions& options,GltfSceneDrawStats& stats,std::string* error) {
    if(!validateInstance(asset,pose,error) || !validateView(asset,options,error))return false;
    auto plan=detail::acquireGltfSceneDrawPlan(asset,error);if(!plan)return false;
    std::shared_ptr<const detail::GltfPreparedMaterials> materials;
    if(!plan->prepareMaterials(asset,options,materials,error))return false;
    std::shared_ptr<const detail::GltfPreparedInstance> instance;
    if(!plan->prepareInstance(asset,pose,options,instance,error))return false;
    if(!options.prewarmOnly) {
        std::vector<WorldSkinPalette> palettes;
        for(const auto& p:instance->deformations)if(p)palettes.emplace_back(p);
        prepareWorldDeformations(renderer,palettes);
    }
    return submitPrepared(renderer,asset,options,plan,instance,materials,
        plan->select(asset,*instance,options),stats,error);
}
namespace detail {
struct GltfCollectedObject {
    std::shared_ptr<const GltfSceneAsset> asset;
    std::shared_ptr<GltfSceneDrawPlan> plan;
    std::shared_ptr<const GltfPreparedInstance> instance;
};
struct GltfCollectedView {
    struct Draw {
        GltfCollectedObject object;
        GltfSceneDrawOptions options;
        std::vector<int> excluded;
        std::shared_ptr<const GltfPreparedMaterials> materials;
        std::shared_ptr<const GltfPreparedSelection> selection;
        std::shared_ptr<const PreparedMaterialView> preparedMaterials;
    };
    std::vector<Draw> draws;
};
}
struct GltfSceneCollectionScheduler::Impl { std::unique_ptr<WorldRecordingWorkers> workers; };
GltfSceneCollectionScheduler::GltfSceneCollectionScheduler():impl_(std::make_unique<Impl>()){}
GltfSceneCollectionScheduler::~GltfSceneCollectionScheduler()=default;
void GltfSceneCollectionScheduler::collectIndependent(const std::function<void()>& owner,
    const std::function<void()>& independent) {
    if(!independent){owner();return;}
    if(!impl_->workers)impl_->workers=std::make_unique<WorldRecordingWorkers>();
    impl_->workers->run([&](unsigned lane){if(lane==0)owner();else independent();});
}
struct GltfSceneViewFamily::Impl {
    GltfSceneCollectionScheduler* scheduler=nullptr;
    std::unique_ptr<GltfSceneCollectionScheduler> ownedScheduler;
    std::vector<detail::GltfCollectedObject> objects;
    GltfSceneCollectionStats stats;
    std::vector<std::shared_ptr<detail::GltfCollectedView>> views;
    std::size_t ready=0;
};
GltfSceneViewFamily::GltfSceneViewFamily(GltfSceneCollectionScheduler* scheduler):impl_(std::make_unique<Impl>()){
    impl_->scheduler=scheduler;
}
GltfSceneViewFamily::~GltfSceneViewFamily()=default;
GltfSceneCollectionStats GltfSceneViewFamily::stats()const{return impl_->stats;}
bool GltfSceneViewFamily::collect(std::shared_ptr<const GltfSceneAsset> asset,
    const GltfScenePose& pose,const std::array<float,16>& placement,std::size_t& object,std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!asset)return fail("Scene collection requires an asset snapshot");
    auto plan=detail::GltfSceneDrawPlanAccess::get(*asset);
    if(!plan)return fail("Scene collection requires a published immutable asset");
    if(!validateInstance(*asset,pose,error))return false;
    for(float v:placement)if(!std::isfinite(v))return fail("Scene placement must be finite");
    GltfSceneDrawOptions options;options.instanceTransform=placement;
    std::shared_ptr<const detail::GltfPreparedInstance> instance;
    if(!plan->prepareInstance(*asset,pose,options,instance,error))return false;
    const auto index=impl_->objects.size();
    impl_->objects.push_back({std::move(asset),std::move(plan),std::move(instance)});
    object=index;++impl_->stats.objects;if(error)error->clear();return true;
}
bool GltfSceneViewFamily::planView(std::span<const GltfSceneViewRequest> requests,
    std::size_t& id,std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(impl_->views.size()>=256)return fail("Scene view plan capacity exceeded");
    auto view=std::make_shared<detail::GltfCollectedView>();
    view->draws.reserve(requests.size());
    // Preflight the whole view before scheduling or exposing any draw.
    for(const auto& r:requests) {
        if(r.object>=impl_->objects.size())return fail("Scene view references an unknown object");
        const auto& object=impl_->objects[r.object];
        if(r.options.instanceTransform!=object.instance->placement)
            return fail("Scene placement changed after collection; recollect the object");
        if(!validateView(*object.asset,r.options,error))return false;
        if(!r.options.prewarmOnly && r.options.motionHistory &&
           (!r.options.motionHistory->active() || !r.options.motionObject))
            return fail("glTF motion capture requires an active view and owner identity");
        detail::GltfCollectedView::Draw draw;draw.object=object;draw.options=r.options;
        if(!object.plan->prepareMaterials(*object.asset,r.options,draw.materials,error))return false;
        draw.excluded.assign(r.options.excludedNodes.begin(),r.options.excludedNodes.end());
        draw.options.excludedNodes=draw.excluded;
        // Material records now own all authored spans; no caller scratch survives.
        draw.options.baseColorFactors={};draw.options.emissiveFactors={};draw.options.probeEmissionChannels={};
        view->draws.push_back(std::move(draw));
    }
    id=impl_->views.size();impl_->views.push_back(std::move(view));
    if(error)error->clear();return true;
}
bool GltfSceneViewFamily::preparePlannedViews(std::string* error,const std::function<void()>& independent) {
    using Draw=detail::GltfCollectedView::Draw;
    std::vector<Draw*> pending;
    std::vector<std::pair<Draw*,Draw*>> aliases;
    std::size_t primitives=0,requests=0;
    const auto same=[](const Draw& a,const Draw& b) {
        const auto& x=a.options;const auto& y=b.options;
        return a.object.plan==b.object.plan && a.object.instance==b.object.instance &&
            x.cullingViewProjection==y.cullingViewProjection && x.prewarmOnly==y.prewarmOnly &&
            x.translucent==y.translucent && (x.materialMode==2u)==(y.materialMode==2u) && a.excluded==b.excluded;
    };
    for(std::size_t v=impl_->ready;v<impl_->views.size();++v)for(auto& d:impl_->views[v]->draws) {
        ++requests;d.selection=d.object.plan->findSelection(*d.object.instance,d.options);
        if(d.selection)continue;
        const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto* prior){return same(d,*prior);});
        if(found!=pending.end())aliases.emplace_back(&d,*found);
        else {pending.push_back(&d);primitives+=d.object.asset->primitives.size();}
    }
    const auto build=[&](std::size_t n) {
        auto& d=*pending[n];d.selection=d.object.plan->buildSelection(*d.object.asset,*d.object.instance,d.options);
    };
    const bool parallel=(pending.size()>1 && primitives>=256) || (independent && !pending.empty());
    if(parallel) {
        if(!impl_->scheduler) {
            impl_->ownedScheduler=std::make_unique<GltfSceneCollectionScheduler>();
            impl_->scheduler=impl_->ownedScheduler.get();
        }
        auto& workers=impl_->scheduler->impl_->workers;
        if(!workers)workers=std::make_unique<WorldRecordingWorkers>();
        std::atomic<std::size_t> next{0};
        const auto count=pending.size()+(independent?1:0);
        workers->run([&](unsigned) {
            for(auto n=next.fetch_add(1);n<count;n=next.fetch_add(1)) {
                if(n==pending.size())independent();else build(n);
            }
        });
    } else {
        for(std::size_t n=0;n<pending.size();++n)build(n);
        if(independent)independent();
    }
    for(auto* d:pending)d->object.plan->retainSelection(d->selection);
    for(auto [d,prior]:aliases)d->selection=prior->selection;
    // Material preparation depends on completed visibility selections. Each job
    // owns its result; cache/stat publication happens on the owner after the join.
    std::vector<Draw*> materialJobs;
    std::vector<std::pair<Draw*,Draw*>> materialAliases;
    const auto sameMaterials=[](const Draw& a,const Draw& b) {
        const auto& x=a.options;const auto& y=b.options;
        return a.object.plan==b.object.plan && a.materials==b.materials && a.selection==b.selection &&
            x.materialMode==y.materialMode && x.translucent==y.translucent &&
            x.cameraPosition==y.cameraPosition && x.cameraForward==y.cameraForward && x.cameraTarget==y.cameraTarget &&
            std::memcmp(&x.environment,&y.environment,sizeof(x.environment))==0;
    };
    std::size_t materialBytes=0;
    for(std::size_t v=impl_->ready;v<impl_->views.size();++v)for(auto& d:impl_->views[v]->draws) {
        const auto prior=std::find_if(materialJobs.begin(),materialJobs.end(),[&](const auto* p){return sameMaterials(d,*p);});
        if(prior!=materialJobs.end()){materialAliases.emplace_back(&d,*prior);continue;}
        // Conservative upper bound before scheduling any material work.
        materialBytes+=std::min(d.object.asset->materials.size(),d.selection->draws.size())*sizeof(WorldTextureData);
        if(materialBytes>32u*1024u*1024u)throw std::runtime_error("Collected view material budget exhausted");
        materialJobs.push_back(&d);
    }
    const auto buildMaterial=[&](std::size_t i) {
        auto& d=*materialJobs[i];
        d.preparedMaterials=prepareMaterialView(*d.object.asset,*d.object.plan,*d.materials,*d.selection,d.options);
    };
    if(materialJobs.size()>1) {
        if(!impl_->scheduler){impl_->ownedScheduler=std::make_unique<GltfSceneCollectionScheduler>();impl_->scheduler=impl_->ownedScheduler.get();}
        auto& workers=impl_->scheduler->impl_->workers;
        if(!workers)workers=std::make_unique<WorldRecordingWorkers>();
        std::atomic<std::size_t> next{0};
        workers->run([&](unsigned){for(auto i=next.fetch_add(1);i<materialJobs.size();i=next.fetch_add(1))buildMaterial(i);});
    } else if(!materialJobs.empty())buildMaterial(0);
    for(auto* d:materialJobs)d->object.plan->materialStats.viewMaterialPreparations+=d->preparedMaterials->values.size();
    for(auto [d,prior]:materialAliases)d->preparedMaterials=prior->preparedMaterials;
    for(std::size_t v=impl_->ready;v<impl_->views.size();++v)for(auto& d:impl_->views[v]->draws)d.options.excludedNodes={};
    auto& stats=impl_->stats;stats.views+=impl_->views.size()-impl_->ready;stats.requests+=requests;
    stats.selectionBuilds+=pending.size();stats.selectionReuses+=requests-pending.size();stats.parallelBatches+=parallel;
    impl_->ready=impl_->views.size();if(error)error->clear();return true;
}
void GltfSceneViewFamily::prepareDeformations(IRenderBackend& renderer) {
    if(impl_->ready!=impl_->views.size())throw std::logic_error("Deformation upload before view preparation joined");
    std::vector<WorldSkinPalette> palettes;
    for(const auto& view:impl_->views)for(const auto& draw:view->draws) {
        if(draw.options.prewarmOnly)continue;
        for(const auto& selected:draw.selection->draws)if(!selected.culled) {
            const auto& p=draw.object.instance->deformations[selected.primitive];
            if(p)palettes.emplace_back(p);
        }
    }
    prepareWorldDeformations(renderer,palettes);
}
GltfSceneCollectedView GltfSceneViewFamily::plannedView(std::size_t id) const {
    if(id>=impl_->ready)throw std::out_of_range("Scene view was not prepared");
    GltfSceneCollectedView result;result.data_=impl_->views[id];return result;
}
bool GltfSceneViewFamily::prepareView(std::span<const GltfSceneViewRequest> requests,
    GltfSceneCollectedView& result,std::string* error) {
    std::size_t id;
    if(!planView(requests,id,error) || !preparePlannedViews(error))return false;
    result=plannedView(id);return true;
}
std::size_t GltfSceneCollectedView::size()const{return data_?data_->draws.size():0;}
bool GltfSceneCollectedView::draw(IRenderBackend& renderer,std::size_t index,
    GltfSceneDrawStats& stats,std::string* error)const {
    if(!data_ || index>=data_->draws.size()) {
        if(error)*error="Scene draw references an unknown collected request";return false;
    }
    const auto& d=data_->draws[index];
    return submitPrepared(renderer,*d.object.asset,d.options,d.object.plan,d.object.instance,
        d.materials,d.selection,stats,error,d.preparedMaterials);
}
bool GltfSceneCollectedView::drawBudgeted(IRenderBackend& renderer,std::size_t request,
    std::size_t& cursor,std::size_t proxyBudget,std::uint64_t triangleBudget,bool& complete,
    GltfSceneDrawStats& stats,std::string* error)const {
    if(!data_||request>=data_->draws.size()||!proxyBudget||!triangleBudget) {
        if(error)*error="Invalid budgeted scene request";return false;
    }
    const auto& d=data_->draws[request];
    if(cursor>d.selection->draws.size()) {if(error)*error="Stale scene batch cursor";return false;}
    std::size_t end=cursor,proxies=0;std::uint64_t triangles=0;
    while(end<d.selection->draws.size()) {
        const auto& selected=d.selection->draws[end];
        const auto count=d.object.asset->primitives[selected.primitive].indices.size()/3;
        if(!selected.culled && proxies && (proxies>=proxyBudget || count>triangleBudget-triangles))break;
        ++end;
        if(!selected.culled) {++proxies;triangles+=count;if(triangles>=triangleBudget)break;}
    }
    if(!submitPrepared(renderer,*d.object.asset,d.options,d.object.plan,d.object.instance,
        d.materials,d.selection,stats,error,d.preparedMaterials,cursor,end))return false;
    cursor=end;complete=end==d.selection->draws.size();return true;
}
bool GltfSceneCollectedView::drawWithMotion(IRenderBackend& renderer,std::size_t index,
    WorldMotionHistory* motion,GltfSceneDrawStats& stats,std::string* error)const {
    if(!data_ || index>=data_->draws.size()) {
        if(error)*error="Scene draw references an unknown collected request";return false;
    }
    const auto& d=data_->draws[index];auto options=d.options;options.motionHistory=motion;
    if(motion && (!motion->active() || !options.motionObject)) {
        if(error)*error="Planned scene motion requires an active view and owner";return false;
    }
    return submitPrepared(renderer,*d.object.asset,options,d.object.plan,d.object.instance,
        d.materials,d.selection,stats,error,d.preparedMaterials);
}

}  // namespace engine::render::gltf::scene

namespace engine::render::gltf::scene {
std::uint64_t GltfSceneViewFamily::shadowContentRevision(std::span<const std::size_t> objects,
    const std::array<float,16>& view) const {
    WorldShadowContent hash;
    for(auto index:objects) {
        const auto& object=impl_->objects.at(index);
        for(unsigned i=0;i<object.asset->primitives.size();++i) {
            const auto& p=object.asset->primitives[i];
            if(p.skinIndex>=0?gltfSceneBoundsOutsideView(object.instance->skinnedBounds.at(i),view):gltfScenePrimitiveOutsideView(p,gltfSceneMultiplyMatrices(view,object.instance->models.at(p.nodeIndex))))continue;
            hash.add(object.asset->generation);hash.add(object.instance->id);hash.add(i);
        }
    }
    return hash.value;
}
}
