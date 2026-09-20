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
};

const std::array<FixtureCaseInfo, 4>& fixtureCaseCatalog();

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase);
const char* fixtureCaseName(FixtureCase fixtureCase);
int fixtureCaseLogicalFrame(FixtureCase fixtureCase);
std::optional<FixtureCase> parseFixtureCase(const std::string& name);

} // namespace engine::animation_fixture
