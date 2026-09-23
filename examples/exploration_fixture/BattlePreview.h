#pragma once

#include "BattleSequence.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/SdlAudioOutput.h"
#include "engine/camera/BattleCamera.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

#include <array>
#include <nlohmann/json.hpp>

namespace engine::exploration_fixture {

inline constexpr float battleFixedDelta = 1.0f / 60.0f;

class BattlePreview {
public:
    explicit BattlePreview(bool audioOutput = false);
    ~BattlePreview();
    BattlePreview(const BattlePreview&) = delete;
    BattlePreview& operator=(const BattlePreview&) = delete;

    void draw(IRenderBackend& renderer, int tick, int width, int height);
    void drawOverlay(IRenderBackend& renderer, int width, int height) const;
    void reset();
    void cancel();
    void skip();
    void release();
    nlohmann::json imports() const;
    nlohmann::json state(int tick, std::array<int, 4> viewport) const;

private:
    void frameCamera(int tick, float aspect);
    void dispatch(BattleCue cue);

    std::shared_ptr<const render::gltf::scene::GltfSceneAsset> field_, wes_;
    render::gltf::scene::GltfScenePose fieldPose_;
    camera::BattleCameraRig camera_;
    camera::BattleCameraPose cameraPose_;
    BattleSequence sequence_;
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
