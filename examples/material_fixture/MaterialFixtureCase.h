#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace engine::material_fixture {

// The three authored presentation cases. Each is drawn by both hosts on all
// three native APIs with the same layout, so the capture matrix stays bounded.
enum class FixtureCase {
    AuthoredMaps,
    EmptyImageBytes,
    AbsentMaps,
};

// One of the eight surfaces in the single bounded layout: seven existing
// per-draw material debug views plus one composed lit region.
enum class FixturePatch {
    BaseColor = 0,
    Albedo = 1,
    Normal = 2,
    Roughness = 3,
    Metallic = 4,
    Occlusion = 5,
    Emissive = 6,
    Lit = 7,
};

inline constexpr int kFixturePatchCount = 8;

struct FixtureCaseInfo {
    FixtureCase fixtureCase = FixtureCase::AuthoredMaps;
    const char* name = "";
    int logicalFrame = 0;
    // True when the glTF document binds the five authored role maps.
    bool authoredMaps = false;
    // True when every role selects a zero-length encoded image bufferView.
    bool emptyImageBytes = false;
    // Authored emission factor for the case. Both fallback cases author zero so
    // the known factor-only emissive limitation is never a required appearance.
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
};

const std::array<FixtureCaseInfo, 3>& fixtureCaseCatalog();

const FixtureCaseInfo& fixtureCaseInfo(FixtureCase fixtureCase);
const char* fixtureCaseName(FixtureCase fixtureCase);
int fixtureCaseLogicalFrame(FixtureCase fixtureCase);
std::optional<FixtureCase> parseFixtureCase(const std::string& name);

const char* fixturePatchName(FixturePatch patch);
std::optional<FixturePatch> parseFixturePatch(const std::string& name);

// The material debug view submitted immediately before that patch's draw. The
// lit patch uses view 0, which is the ordinary material path.
int fixturePatchDebugView(FixturePatch patch);

// Authored orthographic layout, in world units. The projection spans
// +/-fixtureWorldHalfExtent and every patch is centred on its own column.
std::array<float, 2> fixtureWorldHalfExtent();
std::array<float, 2> fixturePatchCenter(std::size_t patchIndex);
float fixturePatchHalfWidth();
float fixturePatchHalfHeight();
float fixturePatchDepth();
float fixtureBackgroundDepth();

// Stable geometry keys. They include the case and the decoded content hash, so
// a stale cached mesh or texture cannot be reused across cases.
std::string fixturePatchGeometryKey(FixtureCase fixtureCase,
                                    std::size_t patchIndex,
                                    std::uint32_t contentHash);

// The authored camera used by both hosts and by the guard's projection math.
std::array<float, 3> fixtureCameraPosition();
std::array<float, 3> fixtureCameraTarget();
std::array<float, 3> fixtureBackgroundColor();

} // namespace engine::material_fixture
