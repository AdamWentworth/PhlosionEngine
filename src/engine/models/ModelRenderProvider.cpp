#include "ModelRenderProvider.h"
#include "WorldIndexedResources.h"
#include "ModelViewPreparation.h"
#include "engine/render/WorldRecordingWorkers.h"
#include "engine/core/Environment.h"
#include <chrono>
#include <iostream>
#include "engine/render/Camera3D.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace engine::models {
namespace {
bool traceViewPreparation() {
    static const bool enabled=engine::env::flagEnabled("PHLOSION_MODEL_VIEW_PREPARATION_LOG");
    return enabled;
}
struct ResourceAdmission {
    IWorldResourceScope* api=nullptr;
    ResourceAdmission(IRenderBackend& renderer,std::uint64_t id) {
        if(!id)return;
        api=dynamic_cast<IWorldResourceScope*>(&renderer);
        if(!api || !api->beginWorldResourceScope(id))
            throw std::runtime_error("Renderer rejected the native model resource scope");
        scope=id;
    }
    std::uint64_t scope=0;
    void requireReady()const {
        if(!api)return;
        render::WorldResourceScopeStats stats;
        if(!api->getWorldResourceScopeStats(scope,stats) || stats.status!=render::WorldResourceScopeStatus::Ready)
            throw std::runtime_error("Native model resource allocation failed: "+stats.error);
    }
    ~ResourceAdmission(){if(api)api->endWorldResourceScope();}
};
struct Resource {
    std::filesystem::path path;
    std::filesystem::file_time_type stamp;
    std::unique_ptr<ModelInstance> instance;
};
struct Provider {
    bool supportsPrewarm = false;
    bool supportsEnvironment = false;
    bool supportsMotion = false;
    std::shared_ptr<const ModelProfile> profile;
    ModelLighting lighting;
    batches::WorldIndexedResources nativeResources;
    std::unordered_map<std::string,Resource> resources;
    std::string error;
    struct Collected { std::uint64_t resourceRevision=0;ModelViewPreparation views;double prepareMs=0;std::string backend,error;bool translucent=false,uploadDeformations=false;IRenderBackend* renderer=nullptr; };
    std::unordered_map<std::uint64_t,Collected> collected;
    bool collectionActive=false,planning=false,prepared=false,consumed=false;
    std::unique_ptr<render::WorldRecordingWorkers> preparationWorkers;
    std::uint64_t preparationJoins=0;
    double preparationMs=0;
    ModelInstance* load(const char* id, const char* path) {
        if (!id || !*id || !path || !*path) { error="Model id and resource path are required"; return nullptr; }
        std::error_code ec;
        const auto resolved=std::filesystem::weakly_canonical(path,ec);
        if (ec) { error=ec.message(); return nullptr; }
        const auto stamp=std::filesystem::last_write_time(resolved,ec);
        if (ec) { error=ec.message(); return nullptr; }
        auto& resource=resources[id];
        if (resource.instance && resource.path==resolved && resource.stamp==stamp) return resource.instance.get();
        auto candidate=std::make_unique<ModelInstance>(profile);
        if (!candidate->load(resolved,&error)) return nullptr;
        resource={resolved,stamp,std::move(candidate)};
        return resource.instance.get();
    }
    template<class Request> bool resolveFrame(const ModelInstance& instance, const Request& request,
        ModelFrame& frame) {
        if (!std::isfinite(request.animationTime) || request.animationTime<0 ||
            !std::isfinite(request.yawRadians) || !std::isfinite(request.scale) || request.scale<=0 ||
            !std::isfinite(request.groundPlaneY) || !std::isfinite(request.groundClearance) ||
            !std::all_of(std::begin(request.position),std::end(request.position),[](float f){return std::isfinite(f);})) {
            error="Invalid model transform or animation time"; return false;
        }
        const auto& mesh=*instance.mesh();
        if (request.animation && *request.animation) {
            const auto clip=std::find_if(mesh.animations.begin(),mesh.animations.end(),[&](const auto& a){
                return a.name==request.animation && a.durationSec>0;
            });
            if (clip==mesh.animations.end()) { error="Missing model clip: "+std::string(request.animation); return false; }
            frame.animationIndex=static_cast<int>(clip-mesh.animations.begin());
        }
        frame.animationTime=request.animationTime; frame.materialTime=request.animationTime;
        frame.looping=request.looping;
        const float scale=std::max(.01f,mesh.modelScaleFactor)*request.scale;
        auto position=glm::make_vec3(request.position);
        if (request.groundPlaneEnabled) position.y+=std::max(0.0f,
            request.groundPlaneY+request.groundClearance-(position.y+profile->supportContactY(mesh)*scale));
        frame.modelMatrix=glm::translate(glm::mat4(1),position)*
            glm::rotate(glm::mat4(1),request.yawRadians,glm::vec3(0,1,0))*
            glm::scale(glm::mat4(1),glm::vec3(scale));
        return true;
    }
};
bool renderModel(void* context,const render::ModelRenderFrame* request) noexcept {
    if (!context) return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if (!request || !request->renderer || request->width<=0 || request->height<=0 || request->poseSlot>=16) {
            provider.error="Incomplete model render frame"; return false;
        }
        const auto finiteVector=[](const float (&v)[3]) {
            return std::all_of(std::begin(v),std::end(v),[](float f){return std::isfinite(f);});
        };
        if (!finiteVector(request->cameraPosition) || !finiteVector(request->cameraTarget) ||
            !finiteVector(request->cameraUp) || !std::isfinite(request->verticalFovDegrees) ||
            request->verticalFovDegrees<=0 || request->verticalFovDegrees>=180 ||
            !std::isfinite(request->nearPlane) || !std::isfinite(request->farPlane) ||
            request->nearPlane<=0 || request->farPlane<=request->nearPlane ||
            !std::isfinite(request->projectionOffset[0]) || !std::isfinite(request->projectionOffset[1]) ||
            glm::length(glm::cross(glm::make_vec3(request->cameraTarget)-glm::make_vec3(request->cameraPosition),
                glm::make_vec3(request->cameraUp)))<1e-6f) {
            provider.error="Invalid model camera"; return false;
        }
        auto* instance=provider.load(request->assetId,request->phloPath);
        ModelFrame frame;
        if (!instance || !provider.resolveFrame(*instance,*request,frame)) return false;
        Camera3D camera(request->verticalFovDegrees,float(request->width)/float(request->height),
            request->nearPlane,request->farPlane);
        camera.setPosition(glm::make_vec3(request->cameraPosition));
        camera.lookAt(glm::make_vec3(request->cameraTarget),glm::make_vec3(request->cameraUp));
        const auto view=camera.getViewMatrix();
        frame.cameraPosition=camera.getPosition();
        frame.cameraUp=glm::vec3(glm::inverse(view)[1]); frame.cameraFacing=glm::vec3(glm::inverse(view)[2]);
        frame.tint=glm::make_vec4(request->tint); frame.translucent=request->translucent; frame.unlit=request->unlit;
        frame.temporalMaterial=request->temporalMaterial;
        const std::string backend=request->renderer->backendId();
        frame.pairedSkinMatrices=backend=="opengl" || backend=="d3d12";
        const auto* prepared=instance->prepare(frame,request->poseSlot,&provider.error);
        if (!prepared) return false;
        auto batches=*prepared;
        if (provider.supportsEnvironment)
            for (auto& batch : batches) batch.environment = request->environment;
        if (provider.lighting.prepare && !provider.lighting.prepare(batches,request->lightingPreset,provider.error)) return false;
        if(!request->prewarmOnly) {
            std::vector<render::WorldSkinPalette> palettes;
            for(const auto& batch:batches)if(batch.gpuSkinning && batch.skinMatrixCount) {
                const auto count=std::size_t(batch.skinMatrixCount)*(batch.gpuSkinningMode?32u:16u);
                const float* data=batch.sharedSkinMatrices?batch.sharedSkinMatrices:batch.skinMatrices.data();
                if(!batch.sharedSkinMatrices && count>batch.skinMatrices.size())
                    throw std::logic_error("Incomplete deformation palette");
                palettes.emplace_back(data,count);
            }
            render::prepareWorldDeformations(*request->renderer,palettes);
        }
        const auto shift=glm::translate(glm::mat4(1),
            glm::vec3(request->projectionOffset[0],request->projectionOffset[1],0));
        const auto matrix=shift*camera.getProjectionMatrix()*view;
        const auto forward=camera.getDirection()*(provider.lighting.forwardScale ?
            provider.lighting.forwardScale(request->lightingPreset) : 1.0f);
        ResourceAdmission admission(*request->renderer,request->resourceScope);
        provider.nativeResources.bind(request->resourceScope ?
            dynamic_cast<render::IWorldPreparedResources*>(request->renderer) : nullptr,request->resourceScope);
        if (provider.supportsPrewarm && request->prewarmOnly)
            batches::prewarmWorldIndexedBatches(*request->renderer,batches,
                glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()));
        else batches::submitWorldIndexedBatches(*request->renderer,batches,glm::value_ptr(matrix),request->width,request->height,
            glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()),
            provider.supportsMotion && !request->translucent ? request->motionHistory : nullptr,
            provider.supportsMotion ? request->motionObject : 0,
            request->resourceScope ? &provider.nativeResources : nullptr);
        admission.requireReady();
        provider.error.clear(); return true;
    } catch (const std::exception& e) { provider.error=e.what(); return false; }
      catch (...) { provider.error="Unknown model rendering error"; return false; }
}
bool beginCollection(void* context) noexcept {
    if(!context)return false;
    auto& p=*static_cast<Provider*>(context);
    if(p.collectionActive){p.error="Model collection already active";return false;}
    p.collected.clear();p.collectionActive=true;p.planning=false;p.prepared=false;p.consumed=false;
    p.preparationJoins=0;p.preparationMs=0;p.error.clear();return true;
}
void endCollection(void* context) noexcept {
    if(!context)return;
    auto& p=*static_cast<Provider*>(context);
    if(traceViewPreparation() && p.collectionActive) {
        ModelViewPreparation::Stats total;ModelViewPreparation::SubmissionStats submission;double prepareMs=0;
        for(const auto& [id,entry]:p.collected) {
            prepareMs+=entry.prepareMs;
            const auto plans=entry.views.submissionStats();
            submission.requests+=plans.requests;submission.plans+=plans.plans;submission.batches+=plans.batches;
            total.requests+=entry.views.stats.requests;
            total.builds+=entry.views.stats.builds;
            total.reuses+=entry.views.stats.reuses;
        }
        std::cout<<"[ModelViewPreparation] objects="<<p.collected.size()
            <<" requests="<<total.requests<<" builds="<<total.builds<<" reuses="<<total.reuses<<" cpu_ms="<<prepareMs
            <<" submission_requests="<<submission.requests<<" submission_plans="<<submission.plans
            <<" lowered_batches="<<submission.batches<<" preparation_joins="<<p.preparationJoins
            <<" preparation_ms="<<p.preparationMs<<" preplanned="<<p.planning<<" ready="<<p.prepared<<'\n';
    }
    p.collected.clear();p.collectionActive=false;
}
bool collectModel(void* context,const render::ModelRenderFrame* request,std::uint64_t* output) noexcept {
    if(!context)return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if (!request || !request->renderer || request->width<=0 || request->height<=0 || request->poseSlot>=16) {
            provider.error="Incomplete model render frame"; return false;
        }
        const auto finiteVector=[](const float (&v)[3]) {
            return std::all_of(std::begin(v),std::end(v),[](float f){return std::isfinite(f);});
        };
        if (!finiteVector(request->cameraPosition) || !finiteVector(request->cameraTarget) ||
            !finiteVector(request->cameraUp) || !std::isfinite(request->verticalFovDegrees) ||
            request->verticalFovDegrees<=0 || request->verticalFovDegrees>=180 ||
            !std::isfinite(request->nearPlane) || !std::isfinite(request->farPlane) ||
            request->nearPlane<=0 || request->farPlane<=request->nearPlane ||
            !std::isfinite(request->projectionOffset[0]) || !std::isfinite(request->projectionOffset[1]) ||
            glm::length(glm::cross(glm::make_vec3(request->cameraTarget)-glm::make_vec3(request->cameraPosition),
                glm::make_vec3(request->cameraUp)))<1e-6f) {
            provider.error="Invalid model camera"; return false;
        }
        if(!provider.collectionActive || provider.planning || !output || provider.collected.size()>=256) {
            provider.error="Model collection is inactive, full or missing its output";return false;
        }
        auto* instance=provider.load(request->assetId,request->phloPath);
        ModelFrame frame;
        if (!instance || !provider.resolveFrame(*instance,*request,frame)) return false;
        Camera3D camera(request->verticalFovDegrees,float(request->width)/float(request->height),
            request->nearPlane,request->farPlane);
        camera.setPosition(glm::make_vec3(request->cameraPosition));
        camera.lookAt(glm::make_vec3(request->cameraTarget),glm::make_vec3(request->cameraUp));
        const auto view=camera.getViewMatrix();
        frame.cameraPosition=camera.getPosition();
        frame.cameraUp=glm::vec3(glm::inverse(view)[1]); frame.cameraFacing=glm::vec3(glm::inverse(view)[2]);
        frame.tint=glm::make_vec4(request->tint); frame.translucent=request->translucent; frame.unlit=request->unlit;
        frame.temporalMaterial=request->temporalMaterial;
        const std::string backend=request->renderer->backendId();
        frame.pairedSkinMatrices=backend=="opengl" || backend=="d3d12";
        Provider::Collected collected;collected.backend=backend;collected.translucent=frame.translucent;collected.renderer=request->renderer;
        ModelCollectedFrame snapshot;
        if(!instance->collect(frame,snapshot,&provider.error))return false;
        collected.resourceRevision=instance->resourceRevision();
        collected.views=ModelViewPreparation(std::move(snapshot));
        static std::atomic<std::uint64_t> next{0};
        const auto id=++next;
        if(!id){provider.error="Model collection handle exhausted";return false;}
        provider.collected.emplace(id,std::move(collected));*output=id;
        provider.error.clear();return true;
    } catch(const std::exception& e){provider.error=e.what();return false;}
      catch(...){provider.error="Unknown model collection error";return false;}
}
bool planCollected(void* context,std::uint64_t handle,const render::ModelRenderFrame* request) noexcept {
    if(!context)return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if(provider.prepared || provider.consumed){provider.error="Model view planning is already closed";return false;}
        if (!request || !request->renderer || request->width<=0 || request->height<=0 || request->poseSlot>=16) {
            provider.error="Incomplete model render frame"; return false;
        }
        const auto finiteVector=[](const float (&v)[3]) {
            return std::all_of(std::begin(v),std::end(v),[](float f){return std::isfinite(f);});
        };
        if (!finiteVector(request->cameraPosition) || !finiteVector(request->cameraTarget) ||
            !finiteVector(request->cameraUp) || !std::isfinite(request->verticalFovDegrees) ||
            request->verticalFovDegrees<=0 || request->verticalFovDegrees>=180 ||
            !std::isfinite(request->nearPlane) || !std::isfinite(request->farPlane) ||
            request->nearPlane<=0 || request->farPlane<=request->nearPlane ||
            !std::isfinite(request->projectionOffset[0]) || !std::isfinite(request->projectionOffset[1]) ||
            glm::length(glm::cross(glm::make_vec3(request->cameraTarget)-glm::make_vec3(request->cameraPosition),
                glm::make_vec3(request->cameraUp)))<1e-6f) {
            provider.error="Invalid model camera"; return false;
        }
        const auto found=provider.collected.find(handle);
        if(!provider.collectionActive || found==provider.collected.end() ||
            found->second.backend!=request->renderer->backendId() || found->second.renderer!=request->renderer) {
            provider.error="Invalid, expired or incompatible collected model";return false;
        }
        auto& collected=found->second;
        ModelFrame frame;
        Camera3D camera(request->verticalFovDegrees,float(request->width)/float(request->height),
            request->nearPlane,request->farPlane);
        camera.setPosition(glm::make_vec3(request->cameraPosition));
        camera.lookAt(glm::make_vec3(request->cameraTarget),glm::make_vec3(request->cameraUp));
        const auto view=camera.getViewMatrix();
        frame.cameraPosition=camera.getPosition();
        frame.cameraUp=glm::vec3(glm::inverse(view)[1]); frame.cameraFacing=glm::vec3(glm::inverse(view)[2]);
        ModelViewPreparation::Request planned{frame.cameraPosition,frame.cameraUp,frame.cameraFacing,
            request->environment,request->lightingPreset,request->renderer->supportsWorldIndexedMeshInstancing()};
        if(!collected.views.plan(planned,provider.error))return false;
        collected.uploadDeformations|=!request->prewarmOnly;
        provider.planning=true;provider.error.clear();return true;
    } catch(const std::exception& e){provider.error=e.what();return false;}
      catch(...){provider.error="Unknown model view planning error";return false;}
}
bool prepareCollected(void* context) noexcept {
    if(!context)return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if(!provider.collectionActive){provider.error="No active model collection";return false;}
        if(provider.prepared){provider.error.clear();return true;}
        std::vector<std::pair<std::uint64_t,Provider::Collected*>> jobs;
        for(auto& [id,entry]:provider.collected)if(entry.views.plannedCount())jobs.emplace_back(id,&entry);
        std::sort(jobs.begin(),jobs.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        const auto start=std::chrono::steady_clock::now();
        const auto prepare=[&](std::size_t index) {
            auto& object=*jobs[index].second;
            object.error.clear();
            const auto began=std::chrono::steady_clock::now();
            if(!object.views.preparePlanned(provider.lighting,object.error) && object.error.empty())
                object.error="Native model material preparation failed";
            object.prepareMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        };
        if(jobs.size()>1) {
            if(!provider.preparationWorkers)provider.preparationWorkers=std::make_unique<render::WorldRecordingWorkers>();
            // One job owns all mutable views of an object. No native API calls.
            provider.preparationWorkers->run([&](unsigned lane) {
                for(std::size_t i=lane;i<jobs.size();i+=render::WorldRecordingWorkers::count)prepare(i);
            });
            ++provider.preparationJoins;
        } else if(!jobs.empty())prepare(0);
        provider.preparationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        for(const auto& job:jobs)if(!job.second->error.empty()){provider.error=job.second->error;return false;}
        std::vector<std::pair<IRenderBackend*,std::vector<render::WorldSkinPalette>>> uploads;
        for(const auto& [id,object]:jobs) {
            if(!object->uploadDeformations)continue;
            auto it=std::find_if(uploads.begin(),uploads.end(),[&](const auto& p){return p.first==object->renderer;});
            if(it==uploads.end()){uploads.emplace_back(object->renderer,std::vector<render::WorldSkinPalette>{});it=uploads.end()-1;}
            object->views.appendDeformations(it->second);
        }
        for(const auto& [renderer,palettes]:uploads)render::prepareWorldDeformations(*renderer,palettes);
        provider.prepared=true;provider.planning=true;provider.error.clear();return true;
    } catch(const std::exception& e){provider.error=e.what();return false;}
      catch(...){provider.error="Unknown model view preparation error";return false;}
}
bool renderCollected(void* context,std::uint64_t handle,const render::ModelRenderFrame* request) noexcept {
    if (!context) return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if(provider.planning && !provider.prepared){provider.error="Native model preparation has not joined";return false;}
        if (!request || !request->renderer || request->width<=0 || request->height<=0 || request->poseSlot>=16) {
            provider.error="Incomplete model render frame"; return false;
        }
        const auto finiteVector=[](const float (&v)[3]) {
            return std::all_of(std::begin(v),std::end(v),[](float f){return std::isfinite(f);});
        };
        if (!finiteVector(request->cameraPosition) || !finiteVector(request->cameraTarget) ||
            !finiteVector(request->cameraUp) || !std::isfinite(request->verticalFovDegrees) ||
            request->verticalFovDegrees<=0 || request->verticalFovDegrees>=180 ||
            !std::isfinite(request->nearPlane) || !std::isfinite(request->farPlane) ||
            request->nearPlane<=0 || request->farPlane<=request->nearPlane ||
            !std::isfinite(request->projectionOffset[0]) || !std::isfinite(request->projectionOffset[1]) ||
            glm::length(glm::cross(glm::make_vec3(request->cameraTarget)-glm::make_vec3(request->cameraPosition),
                glm::make_vec3(request->cameraUp)))<1e-6f) {
            provider.error="Invalid model camera"; return false;
        }
        const auto found=provider.collected.find(handle);
        if(!provider.collectionActive || found==provider.collected.end() ||
            found->second.backend!=request->renderer->backendId() || found->second.renderer!=request->renderer) {
            provider.error="Invalid, expired or incompatible collected model";return false;
        }
        auto& collected=found->second;
        ModelFrame frame;
        Camera3D camera(request->verticalFovDegrees,float(request->width)/float(request->height),
            request->nearPlane,request->farPlane);
        camera.setPosition(glm::make_vec3(request->cameraPosition));
        camera.lookAt(glm::make_vec3(request->cameraTarget),glm::make_vec3(request->cameraUp));
        const auto view=camera.getViewMatrix();
        frame.cameraPosition=camera.getPosition();
        frame.cameraUp=glm::vec3(glm::inverse(view)[1]); frame.cameraFacing=glm::vec3(glm::inverse(view)[2]);
        const bool trace=traceViewPreparation();
        const auto started=trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto* prepared=collected.views.resolve(frame.cameraPosition,frame.cameraUp,
            frame.cameraFacing,request->environment,provider.lighting,request->lightingPreset,provider.error,!provider.planning);
        if(trace)collected.prepareMs+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        if(!prepared)return false;
        const auto& batches=prepared->batches;
        if(!request->prewarmOnly && !provider.planning) {
            std::vector<render::WorldSkinPalette> palettes;
            for(const auto& batch:batches)if(batch.gpuSkinning && batch.skinMatrixCount) {
                const auto count=std::size_t(batch.skinMatrixCount)*(batch.gpuSkinningMode?32u:16u);
                const float* data=batch.sharedSkinMatrices?batch.sharedSkinMatrices:batch.skinMatrices.data();
                if(!batch.sharedSkinMatrices && count>batch.skinMatrices.size())
                    throw std::logic_error("Incomplete deformation palette");
                palettes.emplace_back(data,count);
            }
            render::prepareWorldDeformations(*request->renderer,palettes);
        }
        const auto shift=glm::translate(glm::mat4(1),
            glm::vec3(request->projectionOffset[0],request->projectionOffset[1],0));
        const auto matrix=shift*camera.getProjectionMatrix()*view;
        const auto forward=camera.getDirection()*(provider.lighting.forwardScale ?
            provider.lighting.forwardScale(request->lightingPreset) : 1.0f);
        ResourceAdmission admission(*request->renderer,request->resourceScope);
        provider.nativeResources.bind(request->resourceScope ?
            dynamic_cast<render::IWorldPreparedResources*>(request->renderer) : nullptr,request->resourceScope);
        if (provider.supportsPrewarm && request->prewarmOnly)
            batches::prewarmWorldIndexedBatches(*request->renderer,batches,
                glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()));
        else {
            auto* motion=provider.supportsMotion && !collected.translucent ? request->motionHistory : nullptr;
            prepared->submission(*request->renderer,motion==nullptr,provider.planning).submit(*request->renderer,
                glm::value_ptr(matrix),request->width,request->height,
                glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()),
                motion,provider.supportsMotion ? request->motionObject : 0,
                request->resourceScope ? &provider.nativeResources : nullptr);
        }
        admission.requireReady();
        provider.consumed=true;provider.error.clear(); return true;
    } catch (const std::exception& e) { provider.error=e.what(); return false; }
      catch (...) { provider.error="Unknown model rendering error"; return false; }
}
bool sampleNode(void* context,const render::ModelNodePoseRequest* request,render::ModelNodePoseResult* result) noexcept {
    if (!context) return false;
    auto& provider=*static_cast<Provider*>(context);
    try {
        if (!request || !request->nodeName || !result) { provider.error="Incomplete model node request"; return false; }
        auto* instance=provider.load(request->assetId,request->phloPath);
        ModelFrame frame;
        if (!instance || !provider.resolveFrame(*instance,*request,frame)) return false;
        glm::mat4 transform;
        if (!instance->sampleNodeTransform(frame,request->nodeName,transform,&provider.error)) return false;
        render::ModelNodePoseResult sampled;
        std::copy_n(glm::value_ptr(transform[3]),3,sampled.worldPosition);
        std::copy_n(glm::value_ptr(transform),16,sampled.worldTransform);
        *result=sampled;
        provider.error.clear();
        return true;
    } catch (const std::exception& e) { provider.error=e.what(); return false; }
      catch (...) { provider.error="Unknown model pose error"; return false; }
}
std::uint64_t collectedResourceRevision(void* context,std::uint64_t handle) noexcept {
    if(!context)return 0;
    const auto& provider=*static_cast<Provider*>(context);
    if(!provider.collectionActive)return 0;
    const auto found=provider.collected.find(handle);
    return found==provider.collected.end()?0:found->second.resourceRevision;
}
const char* lastError(void* context) noexcept { return context ? static_cast<Provider*>(context)->error.c_str() : "No model provider"; }
void destroy(void* context) noexcept { delete static_cast<Provider*>(context); }
}
bool createModelRenderProvider(std::uint32_t abi,render::ModelRenderPluginApi* out,
    std::shared_ptr<const ModelProfile> profile,ModelLighting lighting) noexcept {
    // The frame prefix alone cannot make an older renderer vtable compatible.
    if (!out || abi!=render::modelRenderPluginAbi) return false;
    try {
        auto provider=std::make_unique<Provider>();
        provider->supportsPrewarm=abi>=7;
        provider->supportsEnvironment=abi>=19;
        provider->supportsMotion=abi==render::modelRenderPluginAbi;
        provider->profile=profile?std::move(profile):std::make_shared<ModelProfile>();
        provider->lighting=std::move(lighting);
        *out={abi,sizeof(render::ModelRenderPluginApi),provider.release(),&renderModel,&lastError,&destroy,&sampleNode,
            &beginCollection,&collectModel,&renderCollected,&endCollection,&planCollected,&prepareCollected,&collectedResourceRevision};
        return true;
    } catch (...) { return false; }
}
}
