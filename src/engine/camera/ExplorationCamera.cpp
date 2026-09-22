#include "ExplorationCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace engine::camera {
namespace {
bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
void succeed(std::string* error) { if (error) error->clear(); }
bool range(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}
bool bounded(const glm::dvec3& value, double limit) {
    return range(value.x, -limit, limit) && range(value.y, -limit, limit) && range(value.z, -limit, limit);
}
bool bounded(const glm::vec2& value, float limit) {
    return range(value.x, -limit, limit) && range(value.y, -limit, limit);
}
bool validSettings(const ExplorationCameraSettings& s) {
    return bounded(glm::dvec3(s.pivotOffset), 100)
        && range(s.initialYawRadians, -1e6, 1e6)
        && range(s.minPitchRadians, -1.45f, 1.45f) && range(s.maxPitchRadians, s.minPitchRadians, 1.45f)
        && range(s.initialPitchRadians, s.minPitchRadians, s.maxPitchRadians)
        && range(s.minDistance, .1f, 1000) && range(s.maxDistance, s.minDistance, 1000)
        && range(s.desiredDistance, s.minDistance, s.maxDistance)
        && range(s.followHalfLifeSeconds, 0, 10) && range(s.recoveryHalfLifeSeconds, 0, 10)
        && range(s.probeRadius, .01f, 2) && range(s.safetyMargin, 0, .5f)
        && range(s.verticalFovDegrees, 1, 150) && range(s.nearPlane, .001f, 10)
        && range(s.farPlane, s.nearPlane, 100000) && s.farPlane > s.nearPlane;
}
float wrappedYaw(double yaw) {
    constexpr double pi = std::numbers::pi;
    double result = std::fmod(yaw + pi, 2 * pi);
    if (result < 0) result += 2 * pi;
    const float published = static_cast<float>(result - pi);
    // Float rounding near the positive endpoint must not publish +pi.
    constexpr float floatPi = std::numbers::pi_v<float>;
    return published >= floatPi ? -floatPi : published;
}
double damping(float dt, float halfLife) {
    if (dt == 0) return 0;
    if (halfLife == 0) return 1;
    return -std::expm1(-std::numbers::ln2 * static_cast<double>(dt) / halfLife);
}
} // namespace

ExplorationCameraRig::ExplorationCameraRig(const ExplorationCameraSettings& settings)
    : settings_(settings), owner_(std::this_thread::get_id()) {
    if (!validSettings(settings_)) throw std::invalid_argument("Invalid exploration camera settings; see EXPLORATION_CAMERA.md ranges");
}
ExplorationCameraRig::~ExplorationCameraRig() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
}
void ExplorationCameraRig::checkOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("ExplorationCameraRig used outside its owner thread");
}
const ExplorationCameraSettings& ExplorationCameraRig::settings() const {
    checkOwner();
    return settings_;
}
bool ExplorationCameraRig::pose(ExplorationCameraPose& out, std::string* error) const {
    checkOwner();
    if (!pose_) return fail(error, "Exploration camera has not been initialized");
    out = *pose_;
    succeed(error);
    return true;
}
bool ExplorationCameraRig::reset(const glm::vec3& targetPosition, float aspectRatio, ExplorationCameraPose& out,
                                  const collision::CollisionWorld* world, std::string* error) {
    checkOwner();
    ExplorationCameraFrame frame;
    frame.targetPosition = targetPosition;
    frame.aspectRatio = aspectRatio;
    return evaluate(frame, true, out, world, error);
}
bool ExplorationCameraRig::step(const ExplorationCameraFrame& frame, ExplorationCameraPose& out,
                                 const collision::CollisionWorld* world, std::string* error) {
    checkOwner();
    return evaluate(frame, false, out, world, error);
}
bool ExplorationCameraRig::evaluate(const ExplorationCameraFrame& frame, bool resetState, ExplorationCameraPose& out,
                                     const collision::CollisionWorld* world, std::string* error) {
    // Check the borrowed world's ownership even when frame validation would fail.
    if (world) (void)world->colliderCount();
    if (!bounded(glm::dvec3(frame.targetPosition), 10000) || !range(frame.aspectRatio, .25f, 8)
        || !range(frame.deltaSeconds, 0, .25f) || !bounded(frame.lookDeltaRadians, 100)
        || !bounded(frame.lookRateRadiansPerSecond, 20) || !range(frame.zoomDelta, -1000, 1000)
        || !range(frame.zoomRate, -1000, 1000)) return fail(error, "Invalid exploration camera frame");

    const bool snap = resetState || !pose_;
    const auto& s = settings_;
    ExplorationCameraPose candidate;
    const double dt = frame.deltaSeconds;
    candidate.yawRadians = wrappedYaw((snap ? s.initialYawRadians : pose_->yawRadians)
        + static_cast<double>(frame.lookDeltaRadians.x) + frame.lookRateRadiansPerSecond.x * dt);
    candidate.pitchRadians = static_cast<float>(std::clamp((snap ? s.initialPitchRadians : pose_->pitchRadians)
        + static_cast<double>(frame.lookDeltaRadians.y) + frame.lookRateRadiansPerSecond.y * dt,
        static_cast<double>(s.minPitchRadians), static_cast<double>(s.maxPitchRadians)));
    candidate.desiredDistance = static_cast<float>(std::clamp((snap ? s.desiredDistance : pose_->desiredDistance)
        + static_cast<double>(frame.zoomDelta) + frame.zoomRate * dt,
        static_cast<double>(s.minDistance), static_cast<double>(s.maxDistance)));
    const glm::dvec3 targetPivot = glm::dvec3(frame.targetPosition) + glm::dvec3(s.pivotOffset);
    if (!bounded(targetPivot, 10000)) return fail(error, "Exploration camera target pivot exceeds position bounds");
    const glm::dvec3 followedPivot = snap ? targetPivot : glm::dvec3(pose_->pivot)
        + (targetPivot - glm::dvec3(pose_->pivot)) * damping(frame.deltaSeconds, s.followHalfLifeSeconds);
    candidate.pivot = glm::vec3(followedPivot);
    // Use the same representable origin for the probe and published eye.
    const glm::dvec3 pivot(candidate.pivot);
    const double yaw = candidate.yawRadians;
    const double pitch = candidate.pitchRadians;
    const glm::dvec3 boom{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const glm::dvec3 desiredEye = pivot + boom * static_cast<double>(candidate.desiredDistance);
    if (!bounded(pivot, 10000) || !bounded(desiredEye, 10000))
        return fail(error, "Exploration camera desired eye exceeds position bounds");
    candidate.forward = glm::vec3(-boom);
    candidate.up = glm::vec3(-std::sin(yaw) * std::sin(pitch), std::cos(pitch), -std::cos(yaw) * std::sin(pitch));
    float allowed = candidate.desiredDistance;
    if (world) {
        const collision::Capsule probe{candidate.pivot, s.probeRadius, 0};
        const collision::QueryFilter filter{s.layerMask, collision::SensorPolicy::Exclude, s.ignore};
        if (!world->sweepCapsule(probe, glm::vec3(boom * static_cast<double>(candidate.desiredDistance)),
                                  candidate.obstruction, filter, error)) return false;
        if (candidate.obstruction) {
            candidate.obstructed = true;
            candidate.originBlocked = candidate.obstruction->initialOverlap;
            allowed = candidate.originBlocked ? 0 : std::clamp(candidate.obstruction->fraction * candidate.desiredDistance
                - s.safetyMargin, 0.0f, candidate.desiredDistance);
        }
    }
    candidate.actualDistance = snap || allowed <= pose_->actualDistance ? allowed
        : static_cast<float>(pose_->actualDistance + (static_cast<double>(allowed) - pose_->actualDistance)
            * damping(frame.deltaSeconds, s.recoveryHalfLifeSeconds));
    const glm::dvec3 eye = pivot + boom * static_cast<double>(candidate.actualDistance);
    candidate.position = glm::vec3(eye);
    candidate.lookTarget = candidate.position + candidate.forward;
    if (!bounded(eye, 10000) || !bounded(glm::dvec3(candidate.lookTarget), 10000))
        return fail(error, "Exploration camera final pose exceeds position bounds");
    candidate.aspectRatio = frame.aspectRatio;
    candidate.verticalFovDegrees = s.verticalFovDegrees;
    candidate.nearPlane = s.nearPlane;
    candidate.farPlane = s.farPlane;
    pose_ = candidate;
    out = candidate;
    succeed(error);
    return true;
}

bool cameraRelativeMove(const glm::vec2& screenMove, float yawRadians, glm::vec2& outWorldXZ, std::string* error) {
    if (!std::isfinite(screenMove.x) || !std::isfinite(screenMove.y) || !range(yawRadians, -1e6, 1e6))
        return fail(error, "Camera-relative intent and yaw must be finite; yaw magnitude must be <= 1000000");
    double x = screenMove.x;
    double y = screenMove.y;
    const double length = std::hypot(x, y);
    if (length > 1) { x /= length; y /= length; }
    const double sine = std::sin(static_cast<double>(yawRadians));
    const double cosine = std::cos(static_cast<double>(yawRadians));
    outWorldXZ = glm::vec2(cosine * x + sine * y, -sine * x + cosine * y);
    succeed(error);
    return true;
}
} // namespace engine::camera
