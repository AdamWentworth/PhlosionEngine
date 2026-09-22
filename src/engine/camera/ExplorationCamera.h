#pragma once

#include "engine/collision/CollisionWorld.h"

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <thread>

namespace engine::camera {

struct ExplorationCameraSettings {
    glm::vec3 pivotOffset{0, 1, 0};
    float initialYawRadians = 0;
    float initialPitchRadians = .35f;
    float minPitchRadians = -1.4f;
    float maxPitchRadians = 1.4f;
    float desiredDistance = 6;
    float minDistance = .5f;
    float maxDistance = 30;
    float followHalfLifeSeconds = .12f;
    float recoveryHalfLifeSeconds = .15f;
    float probeRadius = .2f;
    float safetyMargin = .03f;
    std::uint32_t layerMask = 0xffffffffu;
    collision::ColliderId ignore;
    float verticalFovDegrees = 45;
    float nearPlane = .1f;
    float farPlane = 1000;
};

struct ExplorationCameraFrame {
    glm::vec3 targetPosition{0};
    float aspectRatio = 16.0f / 9.0f;
    float deltaSeconds = 0;
    glm::vec2 lookDeltaRadians{0}; // X yaw, Y pitch; applied once, including at dt=0.
    glm::vec2 lookRateRadiansPerSecond{0};
    float zoomDelta = 0; // Positive increases distance.
    float zoomRate = 0;
};

struct ExplorationCameraPose {
    glm::vec3 pivot{0};
    glm::vec3 position{0};
    glm::vec3 forward{0, 0, -1};
    glm::vec3 up{0, 1, 0}; // Orthonormal camera up, not fixed world up.
    glm::vec3 lookTarget{0, 0, -1}; // Always position + forward, even at zero boom length.
    float yawRadians = 0;
    float pitchRadians = 0;
    float desiredDistance = 0;
    float actualDistance = 0;
    float aspectRatio = 16.0f / 9.0f;
    float verticalFovDegrees = 45;
    float nearPlane = .1f;
    float farPlane = 1000;
    bool obstructed = false; // This frame's desired radial probe hit; independent of recovery lag.
    bool originBlocked = false; // Positive penetration at the probe origin.
    std::optional<collision::Hit> obstruction; // Copied metadata; no borrowed world storage.
};

// Owner-thread runtime; immutable copied settings. See docs/EXPLORATION_CAMERA.md.
class ExplorationCameraRig {
public:
    explicit ExplorationCameraRig(const ExplorationCameraSettings& settings = {});
    ~ExplorationCameraRig();
    ExplorationCameraRig(const ExplorationCameraRig&) = delete;
    ExplorationCameraRig& operator=(const ExplorationCameraRig&) = delete;
    ExplorationCameraRig(ExplorationCameraRig&&) = delete;
    ExplorationCameraRig& operator=(ExplorationCameraRig&&) = delete;

    const ExplorationCameraSettings& settings() const;
    bool pose(ExplorationCameraPose& out, std::string* error = nullptr) const;
    bool reset(const glm::vec3& targetPosition, float aspectRatio, ExplorationCameraPose& out,
               const collision::CollisionWorld* world = nullptr, std::string* error = nullptr);
    bool step(const ExplorationCameraFrame& frame, ExplorationCameraPose& out,
              const collision::CollisionWorld* world = nullptr, std::string* error = nullptr);

private:
    void checkOwner() const;
    bool evaluate(const ExplorationCameraFrame& frame, bool reset, ExplorationCameraPose& out,
                  const collision::CollisionWorld* world, std::string* error);
    const ExplorationCameraSettings settings_;
    const std::thread::id owner_;
    std::optional<ExplorationCameraPose> pose_;
};

// X right, Y down/back. Returns world X/Z, preserves analog magnitude <= 1.
bool cameraRelativeMove(const glm::vec2& screenMove, float yawRadians, glm::vec2& outWorldXZ,
                        std::string* error = nullptr);

} // namespace engine::camera
