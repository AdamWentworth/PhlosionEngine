// Camera3D.cpp

#include "Camera3D.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDefaultCameraY = 12.0f;
constexpr float kDefaultCameraZ = 12.0f;
constexpr float kDefaultTargetY = -1.0f;
constexpr float kMinZoomDistance = 1.75f;
constexpr float kMinOrbitPitch = -1.45f;
constexpr float kMaxOrbitPitch = 1.45f;
const float kMaxZoomDistance = std::sqrt(
    (kDefaultCameraY - kDefaultTargetY) * (kDefaultCameraY - kDefaultTargetY) +
    kDefaultCameraZ * kDefaultCameraZ);

} // namespace

Camera3D::Camera3D(float fovDeg, float aspect, float nearPlane, float farPlane)
    : position(0.0f, kDefaultCameraY, kDefaultCameraZ),
      target(0.0f, kDefaultTargetY, 0.0f),
      upVector(0.0f, 1.0f, 0.0f),
      fov(glm::radians(fovDeg)),
      aspectRatio(aspect),
      nearZ(nearPlane),
      farZ(farPlane)
{}

void Camera3D::setPosition(const glm::vec3& pos) {
    position = pos;
}

void Camera3D::lookAt(const glm::vec3& tgt, const glm::vec3& up) {
    target = tgt;
    upVector = up;
}

void Camera3D::move(const glm::vec3& delta) {
    position += delta;
    target += delta;
}

void Camera3D::zoom(float delta) {
    glm::vec3 offset = position - target;
    const float dist = glm::length(offset);
    if (dist < 1e-5f) return;

    const glm::vec3 dirToCamera = offset / dist;
    const float newDist =
        std::clamp(dist - delta, kMinZoomDistance, kMaxZoomDistance);
    position = target + dirToCamera * newDist;
}

void Camera3D::panPlanar(float screenDx, float screenDy, float scale) {
    glm::vec3 forward = target - position;
    forward.y = 0.0f;
    const float forwardLen = glm::length(forward);
    if (forwardLen <= 1e-5f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward /= forwardLen;
    }

    glm::vec3 right = glm::cross(forward, upVector);
    right.y = 0.0f;
    const float rightLen = glm::length(right);
    if (rightLen <= 1e-5f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right /= rightLen;
    }

    const glm::vec3 delta =
        (-right * screenDx + forward * screenDy) * scale;
    move(delta);
}

void Camera3D::orbit(float yawDeltaRad, float pitchDeltaRad) {
    glm::vec3 offset = position - target;
    float r = glm::length(offset);
    if (r < 1e-5f) return;

    // Current spherical angles (Y up)
    float yaw   = std::atan2(offset.x, offset.z);
    float pitch = std::asin(std::clamp(offset.y / r, -1.0f, 1.0f));

    yaw   += yawDeltaRad;
    pitch += pitchDeltaRad;

    // Allow orbiting down to target/ground level while still stopping short of
    // a full vertical flip that would invert the camera controls.
    pitch = std::clamp(pitch, kMinOrbitPitch, kMaxOrbitPitch);

    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float sy = std::sin(yaw);
    float cy = std::cos(yaw);

    glm::vec3 newOffset;
    newOffset.x = r * sy * cp;
    newOffset.z = r * cy * cp;
    newOffset.y = r * sp;

    position = target + newOffset;
}

glm::mat4 Camera3D::getViewMatrix() const {
    return glm::lookAt(position, target, upVector);
}

glm::mat4 Camera3D::getProjectionMatrix() const {
    return glm::perspective(fov, aspectRatio, nearZ, farZ);
}

glm::vec3 Camera3D::getDirection() const {
    return glm::normalize(target - position);
}

void Camera3D::setAspectRatio(float aspect) {
    if (aspect > 0.0f) {
        aspectRatio = aspect;
    }
}
