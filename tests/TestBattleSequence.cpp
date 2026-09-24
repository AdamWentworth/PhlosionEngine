#include "BattleSequence.h"
#include "BattlePreviewFreeCamera.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace engine::exploration_fixture;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main() {
    BattleSequence sequence;
    CHECK(sequence.state().active && sequence.state().hudVisible);
    CHECK(sequence.advanceTo(29).empty());
    CHECK(sequence.advance() == std::vector<BattleCue>{BattleCue::CameraBlend});
    CHECK(sequence.advanceTo(59).empty());
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::CameraCut, BattleCue::Gesture}));
    CHECK(sequence.state().gestureClip == 8);
    CHECK(sequence.advanceTo(89).empty());
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::ImpactSound, BattleCue::ImpactEffect}));
    CHECK(sequence.state().effectVisible);
    CHECK(sequence.advanceTo(109) == std::vector<BattleCue>{BattleCue::EffectStop});
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::Damage, BattleCue::Settle}));
    CHECK(sequence.state().health[3] == .6f && sequence.state().gestureClip == 1);
    CHECK(sequence.advanceTo(149).empty());
    CHECK(sequence.advance() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(!sequence.state().active && !sequence.state().hudVisible);
    CHECK(sequence.advance().empty() && sequence.cancel().empty());

    sequence.reset();
    CHECK(sequence.advanceTo(94).size() == 5u);
    CHECK(sequence.cancel() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(sequence.state().cancelled && !sequence.state().effectVisible);
    CHECK(sequence.advance().empty());
    sequence.reset();
    CHECK(sequence.advanceTo(60).size() == 3u);
    CHECK(sequence.skip() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(sequence.state().skipped && sequence.state().health[3] == 1.0f);
    bool rejected = false;
    try { sequence.advanceTo(59); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);

    engine::camera::BattleCameraPose shot;
    shot.position = {1.0f, 2.0f, 3.0f};
    shot.lookTarget = {1.0f, 2.0f, -2.0f};
    shot.distance = 5.0f;
    BattlePreviewFreeCamera freeCamera;
    freeCamera.step(shot, {}, 1.0f / 60.0f);
    CHECK(!freeCamera.active());
    BattleFreeCameraInput move;
    move.move.z = 1.0f;
    freeCamera.step(shot, move, 0.5f);
    CHECK(freeCamera.active());
    CHECK(std::abs(freeCamera.pose(16.0f / 9.0f).position.z - 2.8f) < .0001f);
    move = {};
    move.look = {.2f, .1f};
    move.dolly = 1.0f;
    freeCamera.step(shot, move, 0.0f);
    const auto inspected = freeCamera.pose(21.0f / 9.0f);
    CHECK(inspected.position.x > 1.0f && inspected.position.y > 2.0f);
    CHECK(inspected.aspectRatio == 21.0f / 9.0f);
    CHECK(std::abs(glm::dot(inspected.forward, inspected.right)) < .0001f);
    shot.position = {100.0f, 100.0f, 100.0f};
    freeCamera.step(shot, {}, 0.05f);
    CHECK(glm::distance(inspected.position, freeCamera.pose(1.0f).position) < .0001f);
    freeCamera.reset();
    CHECK(!freeCamera.active());
    freeCamera.step(shot, move, 0.0f);
    CHECK(glm::distance(shot.position, freeCamera.pose(1.0f).position) < 2.0f);
    std::cout << "Battle sequence contracts PASS\n";
}
