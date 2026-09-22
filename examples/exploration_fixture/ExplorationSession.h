#pragma once
#include "engine/camera/ExplorationCamera.h"
#include "engine/input/SdlGameInput.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace engine::exploration_fixture {
struct CourseBox {
    std::string name;
    glm::vec3 center, half;
    float pitch = 0;
    int color = 0;
    bool sensor = false;
};
const std::vector<CourseBox>& course();
const std::vector<std::string>& cases();
const std::vector<std::string>& supplementalCases();
glm::vec3 caseSpawn(const std::string& name);
camera::ExplorationCameraSettings cameraSettings();
camera::ExplorationCameraSettings configuredCameraSettings();
inline constexpr int targetTick = 120;
inline constexpr float fixedDelta = 1.0f / 60;

class ExplorationSession {
public:
    explicit ExplorationSession(std::string caseName = "walk", bool automation = false,
                                camera::ExplorationCameraSettings settings = cameraSettings());
    void begin(const editor::EditorSdlGameInputContext& context);
    void event(const editor::EditorSdlGameInputContext& context, const SDL_Event& event);
    void end(const editor::EditorSdlGameInputContext& context);
    void tick();
    void reset(bool safe = true);
    nlohmann::json state() const;
    void setTrace(const std::string& path);
    const collision::CharacterState& player() const { return playerState_; }
    const collision::CharacterState& npc() const { return npcState_; }
    const camera::ExplorationCameraPose& cameraPose() const { return pose_; }
    bool sensorActive() const { return sensorActive_; }
    int ticks() const { return ticks_; }
    const std::string& caseName() const { return case_; }
    const camera::ExplorationCameraSettings& settings() const { return rig_.settings(); }
private:
    void record(const char* kind);
    void npcTick();
    input::ActionState action(const char* name) const;
    std::string case_;
    bool automation_;
    input::InputActions actions_{{128, 32, 32}};
    std::unique_ptr<input::SdlGameInput> input_;
    collision::CollisionWorld world_;
    collision::CharacterId playerId_, npcId_;
    collision::CharacterState playerState_, npcState_;
    camera::ExplorationCameraRig rig_;
    camera::ExplorationCameraPose pose_;
    editor::EditorSdlGameInputContext context_;
    int ticks_ = 0, resets_ = 0, npcBlocked_ = 0, npcRetries_ = 0, npcStall_ = 0, npcWait_ = 0;
    int inputFrames_ = 0;
    int enters_ = 0, stays_ = 0, exits_ = 0;
    bool sensorActive_ = false;
    float aspect_ = 16.0f / 9;
    std::ofstream trace_;
};
nlohmann::json replayScript(const std::string& name);
} // namespace engine::exploration_fixture
