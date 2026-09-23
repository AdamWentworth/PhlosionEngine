#include "engine/camera/BattleCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace engine::camera;

namespace {

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

bool near(float a, float b, float tolerance = 1.0e-4f) {
    return std::abs(a - b) <= tolerance;
}

bool near(const glm::vec3& a, const glm::vec3& b,
          float tolerance = 1.0e-4f) {
    return glm::length(a - b) <= tolerance;
}

BattleCameraShot flatShot() {
    BattleCameraShot shot;
    shot.pitchRadians = 0.0f;
    shot.verticalFovDegrees = 90.0f;
    shot.marginFraction = 0.1f;
    shot.minDistance = 1.0f;
    return shot;
}

glm::vec3 project(const BattleCameraPose& pose, const glm::vec3& point) {
    const glm::mat4 view = glm::lookAt(pose.position, pose.lookTarget, pose.up);
    const glm::mat4 projection = glm::perspective(
        glm::radians(pose.verticalFovDegrees), pose.aspectRatio,
        pose.nearPlane, pose.farPlane);
    const glm::vec4 clip = projection * view * glm::vec4(point, 1.0f);
    CHECK(clip.w > 0.0f);
    return glm::vec3(clip) / clip.w;
}

void same(const BattleCameraPose& a, const BattleCameraPose& b) {
    CHECK(a.position == b.position && a.lookTarget == b.lookTarget);
    CHECK(a.forward == b.forward && a.up == b.up && a.right == b.right);
    CHECK(a.shakeOffset == b.shakeOffset);
    CHECK(a.yawRadians == b.yawRadians && a.pitchRadians == b.pitchRadians);
    CHECK(a.distance == b.distance && a.aspectRatio == b.aspectRatio);
    CHECK(a.verticalFovDegrees == b.verticalFovDegrees);
    CHECK(a.nearPlane == b.nearPlane && a.farPlane == b.farPlane);
    CHECK(a.blendProgress == b.blendProgress);
}

void checkAnalyticFraming() {
    const auto shot = flatShot();
    const std::array<BattleCameraTarget, 2> pair{{
        {{-2.0f, 0.0f, 0.0f}, 0.0f},
        {{2.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraPose pose;
    std::string error;
    CHECK(frameBattleTargets(shot, pair, 1.0f, pose, &error));
    CHECK(error.empty());
    CHECK(near(pose.lookTarget, {0.0f, 0.0f, 0.0f}));
    CHECK(near(pose.distance, 2.0f / 0.9f));
    CHECK(near(project(pose, pair[0].position).x, -0.9f));
    CHECK(near(project(pose, pair[1].position).x, 0.9f));
    CHECK(near(glm::length(pose.forward), 1.0f));
    CHECK(near(glm::dot(pose.forward, pose.up), 0.0f));
    BattleCameraPose wide;
    BattleCameraPose narrow;
    CHECK(frameBattleTargets(shot, pair, 2.0f, wide, &error));
    CHECK(frameBattleTargets(shot, pair, 0.5f, narrow, &error));
    CHECK(near(wide.distance, 2.0f / 1.8f));
    CHECK(near(narrow.distance, 2.0f / 0.45f));
    CHECK(wide.distance < pose.distance && pose.distance < narrow.distance);
    const std::array<BattleCameraTarget, 1> sphere{{
        {{0.0f, 0.0f, 0.0f}, 1.0f}}};
    CHECK(frameBattleTargets(shot, sphere, 1.0f, pose, &error));
    CHECK(near(pose.distance, 1.0f + 1.0f / 0.9f));
}

void checkInvalidFramingPreservesOutput() {
    BattleCameraShot shot = flatShot();
    const std::array<BattleCameraTarget, 2> pair{{
        {{-2.0f, 0.0f, 0.0f}, 0.0f},
        {{2.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraPose pose;
    std::string error;
    CHECK(frameBattleTargets(shot, pair, 1.0f, pose, &error));
    const BattleCameraPose original = pose;
    shot.farPlane = 2.0f;
    CHECK(!frameBattleTargets(shot, pair, 1.0f, pose, &error));
    CHECK(!error.empty());
    same(pose, original);
    shot = flatShot();
    const std::array<BattleCameraTarget, 1> bad{{
        {{(std::numeric_limits<float>::quiet_NaN)(), 0.0f, 0.0f}, 1.0f}}};
    CHECK(!frameBattleTargets(shot, bad, 1.0f, pose, &error));
    same(pose, original);
    CHECK(!frameBattleTargets(shot, pair, 0.0f, pose, &error));
    same(pose, original);
}

void checkCutsBlendsAndMovingTargets() {
    BattleCameraRig rig;
    BattleCameraPose pose;
    std::string error;
    CHECK(!rig.pose(pose, &error));
    const std::array<BattleCameraTarget, 1> center{{
        {{0.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraShot front = flatShot();
    front.minDistance = 4.0f;
    CHECK(rig.cut(front, center, 16.0f / 9.0f, pose, &error));
    CHECK(near(pose.position, {0.0f, 0.0f, 4.0f}));
    BattleCameraShot side = front;
    side.yawRadians = 1.57079632679f;
    CHECK(rig.blendTo(side, center, 16.0f / 9.0f, 1.0f, pose, &error));
    CHECK(near(pose.blendProgress, 0.0f));
    CHECK(near(pose.position, {0.0f, 0.0f, 4.0f}));
    CHECK(rig.step(center, 16.0f / 9.0f, 0.25f, false, pose, &error));
    CHECK(pose.position.x > 0.0f && pose.position.z < 4.0f);
    CHECK(rig.step(center, 16.0f / 9.0f, 0.25f, false, pose, &error));
    CHECK(near(pose.blendProgress, 0.5f));
    CHECK(near(pose.yawRadians, 0.78539816339f));
    CHECK(near(pose.distance, 4.0f));
    CHECK(rig.step(center, 16.0f / 9.0f, 0.25f, false, pose, &error));
    CHECK(rig.step(center, 16.0f / 9.0f, 0.25f, false, pose, &error));
    CHECK(near(pose.position, {4.0f, 0.0f, 0.0f}));
    const std::array<BattleCameraTarget, 1> moved{{
        {{3.0f, 0.0f, 2.0f}, 0.0f}}};
    CHECK(rig.step(moved, 16.0f / 9.0f, 0.0f, false, pose, &error));
    CHECK(near(pose.lookTarget, {3.0f, 0.0f, 2.0f}));
    CHECK(near(pose.position, {7.0f, 0.0f, 2.0f}));
    // Opposite-side shots must keep a nonzero boom at their midpoint.
    side.yawRadians = 3.14159265359f;
    CHECK(rig.blendTo(side, moved, 1.0f, 1.0f, pose, &error));
    CHECK(rig.step(moved, 1.0f, 0.25f, false, pose, &error));
    CHECK(rig.step(moved, 1.0f, 0.25f, false, pose, &error));
    CHECK(pose.distance >= 4.0f - 1.0e-4f);
    CHECK(near(glm::length(pose.position - pose.lookTarget), pose.distance));
    const BattleCameraPose interrupted = pose;
    CHECK(rig.blendTo(front, moved, 1.0f, 0.5f, pose, &error));
    CHECK(near(pose.position, interrupted.position));
    CHECK(near(pose.lookTarget, interrupted.lookTarget));
    CHECK(rig.cut(front, moved, 1.0f, pose, &error));
    CHECK(rig.step(moved, 1.0f, 0.25f, false, pose, &error));
    CHECK(near(pose.position, {3.0f, 0.0f, 6.0f}));
    CHECK(near(pose.blendProgress, 1.0f));
}

void checkShakeAndCleanup() {
    const std::array<BattleCameraTarget, 1> center{{
        {{0.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraShot shot = flatShot();
    shot.minDistance = 4.0f;
    BattleCameraRig a;
    BattleCameraRig b;
    BattleCameraPose first;
    BattleCameraPose split;
    std::string error;
    CHECK(a.cut(shot, center, 1.0f, first, &error));
    CHECK(b.cut(shot, center, 1.0f, split, &error));
    BattleCameraShake shake;
    shake.amplitudeWorld = 0.2f;
    shake.frequencyHz = 7.0f;
    shake.durationSeconds = 1.0f;
    shake.seed = 19u;
    CHECK(a.startShake(shake, &error));
    CHECK(b.startShake(shake, &error));
    CHECK(a.step(center, 1.0f, 0.1f, false, first, &error));
    CHECK(b.step(center, 1.0f, 0.05f, false, split, &error));
    CHECK(b.step(center, 1.0f, 0.05f, false, split, &error));
    CHECK(near(first.shakeOffset, split.shakeOffset));
    CHECK(glm::length(first.shakeOffset) <= 0.2f + 1.0e-6f);
    CHECK(near(first.position, first.lookTarget + glm::vec3(0, 0, 4)));
    CHECK(a.stopShake(first, &error));
    CHECK(first.shakeOffset == glm::vec3(0.0f));
    CHECK(near(first.position, {0.0f, 0.0f, 4.0f}));
    CHECK(b.step(center, 1.0f, 0.05f, true, split, &error));
    CHECK(split.shakeOffset == glm::vec3(0.0f));
    CHECK(b.step(center, 1.0f, 0.05f, false, split, &error));
    CHECK(split.shakeOffset == glm::vec3(0.0f));
    CHECK(a.startShake(shake, &error));
    CHECK(a.cut(shot, center, 1.0f, first, &error));
    CHECK(a.step(center, 1.0f, 0.1f, false, first, &error));
    CHECK(first.shakeOffset == glm::vec3(0.0f));
    a.reset();
    const BattleCameraPose prior = first;
    CHECK(!a.pose(first, &error));
    same(first, prior);
}

void checkShakeKeepsTargetsInFrame() {
    const std::array<BattleCameraTarget, 2> pair{{
        {{-2.0f, 0.0f, 0.0f}, 0.0f},
        {{2.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraRig rig;
    BattleCameraPose pose;
    std::string error;
    CHECK(rig.cut(flatShot(), pair, 1.0f, pose, &error));
    BattleCameraShake shake;
    shake.amplitudeWorld = 1.0f;
    shake.frequencyHz = 7.0f;
    shake.durationSeconds = 1.0f;
    shake.seed = 19u;
    CHECK(rig.startShake(shake, &error));
    CHECK(rig.step(pair, 1.0f, 0.1f, false, pose, &error));
    CHECK(glm::length(pose.shakeOffset) > 0.0f);
    for (const BattleCameraTarget& target : pair) {
        const glm::vec3 ndc = project(pose, target.position);
        CHECK(std::abs(ndc.x) <= 1.0f + 1.0e-4f);
        CHECK(std::abs(ndc.y) <= 1.0f + 1.0e-4f);
    }
}

void checkFailureAndOwnership() {
    BattleCameraRig rig;
    const std::array<BattleCameraTarget, 1> center{{
        {{0.0f, 0.0f, 0.0f}, 0.0f}}};
    BattleCameraPose pose;
    std::string error;
    CHECK(rig.cut(flatShot(), center, 1.0f, pose, &error));
    const BattleCameraPose prior = pose;
    CHECK(!rig.step(center, 1.0f, -0.1f, false, pose, &error));
    CHECK(!error.empty());
    same(pose, prior);
    CHECK(!rig.blendTo(flatShot(), center, 1.0f, -1.0f, pose, &error));
    same(pose, prior);
    BattleCameraShake invalid;
    invalid.durationSeconds = 0.0f;
    CHECK(!rig.startShake(invalid, &error));
    BattleCameraPose current;
    CHECK(rig.pose(current, &error));
    same(current, prior);
    bool wrongThreadRejected = false;
    std::thread worker([&] {
        try {
            BattleCameraPose unused;
            (void)rig.pose(unused);
        } catch (const std::logic_error&) {
            wrongThreadRejected = true;
        }
    });
    worker.join();
    CHECK(wrongThreadRejected);
}

} // namespace

int main() {
    try {
        checkAnalyticFraming();
        checkInvalidFramingPreservesOutput();
        checkCutsBlendsAndMovingTargets();
        checkShakeAndCleanup();
        checkShakeKeepsTargetsInFrame();
        checkFailureAndOwnership();
        std::cout << "[PASS] battle camera contracts\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] battle camera contracts: " << exception.what() << '\n';
        return 1;
    }
}
