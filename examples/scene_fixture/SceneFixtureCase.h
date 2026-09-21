#pragma once

// Authored presentation cases and camera for the glTF scene fixture. The six
// cases are the bounded native evidence matrix; each case is presented by both
// hosts on all three native APIs with the same camera and layout.

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include <glm/glm.hpp>

namespace engine::scene_fixture {

enum class FixtureCase {
    ABind = 0,
    APose = 1,
    FailedReplacement = 2,
    ReplacementB = 3,
    EmissionOff = 4,
    Unloaded = 5,
};

inline constexpr int kFixtureCaseCount = 6;

struct FixtureCaseInfo {
    FixtureCase fixtureCase = FixtureCase::ABind;
    const char* name = "";
    int logicalFrame = 0;
    // True when the case samples the explicit bind pose (clip -1).
    bool bindPose = false;
    // True when the case presents the emission-off authoring variant.
    bool emissionOff = false;
};

const std::array<FixtureCaseInfo, kFixtureCaseCount>& fixtureCaseCatalog();
const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase);
const char* fixtureCaseName(FixtureCase fixtureCase);
int fixtureCaseLogicalFrame(FixtureCase fixtureCase);
std::optional<FixtureCase> parseFixtureCase(const std::string& name);

// The authored pose sample time for the animated cases. The authored animation
// holds identity at t=0 and the bent pose at t=1.
float fixturePoseTimeSec();

// `0.0` is the bind pose and `1.0` is the fully authored bent pose. Cases that
// sample the explicit bind pose use 0.0; every other case uses 1.0.
float fixturePoseBlend(FixtureCase fixtureCase);

// The authored camera. Both hosts and the verification guard use these values,
// so the guard projects authored geometry with the same transform the hosts
// render with.
std::array<float, 3> fixtureCameraPosition();
std::array<float, 3> fixtureCameraTarget();
std::array<float, 3> fixtureCameraUp();
float fixtureCameraFovYDegrees();
float fixtureCameraNearPlane();
float fixtureCameraFarPlane();

glm::mat4 fixtureViewProjection(int surfaceWidth, int surfaceHeight);

// Projects a world position to pixel coordinates with a top-left origin.
glm::vec2 fixtureProjectToPixels(const glm::mat4& viewProjection,
                                 const glm::vec3& worldPosition,
                                 int surfaceWidth,
                                 int surfaceHeight);

// The authored linear background color the hosts clear and draw with.
std::array<float, 3> fixtureBackgroundLinearColor();

// The authored viewport used by the capture runner unless overridden.
int fixtureDefaultSurfaceWidth();
int fixtureDefaultSurfaceHeight();

// The frame at which both hosts write their capture.
int fixtureCaptureFrame();

}  // namespace engine::scene_fixture
