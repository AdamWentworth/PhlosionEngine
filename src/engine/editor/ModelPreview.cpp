#include "ModelPreview.h"
#include "engine/models/PhlosionModelObject.h"
#include "engine/render/Camera3D.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace engine::editor {
namespace {
using models::MeshData;
using CachedTexture = models::CachedTextureRgba;
struct PreviewLines {
    glm::mat4 view, projection;
    int width, height;
    std::vector<IRenderBackend::DebugLine> lines;
    void appendProjectedLine(glm::vec3 a, glm::vec3 b,
        float r, float g, float blue, float alpha, float thickness) {
        const glm::vec4 viewport(0,0,width,height);
        a = glm::project(a, view, projection, viewport);
        b = glm::project(b, view, projection, viewport);
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
            !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) return;
        if ((a.z < 0 || a.z > 1) && (b.z < 0 || b.z > 1)) return;
        IRenderBackend::DebugLine line;
        line.x1=a.x; line.y1=float(height)-a.y; line.x2=b.x; line.y2=float(height)-b.y;
        line.r=r; line.g=g; line.b=blue; line.a=alpha; line.thickness=thickness;
        lines.push_back(line);
    }
};
class ScopedWorldMaterialDebugView {
public:
    ScopedWorldMaterialDebugView(
        IRenderBackend& renderer,
        int view)
        : renderer_(renderer),
          previous_(renderer.worldMaterialDebugView()) {
        renderer_.setWorldMaterialDebugView(view);
    }

    ~ScopedWorldMaterialDebugView() {
        renderer_.setWorldMaterialDebugView(previous_);
    }

private:
    IRenderBackend& renderer_;
    int previous_ = 0;
};

void clearTexturePixels(
    std::vector<CachedTexture>& textures) {
    for (auto& texture : textures) {
        texture.rgba.clear();
        texture.rgba.shrink_to_fit();
    }
}

void removeTextures(MeshData& mesh) {
    clearTexturePixels(mesh.submeshBaseTextures);
    clearTexturePixels(mesh.submeshNormalTextures);
    clearTexturePixels(
        mesh.submeshMetallicRoughnessTextures);
    clearTexturePixels(mesh.submeshOcclusionTextures);
    clearTexturePixels(mesh.submeshEmissiveTextures);
    clearTexturePixels(mesh.submeshEnvironmentTextures);
}

void makeGeometryOnly(MeshData& mesh) {
    removeTextures(mesh);
    const glm::vec4 neutral(0.20f, 0.24f, 0.30f, 1.0f);
    std::fill(
        mesh.submeshBaseColors.begin(),
        mesh.submeshBaseColors.end(),
        neutral);
    std::fill(
        mesh.triangleBaseColors.begin(),
        mesh.triangleBaseColors.end(),
        glm::vec3(neutral));
    std::fill(
        mesh.vertexBaseColors.begin(),
        mesh.vertexBaseColors.end(),
        glm::vec3(neutral));
    for (auto& vertex : mesh.vertices) {
        vertex.color = neutral;
    }
    std::fill(
        mesh.submeshEmissiveFactors.begin(),
        mesh.submeshEmissiveFactors.end(),
        glm::vec3(0.0f));
    std::fill(
        mesh.submeshMetallicFactor.begin(),
        mesh.submeshMetallicFactor.end(),
        0.0f);
    std::fill(
        mesh.submeshRoughnessFactor.begin(),
        mesh.submeshRoughnessFactor.end(),
        0.92f);
    // The fast indexed path reads its neutral preview tint from authored
    // vertex color, so keep that channel enabled for geometry-only mode.
    mesh.hasVertexColor = true;
    mesh.hasVertexBaseColor = false;
}

std::size_t textureCount(const MeshData& mesh) {
    const auto countSet =
        [](const std::vector<CachedTexture>& textures) {
            return static_cast<std::size_t>(
                std::count_if(
                    textures.begin(),
                    textures.end(),
                    [](const CachedTexture& texture) {
                        return texture.hasPixels();
                    }));
        };
    return countSet(mesh.submeshBaseTextures) +
           countSet(mesh.submeshNormalTextures) +
           countSet(
               mesh.submeshMetallicRoughnessTextures) +
           countSet(mesh.submeshOcclusionTextures) +
           countSet(mesh.submeshEmissiveTextures) +
           countSet(mesh.submeshEnvironmentTextures);
}

std::size_t boneCount(const MeshData& mesh) {
    std::unordered_set<int> joints;
    for (const auto& skin : mesh.skins) {
        for (const int joint : skin.joints) {
            if (joint >= 0) {
                joints.insert(joint);
            }
        }
    }
    return joints.size();
}

} // namespace
struct ModelPreview::Impl {
    std::shared_ptr<const models::ModelProfile> profile;
    ModelPreviewLighting lighting;
    models::ModelInstance textured, material, geometry;
    std::string assetId, assetPath, status;
    EditorProjectAssetPreviewOptions options;
    float animationTime = 0, materialTime = 0, boundsRadius = 1, boundsCenterY = .5f;
    std::size_t materialCount = 0, cookedTextureCount = 0, cookedBoneCount = 0;
    bool ready = false;
    explicit Impl(std::shared_ptr<const models::ModelProfile> p, ModelPreviewLighting l)
        : profile(p ? std::move(p) : std::make_shared<models::ModelProfile>()),
          lighting(std::move(l)), textured(profile), material(profile), geometry(profile) {}
    const MeshData& texturedMesh() const {
        static const MeshData empty;
        return textured.mesh() ? *textured.mesh() : empty;
    }
    models::ModelInstance& displayInstance() {
        return !options.showMaterials ? geometry : (!options.showTextures ? material : textured);
    }
    glm::mat4 modelMatrix() const {
        const auto& mesh = texturedMesh();
        const float scale = std::max(.01f, mesh.modelScaleFactor);
        const float y = std::max(0.0f, .0025f - profile->supportContactY(mesh)*scale);
        return glm::translate(glm::mat4(1),glm::vec3(0,y,0)) * glm::scale(glm::mat4(1),glm::vec3(scale));
    }
    int resolvedAnimationIndex() const {
        if (options.animationIndex < 0 ||
            static_cast<std::size_t>(
                options.animationIndex) >=
                texturedMesh().animations.size()) {
            return -1;
        }
        return options.animationIndex;
    }

    void appendGrid(
        PreviewLines& debug) const {
        const float extent =
            std::max(1.25f, boundsRadius * 1.55f);
        constexpr int divisions = 8;
        for (int index = -divisions;
             index <= divisions;
             ++index) {
            const float coordinate =
                extent *
                static_cast<float>(index) /
                static_cast<float>(divisions);
            const bool axis = index == 0;
            const float alpha = axis ? 0.52f : 0.18f;
            debug.appendProjectedLine(
                glm::vec3(-extent, 0.0f, coordinate),
                glm::vec3(extent, 0.0f, coordinate),
                axis ? 0.28f : 0.42f,
                axis ? 0.78f : 0.48f,
                axis ? 0.58f : 0.52f,
                alpha,
                axis ? 1.5f : 1.0f);
            debug.appendProjectedLine(
                glm::vec3(coordinate, 0.0f, -extent),
                glm::vec3(coordinate, 0.0f, extent),
                axis ? 0.28f : 0.42f,
                axis ? 0.58f : 0.48f,
                axis ? 0.90f : 0.52f,
                alpha,
                axis ? 1.5f : 1.0f);
        }
    }

    void appendSkeleton(
        const models::PoseEval& pose,
        PreviewLines& debug) const {
        if (!options.showSkeleton ||
            pose.nodeGlobals.empty()) {
            return;
        }
        std::unordered_set<int> joints;
        for (const auto& skin : texturedMesh().skins) {
            for (const int joint : skin.joints) {
                if (joint >= 0) {
                    joints.insert(joint);
                }
            }
        }
        const glm::mat4 model =
            modelMatrix();
        for (const int joint : joints) {
            if (joint < 0 ||
                static_cast<std::size_t>(joint) >=
                    pose.nodeGlobals.size() ||
                static_cast<std::size_t>(joint) >=
                    texturedMesh().nodeParent.size()) {
                continue;
            }
            const int parent =
                texturedMesh().nodeParent[
                    static_cast<std::size_t>(joint)];
            if (parent < 0 ||
                !joints.contains(parent) ||
                static_cast<std::size_t>(parent) >=
                    pose.nodeGlobals.size()) {
                continue;
            }
            const glm::vec3 child =
                glm::vec3(
                    model *
                    pose.nodeGlobals[
                        static_cast<std::size_t>(joint)] *
                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const glm::vec3 parentPosition =
                glm::vec3(
                    model *
                    pose.nodeGlobals[
                        static_cast<std::size_t>(parent)] *
                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            debug.appendProjectedLine(
                parentPosition,
                child,
                0.22f,
                0.95f,
                0.58f,
                0.95f,
                2.0f);
        }
    }

    void appendWireframe(
        const MeshData& mesh,
        const models::PoseEval& pose,
        const models::ModelFrame& frame,
        PreviewLines& debug) const {
        if (!options.showWireframe ||
            mesh.indices.empty()) {
            return;
        }

        const std::vector<glm::mat4>& nodeGlobals =
            pose.nodeGlobals.empty()
                ? mesh.bindNodeGlobals
                : pose.nodeGlobals;
        const glm::mat4 model =
            modelMatrix();
        const auto resolveTriangleNode =
            [&mesh](std::size_t triangleIndex) {
                if (triangleIndex <
                    mesh.triangleNodeIndex.size()) {
                    const int node =
                        mesh.triangleNodeIndex[
                            triangleIndex];
                    if (node >= 0) {
                        return node;
                    }
                }
                if (triangleIndex >=
                    mesh.triangleSubmesh.size()) {
                    return -1;
                }
                const std::size_t submesh =
                    mesh.triangleSubmesh[
                        triangleIndex];
                if (submesh >=
                    mesh.submeshMeshIndex.size()) {
                    return -1;
                }
                const int meshIndex =
                    mesh.submeshMeshIndex[submesh];
                if (meshIndex < 0 ||
                    static_cast<std::size_t>(meshIndex) >=
                        mesh.meshIndexToNode.size()) {
                    return -1;
                }
                return mesh.meshIndexToNode[
                    static_cast<std::size_t>(meshIndex)];
            };
        const auto transformVertex =
            [&mesh, &nodeGlobals, &model](
                const models::MeshVertex& vertex,
                int triangleNode,
                int skinIndex) {
                const glm::vec4 local(
                    vertex.position,
                    1.0f);
                glm::vec4 posed(0.0f);
                float posedWeight = 0.0f;
                if (skinIndex >= 0 &&
                    static_cast<std::size_t>(skinIndex) <
                        mesh.skins.size()) {
                    const auto& skin =
                        mesh.skins[
                            static_cast<std::size_t>(
                                skinIndex)];
                    const std::uint16_t jointSlots[4] = {
                        vertex.j0,
                        vertex.j1,
                        vertex.j2,
                        vertex.j3};
                    const float weights[4] = {
                        vertex.w0,
                        vertex.w1,
                        vertex.w2,
                        vertex.w3};
                    for (std::size_t influence = 0u;
                         influence < 4u;
                         ++influence) {
                        const float weight =
                            weights[influence];
                        const std::size_t jointSlot =
                            jointSlots[influence];
                        if (weight <= 0.00001f ||
                            jointSlot >=
                                skin.joints.size()) {
                            continue;
                        }
                        const int jointNode =
                            skin.joints[jointSlot];
                        if (jointNode < 0 ||
                            static_cast<std::size_t>(
                                jointNode) >=
                                nodeGlobals.size()) {
                            continue;
                        }
                        const glm::mat4 inverseBind =
                            jointSlot <
                                    skin.inverseBind.size()
                                ? skin.inverseBind[
                                      jointSlot]
                                : glm::mat4(1.0f);
                        posed +=
                            weight *
                            (nodeGlobals[
                                 static_cast<std::size_t>(
                                     jointNode)] *
                             inverseBind *
                             local);
                        posedWeight += weight;
                    }
                }
                if (posedWeight > 0.00001f) {
                    posed /= posedWeight;
                } else if (
                    triangleNode >= 0 &&
                    static_cast<std::size_t>(
                        triangleNode) <
                        nodeGlobals.size()) {
                    posed =
                        nodeGlobals[
                            static_cast<std::size_t>(
                                triangleNode)] *
                        local;
                } else {
                    posed = local;
                }
                return glm::vec3(model * posed);
            };

        const std::size_t triangleCount =
            mesh.indices.size() / 3u;
        for (std::size_t triangleIndex = 0u;
             triangleIndex < triangleCount;
             ++triangleIndex) {
            const auto submesh = triangleIndex < mesh.triangleSubmesh.size()
                ? mesh.triangleSubmesh[triangleIndex] : 0u;
            if (profile->visibility(mesh, frame, submesh) <= 0.0f) continue;
            const std::size_t base =
                triangleIndex * 3u;
            const std::uint32_t indices[3] = {
                mesh.indices[base],
                mesh.indices[base + 1u],
                mesh.indices[base + 2u]};
            if (indices[0] >= mesh.vertices.size() ||
                indices[1] >= mesh.vertices.size() ||
                indices[2] >= mesh.vertices.size()) {
                continue;
            }
            const int triangleNode =
                resolveTriangleNode(triangleIndex);
            const int skinIndex =
                triangleIndex <
                        mesh.triangleSkinIndex.size()
                    ? mesh.triangleSkinIndex[
                          triangleIndex]
                    : -1;
            const glm::vec3 points[3] = {
                transformVertex(
                    mesh.vertices[indices[0]],
                    triangleNode,
                    skinIndex),
                transformVertex(
                    mesh.vertices[indices[1]],
                    triangleNode,
                    skinIndex),
                transformVertex(
                    mesh.vertices[indices[2]],
                    triangleNode,
                    skinIndex)};
            for (std::size_t edge = 0u;
                 edge < 3u;
                 ++edge) {
                debug.appendProjectedLine(
                    points[edge],
                    points[(edge + 1u) % 3u],
                    0.12f,
                    0.95f,
                    0.72f,
                    0.72f,
                    1.0f);
            }
        }
    }

};
ModelPreview::ModelPreview(std::shared_ptr<const models::ModelProfile> profile, ModelPreviewLighting lighting)
    : impl_(std::make_unique<Impl>(std::move(profile), std::move(lighting))) {}
ModelPreview::~ModelPreview() = default;
bool ModelPreview::select(const char* assetId, const char* path, std::string* error) {
    if (!assetId || !*assetId || !path || !*path) {
        if (error) *error = "Model preview requires an asset id and cooked resource path";
        return false;
    }
    auto candidate = std::make_unique<Impl>(impl_->profile, impl_->lighting);
    if (!candidate->textured.load(path,error)) return false;
    auto plain = candidate->texturedMesh();
    removeTextures(plain);
    if (!candidate->material.setMesh(plain,error)) return false;
    makeGeometryOnly(plain);
    if (!candidate->geometry.setMesh(std::move(plain),error)) return false;
    candidate->assetId=assetId; candidate->assetPath=path;
    candidate->options.graphicsQuality=3;
    const auto& mesh=candidate->texturedMesh();
    candidate->materialCount=std::max({mesh.submeshBaseColors.size(), mesh.submeshBaseTextures.size(),mesh.submeshMaterialModes.size()});
    candidate->cookedTextureCount=textureCount(mesh);
    candidate->cookedBoneCount=boneCount(mesh);
    if (!mesh.animations.empty()) {
        auto idle=std::find_if(mesh.animations.begin(),mesh.animations.end(),[](const auto& clip) {
            auto name=clip.name;
            std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
            return name.find("wait")!=std::string::npos || name.find("idle")!=std::string::npos;
        });
        candidate->options.animationIndex=idle==mesh.animations.end()?0:static_cast<int>(idle-mesh.animations.begin());
    }
    const auto extent=(mesh.boundsMax-mesh.boundsMin)*std::max(.01f,mesh.modelScaleFactor);
    candidate->boundsRadius=std::max(.45f,glm::length(extent)*.5f);
    candidate->boundsCenterY=std::max(.1f,extent.y*.5f+.0025f);
    candidate->status="Rendered directly from the selected .phlo and its cooked dependencies.";
    candidate->ready=true;
    impl_=std::move(candidate);
    if (error) error->clear();
    return true;
}
engine::editor::EditorProjectAssetPreviewInfo
ModelPreview::info() const noexcept {
    return {
        .assetId = impl_->assetId.c_str(),
        .status = impl_->status.c_str(),
        .vertexCount = static_cast<std::uint32_t>(
            impl_->texturedMesh().vertices.size()),
        .triangleCount = static_cast<std::uint32_t>(
            impl_->texturedMesh().indices.size() / 3u),
        .materialCount = static_cast<std::uint32_t>(
            impl_->materialCount),
        .textureCount = static_cast<std::uint32_t>(
            impl_->cookedTextureCount),
        .boneCount = static_cast<std::uint32_t>(
            impl_->cookedBoneCount),
        .animationCount =
            impl_->texturedMesh().animations.size(),
        .animationIndex =
            impl_->resolvedAnimationIndex(),
        .graphicsQuality =
            impl_->options.graphicsQuality,
        .materialDebugView =
            impl_->options.materialDebugView,
        .lightingProfile =
            impl_->options.lightingProfile,
        .animationTimeSeconds =
            impl_->animationTime,
        .animationDurationSeconds =
            impl_->resolvedAnimationIndex() >= 0
                ? impl_->texturedMesh().animations[
                      static_cast<std::size_t>(
                          impl_->resolvedAnimationIndex())]
                      .durationSec
                : 0.0f,
        .boundsRadius = impl_->boundsRadius,
        .boundsCenterY = impl_->boundsCenterY,
        .ready = impl_->ready,
    };
}

engine::editor::EditorProjectAssetAnimation
ModelPreview::animation(
    std::size_t index) const noexcept {
    if (index >= impl_->texturedMesh().animations.size()) {
        return {};
    }
    const auto& animation =
        impl_->texturedMesh().animations[index];
    return {
        .name = animation.name.c_str(),
        .durationSeconds = animation.durationSec,
    };
}

void ModelPreview::setOptions(
    const engine::editor::
        EditorProjectAssetPreviewOptions& options) {
    const int previousAnimation =
        impl_->options.animationIndex;
    impl_->options = options;
    impl_->options.graphicsQuality =
        std::clamp(impl_->options.graphicsQuality, 0, 3);
    impl_->options.materialDebugView = std::clamp(
        impl_->options.materialDebugView,
        0,
        7);
    impl_->options.lightingProfile = std::clamp(
        impl_->options.lightingProfile,
        0,
        4);
    impl_->options.playbackSpeed =
        std::clamp(
            impl_->options.playbackSpeed,
            0.0f,
            4.0f);
    if (impl_->options.animationIndex < -1 ||
        static_cast<std::size_t>(
            std::max(0, impl_->options.animationIndex)) >=
            impl_->texturedMesh().animations.size()) {
        impl_->options.animationIndex = -1;
    }
    if (previousAnimation !=
        impl_->options.animationIndex) {
        impl_->animationTime = 0.0f;
        impl_->materialTime = 0.0f;
    }
    const int animationIndex =
        impl_->resolvedAnimationIndex();
    if (impl_->options.seekRequested) {
        impl_->materialTime =
            std::max(0.0f, impl_->options.seekTimeSeconds);
        if (animationIndex >= 0) {
            const float duration =
                impl_->texturedMesh().animations[
                    static_cast<std::size_t>(animationIndex)]
                    .durationSec;
            if (duration > 0.0001f) {
                impl_->animationTime = std::clamp(
                    impl_->options.seekTimeSeconds,
                    0.0f,
                    duration);
                if (impl_->animationTime >= duration) {
                    impl_->animationTime =
                        std::nextafter(duration, 0.0f);
                }
            } else {
                impl_->animationTime = 0.0f;
            }
        } else {
            impl_->animationTime = 0.0f;
        }
    }
    impl_->options.seekRequested = false;
}

void ModelPreview::update(float deltaSeconds) {
    if (!impl_->ready ||
        !impl_->options.animationPlaying) {
        return;
    }
    const float scaledDeltaSeconds =
        std::max(0.0f, deltaSeconds) *
        impl_->options.playbackSpeed;
    impl_->materialTime += scaledDeltaSeconds;
    const int animationIndex =
        impl_->resolvedAnimationIndex();
    if (animationIndex < 0) {
        impl_->animationTime = 0.0f;
        return;
    }
    const float duration =
        impl_->texturedMesh().animations[
            static_cast<std::size_t>(animationIndex)]
            .durationSec;
    if (duration <= 0.0001f) {
        impl_->animationTime = 0.0f;
        return;
    }
    impl_->animationTime = std::fmod(
        impl_->animationTime +
            scaledDeltaSeconds,
        duration);
}

void ModelPreview::render(const EditorProjectRenderContext& context) {
    if (!impl_->ready || !context.renderer || !context.cameraWorldPosition3 || !context.cameraTarget3) return;
    const int width=std::max(1,context.surfaceWidth), height=std::max(1,context.surfaceHeight);
    Camera3D camera(36.0f,float(width)/float(height),.01f,100.0f);
    camera.setPosition(glm::make_vec3(context.cameraWorldPosition3));
    camera.lookAt(glm::make_vec3(context.cameraTarget3));
    auto& renderer=*context.renderer;
    auto& instance=impl_->displayInstance();
    const auto view=camera.getViewMatrix(), projection=camera.getProjectionMatrix();
    models::ModelFrame frame;
    frame.modelMatrix=impl_->modelMatrix();
    frame.animationIndex=impl_->resolvedAnimationIndex();
    frame.animationTime=impl_->animationTime;
    frame.materialTime=impl_->materialTime;
    frame.graphicsQuality = impl_->options.graphicsQuality;
    const std::string backend=renderer.backendId();
    frame.pairedSkinMatrices=backend=="opengl" || backend=="d3d12";
    frame.cameraPosition=camera.getPosition();
    frame.cameraUp=glm::vec3(glm::inverse(view)[1]);
    frame.cameraFacing=glm::vec3(glm::inverse(view)[2]);
    models::PoseEval pose;
    std::string error;
    if (!instance.samplePose(frame,pose,&error)) { impl_->status=error; return; }
    PreviewLines grid{view,projection,width,height,{}}, overlays{view,projection,width,height,{}};
    impl_->appendGrid(grid);
    impl_->appendSkeleton(pose,overlays);
    impl_->appendWireframe(*instance.mesh(),pose,frame,overlays);
    std::vector<models::batches::WorldIndexedBatch> batches;
    if (impl_->options.showMesh) {
        const auto* prepared=instance.prepare(frame,0,&error);
        if (!prepared) { impl_->status=error; return; }
        batches=*prepared;
        if (impl_->lighting.prepare && !impl_->lighting.prepare(batches,impl_->options.lightingProfile,error)) {
            impl_->status=error; return;
        }
    }
    const ScopedWorldMaterialDebugView debug(renderer,
        impl_->options.showMaterials && impl_->options.showTextures ? impl_->options.materialDebugView : 0);
    renderer.beginWorldSceneColorPass(width,height);
    if (!grid.lines.empty()) renderer.drawDebugLines(grid.lines.data(),grid.lines.size(),width,height);
    const auto viewProjection=projection*view;
    const auto forward=camera.getDirection()*(impl_->lighting.forwardScale ? impl_->lighting.forwardScale(impl_->options.lightingProfile) : 1.0f);
    models::batches::submitWorldIndexedBatches(renderer,batches,glm::value_ptr(viewProjection),width,height,
        glm::value_ptr(camera.getPosition()),glm::value_ptr(forward),glm::value_ptr(camera.getTarget()));
    if (!overlays.lines.empty()) renderer.drawDebugLines(overlays.lines.data(),overlays.lines.size(),width,height);
    renderer.endWorldSceneColorPass();
}
} // namespace engine::editor
