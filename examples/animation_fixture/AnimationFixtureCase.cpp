#include "AnimationFixtureCase.h"

#include <stdexcept>

namespace engine::animation_fixture {
namespace {

const std::array<FixtureCaseInfo, 4> kCatalog{{
    {FixtureCase::Bind,
     "bind",
     "linear",
     0,
     0.0f,
     0,
     0.0f},
    {FixtureCase::LinearIntermediate,
     "linear-intermediate",
     "linear",
     0,
     1.0f,
     30,
     -35.0f},
    {FixtureCase::LinearNearEndpoint,
     "linear-near-endpoint",
     "linear",
     0,
     2.0f,
     60,
     -70.0f},
    {FixtureCase::StepState,
     "step-state",
     "step",
     1,
     1.5f,
     90,
     -110.0f},
}};

} // namespace

const std::array<FixtureCaseInfo, 4>& fixtureCaseCatalog() {
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
