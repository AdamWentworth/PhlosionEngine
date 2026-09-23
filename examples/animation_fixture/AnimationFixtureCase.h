#pragma once

#include <array>
#include <optional>
#include <string>

namespace engine::animation_fixture {

// One authored presentation case. The clip, sample time and logical frame are
// fixture data; the capture host frame used by the screenshot facility is a
// separate value recorded by the runner.
enum class FixtureCase {
    Bind,
    LinearIntermediate,
    LinearNearEndpoint,
    StepState,
    CubicMidpoint,
    CubicEarly,
    CrossfadeQuarter,
    StateCrossfade,
};

struct FixtureCaseInfo {
    FixtureCase fixtureCase = FixtureCase::Bind;
    const char* name = "";
    const char* clipName = "";
    // Index of the authored clip inside the rig's animation list.
    int clipIndex = 0;
    float timeSec = 0.0f;
    int logicalFrame = 0;
    // Authored analytic tip rotation about +Z for this case, in degrees. The
    // verification tools derive expected landmarks from this value instead of
    // from the sampled pose under test.
    float expectedAngleDegrees = 0.0f;
    // The angle a LINEAR interpolation of the same key values would produce at
    // this sample time. It equals expectedAngleDegrees for the LINEAR and STEP
    // cases; on the cubic cases it is the linearized-cubic defect the guard has
    // to reject, so it is always authored here rather than sampled.
    float linearFallbackAngleDegrees = 0.0f;
};

const std::array<FixtureCaseInfo, 8>& fixtureCaseCatalog();

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase);
const char* fixtureCaseName(FixtureCase fixtureCase);
int fixtureCaseLogicalFrame(FixtureCase fixtureCase);
std::optional<FixtureCase> parseFixtureCase(const std::string& name);

} // namespace engine::animation_fixture
