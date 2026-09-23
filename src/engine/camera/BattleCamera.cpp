#include "BattleCamera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace engine::camera {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void succeed(std::string* error) {
    if (error) error->clear();
}

bool range(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

bool bounded(const glm::vec3& value, double limit) {
    return range(value.x, -limit, limit) &&
           range(value.y, -limit, limit) &&
           range(value.z, -limit, limit);
}

bool validShot(const BattleCameraShot& shot) {
    return range(shot.yawRadians, -1e6, 1e6) &&
           range(shot.pitchRadians, -1.4, 1.4) &&
           bounded(shot.focusOffset, 100.0) &&
           range(shot.verticalFovDegrees, 1.0, 150.0) &&
           range(shot.marginFraction, 0.0, 0.4) &&
           range(shot.minDistance, 0.1, 1000.0) &&
           range(shot.maxDistance, shot.minDistance, 1000.0) &&
           range(shot.nearPlane, 0.001, 10.0) &&
           range(shot.farPlane, shot.nearPlane, 100000.0) &&
           shot.farPlane > shot.nearPlane;
}

glm::dvec3 backDirection(double yaw, double pitch) {
    return {std::sin(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::cos(yaw) * std::cos(pitch)};
}

bool makePose(const glm::dvec3& focus, double yaw, double pitch,
              double distance, float aspect, float fov, float nearPlane,
              float farPlane, BattleCameraPose& out) {
    const glm::dvec3 back = backDirection(yaw, pitch);
    const glm::dvec3 forward = -back;
    const glm::dvec3 right = glm::normalize(
        glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0)));
    const glm::dvec3 up = glm::normalize(glm::cross(right, forward));
    const glm::dvec3 eye = focus + distance * back;
    if (!range(eye.x, -10000.0, 10000.0) ||
        !range(eye.y, -10000.0, 10000.0) ||
        !range(eye.z, -10000.0, 10000.0) ||
        !range(focus.x, -10000.0, 10000.0) ||
        !range(focus.y, -10000.0, 10000.0) ||
        !range(focus.z, -10000.0, 10000.0)) return false;
    out.position = glm::vec3(eye);
    out.lookTarget = glm::vec3(focus);
    out.forward = glm::vec3(forward);
    out.up = glm::vec3(up);
    out.right = glm::vec3(right);
    out.shakeOffset = glm::vec3(0.0f);
    out.yawRadians = static_cast<float>(std::remainder(yaw, 2.0 * std::numbers::pi));
    out.pitchRadians = static_cast<float>(pitch);
    out.distance = static_cast<float>(distance);
    out.aspectRatio = aspect;
    out.verticalFovDegrees = fov;
    out.nearPlane = nearPlane;
    out.farPlane = farPlane;
    out.blendProgress = 1.0f;
    return true;
}

bool interpolatePose(const BattleCameraPose& from,
                     const BattleCameraPose& to, float progress,
                     BattleCameraPose& out) {
    const double t = static_cast<double>(progress);
    const double eased = t * t * (3.0 - 2.0 * t);
    const glm::dvec3 focus = glm::mix(glm::dvec3(from.lookTarget),
                                      glm::dvec3(to.lookTarget), eased);
    const double yawDifference = std::remainder(
        static_cast<double>(to.yawRadians) - from.yawRadians,
        2.0 * std::numbers::pi);
    const double yaw = from.yawRadians + yawDifference * eased;
    const double pitch = glm::mix(static_cast<double>(from.pitchRadians),
                                  static_cast<double>(to.pitchRadians), eased);
    const double distance = glm::mix(static_cast<double>(from.distance),
                                     static_cast<double>(to.distance), eased);
    const float fov = glm::mix(from.verticalFovDegrees,
                               to.verticalFovDegrees, static_cast<float>(eased));
    const float nearPlane = glm::mix(from.nearPlane, to.nearPlane,
                                     static_cast<float>(eased));
    const float farPlane = glm::mix(from.farPlane, to.farPlane,
                                    static_cast<float>(eased));
    if (!makePose(focus, yaw, pitch, distance, to.aspectRatio,
                  fov, nearPlane, farPlane, out)) return false;
    out.blendProgress = progress;
    return true;
}

glm::vec3 shakeOffset(const BattleCameraPose& base,
                      const BattleCameraShake& shake,
                      float elapsedSeconds) {
    const double phase = 2.0 * std::numbers::pi *
        static_cast<double>(shake.seed % 1024u) / 1024.0;
    const double time = elapsedSeconds;
    const double envelope = (std::max)(
        0.0, 1.0 - time / static_cast<double>(shake.durationSeconds));
    const double amplitude =
        static_cast<double>(shake.amplitudeWorld) * envelope / std::numbers::sqrt2;
    const float x = static_cast<float>(amplitude *
        std::sin(2.0 * std::numbers::pi * shake.frequencyHz * time + phase));
    const float y = static_cast<float>(amplitude *
        std::sin(2.0 * std::numbers::pi * shake.frequencyHz * 1.37 * time + phase * 0.7));
    return base.right * x + base.up * y;
}

glm::vec3 clampShakeToFraming(const BattleCameraPose& base,
                              std::span<const BattleCameraTarget> targets,
                              const glm::vec3& proposed) {
    const double tangentVertical = std::tan(
        static_cast<double>(base.verticalFovDegrees) *
        std::numbers::pi / 360.0);
    const double tangentHorizontal = tangentVertical * base.aspectRatio;
    double horizontalSlack = (std::numeric_limits<double>::max)();
    double verticalSlack = (std::numeric_limits<double>::max)();
    for (const BattleCameraTarget& target : targets) {
        const glm::dvec3 relative = glm::dvec3(target.position) -
                                     glm::dvec3(base.lookTarget);
        const double nearestDepth = base.distance +
            glm::dot(relative, glm::dvec3(base.forward)) - target.radius;
        const double horizontal = std::abs(glm::dot(
            relative, glm::dvec3(base.right))) + target.radius;
        const double vertical = std::abs(glm::dot(
            relative, glm::dvec3(base.up))) + target.radius;
        horizontalSlack = (std::min)(horizontalSlack,
            nearestDepth * tangentHorizontal - horizontal);
        verticalSlack = (std::min)(verticalSlack,
            nearestDepth * tangentVertical - vertical);
    }
    horizontalSlack = (std::max)(0.0, horizontalSlack);
    verticalSlack = (std::max)(0.0, verticalSlack);
    const float x = static_cast<float>(std::clamp(
        static_cast<double>(glm::dot(proposed, base.right)),
        -horizontalSlack, horizontalSlack));
    const float y = static_cast<float>(std::clamp(
        static_cast<double>(glm::dot(proposed, base.up)),
        -verticalSlack, verticalSlack));
    return base.right * x + base.up * y;
}

} // namespace

bool frameBattleTargets(const BattleCameraShot& shot,
                        std::span<const BattleCameraTarget> targets,
                        float aspectRatio, BattleCameraPose& out,
                        std::string* error) {
    if (!validShot(shot) || !range(aspectRatio, 0.25, 8.0) ||
        targets.empty() || targets.size() > 8u)
        return fail(error, "invalid battle camera shot, aspect or target count");
    glm::dvec3 minimum((std::numeric_limits<double>::max)());
    glm::dvec3 maximum(-(std::numeric_limits<double>::max)());
    for (const BattleCameraTarget& target : targets) {
        if (!bounded(target.position, 10000.0) ||
            !range(target.radius, 0.0, 1000.0))
            return fail(error, "invalid battle camera target");
        const glm::dvec3 point(target.position);
        minimum = glm::min(minimum, point - glm::dvec3(target.radius));
        maximum = glm::max(maximum, point + glm::dvec3(target.radius));
    }
    const glm::dvec3 focus = (minimum + maximum) * 0.5 +
                             glm::dvec3(shot.focusOffset);
    const glm::dvec3 back = backDirection(shot.yawRadians, shot.pitchRadians);
    const glm::dvec3 forward = -back;
    const glm::dvec3 right = glm::normalize(
        glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0)));
    const glm::dvec3 up = glm::normalize(glm::cross(right, forward));
    const double tangentVertical = std::tan(
        static_cast<double>(shot.verticalFovDegrees) *
        std::numbers::pi / 360.0);
    const double usable = 1.0 - shot.marginFraction;
    const double tangentHorizontal = tangentVertical * aspectRatio;
    double required = shot.minDistance;
    double upper = shot.maxDistance;
    for (const BattleCameraTarget& target : targets) {
        const glm::dvec3 relative = glm::dvec3(target.position) - focus;
        const double depth = glm::dot(relative, back);
        const double horizontal = std::abs(glm::dot(relative, right));
        const double vertical = std::abs(glm::dot(relative, up));
        const double radius = target.radius;
        required = (std::max)(required, depth + radius + shot.nearPlane);
        required = (std::max)(required, depth + radius +
            (horizontal + radius) / (tangentHorizontal * usable));
        required = (std::max)(required, depth + radius +
            (vertical + radius) / (tangentVertical * usable));
        upper = (std::min)(upper, shot.farPlane + depth - radius);
    }
    if (!std::isfinite(required) || required > upper)
        return fail(error, "battle targets cannot fit within the shot distance or clip planes");
    BattleCameraPose candidate;
    if (!makePose(focus, shot.yawRadians, shot.pitchRadians, required,
                  aspectRatio, shot.verticalFovDegrees, shot.nearPlane,
                  shot.farPlane, candidate))
        return fail(error, "battle camera pose exceeds the supported world bounds");
    out = candidate;
    succeed(error);
    return true;
}

BattleCameraRig::BattleCameraRig() : owner_(std::this_thread::get_id()) {}

BattleCameraRig::~BattleCameraRig() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
}

void BattleCameraRig::checkOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("BattleCameraRig used outside its owner thread");
}

bool BattleCameraRig::pose(BattleCameraPose& out, std::string* error) const {
    checkOwner();
    if (!pose_) return fail(error, "battle camera has not been initialized");
    out = *pose_;
    succeed(error);
    return true;
}

bool BattleCameraRig::cut(const BattleCameraShot& shot,
                          std::span<const BattleCameraTarget> targets,
                          float aspectRatio, BattleCameraPose& out,
                          std::string* error) {
    checkOwner();
    BattleCameraPose candidate;
    if (!frameBattleTargets(shot, targets, aspectRatio, candidate, error))
        return false;
    shot_ = shot;
    basePose_ = candidate;
    pose_ = candidate;
    blendSource_.reset();
    blendDuration_ = 0.0f;
    blendElapsed_ = 0.0f;
    shake_.reset();
    shakeElapsed_ = 0.0f;
    out = candidate;
    succeed(error);
    return true;
}

bool BattleCameraRig::blendTo(const BattleCameraShot& shot,
                              std::span<const BattleCameraTarget> targets,
                              float aspectRatio, float durationSeconds,
                              BattleCameraPose& out, std::string* error) {
    checkOwner();
    if (!basePose_) return fail(error, "battle camera requires an initial cut");
    if (!range(durationSeconds, 0.0, 10.0))
        return fail(error, "invalid battle camera blend duration");
    if (durationSeconds == 0.0f)
        return cut(shot, targets, aspectRatio, out, error);
    BattleCameraPose goal;
    if (!frameBattleTargets(shot, targets, aspectRatio, goal, error))
        return false;
    BattleCameraPose candidate = *basePose_;
    candidate.aspectRatio = aspectRatio;
    candidate.blendProgress = 0.0f;
    shot_ = shot;
    blendSource_ = *basePose_;
    blendDuration_ = durationSeconds;
    blendElapsed_ = 0.0f;
    shake_.reset();
    shakeElapsed_ = 0.0f;
    basePose_ = candidate;
    pose_ = candidate;
    out = candidate;
    succeed(error);
    return true;
}

bool BattleCameraRig::step(std::span<const BattleCameraTarget> targets,
                           float aspectRatio, float deltaSeconds,
                           bool reducedMotion, BattleCameraPose& out,
                           std::string* error) {
    checkOwner();
    if (!shot_) return fail(error, "battle camera requires an initial cut");
    if (!range(deltaSeconds, 0.0, 0.25))
        return fail(error, "invalid battle camera frame delta");
    BattleCameraPose goal;
    if (!frameBattleTargets(*shot_, targets, aspectRatio, goal, error))
        return false;
    BattleCameraPose base = goal;
    float nextBlendElapsed = blendElapsed_;
    bool blendComplete = true;
    if (blendSource_) {
        nextBlendElapsed = (std::min)(blendDuration_, blendElapsed_ + deltaSeconds);
        blendComplete = nextBlendElapsed >= blendDuration_;
        if (!blendComplete &&
            !interpolatePose(*blendSource_, goal,
                             nextBlendElapsed / blendDuration_, base))
            return fail(error, "blended battle camera pose exceeds world bounds");
    }
    BattleCameraPose candidate = base;
    float nextShakeElapsed = shakeElapsed_;
    bool shakeComplete = true;
    if (shake_ && !reducedMotion) {
        nextShakeElapsed = (std::min)(shake_->durationSeconds,
                                     shakeElapsed_ + deltaSeconds);
        shakeComplete = nextShakeElapsed >= shake_->durationSeconds;
        if (!shakeComplete) {
            candidate.shakeOffset = clampShakeToFraming(
                base, targets, shakeOffset(base, *shake_, nextShakeElapsed));
            candidate.position += candidate.shakeOffset;
            candidate.lookTarget += candidate.shakeOffset;
        }
    }
    if (!bounded(candidate.position, 10000.0) ||
        !bounded(candidate.lookTarget, 10000.0))
        return fail(error, "shaken battle camera pose exceeds world bounds");
    basePose_ = base;
    pose_ = candidate;
    blendElapsed_ = nextBlendElapsed;
    if (blendComplete) blendSource_.reset();
    shakeElapsed_ = nextShakeElapsed;
    if (shakeComplete || reducedMotion) shake_.reset();
    out = candidate;
    succeed(error);
    return true;
}

bool BattleCameraRig::startShake(const BattleCameraShake& shake,
                                 std::string* error) {
    checkOwner();
    if (!basePose_) return fail(error, "battle camera requires an initial cut");
    if (!range(shake.amplitudeWorld, 0.0, 1.0) ||
        !range(shake.frequencyHz, 0.1, 40.0) ||
        !range(shake.durationSeconds, 0.001, 2.0))
        return fail(error, "invalid battle camera shake");
    shake_ = shake;
    shakeElapsed_ = 0.0f;
    succeed(error);
    return true;
}

bool BattleCameraRig::stopShake(BattleCameraPose& out,
                                std::string* error) {
    checkOwner();
    if (!basePose_) return fail(error, "battle camera requires an initial cut");
    shake_.reset();
    shakeElapsed_ = 0.0f;
    pose_ = *basePose_;
    out = *basePose_;
    succeed(error);
    return true;
}

void BattleCameraRig::reset() {
    checkOwner();
    shot_.reset();
    basePose_.reset();
    pose_.reset();
    blendSource_.reset();
    blendElapsed_ = 0.0f;
    blendDuration_ = 0.0f;
    shake_.reset();
    shakeElapsed_ = 0.0f;
}

} // namespace engine::camera
