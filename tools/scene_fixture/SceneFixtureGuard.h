#pragma once

// Verification oracle and matrix guard for the glTF scene fixture.
//
// Every expectation here is derived from the authored sample source (camera,
// authored geometry, authored rig and the documented display transfer), not
// from captured output. Image I/O, comparison and contact-sheet helpers are the
// existing public-asset-fixture verification utilities.

#include "SceneFixtureAssets.h"
#include "SceneFixtureCase.h"
#include "SceneFixturePresentation.h"

#include "FixtureImageGuard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine::scene_fixture::verification {

using engine::fixture::FixtureCrop;
using engine::fixture::FixtureImage;
using engine::fixture::ImageCompareMetrics;

// Approved parity thresholds for every cross-host and cross-API comparison.
inline constexpr double kMaxNormalizedMae = 0.01;
inline constexpr double kMaxNormalizedRmse = 0.08;
inline constexpr double kMaxChangedRatio = 0.05;
inline constexpr int kChannelTolerance = 8;

// Authored-content tolerances.
inline constexpr int kBackgroundTolerance = 10;
inline constexpr int kSilhouetteTolerancePx = 10;
inline constexpr double kCharacterCoverageMinimum = 0.45;
inline constexpr double kEnvironmentCoverageMinimum = 0.45;
inline constexpr double kUnloadedBackgroundMinimum = 0.98;
inline constexpr double kReplacementChangedRatioMinimum = 0.08;
inline constexpr double kEmissionLuminanceMinimum = 6.0;
inline constexpr double kDeformationChangedRatioMinimum = 0.02;
// Per-capture authored emission bands. The emissive-factor-only surface is
// authored dark with a strong emissive factor, so a capture whose authored
// emission is on must be far brighter than one whose factor is zero.
inline constexpr double kEmissionOnMinimumLuminance = 140.0;
inline constexpr double kEmissionOffMaximumLuminance = 130.0;
// The albedo inspection surface must show the authored source bytes multiplied
// once by the authored linear base-color factor.
inline constexpr int kProbeChannelTolerance = 8;

struct GuardResult {
    bool ok = false;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
};

struct CaseExpectation {
    SceneVariant variant = SceneVariant::A;
    float poseBlend = 0.0f;
    bool bindPose = false;
    bool emissionOff = false;
    bool scenePresent = true;
    bool sceneDrawn = true;
    bool replacementAttempted = false;
    bool replacementFailed = false;
    bool replacementSucceeded = false;
    bool unloaded = false;
};

const CaseExpectation& caseExpectation(FixtureCase fixtureCase);

// The displayed background derived from the authored linear color through the
// engine's documented ACES tone map and sRGB transfer.
std::array<int, 3> expectedBackgroundRgb();

// One capture's expected content: background consistency, character and
// environment presence, and the analytic silhouette bounds of the authored
// geometry for the case's variant and pose.
bool validateCaptureContent(const FixtureImage& viewport,
                            FixtureCase fixtureCase,
                            GuardResult& outResult,
                            std::string* outError = nullptr);

// A defect mode for the synthetic albedo inspection surface, used by the
// CPU-only adversarial harness to prove the oracle rejects corrupted factor
// application and transfer.
enum class ProbeDefect {
    None = 0,
    NoFactor,
    DoubleFactor,
    WrongTransfer,
};

bool parseProbeDefect(const std::string& name, ProbeDefect& outDefect);
const char* probeDefectName(ProbeDefect defect);

// The analytic silhouette bounds (x0, y0, x1, y1) of the authored character and
// limb in pixels for one variant and pose.
bool analyticCharacterBounds(SceneVariant variant,
                             float poseBlend,
                             int surfaceWidth,
                             int surfaceHeight,
                             std::array<float, 4>& outCharacter,
                             std::array<float, 4>& outLimb,
                             std::array<float, 4>& outHead);

// Presence bounds measured from a capture: the bounding box of pixels that
// differ from the expected background inside the given pixel region.
bool measuredContentBounds(const FixtureImage& viewport,
                           const std::array<float, 4>& region,
                           std::array<float, 4>& outBounds,
                           double* outCoverage,
                           std::string* outError = nullptr);

struct RegionMean {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double luminance = 0.0;
    std::size_t samples = 0u;
};

RegionMean regionMean(const FixtureImage& viewport,
                      const std::array<float, 4>& region,
                      float insetFraction);

double changedPixelRatioInRegion(const FixtureImage& left,
                                 const FixtureImage& right,
                                 const std::array<float, 4>& region,
                                 int channelTolerance);

// SHA-256 of an on-disk file.
bool sha256FileHex(const std::string& path,
                   std::string& outHex,
                   std::string* outError = nullptr);

// Resolves a recorded path against `root` requiring component containment.
bool resolveWithinRoot(const std::string& value,
                       const std::filesystem::path& root,
                       std::filesystem::path& outResolved,
                       std::string& outError);

// Validates a complete capture matrix manifest, its evidence and every
// cross-host/cross-API comparison. Writes `contact-sheet.png` and
// `validation.json` next to the manifest.
bool validateMatrix(const std::string& manifestPath,
                    GuardResult& outResult,
                    std::string* outError = nullptr);

// Builds a hermetic synthetic capture that satisfies the expected-content
// oracle for the case. Used by the CPU-only adversarial harness.
bool buildSyntheticCapture(FixtureCase fixtureCase,
                           int width,
                           int height,
                           FixtureImage& outImage,
                           std::string* outError = nullptr);

bool buildSyntheticCapture(FixtureCase fixtureCase,
                           ProbeDefect defect,
                           int width,
                           int height,
                           FixtureImage& outImage,
                           std::string* outError = nullptr);

bool writeSyntheticHandshake(const std::string& path,
                             FixtureCase fixtureCase,
                             const std::string& host,
                             const std::string& api,
                             int width,
                             int height,
                             std::string* outError = nullptr);

}  // namespace engine::scene_fixture::verification
