#pragma once

#include "BattleSequence.h"
#include "BattleHudAtlas.h"
#include "BattlePreviewFreeCamera.h"
#include "BattleVfxPlugin.h"
#include "engine/render/BattlePokemonPlugin.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/SdlAudioOutput.h"
#include "engine/camera/BattleCamera.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

#include <array>
#include <SDL.h>
#include <nlohmann/json.hpp>

namespace engine::exploration_fixture {

inline constexpr float battleFixedDelta = 1.0f / 60.0f;

class BattlePreview {
public:
    explicit BattlePreview(bool audioOutput = false);
    ~BattlePreview();
    BattlePreview(const BattlePreview&) = delete;
    BattlePreview& operator=(const BattlePreview&) = delete;

    void draw(IRenderBackend& renderer, int tick, int width, int height,
              bool cameraProbeAllowed = true);
    void drawOverlay(IRenderBackend& renderer, int width, int height) const;
    void reset();
    void cancel();
    void skip();
    void release();
    void setNavigationEnabled(bool enabled);
    void navigationEvent(const SDL_Event& event, Uint32 windowId, bool pointerInside);
    void navigationFrame(float deltaSeconds);
    bool usesAuthoredCast() const noexcept { return castEnabled_; }
    nlohmann::json imports() const;
    nlohmann::json state(int tick, std::array<int, 4> viewport) const;

private:
    struct CastActor {
        std::string slot;
        std::string model;
        std::string nativeObject;
        std::string nativeSha256;
        nlohmann::json nativeStats;
        std::string displayName;
        glm::vec3 position{0.0f};
        glm::vec3 modelOffset{0.0f};
        float yawRadians = 0.0f;
        float scale = 1.0f;
        float focusHeight = 0.9f;
        std::string idleAnimation;
        std::string gestureAnimation;
    };
    void frameCamera(int tick, float aspect);
    void dispatch(BattleCue cue);

    std::shared_ptr<const render::gltf::scene::GltfSceneAsset> field_, wes_;
    std::array<CastActor, 6> castActors_{};
    std::array<std::shared_ptr<const render::gltf::scene::GltfSceneAsset>, 6> castModels_{};
    std::array<render::gltf::scene::GltfScenePose, 6> castPoses_{};
    std::array<render::gltf::scene::GltfScenePose, 4> standInPoses_{};
    std::array<int, 6> castIdleClips_{};
    void* battlePokemonLibrary_ = nullptr;
    render::BattlePokemonPluginApi battlePokemonApi_{};
    int castGestureClip_ = -1;
    bool castEnabled_ = false;
    render::gltf::scene::GltfScenePose fieldPose_;
    camera::BattleCameraRig camera_;
    camera::BattleCameraPose cameraPose_;
    BattlePreviewFreeCamera freeCamera_;
    std::array<bool, 7> navigationKeys_{};
    glm::vec2 pendingLook_{0.0f};
    float pendingDolly_ = 0.0f;
    float navigationSeconds_ = 0.0f;
    bool navigationEnabled_ = false;
    bool rightMouseDown_ = false;
    bool cameraProbe_ = false;
    bool cameraProbeApplied_ = false;
    int cameraProbeAspectNumerator_ = 0;
    BattleSequence sequence_;
    BattleHudAtlas hudAtlas_;
    BattleVfxPlugin vfxPlugin_;
    audio::AudioMixer mixer_;
    audio::SdlAudioOutput audioOutput_;
    std::shared_ptr<const audio::AudioClip> impactClip_;
    bool activeEffect_ = false;
    float lastAudioPeak_ = 0.0f;
    std::string automationAction_;
    int automationActionTick_ = -1;
    std::string phase_ = "wide";
    IWorldResourceScope* scopes_ = nullptr;
    render::WorldResourceScopeId scope_ = 0;
};

void writeBattleHandshake(const std::string& path, const char* host,
                          const std::string& requested, const BattlePreview& preview,
                          IRenderBackend& renderer, int tick, int width, int height,
                          int renderFrame, std::array<int, 4> viewport);

} // namespace engine::exploration_fixture
