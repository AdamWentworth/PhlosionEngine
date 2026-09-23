#include "AnimationFixtureCase.h"

#include <stdexcept>

namespace engine::animation_fixture {
namespace {

// The cubic clip's analytic angles: keys at 0 s and 2 s rotate from identity to
// -120 degrees about +Z, with an out-tangent of 3x the chord / interval on the
// first key and an in-tangent of -1x on the second. The curve is then
// value0 + (3a - 2a^2) * chord, so a = 0.5 (t = 1.0 s) lands exactly on the
// authored -120 degree key and a = 0.25 (t = 0.5 s) lands on
// 2 * atan2(-0.541265877365274, 0.6875) = -76.4264214034764 degrees.
// A LINEAR interpolation of the same keys would return -60 and -30 degrees.
const std::array<FixtureCaseInfo, 7> kCatalog{{
    {FixtureCase::Bind,
     "bind",
     "linear",
     0,
     0.0f,
     0,
     0.0f,
     0.0f},
    {FixtureCase::LinearIntermediate,
     "linear-intermediate",
     "linear",
     0,
     1.0f,
     30,
     -35.0f,
     -35.0f},
    {FixtureCase::LinearNearEndpoint,
     "linear-near-endpoint",
     "linear",
     0,
     2.0f,
     60,
     -70.0f,
     -70.0f},
    {FixtureCase::StepState,
     "step-state",
     "step",
     1,
     1.5f,
     90,
     -110.0f,
     -110.0f},
    {FixtureCase::CubicMidpoint,
     "cubic-midpoint",
     "cubic",
     2,
     1.0f,
     120,
     -120.0f,
     -60.0f},
    {FixtureCase::CubicEarly,
     "cubic-early",
     "cubic",
     2,
     0.5f,
     150,
     -76.4264214034764f,
     -30.0f},
    {FixtureCase::CrossfadeQuarter,
     "crossfade-quarter",
     "cubic",
     2,
     1.0f,
     180,
     -30.0f,
     -15.0f},
}};

} // namespace

const std::array<FixtureCaseInfo, 7>& fixtureCaseCatalog() {
    return kCatalog;
}

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase) {
    for (const FixtureCaseInfo& info : kCatalog) {
        if (info.fixtureCase == fixtureCase) {
            return info;
        }
    }
    throw std::runtime_error("animation fixture case is not in the catalog");
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

} // namespace engine::animation_fixture
