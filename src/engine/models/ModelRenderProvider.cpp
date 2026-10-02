#include "ModelRenderProvider.h"
#include "engine/render/Camera3D.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace engine::models {
namespace {
struct Resource {
    std::filesystem::path path;
    std::filesystem::file_time_type stamp;
    std::unique_ptr<ModelInstance> instance;
};
struct Provider {
    std::shared_ptr<const ModelProfile> profile;
    ModelLighting lighting;
    std::unordered_map<std::string,Resource> resources;
    std::string error;
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
        const std::string backend=request->renderer->backendId();
        frame.pairedSkinMatrices=backend=="opengl" || backend=="d3d12";
        const auto* prepared=instance->prepare(frame,request->poseSlot,&provider.error);
        if (!prepared) return false;
        auto batches=*prepared;
        if (provider.lighting.prepare && !provider.lighting.prepare(batches,request->lightingPreset,provider.error)) return false;
        const auto matrix=camera.getProjectionMatrix()*view;
        const auto forward=camera.getDirection()*(provider.lighting.forwardScale ?
            provider.lighting.forwardScale(request->lightingPreset) : 1.0f);
        batches::submitWorldIndexedBatches(*request->renderer,batches,glm::value_ptr(matrix),request->width,request->height,
            glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()));
        provider.error.clear(); return true;
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
        glm::vec3 position;
        if (!instance->sampleNode(frame,request->nodeName,position,&provider.error)) return false;
        std::copy_n(glm::value_ptr(position),3,result->worldPosition);
        provider.error.clear();
        return true;
    } catch (const std::exception& e) { provider.error=e.what(); return false; }
      catch (...) { provider.error="Unknown model pose error"; return false; }
}
const char* lastError(void* context) noexcept { return context ? static_cast<Provider*>(context)->error.c_str() : "No model provider"; }
void destroy(void* context) noexcept { delete static_cast<Provider*>(context); }
}
bool createModelRenderProvider(std::uint32_t abi,render::ModelRenderPluginApi* out,
    std::shared_ptr<const ModelProfile> profile,ModelLighting lighting) noexcept {
    if (!out || abi!=render::modelRenderPluginAbi) return false;
    try {
        auto provider=std::make_unique<Provider>();
        provider->profile=profile?std::move(profile):std::make_shared<ModelProfile>();
        provider->lighting=std::move(lighting);
        *out={abi,sizeof(render::ModelRenderPluginApi),provider.release(),&renderModel,&lastError,&destroy,&sampleNode};
        return true;
    } catch (...) { return false; }
}
}
