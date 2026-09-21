#pragma once

#include "AnimationFixtureCase.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::animation_fixture::verification {

struct FixtureImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool empty() const noexcept {
        return width <= 0 || height <= 0 ||
            rgba.size() !=
                static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u;
    }
};

bool loadFixturePng(const std::string& path,
                    FixtureImage& outImage,
                    std::string* outError = nullptr);

bool saveFixturePng(const std::string& path,
                    const FixtureImage& image,
                    std::string* outError = nullptr);

struct FixtureCrop {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

FixtureImage cropFixtureImage(const FixtureImage& image, const FixtureCrop& crop);

// Detects the fixture's green background so editor chrome never contaminates
// the comparison. Standalone hosts fill the whole window and resolve to the
// full image.
bool detectFixtureViewport(const FixtureImage& image,
                           FixtureCrop& outCrop,
                           std::string* outError = nullptr);

struct ImageCompareMetrics {
    double normalizedMae = 0.0;
    double normalizedRmse = 0.0;
    double changedPixelRatio = 0.0;
    int maxChannelDiff = 0;
};

bool compareFixtureImages(const FixtureImage& left,
                          const FixtureImage& right,
                          int channelTolerance,
                          ImageCompareMetrics& outMetrics,
                          std::string* outError = nullptr);

// Analytic expectations derived from the authored rig and case catalog, never
// from the sampled pose. Screen points are normalized to the viewport with a
// top-left origin.
struct FixtureExpectation {
    FixtureCase fixtureCase = FixtureCase::Bind;
    std::string caseName;
    std::string clipName;
    int clipIndex = 0;
    int logicalFrame = 0;
    float sampleTimeSec = 0.0f;
    float angleDegrees = 0.0f;
    std::array<double, 2> base{};
    std::array<double, 2> middle{};
    std::array<double, 2> tip{};
    std::array<double, 2> bindBase{};
    std::array<double, 2> bindMiddle{};
    std::array<double, 2> bindTip{};
    std::array<double, 2> rigidTipBase{};
    std::array<double, 2> rigidMiddleBind{};
    std::array<double, 2> rigidMiddleTip{};
    std::array<float, 16> meshTransform{};
    std::array<float, 32> palette{};
};

FixtureExpectation computeFixtureExpectation(FixtureCase fixtureCase,
                                             int surfaceWidth,
                                             int surfaceHeight);

// Authored bind vertices the handshake must describe: stations 0..2 in halves,
// joints (0, 1, 0, 0) and weights (1 - t, t, 0, 0) with t = x / 2.
struct AuthoredVertex {
    double x = 0.0;
    double y = 0.0;
    int joints[4] = {0, 1, 0, 0};
    double weights[4] = {1.0, 0.0, 0.0, 0.0};
};

std::vector<AuthoredVertex> authoredFixtureVertices();

// Lowercase hexadecimal SHA-256 of the given bytes and of an on-disk file.
// The capture matrix re-hashes the recorded executables instead of trusting
// the manifest fields.
std::string sha256Hex(const std::string& bytes);
bool sha256FileHex(const std::string& path,
                   std::string& outHex,
                   std::string* outError = nullptr);

// Defect modes used by the hermetic synthetic captures. They model the failures
// the guard has to reject: skinning disabled, a stale bind palette, a palette
// built in the mesh's own space and joint/weight mapping errors.
enum class SynthDefect {
    None,
    DisabledSkinning,
    StalePalette,
    WrongSpace,
    WrongJoint,
    Blank,
    // A cubic clip evaluated as if it were a LINEAR curve between its keys.
    // On the LINEAR and STEP cases the fallback equals the authored pose, so
    // only the cubic cases are distinguishable.
    LinearizedCubic,
};

bool parseSynthDefect(const std::string& name, SynthDefect& outDefect);
const char* synthDefectName(SynthDefect defect);

bool buildSyntheticFixtureCapture(FixtureCase fixtureCase,
                                  SynthDefect defect,
                                  int width,
                                  int height,
                                  FixtureImage& outImage,
                                  std::string* outError = nullptr);

struct ExpectedContentResult {
    bool ok = false;
    std::vector<std::string> failures;
    // Measured image values for this capture.
    std::map<std::string, double> metrics;
    // Distances between authored landmark positions. These are geometry
    // expectations, not measurements of the submitted pixels.
    std::map<std::string, double> expectedGeometry;
};

bool validateFixtureExpectedContent(const FixtureImage& viewport,
                                    const FixtureExpectation& expectation,
                                    ExpectedContentResult& outResult,
                                    std::string* outError = nullptr);

// Numeric provenance for one handshake document: case identity, sample time,
// logical frame, palette space and the submitted joint/weight/vertex data.
struct HandshakeCheckResult {
    bool ok = false;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
};

bool validateFixtureHandshake(const std::string& handshakePath,
                              FixtureCase fixtureCase,
                              int surfaceWidth,
                              int surfaceHeight,
                              HandshakeCheckResult& outResult,
                              std::string* outError = nullptr,
                              const std::string& expectedApi = std::string{},
                              const std::string& expectedHost = std::string{});

// Builds a contact sheet from labelled, already-cropped viewport images.
FixtureImage buildFixtureContactSheet(
    const std::vector<std::pair<std::string, FixtureImage>>& images,
    int columns,
    int cellWidth,
    int cellHeight);

} // namespace engine::animation_fixture::verification
