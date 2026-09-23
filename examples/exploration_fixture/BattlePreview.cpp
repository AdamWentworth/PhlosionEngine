#include "BattlePreview.h"

#include "engine/core/Environment.h"
#include "engine/render/Camera3D.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace engine::exploration_fixture {
namespace {
using namespace render::gltf::scene;

struct ActorSlot {
    const char* name;
    glm::vec3 position;
};

constexpr std::array<ActorSlot, 4> slots{{
    {"ally-left", {-3.2f, 0.0f, 5.0f}},
    {"ally-right", {-0.8f, 0.0f, 5.0f}},
    {"opponent-left", {-3.2f, 0.0f, 2.0f}},
    {"opponent-right", {-0.8f, 0.0f, 2.0f}},
}};

void require(bool passed, const std::string& error) {
    if (!passed) throw std::runtime_error(error);
}

nlohmann::json vec(glm::vec3 value) {
    return {value.x, value.y, value.z};
}

camera::BattleCameraShot shot(float yaw, float pitch, float fov) {
    camera::BattleCameraShot value;
    value.yawRadians = yaw;
    value.pitchRadians = pitch;
    value.verticalFovDegrees = fov;
    value.marginFraction = .15f;
    value.minDistance = 5.0f;
    value.maxDistance = 20.0f;
    value.farPlane = 2500.0f;
    return value;
}

std::array<camera::BattleCameraTarget, 4> targets() {
    std::array<camera::BattleCameraTarget, 4> result{};
    for (std::size_t index = 0; index < slots.size(); ++index)
        result[index] = {slots[index].position + glm::vec3(0.0f, .9f, 0.0f), .9f};
    return result;
}
} // namespace

BattlePreview::BattlePreview() {
    const auto fieldPath = env::get("PHLOSION_OUTSKIRT_BATTLE_GLB");
    const auto wesPath = env::get("PHLOSION_OUTSKIRT_WES_GLB");
    require(fieldPath && !fieldPath->empty() && wesPath && !wesPath->empty(),
            "Battle preview requires PHLOSION_OUTSKIRT_BATTLE_GLB and PHLOSION_OUTSKIRT_WES_GLB");
    std::string error;
    const bool loadedField = loadGltfSceneAsset(*fieldPath, field_, &error);
    require(loadedField, error);
    const bool posedField = sampleGltfScenePose(field_, 0.0f, kBindPoseClip,
                                                fieldPose_, &error);
    require(posedField, error);
    const bool loadedWes = loadGltfSceneAsset(*wesPath, wes_, &error);
    require(loadedWes, error);
    require(wes_->animations.size() > 1u && wes_->animations[1].durationSec > 0.0f,
            "Battle preview needs Wes's idle clip");
}

BattlePreview::~BattlePreview() { release(); }

void BattlePreview::release() {
    if (scopes_ && scope_) {
        if (!scopes_->requestWorldResourceScopeRelease(scope_)) std::terminate();
        scope_ = 0;
        scopes_ = nullptr;
    }
}

void BattlePreview::frameCamera(int tick, float aspect) {
    std::string error;
    const auto all = targets();
    const std::array<camera::BattleCameraTarget, 2> first{all[0], all[2]};
    const std::array<camera::BattleCameraTarget, 2> second{all[1], all[3]};
    camera_.reset();
    const bool opened = camera_.cut(shot(.10f, .38f, 48.0f), all, aspect,
                                    cameraPose_, &error);
    require(opened, error);
    phase_ = "wide";
    for (int current = 1; current <= tick; ++current) {
        if (current == 30) {
            const bool blended = camera_.blendTo(shot(-.48f, .25f, 42.0f), first,
                                                 aspect, .5f, cameraPose_, &error);
            require(blended, error);
            phase_ = "blend";
        }
        if (current == 60) {
            const bool cut = camera_.cut(shot(.52f, .28f, 42.0f), second,
                                         aspect, cameraPose_, &error);
            require(cut, error);
            phase_ = "counter";
        }
        if (current == 90) {
            const bool shaken = camera_.startShake({.12f, 8.0f, .3f, 17u}, &error);
            require(shaken, error);
            phase_ = "impact";
        }
        const auto active = current < 30 ? std::span<const camera::BattleCameraTarget>(all)
                           : current < 60 ? std::span<const camera::BattleCameraTarget>(first)
                                          : std::span<const camera::BattleCameraTarget>(second);
        const bool stepped = camera_.step(active, aspect, battleFixedDelta,
                                          false, cameraPose_, &error);
        require(stepped, error);
        if (current == 110) phase_ = "settle";
    }
}

void BattlePreview::draw(IRenderBackend& renderer, int tick, int width, int height) {
    if (tick < 0 || width <= 0 || height <= 0) throw std::runtime_error("Invalid battle preview frame");
    if (!scope_) {
        scopes_ = dynamic_cast<IWorldResourceScope*>(&renderer);
        require(scopes_ != nullptr, "Managed battle resources required");
        scope_ = scopes_->createWorldResourceScope(256u * 1024u * 1024u);
        require(scope_ != 0, "Could not create battle GPU scope");
    }
    require(dynamic_cast<IWorldResourceScope*>(&renderer) == scopes_,
            "Battle renderer changed without release");
    frameCamera(tick, static_cast<float>(width) / height);
    Camera3D camera(cameraPose_.verticalFovDegrees,
                    static_cast<float>(width) / height,
                    cameraPose_.nearPlane, cameraPose_.farPlane);
    camera.setPosition(cameraPose_.position);
    camera.lookAt(cameraPose_.lookTarget, cameraPose_.up);
    const glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
    GltfSceneDrawOptions options;
    std::copy_n(glm::value_ptr(viewProjection), 16, options.viewProjectionMatrix.begin());
    options.surfaceWidth = width;
    options.surfaceHeight = height;
    options.cameraPosition = {cameraPose_.position.x, cameraPose_.position.y,
                              cameraPose_.position.z};
    options.cameraTarget = {cameraPose_.lookTarget.x, cameraPose_.lookTarget.y,
                            cameraPose_.lookTarget.z};
    options.cameraForward = {cameraPose_.forward.x, cameraPose_.forward.y,
                             cameraPose_.forward.z};
    options.resourceScope = scope_;
    options.materialMode = 2;
    GltfSceneDrawStats stats;
    std::string error;
    const bool drewField = drawGltfSceneAsset(renderer, *field_, fieldPose_,
                                               options, stats, &error);
    require(drewField, error);
    const float duration = wes_->animations[1].durationSec;
    // Keep each skin palette alive at a distinct address until all actor draws finish.
    std::array<GltfScenePose, 4> actorPoses;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const float sampleTime = std::fmod(tick * battleFixedDelta + index * .17f,
                                           duration);
        const bool sampled = sampleGltfScenePose(wes_, sampleTime, 1,
                                                  actorPoses[index], &error);
        require(sampled, error);
    }
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const glm::mat4 placement = glm::translate(glm::mat4(1.0f), slots[index].position) *
            glm::rotate(glm::mat4(1.0f), index < 2 ? 0.0f : 3.14159265f,
                        glm::vec3(0.0f, 1.0f, 0.0f));
        std::copy_n(glm::value_ptr(placement), 16, options.instanceTransform.begin());
        const bool drewActor = drawGltfSceneAsset(renderer, *wes_, actorPoses[index],
                                                  options, stats, &error);
        require(drewActor, error);
    }
}

nlohmann::json BattlePreview::imports() const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& asset : {field_, wes_})
        result.push_back({{"identity", asset->sourceIdentity},
                          {"primitives", asset->stats.primitiveCount},
                          {"vertices", asset->stats.vertexCount},
                          {"indices", asset->stats.indexCount},
                          {"materials", asset->stats.materialCount},
                          {"clips", asset->stats.clipCount}});
    return result;
}

nlohmann::json BattlePreview::state(int tick, std::array<int, 4> viewport) const {
    nlohmann::json actors = nlohmann::json::array();
    for (const auto& actor : slots)
        actors.push_back({{"slot", actor.name}, {"position", vec(actor.position)},
                          {"stand_in", "wes"}});
    return {{"tick", tick}, {"phase", phase_}, {"viewport", viewport},
            {"camera", {{"position", vec(cameraPose_.position)},
                         {"target", vec(cameraPose_.lookTarget)},
                         {"forward", vec(cameraPose_.forward)},
                         {"right", vec(cameraPose_.right)},
                         {"up", vec(cameraPose_.up)},
                         {"distance", cameraPose_.distance},
                         {"aspect", cameraPose_.aspectRatio},
                         {"fov", cameraPose_.verticalFovDegrees},
                         {"blend", cameraPose_.blendProgress},
                         {"shake", vec(cameraPose_.shakeOffset)}}},
            {"actors", actors}};
}

void writeBattleHandshake(const std::string& path, const char* host,
                          const std::string& requested, const BattlePreview& preview,
                          IRenderBackend& renderer, int tick, int width, int height,
                          int renderFrame, std::array<int, 4> viewport) {
    if (path.empty()) return;
    nlohmann::json result{{"schema", "phlosion-battle-preview-v1"},
                          {"host", host}, {"requested_api", requested},
                          {"api", renderer.backendId()},
                          {"gpu", renderer.activeGpuName()},
                          {"gpu_discrete", renderer.activeGpuIsDiscrete()},
                          {"width", width}, {"height", height},
                          {"render_frame", renderFrame},
                          {"state", preview.state(tick, viewport)},
                          {"imports", preview.imports()}};
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot write battle handshake");
    output << result.dump(2);
}

} // namespace engine::exploration_fixture
