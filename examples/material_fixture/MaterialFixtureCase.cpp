#include "MaterialFixtureCase.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace engine::material_fixture {
namespace {

// One layout for every case: eight patches of equal size in a single row,
// centred in an orthographic view. The pitch leaves a visible gap between
// patches so an interior region never mixes two neighbours.
constexpr float kWorldHalfX = 2.0f;
constexpr float kWorldHalfY = 1.125f;
constexpr float kPatchHalfWidth = 0.18f;
constexpr float kPatchHalfHeight = 0.6f;
constexpr float kPatchPitch = 0.48f;
constexpr float kPatchDepth = 0.0f;
constexpr float kBackgroundDepth = -0.75f;

const std::array<FixtureCaseInfo, 3> kCatalog{{
    {FixtureCase::AuthoredMaps,
     "authored-maps",
     0,
     true,
     false,
     {0.4f, 0.2f, 0.1f}},
    {FixtureCase::EmptyImageBytes,
     "empty-image-bytes",
     60,
     false,
     true,
     {0.0f, 0.0f, 0.0f}},
    {FixtureCase::AbsentMaps,
     "absent-maps",
     120,
     false,
     false,
     {0.0f, 0.0f, 0.0f}},
}};

struct PatchInfo {
    FixturePatch patch;
    const char* name;
    int debugView;
};

const std::array<PatchInfo, kFixturePatchCount> kPatches{{
    {FixturePatch::BaseColor, "base", 1},
    {FixturePatch::Albedo, "albedo", 2},
    {FixturePatch::Normal, "normal", 3},
    {FixturePatch::Roughness, "roughness", 4},
    {FixturePatch::Metallic, "metallic", 5},
    {FixturePatch::Occlusion, "occlusion", 6},
    {FixturePatch::Emissive, "emissive", 7},
    {FixturePatch::Lit, "lit", 0},
}};

std::string floatText(float value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(value));
    return std::string(buffer);
}

} // namespace

const std::array<FixtureCaseInfo, 3>& fixtureCaseCatalog() {
    return kCatalog;
}

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase) {
    for (const FixtureCaseInfo& info : kCatalog) {
        if (info.fixtureCase == fixtureCase) {
            return info;
        }
    }
    throw std::runtime_error("material fixture case is not in the catalog");
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

const char* fixturePatchName(FixturePatch patch) {
    for (const PatchInfo& info : kPatches) {
        if (info.patch == patch) {
            return info.name;
        }
    }
    return "unknown";
}

std::optional<FixturePatch> parseFixturePatch(const std::string& name) {
    for (const PatchInfo& info : kPatches) {
        if (name == info.name) {
            return info.patch;
        }
    }
    return std::nullopt;
}

int fixturePatchDebugView(FixturePatch patch) {
    for (const PatchInfo& info : kPatches) {
        if (info.patch == patch) {
            return info.debugView;
        }
    }
    return 0;
}

std::array<float, 2> fixtureWorldHalfExtent() {
    return {kWorldHalfX, kWorldHalfY};
}

std::array<float, 2> fixturePatchCenter(std::size_t patchIndex) {
    const float rowWidth =
        kPatchPitch * static_cast<float>(kFixturePatchCount);
    const float firstCenter =
        -rowWidth * 0.5f + kPatchPitch * 0.5f;
    return {firstCenter + kPatchPitch * static_cast<float>(patchIndex),
            kPatchDepth};
}

float fixturePatchHalfWidth() {
    return kPatchHalfWidth;
}

float fixturePatchHalfHeight() {
    return kPatchHalfHeight;
}

float fixturePatchDepth() {
    return kPatchDepth;
}

float fixtureBackgroundDepth() {
    return kBackgroundDepth;
}

std::string fixturePatchGeometryKey(FixtureCase fixtureCase,
                                    std::size_t patchIndex,
                                    std::uint32_t contentHash) {
    if (patchIndex >= static_cast<std::size_t>(kFixturePatchCount)) {
        throw std::runtime_error(
            "material fixture patch index is outside the layout");
    }
    const std::array<float, 2> center = fixturePatchCenter(patchIndex);
    std::string key = "fixture/mat/";
    key += fixtureCaseName(fixtureCase);
    key += "/patch";
    key += std::to_string(patchIndex);
    key += "/" + std::to_string(contentHash);
    key += "/x" + floatText(center[0]);
    return key;
}

std::array<float, 3> fixtureCameraPosition() {
    return {0.0f, 0.0f, 4.0f};
}

std::array<float, 3> fixtureCameraTarget() {
    return {0.0f, 0.0f, 0.0f};
}

std::array<float, 3> fixtureBackgroundColor() {
    return {0.05f, 0.60f, 0.08f};
}

} // namespace engine::material_fixture
