#pragma once

#include "engine/camera/BattleCamera.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine::exploration_fixture {

struct BattleFreeCameraInput {
    // Local right, world up, and view-forward movement axes.
    glm::vec3 move{0.0f};
    glm::vec2 look{0.0f};
    float dolly = 0.0f;
    bool fast = false;
};

class BattlePreviewFreeCamera {
public:
    void reset() { active_ = false; }
    bool active() const noexcept { return active_; }

    void step(const camera::BattleCameraPose& scripted,
              const BattleFreeCameraInput& input, float deltaSeconds) {
        const bool hasInput = glm::dot(input.move, input.move) > 0.0f ||
                              glm::dot(input.look, input.look) > 0.0f ||
                              input.dolly != 0.0f;
        if (!active_ && !hasInput) return;
        if (!active_) {
            pose_ = scripted;
            yaw_ = std::atan2(scripted.forward.x, -scripted.forward.z);
            pitch_ = std::asin(glm::clamp(scripted.forward.y, -1.0f, 1.0f));
            active_ = true;
        }
        yaw_ += input.look.x;
        pitch_ = glm::clamp(pitch_ + input.look.y, -1.45f, 1.45f);
        pose_.forward = glm::normalize(glm::vec3(
            std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
            -std::cos(yaw_) * std::cos(pitch_)));
        pose_.right = glm::normalize(glm::cross(pose_.forward, glm::vec3(0, 1, 0)));
        pose_.up = glm::normalize(glm::cross(pose_.right, pose_.forward));
        glm::vec3 movement = pose_.right * input.move.x +
                             glm::vec3(0, 1, 0) * input.move.y +
                             pose_.forward * input.move.z;
        if (glm::dot(movement, movement) > 1.0f) movement = glm::normalize(movement);
        pose_.position += movement * (input.fast ? 12.0f : 4.0f) *
                          glm::clamp(deltaSeconds, 0.0f, 0.05f);
        pose_.position += pose_.forward * input.dolly * 1.25f;
        pose_.distance = std::max(1.0f, pose_.distance);
        pose_.lookTarget = pose_.position + pose_.forward * pose_.distance;
        pose_.yawRadians = yaw_;
        pose_.pitchRadians = pitch_;
        pose_.shakeOffset = glm::vec3(0.0f);
        pose_.blendProgress = 1.0f;
    }

    camera::BattleCameraPose pose(float aspect) const {
        auto result = pose_;
        result.aspectRatio = aspect;
        return result;
    }

private:
    bool active_ = false;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    camera::BattleCameraPose pose_;
};

} // namespace engine::exploration_fixture
