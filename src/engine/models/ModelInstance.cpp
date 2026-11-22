#include "ModelInstance.h"
#include "ModelAttachments.h"
#include "ModelMaterials.h"
#include "ModelTracks.h"
#include "ModelTransforms.h"
#include "PhlosionModelObject.h"
#include "engine/render/ModelPoseSampler.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

namespace engine::models {
namespace {
bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
bool validFrame(const MeshData& mesh, const ModelFrame& frame) {
    if (frame.animationIndex < -1 || frame.animationIndex >= static_cast<int>(mesh.animations.size()) ||
        !std::isfinite(frame.animationTime) || !std::isfinite(frame.materialTime) ||
        frame.animationTime < 0.0f || frame.materialTime < 0.0f) return false;
    for (int i = 0; i < 16; ++i) if (!std::isfinite(glm::value_ptr(frame.modelMatrix)[i])) return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(frame.tint[i]) || frame.tint[i] < 0.0f || frame.tint[i] > (i == 3 ? 1.0f : 2.0f)) return false;
    return true;
}
ModelFrame resolvedFrame(const MeshData& mesh, ModelFrame frame) {
    if (frame.animationIndex >= 0) {
        const float duration = mesh.animations[frame.animationIndex].durationSec;
        frame.animationTime = duration > 0.0f
            ? (frame.looping ? std::fmod(frame.animationTime, duration) : std::min(frame.animationTime, duration))
            : 0.0f;
    }
    return frame;
}
std::size_t submeshCount(const MeshData& mesh) {
    std::size_t count = std::max({std::size_t(1), mesh.submeshBaseColors.size(),
        mesh.submeshBaseTextures.size(), mesh.submeshMaterialModes.size(), mesh.submeshMeshIndex.size()});
    for (auto index : mesh.triangleSubmesh) count = std::max(count, std::size_t(index) + 1);
    return count;
}
}

bool ModelProfile::validate(const MeshData& mesh, std::string& error) const {
    for (const auto mode : mesh.submeshMaterialModes) if (mode > 2u) {
        error = "Model requires a source material profile";
        return false;
    }
    error.clear();
    return true;
}
float ModelProfile::supportContactY(const MeshData& mesh) const { return mesh.boundsMin.y; }
void ModelProfile::samplePose(const MeshData& mesh, const ModelFrame& frame, PoseEval& pose) const {
    render::animation::samplePose({mesh.nodesDefault, mesh.nodeChildren, mesh.sceneRoots,
        mesh.skins, mesh.animations, mesh.nodeNames}, frame.animationTime, frame.animationIndex,
        pose.nodeLocals, pose.nodeGlobals, false, frame.looping);
    pose.hasScenePose = !mesh.nodesDefault.empty();
    pose.hasClipPose = frame.animationIndex >= 0;
}
std::vector<std::uint8_t> ModelProfile::preserveVertexColor(const MeshData&) const { return {}; }
std::vector<batches::WorldIndexedBatch> ModelProfile::materialTemplates(
    const MeshData& mesh, const std::string& key, const ModelFrame&) const {
    return buildMaterialTemplates(mesh, key, submeshCount(mesh));
}
void ModelProfile::prepareMaterial(const MeshData& mesh, const PoseEval&,
    const ModelFrame& frame, std::size_t submesh, const batches::WorldIndexedBatch& source,
    batches::WorldIndexedBatch& destination) const {
    if (!applyEmissionAnimation(source, destination, mesh, submesh,
            frame.animationIndex, frame.animationTime)) destination = source;
}
float ModelProfile::visibility(const MeshData& mesh, const ModelFrame& frame, std::size_t submesh) const {
    if (frame.animationIndex < 0 || std::size_t(frame.animationIndex) >= mesh.animationMeshVisibility.size() ||
        submesh >= mesh.submeshMeshIndex.size()) return 1.0f;
    const int meshIndex = mesh.submeshMeshIndex[submesh];
    if (meshIndex < 0 || std::size_t(meshIndex) >= mesh.meshIndexToNode.size()) return 1.0f;
    const int node = mesh.meshIndexToNode[meshIndex];
    for (const auto& track : mesh.animationMeshVisibility[frame.animationIndex])
        if (track.nodeIndex == node) return sampleVisibilityTrack(track, frame.animationTime);
    return 1.0f;
}

ModelInstance::ModelInstance(std::shared_ptr<const ModelProfile> profile)
    : profile_(profile ? std::move(profile) : std::make_shared<ModelProfile>()) {}

bool ModelInstance::load(const std::filesystem::path& path, std::string* error) {
    MeshData candidate;
    if (!loadModelObject(path.string(), candidate, error)) return false;
    return setMesh(std::move(candidate), error);
}
bool ModelInstance::setMesh(MeshData mesh, std::string* error) {
    std::string reason;
    if (!profile_->validate(mesh, reason)) { if (error) *error = reason; return false; }
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u ||
        std::any_of(mesh.indices.begin(), mesh.indices.end(), [&](auto i) { return i >= mesh.vertices.size(); }))
        return fail(error, "Invalid model geometry");
    static std::atomic<std::uint64_t> revision{0};
    const auto resourceRevision = ++revision;
    mesh.assetCacheIdentity += "#resource:" + std::to_string(resourceRevision);
    auto candidate = std::make_shared<const MeshData>(std::move(mesh));
    geometry::FastTexturedMeshTemplateCache geometry;
    const auto preserveColor = profile_->preserveVertexColor(*candidate);
    if (!geometry::ensureFastTexturedMeshTemplateCache(geometry, candidate.get(),
            geometry::buildSubmeshNodeFallback(*candidate), submeshCount(*candidate), true, preserveColor))
        return fail(error, "Model has no renderable geometry");
    clear();
    mesh_ = std::move(candidate);
    geometry_ = std::make_shared<const geometry::FastTexturedMeshTemplateCache>(std::move(geometry));
    motionRevision_ = resourceRevision;
    if (error) error->clear();
    return true;
}
void ModelInstance::clear() {
    frames_ = {};
    deformations_=std::make_shared<render::WorldDeformationSet>();
    materials_ = {};
    geometry_ = {};
    mesh_.reset();
}
bool ModelInstance::samplePose(const ModelFrame& input, PoseEval& out, std::string* error) const {
    if (!mesh_ || !validFrame(*mesh_, input)) return fail(error, "Invalid model pose request");
    PoseEval candidate;
    profile_->samplePose(*mesh_, resolvedFrame(*mesh_, input), candidate);
    if (candidate.nodeGlobals.size() != mesh_->nodesDefault.size())
        return fail(error, "Model profile returned an incomplete pose");
    out = std::move(candidate);
    if (error) error->clear();
    return true;
}
bool ModelInstance::sampleNode(const ModelFrame& frame, std::string_view name, glm::vec3& out,
    std::string* error) const {
    glm::mat4 transform;
    if (!sampleNodeTransform(frame,name,transform,error)) return false;
    out=glm::vec3(transform[3]);
    return true;
}
bool ModelInstance::sampleNodeTransform(const ModelFrame& frame,std::string_view name,glm::mat4& out,
    std::string* error) const {
    if (!mesh_) return fail(error, "No model loaded");
    auto found = std::find(mesh_->nodeNames.begin(), mesh_->nodeNames.end(), name);
    if (found == mesh_->nodeNames.end()) return fail(error, "Model node is missing");
    PoseEval pose;
    if (!samplePose(frame, pose, error)) return false;
    const auto index = static_cast<std::size_t>(found - mesh_->nodeNames.begin());
    if (index >= pose.nodeGlobals.size()) return fail(error, "Model node pose is missing");
    const auto result=frame.modelMatrix * pose.nodeGlobals[index];
    for(int i=0;i<16;++i) if(!std::isfinite(glm::value_ptr(result)[i]))
        return fail(error, "Non-finite model node pose");
    out = result;
    return true;
}

struct ModelCollectedFrame::Data {
    std::shared_ptr<const MeshData> mesh;
    std::shared_ptr<const geometry::FastTexturedMeshTemplateCache> geometry;
    std::shared_ptr<const std::vector<batches::WorldIndexedBatch>> materials;
    ModelFrame frame;
    PoseEval pose;
    std::vector<batches::WorldIndexedBatch> batches;
    std::shared_ptr<render::WorldDeformationSet> deformations;
};
bool ModelInstance::collect(const ModelFrame& input,ModelCollectedFrame& output,std::string* error) {
    if (!mesh_ || !validFrame(*mesh_, input)) {
        fail(error, "Invalid model render request"); return false;
    }
    const auto frame = resolvedFrame(*mesh_, input);
    PoseEval pose;
    if (!samplePose(frame, pose, error)) return false;
    const auto quality = static_cast<std::size_t>(std::clamp(frame.graphicsQuality, 0, 3)) * 2 + (frame.inking ? 1 : 0);
    if (!materials_[quality]) materials_[quality] = std::make_shared<const std::vector<batches::WorldIndexedBatch>>(
        profile_->materialTemplates(*mesh_, "__runtime_mesh_id__:" + mesh_->assetCacheIdentity, frame));
    auto owned=std::make_shared<ModelCollectedFrame::Data>();
    auto& candidate=*owned;
    candidate.mesh=mesh_;candidate.geometry=geometry_;candidate.frame=frame;
    candidate.pose=pose;candidate.deformations=deformations_;
    candidate.materials = materials_[quality];
    candidate.batches.reserve(geometry_->batches.size() + mesh_->animatedBillboards.size());
    transforms::Resolver resolver;
    resolver.initialize({.mesh = mesh_.get(), .nodeGlobals = &pose.nodeGlobals,
        .modelMatrix = frame.modelMatrix, .hasClipPose = pose.hasClipPose,
        .pairedSkinMatrices = frame.pairedSkinMatrices});
    std::uint64_t geometryPart = 0;
    for (const auto& geometry : geometry_->batches) {
        const auto motionPart = geometryPart++;
        const auto submesh = geometry.baseSubmeshIndex;
        if (submesh >= candidate.materials->size()) {
            fail(error, "Model profile omitted a material"); return false;
        }
        const float visibility = profile_->visibility(*mesh_, frame, submesh);
        if (visibility <= 0.0f) continue;
        batches::WorldIndexedBatch batch;
        profile_->prepareMaterial(*mesh_, pose, frame, submesh, (*candidate.materials)[submesh], batch);
        batch.geometryCacheKey = geometry.geometryCacheKey;
        batch.motionPart = motionPart;
        batch.motionRevision = motionRevision_;
        batch.temporalMaterial=frame.temporalMaterial;
        batch.vertexColorMulR = frame.tint.r;
        batch.vertexColorMulG = frame.tint.g;
        batch.vertexColorMulB = frame.tint.b;
        batch.vertexColorMulA = frame.tint.a * visibility;
        batch.sortDepth = glm::dot(frame.cameraPosition - glm::vec3(frame.modelMatrix[3]),
            frame.cameraPosition - glm::vec3(frame.modelMatrix[3]));
        const int node = geometry.triNodeIndex >= 0 ? geometry.triNodeIndex : geometry_->defaultSkinNodeIndex;
        batch.sharedVertices = geometry.gpuTemplateVertices.data();
        batch.sharedVertexCount = geometry.gpuTemplateVertices.size();
        batch.sharedIndices = geometry.indices.data();
        batch.sharedIndexCount = geometry.indices.size();
        if (resolver.configureGpuClipSkinningBatch(node,
                geometry.gpuJointPalette.empty() ? nullptr : &geometry.gpuJointPalette,
                batch.modelMatrix, batch.gpuSkinningMode, batch.skinMatrices, batch.skinMatrixCount)) {
            batch.gpuSkinning = 1u;
        } else if (!geometry.skinnedBatch) {
            const auto matrix = resolver.worldMatrixForNode(node);
            std::copy_n(glm::value_ptr(matrix), 16, batch.modelMatrix.begin());
        } else {
            batch.geometryCacheKey.clear();
            batch.cpuDeformedMotion = true;
            batch.sharedVertices = nullptr;
            batch.sharedVertexCount = 0;
            batch.vertices = geometry.gpuTemplateVertices;
            std::copy_n(glm::value_ptr(frame.modelMatrix), 16, batch.modelMatrix.begin());
            for (std::size_t i = 0; i < batch.vertices.size(); ++i) {
                const auto index = geometry.sourceVertexIndices[i];
                const auto sample = resolver.resolveModelVertexSurface(node, index, mesh_->vertices[index], true, true);
                auto& vertex = batch.vertices[i];
                vertex.x = sample.pos.x; vertex.y = sample.pos.y; vertex.z = sample.pos.z;
                vertex.nx = sample.normal.x; vertex.ny = sample.normal.y; vertex.nz = sample.normal.z;
                vertex.tx = sample.tangent.x; vertex.ty = sample.tangent.y;
                vertex.tz = sample.tangent.z; vertex.tw = sample.tangent.w;
            }
        }
        batch.unlit = batch.unlit || frame.unlit;
        if (frame.translucent) {
            batch.materialAlphaOverride = true;
            batch.alphaMode = 2; batch.blendMode = 0; batch.dualSourceBlendEnabled = 0;
            batch.depthTestEnabled = 1; batch.alphaWindowMin = 0; batch.alphaWindowMax = 1;
        }
        candidate.batches.push_back(std::move(batch));
    }
    for(auto& batch:candidate.batches)if(!batch.skinMatrices.empty()) {
        batch.deformation=deformations_->publish(batch.motionPart,batch.skinMatrices);
        batch.sharedSkinMatrices=batch.deformation->values().data();
        batch.skinMatrices.clear();
    }
    output.data_=std::move(owned);if(error)error->clear();return true;
}
bool ModelCollectedFrame::prepareView(const glm::vec3& position,const glm::vec3& up,
    const glm::vec3& facing,std::vector<batches::WorldIndexedBatch>& output,std::string* error) const {
    if(!data_)return fail(error,"No collected model frame");
    for(const auto& v:{position,up,facing})for(int i=0;i<3;++i)
        if(!std::isfinite(v[i]))return fail(error,"Invalid collected model view");
    const auto& d=*data_;auto batches=d.batches;
    const float depth=glm::dot(position-glm::vec3(d.frame.modelMatrix[3]),
        position-glm::vec3(d.frame.modelMatrix[3]));
    for(auto& batch:batches)batch.sortDepth=depth;
    appendAnimatedBillboards(*d.mesh,d.pose.nodeGlobals,d.frame.modelMatrix,d.frame.animationIndex,
        d.frame.animationTime,position,up,facing,d.frame.tint.a,batches);
    output=std::move(batches);
    for(std::size_t i=0;i<output.size();++i)if(auto& batch=output[i];!batch.skinMatrices.empty()) {
        batch.deformation=d.deformations->publish((1ull<<32)+i,batch.skinMatrices);
        batch.sharedSkinMatrices=batch.deformation->values().data();batch.skinMatrices.clear();
    }
    if(error)error->clear();return true;
}
const std::vector<batches::WorldIndexedBatch>* ModelInstance::prepare(const ModelFrame& input,
    std::size_t slot,std::string* error) {
    if(slot>=frames_.size()){fail(error,"Invalid model pose slot");return nullptr;}
    FrameStorage candidate;
    if(!collect(input,candidate.collection,error) ||
       !candidate.collection.prepareView(input.cameraPosition,input.cameraUp,input.cameraFacing,
           candidate.batches,error))return nullptr;
    frames_[slot]=std::move(candidate);
    return &frames_[slot].batches;
}
}
