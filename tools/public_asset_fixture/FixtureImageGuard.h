#pragma once

#include "FixturePresentation.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::fixture {

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

struct FixtureCrop {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool loadFixturePng(
    const std::string& path,
    FixtureImage& outImage,
    std::string* outError = nullptr);

bool saveFixturePng(
    const std::string& path,
    const FixtureImage& image,
    std::string* outError = nullptr);

FixtureImage cropFixtureImage(
    const FixtureImage& image,
    const FixtureCrop& crop);

// Detects the fixture's saturated-green background so editor chrome never
// contaminates the comparison. Standalone hosts fill the whole window and
// therefore resolve to the full image.
bool detectFixtureViewport(
    const FixtureImage& image,
    FixtureCrop& outCrop,
    std::string* outError = nullptr);

struct ImageCompareMetrics {
    double normalizedMae = 0.0;
    double normalizedRmse = 0.0;
    double changedPixelRatio = 0.0;
    int maxChannelDiff = 0;
};

bool compareFixtureImages(
    const FixtureImage& left,
    const FixtureImage& right,
    int channelTolerance,
    ImageCompareMetrics& outMetrics,
    std::string* outError = nullptr);

struct FixtureClassFractions {
    double green = 0.0;
    double hidden = 0.0;
    double blue = 0.0;
    double white = 0.0;
    double blend = 0.0;
    double boardA = 0.0;
    double boardB = 0.0;
    double magenta = 0.0;
    double cyan = 0.0;
    double orange = 0.0;
    double yellow = 0.0;
};

FixtureClassFractions classifyFixtureRegion(
    const FixtureImage& image,
    const FixtureCrop& region);

// The alpha-covered fraction of a normalized role rectangle, expressed as a
// zero-to-one inset. Every class test uses interior sampling so native
// filtering at object edges cannot dominate the result.
FixtureCrop roleRectToCrop(
    const FixtureImage& viewport,
    const FixtureRoleRect& rect,
    float insetFraction);

struct ExpectedContentResult {
    bool ok = false;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
};

bool validateFixtureExpectedContent(
    const FixtureImage& viewport,
    const std::string& expectedContentId,
    int logicalFrame,
    const std::map<std::string, FixtureRoleRect>& roleRects,
    ExpectedContentResult& outResult,
    std::string* outError = nullptr);

// Builds a contact sheet from labelled, already-cropped viewport images. The
// caller's ordering is preserved; label text is not drawn because the matrix
// JSON records the row/column identity.
FixtureImage buildFixtureContactSheet(
    const std::vector<std::pair<std::string, FixtureImage>>& images,
    int columns,
    int cellWidth,
    int cellHeight);

// Builds a deterministic synthetic capture with the same role rectangles,
// 2x2 board checker arrangement and color classes as the rendered fixture.
// The unit tests and the guard's `synth` CLI command share it so validator
// negative tests exercise the real expected-content gates.
bool buildSyntheticFixtureCapture(
    const std::string& contentId,
    int logicalFrame,
    int width,
    int height,
    FixtureImage& outImage,
    std::map<std::string, FixtureRoleRect>& outRoleRects,
    std::string* outError = nullptr);

} // namespace engine::fixture
