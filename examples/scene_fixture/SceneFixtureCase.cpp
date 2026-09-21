#include "SceneFixtureCase.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>

namespace engine::scene_fixture {
namespace {

const std::array<FixtureCaseInfo, kFixtureCaseCount> kCatalog{{
    {FixtureCase::ABind, "a-bind", 0, true, false},
    {FixtureCase::APose, "a-pose", 30, false, false},
    {FixtureCase::FailedReplacement, "failed-replacement", 60, false, false},
    {FixtureCase::ReplacementB, "replacement-b", 90, false, false},
    {FixtureCase::EmissionOff, "emission-off", 120, false, true},
    {FixtureCase::Unloaded, "unloaded", 150, false, false},
}};

constexpr float kFovYDegrees = 45.0f;
constexpr float kNearPlane = 0.05f;
constexpr float kFarPlane = 40.0f;
constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 360;
constexpr int kCaptureFrame = 6;

}  // namespace

const std::array<FixtureCaseInfo, kFixtureCaseCount>& fixtureCaseCatalog() {
    return kCatalog;
}

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase) {
    for (const FixtureCaseInfo& info : kCatalog) {
        if (info.fixtureCase == fixtureCase) {
            return info;
        }
    }
    throw std::runtime_error("scene fixture case is not in the catalog");
}

const char* fixtureCaseName(FixtureCase fixtureCase) {
    return fixtureCaseInfo(fixtureCase).name;
}

int fixtureCaseLogicalFrame(FixtureCase fixtureCase) {
    return fixtureCaseInfo(fixtureCase).logicalFrame;
}

std::optional<FixtureCase> parseFixtureCase(const std::string& name) {
    for (const FixtureCaseInfo& info : kCatalog) {
        if (name == info.name) {
            return info.fixtureCase;
        }
    }
    return std::nullopt;
}

float fixturePoseTimeSec() {
    return 1.0f;
}

float fixturePoseBlend(FixtureCase fixtureCase) {
    return fixtureCaseInfo(fixtureCase).bindPose ? 0.0f : 1.0f;
}

std::array<float, 3> fixtureCameraPosition() {
    return {2.45f, 1.72f, 3.35f};
}

std::array<float, 3> fixtureCameraTarget() {
    return {0.22f, 0.78f, 0.0f};
}

std::array<float, 3> fixtureCameraUp() {
    return {0.0f, 1.0f, 0.0f};
}

float fixtureCameraFovYDegrees() {
    return kFovYDegrees;
}

float fixtureCameraNearPlane() {
    return kNearPlane;
}

float fixtureCameraFarPlane() {
    return kFarPlane;
}

glm::mat4 fixtureViewProjection(int surfaceWidth, int surfaceHeight) {
    const float aspect =
        static_cast<float>(surfaceWidth > 0 ? surfaceWidth : kDefaultWidth) /
        static_cast<float>(surfaceHeight > 0 ? surfaceHeight : kDefaultHeight);
    const std::array<float, 3> position = fixtureCameraPosition();
    const std::array<float, 3> target = fixtureCameraTarget();
    const std::array<float, 3> up = fixtureCameraUp();
    const glm::mat4 view = glm::lookAt(
        glm::vec3(position[0], position[1], position[2]),
        glm::vec3(target[0], target[1], target[2]),
        glm::vec3(up[0], up[1], up[2]));
    const glm::mat4 projection = glm::perspective(
        glm::radians(kFovYDegrees), aspect, kNearPlane, kFarPlane);
    return projection * view;
}

glm::vec2 fixtureProjectToPixels(const glm::mat4& viewProjection,
                                 const glm::vec3& worldPosition,
                                 int surfaceWidth,
                                 int surfaceHeight) {
    const glm::vec4 clip =
        viewProjection * glm::vec4(worldPosition, 1.0f);
    if (!(std::fabs(clip.w) > 1e-6f)) {
        return glm::vec2(-1.0f);
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(surfaceWidth),
                     (1.0f - (ndc.y * 0.5f + 0.5f)) *
                         static_cast<float>(surfaceHeight));
}

std::array<float, 3> fixtureBackgroundLinearColor() {
    // Authored dark teal background. The verification guard derives its
    // expected displayed value from this literal with the documented transfer
    // math instead of learning it from a capture.
    return {0.055f, 0.10f, 0.145f};
}

int fixtureDefaultSurfaceWidth() {
    return kDefaultWidth;
}

int fixtureDefaultSurfaceHeight() {
    return kDefaultHeight;
}

int fixtureCaptureFrame() {
    return kCaptureFrame;
}

}  // namespace engine::scene_fixture
