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
    require(wes_->animations.size() > 8u &&
            wes_->animations[1].durationSec > 0.0f &&
            wes_->animations[8].durationSec > 0.0f,
            "Battle preview needs Wes's idle and gesture clips");
    std::array<float, 9600> tone{};
    for (std::size_t index = 0; index < tone.size(); ++index) {
        const float time = static_cast<float>(index) / audio::sampleRate;
        const float falloff = 1.0f - static_cast<float>(index) / tone.size();
        tone[index] = .2f * falloff * std::sin(2.0f * 3.14159265f * 330.0f * time);
    }
    require(audio::AudioClip::create(audio::sampleRate, 1u, tone, impactClip_, &error), error);
    if (env::get("PHLOSION_EXPLORATION_AUTOMATION").value_or("0") == "1") {
        automationAction_ = env::get("PHLOSION_BATTLE_ACTION").value_or("");
        if (!automationAction_.empty()) {
            require(automationAction_ == "cancel" || automationAction_ == "skip",
                    "Invalid battle automation action");
            automationActionTick_ = std::stoi(env::get("PHLOSION_BATTLE_ACTION_TICK").value_or("-1"));
            require(automationActionTick_ > 0 && automationActionTick_ < 150,
                    "Invalid battle automation action tick");
        }
    }
}

BattlePreview::~BattlePreview() { cancel(); release(); }

void BattlePreview::release() {
    if (scopes_ && scope_) {
        if (!scopes_->requestWorldResourceScopeRelease(scope_)) std::terminate();
        scope_ = 0;
        scopes_ = nullptr;
    }
}

void BattlePreview::dispatch(BattleCue cue) {
    if (cue == BattleCue::ImpactSound) {
        audio::AudioVoiceId voice = 0;
        audio::AudioPlayOptions options;
        options.bus = audio::AudioBus::Sfx;
        options.spatial = true;
        options.emitterPosition = slots[3].position;
        std::string error;
        require(mixer_.play(impactClip_, options, voice, &error), error);
    } else if (cue == BattleCue::ImpactEffect) {
        activeEffect_ = true;
    } else if (cue == BattleCue::EffectStop) {
        activeEffect_ = false;
    } else if (cue == BattleCue::Cleanup) {
        mixer_.stopAll();
        lastAudioPeak_ = 0.0f;
        camera_.reset();
        activeEffect_ = false;
        phase_ = "complete";
    }
}

void BattlePreview::reset() {
    sequence_.reset();
    mixer_.reset();
    camera_.reset();
    activeEffect_ = false;
    lastAudioPeak_ = 0.0f;
    phase_ = "wide";
}

void BattlePreview::cancel() {
    for (const auto cue : sequence_.cancel()) dispatch(cue);
}

void BattlePreview::skip() {
    for (const auto cue : sequence_.skip()) dispatch(cue);
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
    if (tick < 0 || tick > 3600 || width <= 0 || height <= 0) throw std::runtime_error("Invalid battle preview frame");
    if (!scope_) {
        scopes_ = dynamic_cast<IWorldResourceScope*>(&renderer);
        require(scopes_ != nullptr, "Managed battle resources required");
        scope_ = scopes_->createWorldResourceScope(256u * 1024u * 1024u);
        require(scope_ != 0, "Could not create battle GPU scope");
    }
    require(dynamic_cast<IWorldResourceScope*>(&renderer) == scopes_,
            "Battle renderer changed without release");
    if (sequence_.state().active)
        frameCamera(tick, static_cast<float>(width) / height);
    std::string error;
    require(mixer_.setListener(cameraPose_.position, cameraPose_.right, &error), error);
    while (sequence_.state().active && sequence_.state().tick < tick) {
        for (const auto cue : sequence_.advance()) dispatch(cue);
        if (sequence_.state().tick == automationActionTick_) {
            if (automationAction_ == "cancel") cancel();
            else skip();
        }
        require(mixer_.render(800u, mixFrame_, &error), error);
        lastAudioPeak_ = 0.0f;
        for (const float sample : mixFrame_)
            lastAudioPeak_ = std::max(lastAudioPeak_, std::abs(sample));
    }
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
    const bool drewField = drawGltfSceneAsset(renderer, *field_, fieldPose_,
                                               options, stats, &error);
    require(drewField, error);
    const float duration = wes_->animations[1].durationSec;
    // Keep each skin palette alive at a distinct address until all actor draws finish.
    std::array<GltfScenePose, 4> actorPoses;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const float sampleTime = std::fmod(tick * battleFixedDelta + index * .17f,
                                           duration);
        bool sampled = false;
        if (index == 1 && sequence_.state().active && tick >= 110 && tick < 119) {
            render::animation::PoseBlendRequest recovery;
            recovery.baseClipIndex = 8;
            recovery.baseTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                                             wes_->animations[8].durationSec);
            recovery.targetClipIndex = 1;
            recovery.targetTimeSec = sampleTime;
            recovery.crossfadeWeight = (tick - 109) / 9.0f;
            sampled = sampleGltfSceneBlendedPose(wes_, recovery, actorPoses[index], &error);
        } else if (index == 1 && sequence_.state().gestureClip == 8) {
            render::animation::PoseBlendRequest gesture;
            gesture.baseClipIndex = 1;
            gesture.baseTimeSec = sampleTime;
            gesture.targetClipIndex = 8;
            gesture.targetTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                                               wes_->animations[8].durationSec);
            gesture.crossfadeWeight = std::min(1.0f, (tick - 59) / 9.0f);
            sampled = sampleGltfSceneBlendedPose(wes_, gesture, actorPoses[index], &error);
        } else {
            sampled = sampleGltfScenePose(wes_, sampleTime, 1,
                                          actorPoses[index], &error);
        }
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

void BattlePreview::drawOverlay(IRenderBackend& renderer, int width, int height) const {
    if (!sequence_.state().hudVisible) return;
    std::array<IRenderBackend::DebugQuad, 10> quads{};
    std::size_t count = 0;
    const float panelWidth = std::min(180.0f, width * .24f);
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const float x = index < 2 ? 12.0f : width - panelWidth - 12.0f;
        const float y = height - 50.0f + (index % 2) * 18.0f;
        quads[count++] = {x, y, panelWidth, 12.0f, .04f, .06f, .09f, 1.0f};
        quads[count++] = {x + 3.0f, y + 3.0f,
                          (panelWidth - 6.0f) * sequence_.state().health[index], 6.0f,
                          index < 2 ? .15f : .8f, index < 2 ? .8f : .55f,
                          .25f, 1.0f};
    }
    if (activeEffect_ && sequence_.state().effectVisible)
        quads[count++] = {width * .5f + 80.0f, height * .5f - 45.0f,
                          14.0f, 14.0f, 1.0f, .75f, .2f, 1.0f};
    renderer.drawDebugQuads(quads.data(), count, width, height);
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
    const auto& encounter = sequence_.state();
    camera::BattleCameraPose rigPose;
    const bool cameraActive = camera_.pose(rigPose);
    return {{"tick", tick}, {"phase", phase_}, {"viewport", viewport},
            {"encounter", {{"tick", encounter.tick}, {"active", encounter.active},
                           {"camera_active", cameraActive},
                           {"cancelled", encounter.cancelled}, {"skipped", encounter.skipped},
                           {"hud_visible", encounter.hudVisible},
                           {"effect_visible", encounter.effectVisible},
                           {"vfx_active", activeEffect_},
                           {"gesture_clip", encounter.gestureClip},
                           {"health", encounter.health},
                           {"audio_voices", mixer_.activeVoices()},
                           {"audio_peak", lastAudioPeak_}}},
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
