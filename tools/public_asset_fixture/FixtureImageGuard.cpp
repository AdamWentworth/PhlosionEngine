#include "FixtureImageGuard.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace engine::fixture {
namespace {

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
};

Pixel pixelAt(const FixtureImage& image, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) * 4u;
    return Pixel{
        static_cast<int>(image.rgba[offset + 0u]),
        static_cast<int>(image.rgba[offset + 1u]),
        static_cast<int>(image.rgba[offset + 2u])};
}

bool isGreenClass(const Pixel& pixel) {
    return pixel.g > pixel.r + 25 && pixel.g > pixel.b + 25;
}

// The fixture's hidden ring is a dark green object in front of the bright
// green background. Luminance separates the two classes so a missing hidden
// object cannot be mistaken for the background it would expose.
bool isHiddenClass(const Pixel& pixel) {
    const int luminance = (pixel.r + pixel.g + pixel.b) / 3;
    return isGreenClass(pixel) && luminance < 130;
}

bool isBlueClass(const Pixel& pixel) {
    return pixel.b > pixel.r + 30 && pixel.b > pixel.g + 25;
}

bool isWhiteClass(const Pixel& pixel) {
    return std::min({pixel.r, pixel.g, pixel.b}) > 185;
}

bool isBlendClass(const Pixel& pixel) {
    return !isWhiteClass(pixel) &&
        pixel.r > pixel.b + 25 &&
        pixel.g > pixel.b + 25 &&
        std::abs(pixel.r - pixel.g) < 70;
}

bool isMagentaClass(const Pixel& pixel) {
    return pixel.r > pixel.g + 30 && pixel.b > pixel.g + 20;
}

bool isCyanClass(const Pixel& pixel) {
    return pixel.g > pixel.r + 35 && pixel.b > pixel.r + 25;
}

bool isOrangeClass(const Pixel& pixel) {
    return pixel.r > pixel.b + 40 && pixel.r > pixel.g + 30;
}

bool isYellowClass(const Pixel& pixel) {
    // Yellow is distinguished from orange by keeping the green channel near
    // the red channel; both remain far above blue.
    return pixel.r > pixel.b + 40 && pixel.g > pixel.b + 40 &&
        pixel.g > pixel.r - 60;
}

bool isBoardAClass(const Pixel& pixel) {
    return isMagentaClass(pixel) || isCyanClass(pixel);
}

bool isBoardBClass(const Pixel& pixel) {
    return isOrangeClass(pixel) || isYellowClass(pixel);
}

bool validCrop(const FixtureImage& image, const FixtureCrop& crop) {
    return crop.x >= 0 && crop.y >= 0 && crop.width > 0 && crop.height > 0 &&
        crop.x + crop.width <= image.width &&
        crop.y + crop.height <= image.height;
}

double fractionOf(
    const FixtureImage& image,
    const FixtureCrop& crop,
    bool (*predicate)(const Pixel&)) {
    if (!validCrop(image, crop)) {
        return 0.0;
    }
    std::size_t matches = 0u;
    const std::size_t total =
        static_cast<std::size_t>(crop.width) *
        static_cast<std::size_t>(crop.height);
    for (int y = crop.y; y < crop.y + crop.height; ++y) {
        for (int x = crop.x; x < crop.x + crop.width; ++x) {
            if (predicate(pixelAt(image, x, y))) {
                ++matches;
            }
        }
    }
    return total == 0u
        ? 0.0
        : static_cast<double>(matches) / static_cast<double>(total);
}

void addFailure(
    ExpectedContentResult& result,
    const std::string& name,
    double actual,
    double minimum,
    const char* direction) {
    result.failures.push_back(
        name + " = " + std::to_string(actual) + " expected " + direction +
        " " + std::to_string(minimum));
}

void requireAtLeast(
    ExpectedContentResult& result,
    const std::string& name,
    double actual,
    double minimum) {
    result.metrics[name] = actual;
    if (actual < minimum) {
        addFailure(result, name, actual, minimum, ">=");
    }
}

void requireAtMost(
    ExpectedContentResult& result,
    const std::string& name,
    double actual,
    double maximum) {
    result.metrics[name] = actual;
    if (actual > maximum) {
        addFailure(result, name, actual, maximum, "<=");
    }
}

} // namespace

bool loadFixturePng(
    const std::string& path,
    FixtureImage& outImage,
    std::string* outError) {
    outImage = FixtureImage{};
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        return fail(outError, "could not load fixture PNG: " + path);
    }
    outImage.width = width;
    outImage.height = height;
    outImage.rgba.assign(
        pixels,
        pixels + static_cast<std::ptrdiff_t>(
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4u));
    stbi_image_free(pixels);
    if (outError) {
        outError->clear();
    }
    return true;
}

bool saveFixturePng(
    const std::string& path,
    const FixtureImage& image,
    std::string* outError) {
    if (image.empty()) {
        return fail(outError, "fixture PNG has no pixels to write");
    }
    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        const int wrote = stbi_write_png(
            path.c_str(),
            image.width,
            image.height,
            4,
            image.rgba.data(),
            image.width * 4);
        if (wrote == 0) {
            return fail(outError, "could not write fixture PNG: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(
            outError,
            std::string("fixture PNG write failed: ") + exception.what());
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureImage cropFixtureImage(
    const FixtureImage& image,
    const FixtureCrop& crop) {
    FixtureImage out;
    if (!validCrop(image, crop)) {
        return out;
    }
    out.width = crop.width;
    out.height = crop.height;
    out.rgba.resize(
        static_cast<std::size_t>(crop.width) *
        static_cast<std::size_t>(crop.height) * 4u);
    for (int y = 0; y < crop.height; ++y) {
        const std::size_t sourceOffset =
            (static_cast<std::size_t>(crop.y + y) *
                 static_cast<std::size_t>(image.width) +
             static_cast<std::size_t>(crop.x)) * 4u;
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(crop.width) * 4u;
        std::copy_n(
            image.rgba.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
            static_cast<std::ptrdiff_t>(crop.width) * 4,
            out.rgba.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
    }
    return out;
}

bool detectFixtureViewport(
    const FixtureImage& image,
    FixtureCrop& outCrop,
    std::string* outError) {
    if (image.empty()) {
        return fail(outError, "fixture viewport detection requires an image");
    }
    int minimumX = image.width;
    int minimumY = image.height;
    int maximumX = -1;
    int maximumY = -1;
    std::size_t greenPixels = 0u;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (!isGreenClass(pixelAt(image, x, y))) {
                continue;
            }
            ++greenPixels;
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
    }
    const std::size_t total =
        static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height);
    if (greenPixels == 0u || maximumX < minimumX || maximumY < minimumY) {
        return fail(
            outError,
            "fixture viewport detection found no green background pixels");
    }
    const bool fullExtent =
        minimumX == 0 && minimumY == 0 &&
        maximumX == image.width - 1 && maximumY == image.height - 1;
    if (fullExtent || greenPixels >= total * 9u / 10u) {
        outCrop = FixtureCrop{0, 0, image.width, image.height};
        if (outError) {
            outError->clear();
        }
        return true;
    }
    const FixtureCrop crop{
        minimumX,
        minimumY,
        maximumX - minimumX + 1,
        maximumY - minimumY + 1};
    if (crop.width < image.width / 5 || crop.height < image.height / 5) {
        return fail(outError, "fixture viewport detection found a green region that is too small");
    }
    if (crop.x <= 0 || crop.y <= 0 ||
        crop.x + crop.width >= image.width ||
        crop.y + crop.height >= image.height) {
        return fail(
            outError,
            "fixture viewport detection did not isolate an editor viewport inside the capture");
    }
    const double greenFill =
        static_cast<double>(greenPixels) /
        (static_cast<double>(crop.width) * static_cast<double>(crop.height));
    if (greenFill < 0.2) {
        return fail(outError, "fixture viewport detection found an implausible green fill ratio");
    }
    outCrop = crop;
    if (outError) {
        outError->clear();
    }
    return true;
}

bool compareFixtureImages(
    const FixtureImage& left,
    const FixtureImage& right,
    int channelTolerance,
    ImageCompareMetrics& outMetrics,
    std::string* outError) {
    outMetrics = ImageCompareMetrics{};
    if (left.empty() || right.empty()) {
        return fail(outError, "fixture image comparison requires two images");
    }
    if (left.width != right.width || left.height != right.height) {
        return fail(outError, "fixture image dimensions do not match");
    }
    const std::size_t pixels =
        static_cast<std::size_t>(left.width) *
        static_cast<std::size_t>(left.height);
    if (pixels == 0u) {
        return fail(outError, "fixture image comparison found an empty image");
    }
    double absolute = 0.0;
    double squared = 0.0;
    std::size_t changed = 0u;
    int maxDiff = 0;
    for (std::size_t index = 0u; index < pixels; ++index) {
        int pixelMaxDiff = 0;
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            const int difference = std::abs(
                static_cast<int>(left.rgba[index * 4u + channel]) -
                static_cast<int>(right.rgba[index * 4u + channel]));
            absolute += static_cast<double>(difference);
            squared += static_cast<double>(difference) * static_cast<double>(difference);
            pixelMaxDiff = std::max(pixelMaxDiff, difference);
        }
        maxDiff = std::max(maxDiff, pixelMaxDiff);
        if (pixelMaxDiff > channelTolerance) {
            ++changed;
        }
    }
    const double channelCount =
        static_cast<double>(pixels) * 4.0;
    outMetrics.normalizedMae = absolute / channelCount / 255.0;
    outMetrics.normalizedRmse =
        std::sqrt(squared / channelCount) / 255.0;
    outMetrics.changedPixelRatio =
        static_cast<double>(changed) / static_cast<double>(pixels);
    outMetrics.maxChannelDiff = maxDiff;
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureClassFractions classifyFixtureRegion(
    const FixtureImage& image,
    const FixtureCrop& region) {
    FixtureClassFractions fractions;
    if (!validCrop(image, region)) {
        return fractions;
    }
    std::size_t green = 0u;
    std::size_t hidden = 0u;
    std::size_t blue = 0u;
    std::size_t white = 0u;
    std::size_t blend = 0u;
    std::size_t boardA = 0u;
    std::size_t boardB = 0u;
    std::size_t magenta = 0u;
    std::size_t cyan = 0u;
    std::size_t orange = 0u;
    std::size_t yellow = 0u;
    for (int y = region.y; y < region.y + region.height; ++y) {
        for (int x = region.x; x < region.x + region.width; ++x) {
            const Pixel pixel = pixelAt(image, x, y);
            green += isGreenClass(pixel) ? 1u : 0u;
            hidden += isHiddenClass(pixel) ? 1u : 0u;
            blue += isBlueClass(pixel) ? 1u : 0u;
            white += isWhiteClass(pixel) ? 1u : 0u;
            blend += isBlendClass(pixel) ? 1u : 0u;
            boardA += isBoardAClass(pixel) ? 1u : 0u;
            boardB += isBoardBClass(pixel) ? 1u : 0u;
            magenta += isMagentaClass(pixel) ? 1u : 0u;
            cyan += isCyanClass(pixel) ? 1u : 0u;
            orange += isOrangeClass(pixel) ? 1u : 0u;
            yellow += isYellowClass(pixel) ? 1u : 0u;
        }
    }
    const double total =
        static_cast<double>(region.width) * static_cast<double>(region.height);
    if (total <= 0.0) {
        return fractions;
    }
    fractions.green = static_cast<double>(green) / total;
    fractions.hidden = static_cast<double>(hidden) / total;
    fractions.blue = static_cast<double>(blue) / total;
    fractions.white = static_cast<double>(white) / total;
    fractions.blend = static_cast<double>(blend) / total;
    fractions.boardA = static_cast<double>(boardA) / total;
    fractions.boardB = static_cast<double>(boardB) / total;
    fractions.magenta = static_cast<double>(magenta) / total;
    fractions.cyan = static_cast<double>(cyan) / total;
    fractions.orange = static_cast<double>(orange) / total;
    fractions.yellow = static_cast<double>(yellow) / total;
    return fractions;
}

FixtureCrop roleRectToCrop(
    const FixtureImage& viewport,
    const FixtureRoleRect& rect,
    float insetFraction) {
    FixtureCrop crop;
    if (viewport.empty()) {
        return crop;
    }
    const float x = std::clamp(rect[0], 0.0f, 1.0f);
    const float y = std::clamp(rect[1], 0.0f, 1.0f);
    const float width = std::clamp(rect[2], 0.0f, 1.0f - x);
    const float height = std::clamp(rect[3], 0.0f, 1.0f - y);
    const float inset = std::clamp(insetFraction, 0.0f, 0.45f);
    const float insetX = width * inset;
    const float insetY = height * inset;
    crop.x = static_cast<int>(std::floor((x + insetX) * static_cast<float>(viewport.width)));
    crop.y = static_cast<int>(std::floor((y + insetY) * static_cast<float>(viewport.height)));
    crop.width = static_cast<int>(std::floor(
        (width - insetX * 2.0f) * static_cast<float>(viewport.width)));
    crop.height = static_cast<int>(std::floor(
        (height - insetY * 2.0f) * static_cast<float>(viewport.height)));
    crop.x = std::clamp(crop.x, 0, std::max(0, viewport.width - 1));
    crop.y = std::clamp(crop.y, 0, std::max(0, viewport.height - 1));
    crop.width = std::clamp(crop.width, 1, viewport.width - crop.x);
    crop.height = std::clamp(crop.height, 1, viewport.height - crop.y);
    return crop;
}

bool validateFixtureExpectedContent(
    const FixtureImage& viewport,
    const std::string& expectedContentId,
    int logicalFrame,
    const std::map<std::string, FixtureRoleRect>& roleRects,
    ExpectedContentResult& outResult,
    std::string* outError) {
    outResult = ExpectedContentResult{};
    if (viewport.empty()) {
        return fail(outError, "fixture expected-content validation requires a viewport image");
    }
    if (expectedContentId != "a" && expectedContentId != "b") {
        return fail(
            outError,
            "fixture expected-content validation requires content id a or b");
    }
    for (const char* role :
         {"background", "board", "occluder", "hidden", "blend", "marker"}) {
        if (!roleRects.contains(role)) {
            return fail(
                outError,
                std::string("fixture handshake is missing role rect '") +
                    role + "'");
        }
    }

    const FixtureCrop full{0, 0, viewport.width, viewport.height};
    const FixtureClassFractions global = classifyFixtureRegion(viewport, full);
    requireAtLeast(outResult, "global_green_fraction", global.green, 0.15);
    int distinctClasses = 0;
    for (const double fraction :
         {global.green, global.blue, global.white, global.blend,
          global.boardA, global.boardB}) {
        if (fraction >= 0.01) {
            ++distinctClasses;
        }
    }
    requireAtLeast(
        outResult,
        "distinct_color_classes",
        static_cast<double>(distinctClasses),
        4.0);

    int minimumLuminance = 255;
    int maximumLuminance = 0;
    for (int y = 0; y < viewport.height; y += 4) {
        for (int x = 0; x < viewport.width; x += 4) {
            const Pixel pixel = pixelAt(viewport, x, y);
            const int luminance = (pixel.r + pixel.g + pixel.b) / 3;
            minimumLuminance = std::min(minimumLuminance, luminance);
            maximumLuminance = std::max(maximumLuminance, luminance);
        }
    }
    requireAtLeast(
        outResult,
        "luminance_range",
        static_cast<double>(maximumLuminance - minimumLuminance),
        20.0);

    const std::array<FixtureRoleRect, 4> backgroundCorners{{
        {0.02f, 0.02f, 0.10f, 0.10f},
        {0.88f, 0.02f, 0.10f, 0.10f},
        {0.02f, 0.88f, 0.10f, 0.10f},
        {0.88f, 0.88f, 0.10f, 0.10f}}};
    double backgroundGreen = 0.0;
    for (const FixtureRoleRect& corner : backgroundCorners) {
        backgroundGreen += classifyFixtureRegion(
            viewport,
            roleRectToCrop(viewport, corner, 0.0f)).green;
    }
    requireAtLeast(
        outResult,
        "background_green_fraction",
        backgroundGreen / static_cast<double>(backgroundCorners.size()),
        0.6);

    const FixtureRoleRect boardRect = roleRects.at("board");
    const FixtureCrop board = roleRectToCrop(viewport, boardRect, 0.15f);
    const FixtureCrop boardEdges = roleRectToCrop(viewport, boardRect, 0.05f);
    const FixtureClassFractions boardFractions =
        classifyFixtureRegion(viewport, board);
    const FixtureClassFractions boardEdgeFractions =
        classifyFixtureRegion(viewport, boardEdges);
    if (expectedContentId == "a") {
        requireAtLeast(
            outResult, "board_a_fraction", boardFractions.boardA, 0.4);
        requireAtLeast(
            outResult, "board_a_edge_fraction", boardEdgeFractions.boardA,
            0.35);
    } else {
        requireAtLeast(
            outResult, "board_b_fraction", boardFractions.boardB, 0.4);
        requireAtLeast(
            outResult, "board_b_edge_fraction", boardEdgeFractions.boardB,
            0.35);
    }

    // The board is a 2x2 checker. Quadrant sampling proves both colors exist
    // and that they occupy the expected screen-space arrangement, so a solid
    // rectangle or a flipped checker cannot satisfy the union coverage check.
    const auto quadrantCrop = [&](int column, int row) {
        const FixtureRoleRect cell{
            boardRect[0] + boardRect[2] * 0.5f * static_cast<float>(column),
            boardRect[1] + boardRect[3] * 0.5f * static_cast<float>(row),
            boardRect[2] * 0.5f,
            boardRect[3] * 0.5f};
        return roleRectToCrop(viewport, cell, 0.30f);
    };
    struct QuadrantCheck {
        const char* name;
        int column;
        int row;
        bool firstColor;
    };
    // Screen-space arrangement produced by makeCheckerTexture(): content A is
    // cyan/magenta, content B is orange/yellow, with the first texture color in
    // the bottom-left/bottom-right pair after the UV-to-screen Y flip.
    const std::array<QuadrantCheck, 4> checks =
        expectedContentId == "a"
        ? std::array<QuadrantCheck, 4>{{
              {"board_checker_tl_cyan", 0, 0, false},
              {"board_checker_tr_magenta", 1, 0, true},
              {"board_checker_bl_magenta", 0, 1, true},
              {"board_checker_br_cyan", 1, 1, false}}}
        : std::array<QuadrantCheck, 4>{{
              {"board_checker_tl_orange", 0, 0, true},
              {"board_checker_tr_yellow", 1, 0, false},
              {"board_checker_bl_yellow", 0, 1, false},
              {"board_checker_br_orange", 1, 1, true}}};
    for (const QuadrantCheck& check : checks) {
        const FixtureClassFractions fractions = classifyFixtureRegion(
            viewport, quadrantCrop(check.column, check.row));
        const double expected =
            expectedContentId == "a"
            ? (check.firstColor ? fractions.magenta : fractions.cyan)
            : (check.firstColor ? fractions.orange : fractions.yellow);
        const double other =
            expectedContentId == "a"
            ? (check.firstColor ? fractions.cyan : fractions.magenta)
            : (check.firstColor ? fractions.yellow : fractions.orange);
        requireAtLeast(outResult, check.name, expected, 0.45);
        requireAtMost(
            outResult, std::string(check.name) + "_other", other, 0.25);
    }

    const FixtureCrop occluder = roleRectToCrop(
        viewport, roleRects.at("occluder"), 0.25f);
    const FixtureClassFractions occluderFractions =
        classifyFixtureRegion(viewport, occluder);
    requireAtLeast(
        outResult,
        "occluder_blue_fraction",
        occluderFractions.blue,
        0.6);
    requireAtMost(
        outResult,
        "occluder_hidden_leak",
        occluderFractions.hidden,
        0.05);
    requireAtMost(
        outResult,
        "occluder_background_leak",
        occluderFractions.green - occluderFractions.hidden,
        0.05);

    const FixtureCrop hidden = roleRectToCrop(
        viewport, roleRects.at("hidden"), 0.05f);
    const FixtureClassFractions hiddenFractions =
        classifyFixtureRegion(viewport, hidden);
    requireAtLeast(
        outResult,
        "hidden_ring_fraction",
        hiddenFractions.hidden,
        0.30);
    requireAtMost(
        outResult,
        "hidden_bright_green_leak",
        hiddenFractions.green - hiddenFractions.hidden,
        0.15);

    const FixtureCrop blend = roleRectToCrop(
        viewport, roleRects.at("blend"), 0.20f);
    requireAtLeast(
        outResult,
        "blend_fraction",
        classifyFixtureRegion(viewport, blend).blend,
        0.5);
    const FixtureRoleRect blendRect = roleRects.at("blend");
    const FixtureRoleRect mirroredBlend{
        1.0f - (blendRect[0] + blendRect[2]),
        blendRect[1],
        blendRect[2],
        blendRect[3]};
    const FixtureCrop control = roleRectToCrop(viewport, mirroredBlend, 0.20f);
    requireAtLeast(
        outResult,
        "blend_control_green_fraction",
        classifyFixtureRegion(viewport, control).green,
        0.5);

    const FixtureCrop marker = roleRectToCrop(
        viewport, roleRects.at("marker"), 0.20f);
    const FixtureClassFractions markerFractions =
        classifyFixtureRegion(viewport, marker);
    requireAtLeast(
        outResult,
        "marker_white_fraction",
        markerFractions.white,
        0.5);
    double whiteX = 0.0;
    std::size_t whiteCount = 0u;
    for (int y = marker.y; y < marker.y + marker.height; ++y) {
        for (int x = marker.x; x < marker.x + marker.width; ++x) {
            if (!isWhiteClass(pixelAt(viewport, x, y))) {
                continue;
            }
            whiteX += static_cast<double>(x);
            ++whiteCount;
        }
    }
    const double measuredCenterX = whiteCount == 0u
        ? 0.0
        : (whiteX / static_cast<double>(whiteCount)) /
            static_cast<double>(viewport.width);
    const double expectedCenterX =
        static_cast<double>(roleRects.at("marker")[0]) +
        static_cast<double>(roleRects.at("marker")[2]) * 0.5;
    requireAtMost(
        outResult,
        "marker_center_error",
        std::abs(measuredCenterX - expectedCenterX),
        0.08);
    outResult.metrics["marker_center_x"] = measuredCenterX;
    outResult.metrics["marker_expected_center_x"] = expectedCenterX;

    const bool markerLeft = measuredCenterX < 0.45;
    const bool markerRight = measuredCenterX > 0.55;
    const bool expectLeft =
        (expectedContentId == "a" && logicalFrame == 0) ||
        (expectedContentId == "b" && logicalFrame == 30);
    if (expectLeft) {
        if (!markerLeft) {
            outResult.failures.push_back(
                "marker is not on the left side for content " +
                expectedContentId + " at frame " +
                std::to_string(logicalFrame));
        }
    } else if (!markerRight) {
        outResult.failures.push_back(
            "marker is not on the right side for content " +
            expectedContentId + " at frame " +
            std::to_string(logicalFrame));
    }

    outResult.ok = outResult.failures.empty();
    if (outError) {
        outError->clear();
    }
    return true;
}

FixtureImage buildFixtureContactSheet(
    const std::vector<std::pair<std::string, FixtureImage>>& images,
    int columns,
    int cellWidth,
    int cellHeight) {
    FixtureImage sheet;
    if (images.empty() || columns <= 0 || cellWidth <= 0 || cellHeight <= 0) {
        return sheet;
    }
    const int rowCount =
        (static_cast<int>(images.size()) + columns - 1) / columns;
    sheet.width = columns * cellWidth;
    sheet.height = rowCount * cellHeight;
    sheet.rgba.assign(
        static_cast<std::size_t>(sheet.width) *
            static_cast<std::size_t>(sheet.height) * 4u,
        0u);
    for (std::size_t index = 0u; index < images.size(); ++index) {
        const FixtureImage& source = images[index].second;
        if (source.empty()) {
            continue;
        }
        const int cellX = static_cast<int>(index) % columns;
        const int cellY = static_cast<int>(index) / columns;
        for (int y = 0; y < cellHeight; ++y) {
            const int sourceY = std::min(
                source.height - 1,
                y * source.height / cellHeight);
            for (int x = 0; x < cellWidth; ++x) {
                const int sourceX = std::min(
                    source.width - 1,
                    x * source.width / cellWidth);
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceY) *
                         static_cast<std::size_t>(source.width) +
                     static_cast<std::size_t>(sourceX)) * 4u;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(cellY * cellHeight + y) *
                         static_cast<std::size_t>(sheet.width) +
                     static_cast<std::size_t>(cellX * cellWidth + x)) * 4u;
                std::copy_n(
                    source.rgba.begin() +
                        static_cast<std::ptrdiff_t>(sourceOffset),
                    4,
                    sheet.rgba.begin() +
                        static_cast<std::ptrdiff_t>(destinationOffset));
            }
        }
    }
    return sheet;
}

namespace {

void fillSyntheticRect(
    FixtureImage& image,
    const FixtureRoleRect& rect,
    const std::array<std::uint8_t, 3>& color) {
    const int x0 = static_cast<int>(rect[0] * static_cast<float>(image.width));
    const int y0 = static_cast<int>(rect[1] * static_cast<float>(image.height));
    const int x1 = static_cast<int>(
        (rect[0] + rect[2]) * static_cast<float>(image.width));
    const int y1 = static_cast<int>(
        (rect[1] + rect[3]) * static_cast<float>(image.height));
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) * 4u;
            image.rgba[offset + 0u] = color[0];
            image.rgba[offset + 1u] = color[1];
            image.rgba[offset + 2u] = color[2];
            image.rgba[offset + 3u] = 255u;
        }
    }
}

} // namespace

bool buildSyntheticFixtureCapture(
    const std::string& contentId,
    int logicalFrame,
    int width,
    int height,
    FixtureImage& outImage,
    std::map<std::string, FixtureRoleRect>& outRoleRects,
    std::string* outError) {
    outImage = FixtureImage{};
    outRoleRects.clear();
    if (contentId != "a" && contentId != "b") {
        return fail(outError, "synthetic fixture capture requires content id a or b");
    }
    if (logicalFrame != 0 && logicalFrame != 30) {
        return fail(outError, "synthetic fixture capture requires logical frame 0 or 30");
    }
    if (width < 64 || height < 36) {
        return fail(outError, "synthetic fixture capture requires at least 64x36 pixels");
    }
    const bool contentB = contentId == "b";
    const FixtureRoleRect marker = contentB
        ? (logicalFrame == 0
              ? FixtureRoleRect{0.62f, 0.80f, 0.08f, 0.12f}
              : FixtureRoleRect{0.28f, 0.80f, 0.08f, 0.12f})
        : (logicalFrame == 0
              ? FixtureRoleRect{0.28f, 0.80f, 0.08f, 0.12f}
              : FixtureRoleRect{0.62f, 0.80f, 0.08f, 0.12f});
    outRoleRects = {
        {"background", FixtureRoleRect{0.0f, 0.0f, 1.0f, 1.0f}},
        {"board", FixtureRoleRect{0.25f, 0.25f, 0.50f, 0.50f}},
        {"hidden", FixtureRoleRect{0.40f, 0.375f, 0.20f, 0.25f}},
        {"occluder", FixtureRoleRect{0.45f, 0.425f, 0.10f, 0.15f}},
        {"blend",
         contentB
             ? FixtureRoleRect{0.13f, 0.05f, 0.25f, 0.18f}
             : FixtureRoleRect{0.62f, 0.05f, 0.25f, 0.18f}},
        {"marker", marker}};

    outImage.width = width;
    outImage.height = height;
    outImage.rgba.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        255u);
    fillSyntheticRect(outImage, outRoleRects.at("background"), {140u, 238u, 163u});

    const FixtureRoleRect board = outRoleRects.at("board");
    const FixtureRoleRect topLeft{
        board[0], board[1], board[2] * 0.5f, board[3] * 0.5f};
    const FixtureRoleRect topRight{
        board[0] + board[2] * 0.5f, board[1], board[2] * 0.5f, board[3] * 0.5f};
    const FixtureRoleRect bottomLeft{
        board[0], board[1] + board[3] * 0.5f, board[2] * 0.5f, board[3] * 0.5f};
    const FixtureRoleRect bottomRight{
        board[0] + board[2] * 0.5f,
        board[1] + board[3] * 0.5f,
        board[2] * 0.5f,
        board[3] * 0.5f};
    if (contentB) {
        fillSyntheticRect(outImage, topLeft, {235u, 150u, 60u});
        fillSyntheticRect(outImage, topRight, {235u, 225u, 110u});
        fillSyntheticRect(outImage, bottomLeft, {235u, 225u, 110u});
        fillSyntheticRect(outImage, bottomRight, {235u, 150u, 60u});
    } else {
        fillSyntheticRect(outImage, topLeft, {80u, 220u, 230u});
        fillSyntheticRect(outImage, topRight, {230u, 80u, 220u});
        fillSyntheticRect(outImage, bottomLeft, {230u, 80u, 220u});
        fillSyntheticRect(outImage, bottomRight, {80u, 220u, 230u});
    }
    fillSyntheticRect(outImage, outRoleRects.at("hidden"), {64u, 148u, 74u});
    fillSyntheticRect(outImage, outRoleRects.at("occluder"), {40u, 70u, 220u});
    fillSyntheticRect(outImage, outRoleRects.at("blend"), {222u, 199u, 122u});
    fillSyntheticRect(outImage, outRoleRects.at("marker"), {245u, 245u, 245u});
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace engine::fixture
