#include "AnimationFixtureGuard.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine::animation_fixture::verification {
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
    return pixel.g > pixel.r + 20 && pixel.g > pixel.b + 20;
}

// The rig is a red-to-yellow band; the background is green. Red dominance over
// blue separates the band from the background under either shading.
bool isRigClass(const Pixel& pixel) {
    return !isGreenClass(pixel) && pixel.r > pixel.b + 12 && pixel.r > 25;
}

bool isRedClass(const Pixel& pixel) {
    return pixel.r > pixel.g + 25 && pixel.r > pixel.b + 25;
}

bool isYellowClass(const Pixel& pixel) {
    return pixel.r > pixel.b + 25 && pixel.g > pixel.b + 25;
}

bool validCrop(const FixtureImage& image, const FixtureCrop& crop) {
    return crop.x >= 0 && crop.y >= 0 && crop.width > 0 && crop.height > 0 &&
        crop.x + crop.width <= image.width &&
        crop.y + crop.height <= image.height;
}

double fractionOf(const FixtureImage& image,
                  const FixtureCrop& crop,
                  bool (*predicate)(const Pixel&)) {
    if (!validCrop(image, crop)) {
        return 0.0;
    }
    std::size_t matches = 0u;
    const std::size_t total = static_cast<std::size_t>(crop.width) *
        static_cast<std::size_t>(crop.height);
    for (int y = crop.y; y < crop.y + crop.height; ++y) {
        for (int x = crop.x; x < crop.x + crop.width; ++x) {
            if (predicate(pixelAt(image, x, y))) {
                ++matches;
            }
        }
    }
    return total == 0u ? 0.0
                       : static_cast<double>(matches) /
            static_cast<double>(total);
}

// Share of rig pixels inside a window that also satisfy a colour predicate.
// Normalizing by rig pixels keeps endpoint windows (which straddle the band
// edge) meaningful without treating background pixels as missing colour.
double classFractionWithinRig(const FixtureImage& image,
                              const FixtureCrop& crop,
                              bool (*predicate)(const Pixel&)) {
    if (!validCrop(image, crop)) {
        return 0.0;
    }
    std::size_t rigPixels = 0u;
    std::size_t matches = 0u;
    for (int y = crop.y; y < crop.y + crop.height; ++y) {
        for (int x = crop.x; x < crop.x + crop.width; ++x) {
            const Pixel pixel = pixelAt(image, x, y);
            if (!isRigClass(pixel)) {
                continue;
            }
            ++rigPixels;
            if (predicate(pixel)) {
                ++matches;
            }
        }
    }
    return rigPixels == 0u
        ? 0.0
        : static_cast<double>(matches) / static_cast<double>(rigPixels);
}

void requireAtLeast(ExpectedContentResult& result,
                    const std::string& name,
                    double actual,
                    double minimum,
                    std::map<std::string, double>& into) {
    into[name] = actual;
    if (actual < minimum) {
        result.failures.push_back(
            name + " = " + std::to_string(actual) + " expected >= " +
            std::to_string(minimum));
    }
}

void requireAtMost(ExpectedContentResult& result,
                   const std::string& name,
                   double actual,
                   double maximum,
                   std::map<std::string, double>& into) {
    into[name] = actual;
    if (actual > maximum) {
        result.failures.push_back(
            name + " = " + std::to_string(actual) + " expected <= " +
            std::to_string(maximum));
    }
}

void requireMeasuredAtLeast(ExpectedContentResult& result,
                            const std::string& name,
                            double actual,
                            double minimum) {
    requireAtLeast(result, name, actual, minimum, result.metrics);
}

void requireMeasuredAtMost(ExpectedContentResult& result,
                           const std::string& name,
                           double actual,
                           double maximum) {
    requireAtMost(result, name, actual, maximum, result.metrics);
}

void requireExpectedAtLeast(ExpectedContentResult& result,
                            const std::string& name,
                            double actual,
                            double minimum) {
    requireAtLeast(result, name, actual, minimum, result.expectedGeometry);
}

void requireExpectedAtMost(ExpectedContentResult& result,
                           const std::string& name,
                           double actual,
                           double maximum) {
    requireAtMost(result, name, actual, maximum, result.expectedGeometry);
}

// ---------------------------------------------------------------------------
// Authored rig constants. These duplicate the public fixture's authored values
// on purpose: expected landmarks come from the authored outcome, never from the
// sampled pose under test.
// ---------------------------------------------------------------------------

const glm::vec2 kBaseJoint(-0.5f, 0.0f);
const glm::vec2 kTipJoint(2.5f, 0.0f);
constexpr float kStripHalfHeight = 0.35f;
constexpr std::array<float, 5> kStripStations{0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
constexpr std::array<float, 3> kBaseColor{0.85f, 0.08f, 0.08f};
constexpr std::array<float, 3> kTipColor{0.90f, 0.85f, 0.05f};
constexpr std::array<float, 3> kBackgroundColor{0.05f, 0.60f, 0.08f};
constexpr float kMeshOffsetX = -0.5f;
constexpr float kMeshOffsetY = 0.25f;
constexpr float kMeshScale = 0.5f;

glm::mat4 fixtureMeshTransform() {
    return glm::translate(
               glm::mat4(1.0f), glm::vec3(kMeshOffsetX, kMeshOffsetY, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(kMeshScale));
}

glm::mat4 fixtureCaseRotation(float angleDegrees) {
    return glm::rotate(
        glm::mat4(1.0f),
        glm::radians(angleDegrees),
        glm::vec3(0.0f, 0.0f, 1.0f));
}

// Analytic linear-blend-skinning position of a bind vertex.
glm::vec3 analyticBlend(const glm::vec3& bindPosition,
                        double tipWeight,
                        float angleDegrees) {
    const glm::mat4 tipTransform =
        glm::translate(glm::mat4(1.0f), glm::vec3(kTipJoint.x, kTipJoint.y, 0.0f)) *
        fixtureCaseRotation(angleDegrees) *
        glm::translate(
            glm::mat4(1.0f), glm::vec3(-kTipJoint.x, -kTipJoint.y, 0.0f));
    const glm::vec3 rigidTip =
        glm::vec3(tipTransform * glm::vec4(bindPosition, 1.0f));
    return static_cast<float>(1.0 - tipWeight) * bindPosition +
        static_cast<float>(tipWeight) * rigidTip;
}

glm::mat4 analyticViewProjection(int surfaceWidth, int surfaceHeight) {
    const float aspect = static_cast<float>(std::max(1, surfaceWidth)) /
        static_cast<float>(std::max(1, surfaceHeight));
    const glm::mat4 projection =
        glm::perspective(glm::radians(35.0f), aspect, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(
        glm::vec3(1.25f, 0.35f, 6.0f),
        glm::vec3(1.25f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

std::array<double, 2> projectNormalized(const glm::mat4& viewProjection,
                                        const glm::vec3& position) {
    const glm::vec4 clip = viewProjection * glm::vec4(position, 1.0f);
    const double w = std::abs(static_cast<double>(clip.w)) > 1.0e-6
        ? static_cast<double>(clip.w)
        : 1.0;
    const double ndcX = static_cast<double>(clip.x) / w;
    const double ndcY = static_cast<double>(clip.y) / w;
    return {
        std::clamp((ndcX + 1.0) * 0.5, 0.0, 1.0),
        std::clamp((1.0 - ndcY) * 0.5, 0.0, 1.0)};
}

double pointDistance(const std::array<double, 2>& left,
                     const std::array<double, 2>& right) {
    const double dx = left[0] - right[0];
    const double dy = left[1] - right[1];
    return std::sqrt(dx * dx + dy * dy);
}

FixtureCrop windowAround(const FixtureImage& image,
                         const std::array<double, 2>& point,
                         int radius) {
    const int centerX =
        static_cast<int>(point[0] * static_cast<double>(image.width));
    const int centerY =
        static_cast<int>(point[1] * static_cast<double>(image.height));
    const int x0 = std::max(0, centerX - radius);
    const int y0 = std::max(0, centerY - radius);
    const int x1 = std::min(image.width, centerX + radius + 1);
    const int y1 = std::min(image.height, centerY + radius + 1);
    return FixtureCrop{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

} // namespace

bool loadFixturePng(const std::string& path,
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

bool saveFixturePng(const std::string& path,
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

FixtureImage cropFixtureImage(const FixtureImage& image, const FixtureCrop& crop) {
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

bool detectFixtureViewport(const FixtureImage& image,
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
    const std::size_t total = static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height);
    if (greenPixels == 0u || maximumX < minimumX || maximumY < minimumY) {
        return fail(
            outError,
            "fixture viewport detection found no green background pixels");
    }
    const bool fullExtent = minimumX == 0 && minimumY == 0 &&
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
        return fail(
            outError,
            "fixture viewport detection found a green region that is too small");
    }
    outCrop = crop;
    if (outError) {
        outError->clear();
    }
    return true;
}

bool compareFixtureImages(const FixtureImage& left,
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
    const std::size_t pixelCount =
        static_cast<std::size_t>(left.width) *
        static_cast<std::size_t>(left.height);
    double absoluteError = 0.0;
    double squaredError = 0.0;
    std::size_t changedPixels = 0u;
    int maximumDifference = 0;
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const std::size_t offset = index * 4u;
        int pixelMaximum = 0;
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            const int difference = std::abs(
                static_cast<int>(left.rgba[offset + channel]) -
                static_cast<int>(right.rgba[offset + channel]));
            absoluteError += static_cast<double>(difference);
            squaredError += static_cast<double>(difference) * difference;
            maximumDifference = std::max(maximumDifference, difference);
            pixelMaximum = std::max(pixelMaximum, difference);
        }
        if (pixelMaximum > channelTolerance) {
            ++changedPixels;
        }
    }
    const double channelCount = static_cast<double>(pixelCount) * 3.0;
    outMetrics.normalizedMae = absoluteError / (channelCount * 255.0);
    outMetrics.normalizedRmse =
        std::sqrt(squaredError / channelCount) / 255.0;
    outMetrics.changedPixelRatio =
        static_cast<double>(changedPixels) / static_cast<double>(pixelCount);
    outMetrics.maxChannelDiff = maximumDifference;
    if (outError) {
        outError->clear();
    }
    return true;
}

namespace {

struct ScreenVertex {
    double x = 0.0;
    double y = 0.0;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

double edgeFunction(const ScreenVertex& a,
                    const ScreenVertex& b,
                    double px,
                    double py) {
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

void rasterizeTriangle(FixtureImage& image,
                       const ScreenVertex& a,
                       const ScreenVertex& b,
                       const ScreenVertex& c) {
    const double area = edgeFunction(a, b, c.x, c.y);
    if (std::abs(area) < 1.0e-9) {
        return;
    }
    const double minimumX = std::min({a.x, b.x, c.x});
    const double maximumX = std::max({a.x, b.x, c.x});
    const double minimumY = std::min({a.y, b.y, c.y});
    const double maximumY = std::max({a.y, b.y, c.y});
    const int x0 = std::max(0, static_cast<int>(std::floor(minimumX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minimumY)));
    const int x1 =
        std::min(image.width, static_cast<int>(std::ceil(maximumX)) + 1);
    const int y1 =
        std::min(image.height, static_cast<int>(std::ceil(maximumY)) + 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            double w0 = edgeFunction(a, b, px, py) / area;
            double w1 = edgeFunction(b, c, px, py) / area;
            double w2 = edgeFunction(c, a, px, py) / area;
            if (w0 < -1.0e-6 || w1 < -1.0e-6 || w2 < -1.0e-6) {
                continue;
            }
            w0 = std::max(0.0, w0);
            w1 = std::max(0.0, w1);
            w2 = std::max(0.0, w2);
            const double red = w0 * c.r + w1 * a.r + w2 * b.r;
            const double green = w0 * c.g + w1 * a.g + w2 * b.g;
            const double blue = w0 * c.b + w1 * a.b + w2 * b.b;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) * 4u;
            image.rgba[offset + 0u] =
                static_cast<std::uint8_t>(std::clamp(red, 0.0, 255.0));
            image.rgba[offset + 1u] =
                static_cast<std::uint8_t>(std::clamp(green, 0.0, 255.0));
            image.rgba[offset + 2u] =
                static_cast<std::uint8_t>(std::clamp(blue, 0.0, 255.0));
            image.rgba[offset + 3u] = 255u;
        }
    }
}

std::array<double, 3> vertexColor(std::size_t station) {
    const double tipWeight = static_cast<double>(
        kStripStations[station] / kStripStations.back());
    return {
        (kBaseColor[0] + (kTipColor[0] - kBaseColor[0]) * tipWeight) * 255.0,
        (kBaseColor[1] + (kTipColor[1] - kBaseColor[1]) * tipWeight) * 255.0,
        (kBaseColor[2] + (kTipColor[2] - kBaseColor[2]) * tipWeight) * 255.0};
}

glm::vec3 rigVertexPosition(std::size_t station,
                            bool top,
                            SynthDefect defect,
                            const FixtureCaseInfo& info) {
    const float x = kStripStations[station];
    const float y = top ? kStripHalfHeight : -kStripHalfHeight;
    const glm::vec3 bind(x, y, 0.0f);
    const double tipWeight = static_cast<double>(x / kStripStations.back());
    const float angleDegrees = (defect == SynthDefect::LinearizedCubic)
        ? info.linearFallbackAngleDegrees
        : info.expectedAngleDegrees;
    switch (defect) {
        case SynthDefect::WrongJoint: {
            const glm::mat4 tipTransform =
                glm::translate(
                    glm::mat4(1.0f), glm::vec3(kTipJoint.x, kTipJoint.y, 0.0f)) *
                fixtureCaseRotation(angleDegrees) *
                glm::translate(
                    glm::mat4(1.0f),
                    glm::vec3(-kTipJoint.x, -kTipJoint.y, 0.0f));
            return glm::vec3(tipTransform * glm::vec4(bind, 1.0f));
        }
        case SynthDefect::WrongSpace: {
            const glm::vec3 blended =
                analyticBlend(bind, tipWeight, angleDegrees);
            return glm::vec3(fixtureMeshTransform() * glm::vec4(blended, 1.0f));
        }
        case SynthDefect::DisabledSkinning:
            // With skinning disabled the renderer still applies the mesh
            // transform to the raw bind vertex.
            return glm::vec3(fixtureMeshTransform() * glm::vec4(bind, 1.0f));
        case SynthDefect::StalePalette:
            // mesh * bindPalette * vertex resolves to the original bind point.
            return bind;
        case SynthDefect::None:
        case SynthDefect::Blank:
        case SynthDefect::LinearizedCubic:
            break;
    }
    return analyticBlend(bind, tipWeight, angleDegrees);
}

} // namespace

FixtureExpectation computeFixtureExpectation(FixtureCase fixtureCase,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);
    const glm::mat4 viewProjection =
        analyticViewProjection(surfaceWidth, surfaceHeight);
    const float angle = info.expectedAngleDegrees;

    FixtureExpectation expectation;
    expectation.fixtureCase = fixtureCase;
    expectation.caseName = info.name;
    expectation.clipName = info.clipName;
    expectation.clipIndex = info.clipIndex;
    expectation.logicalFrame = info.logicalFrame;
    expectation.sampleTimeSec = info.timeSec;
    expectation.angleDegrees = angle;

    const auto landmark = [&](double station) {
        const double tipWeight = station / kStripStations.back();
        return analyticBlend(
            glm::vec3(static_cast<float>(station), 0.0f, 0.0f),
            tipWeight,
            angle);
    };
    const glm::vec3 basePoint = landmark(0.0);
    const glm::vec3 middlePoint = landmark(1.0);
    const glm::vec3 tipPoint = landmark(2.0);
    const glm::mat4 tipTransform =
        glm::translate(
            glm::mat4(1.0f), glm::vec3(kTipJoint.x, kTipJoint.y, 0.0f)) *
        fixtureCaseRotation(angle) *
        glm::translate(
            glm::mat4(1.0f), glm::vec3(-kTipJoint.x, -kTipJoint.y, 0.0f));

    expectation.base = projectNormalized(viewProjection, basePoint);
    expectation.middle = projectNormalized(viewProjection, middlePoint);
    expectation.tip = projectNormalized(viewProjection, tipPoint);
    expectation.bindBase =
        projectNormalized(viewProjection, glm::vec3(0.0f, 0.0f, 0.0f));
    expectation.bindMiddle =
        projectNormalized(viewProjection, glm::vec3(1.0f, 0.0f, 0.0f));
    expectation.bindTip =
        projectNormalized(viewProjection, glm::vec3(2.0f, 0.0f, 0.0f));
    expectation.rigidTipBase = projectNormalized(
        viewProjection,
        glm::vec3(tipTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    expectation.rigidMiddleBind = expectation.bindMiddle;
    expectation.rigidMiddleTip = projectNormalized(
        viewProjection,
        glm::vec3(tipTransform * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));

    const glm::mat4 mesh = fixtureMeshTransform();
    const glm::mat4 inverseMesh = glm::inverse(mesh);
    // Node globals at bind: the base joint sits at (-0.5, 0, 0) and the tip
    // joint at (2.5, 0, 0) before the case rotation.
    const glm::mat4 baseGlobal = glm::translate(
        glm::mat4(1.0f), glm::vec3(kBaseJoint.x, kBaseJoint.y, 0.0f));
    const glm::mat4 tipGlobal =
        glm::translate(
            glm::mat4(1.0f), glm::vec3(kTipJoint.x, kTipJoint.y, 0.0f)) *
        fixtureCaseRotation(angle);
    const glm::mat4 palette0 = inverseMesh * baseGlobal *
        glm::translate(
            glm::mat4(1.0f), glm::vec3(-kBaseJoint.x, -kBaseJoint.y, 0.0f));
    const glm::mat4 palette1 = inverseMesh * tipGlobal *
        glm::translate(
            glm::mat4(1.0f), glm::vec3(-kTipJoint.x, -kTipJoint.y, 0.0f));
    for (std::size_t index = 0; index < 16u; ++index) {
        expectation.meshTransform[index] = glm::value_ptr(mesh)[index];
        expectation.palette[index] = glm::value_ptr(palette0)[index];
        expectation.palette[index + 16u] = glm::value_ptr(palette1)[index];
    }
    return expectation;
}

std::vector<AuthoredVertex> authoredFixtureVertices() {
    std::vector<AuthoredVertex> vertices;
    for (const float station : kStripStations) {
        const double tipWeight = station / kStripStations.back();
        for (int row = 0; row < 2; ++row) {
            AuthoredVertex vertex;
            vertex.x = station;
            vertex.y = row == 0 ? -kStripHalfHeight : kStripHalfHeight;
            vertex.joints[0] = 0;
            vertex.joints[1] = 1;
            vertex.joints[2] = 0;
            vertex.joints[3] = 0;
            vertex.weights[0] = 1.0 - tipWeight;
            vertex.weights[1] = tipWeight;
            vertex.weights[2] = 0.0;
            vertex.weights[3] = 0.0;
            vertices.push_back(vertex);
        }
    }
    return vertices;
}

bool parseSynthDefect(const std::string& name, SynthDefect& outDefect) {
    if (name == "none") {
        outDefect = SynthDefect::None;
        return true;
    }
    if (name == "disabled-skinning") {
        outDefect = SynthDefect::DisabledSkinning;
        return true;
    }
    if (name == "stale-palette") {
        outDefect = SynthDefect::StalePalette;
        return true;
    }
    if (name == "wrong-space") {
        outDefect = SynthDefect::WrongSpace;
        return true;
    }
    if (name == "wrong-joint") {
        outDefect = SynthDefect::WrongJoint;
        return true;
    }
    if (name == "blank") {
        outDefect = SynthDefect::Blank;
        return true;
    }
    if (name == "linearized-cubic") {
        outDefect = SynthDefect::LinearizedCubic;
        return true;
    }
    return false;
}

const char* synthDefectName(SynthDefect defect) {
    switch (defect) {
        case SynthDefect::None:
            return "none";
        case SynthDefect::DisabledSkinning:
            return "disabled-skinning";
        case SynthDefect::StalePalette:
            return "stale-palette";
        case SynthDefect::WrongSpace:
            return "wrong-space";
        case SynthDefect::WrongJoint:
            return "wrong-joint";
        case SynthDefect::Blank:
            return "blank";
        case SynthDefect::LinearizedCubic:
            return "linearized-cubic";
    }
    return "unknown";
}

bool buildSyntheticFixtureCapture(FixtureCase fixtureCase,
                                  SynthDefect defect,
                                  int width,
                                  int height,
                                  FixtureImage& outImage,
                                  std::string* outError) {
    if (width <= 0 || height <= 0) {
        return fail(outError, "synthetic capture requires a positive surface");
    }
    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);
    outImage = FixtureImage{};
    outImage.width = width;
    outImage.height = height;
    outImage.rgba.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        255u);
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(width) *
             static_cast<std::size_t>(height);
         ++index) {
        const std::size_t offset = index * 4u;
        outImage.rgba[offset + 0u] = static_cast<std::uint8_t>(
            kBackgroundColor[0] * 255.0f);
        outImage.rgba[offset + 1u] = static_cast<std::uint8_t>(
            kBackgroundColor[1] * 255.0f);
        outImage.rgba[offset + 2u] = static_cast<std::uint8_t>(
            kBackgroundColor[2] * 255.0f);
    }
    if (defect == SynthDefect::Blank) {
        if (outError) {
            outError->clear();
        }
        return true;
    }

    const glm::mat4 viewProjection = analyticViewProjection(width, height);
    std::vector<std::array<ScreenVertex, 2>> columns;
    for (std::size_t station = 0; station < kStripStations.size(); ++station) {
        const std::array<double, 3> color = vertexColor(station);
        std::array<ScreenVertex, 2> column;
        for (int row = 0; row < 2; ++row) {
            const glm::vec3 world = rigVertexPosition(
                station, row == 1, defect, info);
            const std::array<double, 2> projected =
                projectNormalized(viewProjection, world);
            ScreenVertex vertex;
            vertex.x = projected[0] * static_cast<double>(width);
            vertex.y = projected[1] * static_cast<double>(height);
            vertex.r = color[0];
            vertex.g = color[1];
            vertex.b = color[2];
            column[row] = vertex;
        }
        columns.push_back(column);
    }
    for (std::size_t index = 0; index + 1u < columns.size(); ++index) {
        const ScreenVertex bottomLeft = columns[index][0];
        const ScreenVertex topLeft = columns[index][1];
        const ScreenVertex bottomRight = columns[index + 1u][0];
        const ScreenVertex topRight = columns[index + 1u][1];
        rasterizeTriangle(outImage, bottomLeft, bottomRight, topRight);
        rasterizeTriangle(outImage, bottomLeft, topRight, topLeft);
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool validateFixtureExpectedContent(const FixtureImage& viewport,
                                    const FixtureExpectation& expectation,
                                    ExpectedContentResult& outResult,
                                    std::string* outError) {
    outResult = ExpectedContentResult{};
    if (viewport.empty()) {
        return fail(
            outError,
            "fixture expected-content validation requires a viewport image");
    }
    const FixtureCrop full{0, 0, viewport.width, viewport.height};
    const double greenFraction = fractionOf(viewport, full, isGreenClass);
    const double rigFraction = fractionOf(viewport, full, isRigClass);
    requireMeasuredAtLeast(
        outResult, "background_green_fraction", greenFraction, 0.5);
    requireMeasuredAtLeast(
        outResult, "rig_coverage_fraction", rigFraction, 0.004);
    requireMeasuredAtMost(
        outResult, "rig_coverage_fraction", rigFraction, 0.5);

    const int window =
        std::max(2, std::min(viewport.width, viewport.height) / 150);
    const auto rigAt = [&](const std::array<double, 2>& point) {
        return fractionOf(
            viewport, windowAround(viewport, point, window), isRigClass);
    };
    const auto greenAt = [&](const std::array<double, 2>& point) {
        return fractionOf(
            viewport, windowAround(viewport, point, window), isGreenClass);
    };
    // The endpoint landmarks sit exactly on the band edge, so roughly half of
    // their window is background even for a correct capture. A displaced or
    // missing band still scores near zero here.
    requireMeasuredAtLeast(
        outResult,
        "base_landmark_rig_fraction",
        rigAt(expectation.base),
        0.3);
    requireMeasuredAtLeast(
        outResult,
        "middle_landmark_rig_fraction",
        rigAt(expectation.middle),
        0.6);
    requireMeasuredAtLeast(
        outResult,
        "tip_landmark_rig_fraction",
        rigAt(expectation.tip),
        0.3);
    requireMeasuredAtLeast(
        outResult,
        "base_landmark_red_fraction",
        classFractionWithinRig(
            viewport,
            windowAround(viewport, expectation.base, window),
            isRedClass),
        0.5);
    requireMeasuredAtLeast(
        outResult,
        "tip_landmark_yellow_fraction",
        classFractionWithinRig(
            viewport,
            windowAround(viewport, expectation.tip, window),
            isYellowClass),
        0.5);

    const double baseAnchor = pointDistance(expectation.base, expectation.bindBase);
    requireExpectedAtMost(
        outResult, "base_anchor_shift", baseAnchor, 0.012);

    const bool posed = std::abs(expectation.angleDegrees) > 1.0f;
    const double tipMotion = pointDistance(expectation.tip, expectation.bindTip);
    if (posed) {
        requireExpectedAtLeast(outResult, "tip_motion", tipMotion, 0.03);
    } else {
        requireExpectedAtMost(outResult, "tip_motion", tipMotion, 0.012);
    }
    const double tipRise = expectation.tip[1] - expectation.base[1];
    if (posed) {
        requireExpectedAtMost(
            outResult, "tip_vertical_rise", tipRise, -0.02);
    } else {
        requireExpectedAtMost(
            outResult, "tip_vertical_rise", std::abs(tipRise), 0.012);
    }

    // A rigid base (everything following the tip joint) and a rigid middle (all
    // vertices pinned to either single joint) must leave visible background at
    // the positions those mistakes would occupy.
    const double rigidBaseSeparation =
        pointDistance(expectation.rigidTipBase, expectation.base);
    outResult.expectedGeometry["rigid_base_separation"] = rigidBaseSeparation;
    if (posed && rigidBaseSeparation >= 0.05) {
        requireMeasuredAtLeast(
            outResult,
            "rigid_base_alternative_background_fraction",
            greenAt(expectation.rigidTipBase),
            0.6);
    }
    const double rigidMiddleBindSeparation =
        pointDistance(expectation.rigidMiddleBind, expectation.middle);
    outResult.expectedGeometry["rigid_middle_bind_separation"] =
        rigidMiddleBindSeparation;
    if (posed && rigidMiddleBindSeparation >= 0.02) {
        requireMeasuredAtLeast(
            outResult,
            "rigid_middle_bind_background_fraction",
            greenAt(expectation.rigidMiddleBind),
            0.6);
    }
    const double rigidMiddleTipSeparation =
        pointDistance(expectation.rigidMiddleTip, expectation.middle);
    outResult.expectedGeometry["rigid_middle_tip_separation"] =
        rigidMiddleTipSeparation;
    if (posed && rigidMiddleTipSeparation >= 0.02) {
        requireMeasuredAtLeast(
            outResult,
            "rigid_middle_tip_background_fraction",
            greenAt(expectation.rigidMiddleTip),
            0.6);
    }

    outResult.ok = outResult.failures.empty();
    if (outError) {
        outError->clear();
    }
    return true;
}

namespace {

constexpr std::uint32_t kSha256RoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

void sha256Transform(std::uint32_t state[8], const unsigned char block[64]) {
    std::uint32_t schedule[64];
    for (int index = 0; index < 16; ++index) {
        schedule[index] =
            (static_cast<std::uint32_t>(block[index * 4 + 0]) << 24u) |
            (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16u) |
            (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8u) |
            static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (int index = 16; index < 64; ++index) {
        const std::uint32_t s0 = rotateRight(schedule[index - 15], 7) ^
            rotateRight(schedule[index - 15], 18) ^
            (schedule[index - 15] >> 3u);
        const std::uint32_t s1 = rotateRight(schedule[index - 2], 17) ^
            rotateRight(schedule[index - 2], 19) ^
            (schedule[index - 2] >> 10u);
        schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (int index = 0; index < 64; ++index) {
        const std::uint32_t s1 =
            rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 =
            h + s1 + choose + kSha256RoundConstants[index] + schedule[index];
        const std::uint32_t s0 =
            rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

std::string sha256Hex(const std::string& bytes) {
    std::uint32_t state[8] = {
        0x6a09e667u,
        0xbb67ae85u,
        0x3c6ef372u,
        0xa54ff53au,
        0x510e527fu,
        0x9b05688cu,
        0x1f83d9abu,
        0x5be0cd19u};
    std::vector<unsigned char> padded(bytes.begin(), bytes.end());
    const std::uint64_t bitLength =
        static_cast<std::uint64_t>(bytes.size()) * 8u;
    padded.push_back(0x80u);
    while (padded.size() % 64u != 56u) {
        padded.push_back(0u);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<unsigned char>(
            (bitLength >> static_cast<unsigned int>(shift)) & 0xffu));
    }
    for (std::size_t offset = 0; offset < padded.size(); offset += 64u) {
        sha256Transform(state, padded.data() + offset);
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64u);
    for (const std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(word >> static_cast<unsigned int>(shift)) & 0xfu]);
        }
    }
    return out;
}

bool sha256FileHex(const std::string& path,
                   std::string& outHex,
                   std::string* outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail(outError, "could not open file for hashing: " + path);
    }
    std::string bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (input.bad()) {
        return fail(outError, "could not read file for hashing: " + path);
    }
    outHex = sha256Hex(bytes);
    if (outError) {
        outError->clear();
    }
    return true;
}

bool validateFixtureHandshake(const std::string& handshakePath,
                              FixtureCase fixtureCase,
                              int surfaceWidth,
                              int surfaceHeight,
                              HandshakeCheckResult& outResult,
                              std::string* outError,
                              const std::string& expectedApi,
                              const std::string& expectedHost) {
    outResult = HandshakeCheckResult{};
    std::ifstream input(handshakePath, std::ios::binary);
    if (!input) {
        return fail(
            outError, "could not open animation fixture handshake: " + handshakePath);
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            outError,
            std::string("animation fixture handshake JSON parse failed: ") +
                exception.what());
    }
    const FixtureExpectation expectation =
        computeFixtureExpectation(fixtureCase, surfaceWidth, surfaceHeight);
    const auto expect = [&outResult](bool condition, const std::string& message) {
        if (!condition) {
            outResult.failures.push_back(message);
        }
    };
    const auto finite = [&expect](
                            const std::vector<float>& values,
                            const std::string& name) {
        bool allFinite = true;
        for (const float value : values) {
            if (!std::isfinite(value)) {
                allFinite = false;
            }
        }
        expect(allFinite, name + " must contain only finite values");
    };

    try {
        expect(
            document.value("schema", std::string{}) ==
                "phlosion-animation-fixture-handshake-v1",
            "handshake schema is not phlosion-animation-fixture-handshake-v1");
        expect(
            document.contains("host") && document.at("host").is_string() &&
                !document.at("host").get<std::string>().empty(),
            "handshake is missing its host");
        if (!expectedHost.empty()) {
            expect(
                document.value("host", std::string{}) == expectedHost,
                "handshake host does not match the matrix host");
        }
        expect(
            document.value("case", std::string{}) == expectation.caseName,
            "handshake case does not match the requested case");
        // The requested backend must be recorded explicitly; a missing field
        // must not silently fall back to the expected value.
        expect(
            document.contains("requested_backend") &&
                document.at("requested_backend").is_string(),
            "handshake is missing its requested backend");
        if (!expectedApi.empty()) {
            expect(
                document.value("actual_backend", std::string{}) == expectedApi,
                "handshake actual backend does not match the requested API");
            expect(
                document.value("requested_backend", std::string{}) ==
                    expectedApi,
                "handshake requested backend does not match the requested API");
        }
        const auto& animation = document.at("animation");
        expect(
            animation.at("clip").get<std::string>() == expectation.clipName,
            "handshake clip does not match the case catalog");
        expect(
            std::abs(
                animation.at("sample_time_sec").get<double>() -
                static_cast<double>(expectation.sampleTimeSec)) <= 1.0e-6,
            "handshake sample time does not match the case catalog");
        expect(
            std::isfinite(animation.at("sample_time_sec").get<double>()) &&
                std::isfinite(
                    animation.at("expected_angle_degrees").get<double>()),
            "handshake animation timing values must be finite");
        expect(
            animation.at("clip_index").get<int>() == expectation.clipIndex,
            "handshake clip index does not match the case catalog");
        expect(
            animation.at("logical_frame").get<int>() ==
                expectation.logicalFrame,
            "handshake logical frame does not match the case catalog");
        expect(
            std::abs(
                animation.at("expected_angle_degrees").get<double>() -
                static_cast<double>(expectation.angleDegrees)) <= 1.0e-3,
            "handshake authored angle does not match the case catalog");

        const auto& presentation = document.at("presentation");
        outResult.metrics["bind_frames"] =
            presentation.at("bind_frames").get<double>();
        outResult.metrics["pose_frames"] =
            presentation.at("pose_frames").get<double>();
        expect(
            presentation.at("gpu_skinning").get<bool>(),
            "handshake does not report submitted GPU skinning");
        expect(
            presentation.at("geometry_key").get<std::string>() ==
                "fixture/anim/strip",
            "handshake geometry key is not the shared rig strip");
        expect(
            presentation.at("joint_count").get<int>() == 2 &&
                presentation.at("skin_matrix_count").get<int>() == 2,
            "handshake palette does not carry exactly two joint matrices");
        expect(
            presentation.at("bind_frames").get<int>() >= 1 &&
                presentation.at("pose_frames").get<int>() >= 1,
            "handshake does not show bind and requested-pose presentation");
        // The submitted palette allocation must be the same one for the bind
        // and requested draws, so the transition is an in-place value update.
        const std::string paletteAddress =
            presentation.value("palette_address", std::string{});
        const std::string bindDrawAddress =
            presentation.value("bind_draw_palette_address", std::string{});
        const std::string poseDrawAddress =
            presentation.value("pose_draw_palette_address", std::string{});
        expect(
            !paletteAddress.empty() && !bindDrawAddress.empty() &&
                !poseDrawAddress.empty(),
            "handshake is missing its submitted palette addresses");
        expect(
            bindDrawAddress == paletteAddress && poseDrawAddress == paletteAddress,
            "handshake palette storage changed between the bind and requested draws");
        outResult.metrics["palette_address"] =
            paletteAddress.empty()
            ? 0.0
            : static_cast<double>(
                  std::strtoull(paletteAddress.c_str(), nullptr, 16));

        const auto& surface = document.at("surface");
        expect(
            surface.at("width").get<int>() == surfaceWidth &&
                surface.at("height").get<int>() == surfaceHeight,
            "handshake surface does not match the validated viewport");

        const std::vector<float> meshTransform =
            document.at("mesh_transform").get<std::vector<float>>();
        expect(meshTransform.size() == 16u, "handshake mesh transform is not 4x4");
        finite(meshTransform, "handshake mesh transform");
        double maximumMeshError = 0.0;
        if (meshTransform.size() == 16u) {
            for (std::size_t index = 0; index < 16u; ++index) {
                maximumMeshError = std::max(
                    maximumMeshError,
                    std::abs(
                        static_cast<double>(meshTransform[index]) -
                        static_cast<double>(expectation.meshTransform[index])));
            }
        }
        outResult.metrics["mesh_transform_max_error"] = maximumMeshError;
        expect(
            maximumMeshError <= 1.0e-4,
            "handshake mesh transform does not match the authored transform");

        const std::vector<float> palette =
            document.at("palette").get<std::vector<float>>();
        expect(palette.size() == 32u, "handshake palette is not two 4x4 matrices");
        finite(palette, "handshake palette");
        double maximumPaletteError = 0.0;
        if (palette.size() == 32u) {
            for (std::size_t index = 0; index < 32u; ++index) {
                maximumPaletteError = std::max(
                    maximumPaletteError,
                    std::abs(
                        static_cast<double>(palette[index]) -
                        static_cast<double>(expectation.palette[index])));
            }
        }
        outResult.metrics["palette_max_error"] = maximumPaletteError;
        expect(
            maximumPaletteError <= 1.0e-3,
            "handshake palette is not inverse(mesh) * jointGlobal * inverseBind "
            "for the authored pose");

        const std::vector<float> bindPalette =
            document.at("bind_palette").get<std::vector<float>>();
        finite(bindPalette, "handshake bind palette");
        const FixtureExpectation bindExpectation =
            computeFixtureExpectation(FixtureCase::Bind, surfaceWidth, surfaceHeight);
        double maximumBindError = 0.0;
        if (bindPalette.size() == 32u) {
            for (std::size_t index = 0; index < 32u; ++index) {
                maximumBindError = std::max(
                    maximumBindError,
                    std::abs(
                        static_cast<double>(bindPalette[index]) -
                        static_cast<double>(bindExpectation.palette[index])));
            }
        } else {
            maximumBindError = 1.0e9;
        }
        outResult.metrics["bind_palette_max_error"] = maximumBindError;
        expect(
            maximumBindError <= 1.0e-3,
            "handshake bind palette is not the authored bind palette");

        // Independent transition evidence: the requested palette must differ
        // from the recorded bind palette exactly when the case is posed.
        double paletteDelta = 0.0;
        if (palette.size() == bindPalette.size()) {
            for (std::size_t index = 0; index < palette.size(); ++index) {
                paletteDelta = std::max(
                    paletteDelta,
                    std::abs(
                        static_cast<double>(palette[index]) -
                        static_cast<double>(bindPalette[index])));
            }
        } else {
            paletteDelta = 1.0e9;
        }
        outResult.metrics["palette_delta_from_bind"] = paletteDelta;
        const bool posedCase =
            std::abs(static_cast<double>(expectation.angleDegrees)) > 1.0;
        const bool reportedChange =
            presentation.value("palette_contents_changed", false);
        if (posedCase) {
            expect(
                paletteDelta > 1.0e-3,
                "handshake requested palette still equals the bind palette");
            expect(
                reportedChange,
                "handshake did not report the palette contents changing");
        } else {
            expect(
                paletteDelta <= 1.0e-6,
                "handshake bind case palette differs from the bind palette");
            expect(
                !reportedChange,
                "handshake reported a palette change for the bind case");
        }

        const auto& vertices = document.at("vertices");
        const std::vector<AuthoredVertex> authored = authoredFixtureVertices();
        expect(
            vertices.is_array() && vertices.size() == authored.size(),
            "handshake vertex list does not match the authored bind vertices");
        double maximumWeightError = 0.0;
        bool allVertexValuesFinite = true;
        if (vertices.is_array() && vertices.size() == authored.size()) {
            for (std::size_t index = 0; index < authored.size(); ++index) {
                const auto& vertex = vertices.at(index);
                const std::array<double, 3> position =
                    vertex.at("position").get<std::array<double, 3>>();
                for (const double component : position) {
                    allVertexValuesFinite =
                        allVertexValuesFinite && std::isfinite(component);
                }
                maximumWeightError = std::max(
                    maximumWeightError,
                    std::abs(position[0] - authored[index].x));
                maximumWeightError = std::max(
                    maximumWeightError,
                    std::abs(position[1] - authored[index].y));
                maximumWeightError = std::max(
                    maximumWeightError,
                    std::abs(position[2]));
                const auto joints =
                    vertex.at("joints").get<std::array<int, 4>>();
                const auto weights =
                    vertex.at("weights").get<std::array<double, 4>>();
                for (std::size_t influence = 0; influence < 4u; ++influence) {
                    allVertexValuesFinite =
                        allVertexValuesFinite && std::isfinite(weights[influence]);
                    expect(
                        joints[influence] == authored[index].joints[influence],
                        "handshake vertex joint mapping does not match the rig");
                    maximumWeightError = std::max(
                        maximumWeightError,
                        std::abs(weights[influence] - authored[index].weights[influence]));
                }
            }
        }
        expect(
            allVertexValuesFinite,
            "handshake vertex positions and weights must be finite");
        outResult.metrics["vertex_max_error"] = maximumWeightError;
        expect(
            maximumWeightError <= 1.0e-5,
            "handshake vertex positions or weights do not match the authored rig");
    } catch (const nlohmann::json::exception& exception) {
        outResult.failures.push_back(
            std::string("handshake fields are invalid: ") + exception.what());
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
    const int rows = (static_cast<int>(images.size()) + columns - 1) / columns;
    sheet.width = columns * cellWidth;
    sheet.height = rows * cellHeight;
    sheet.rgba.assign(
        static_cast<std::size_t>(sheet.width) *
            static_cast<std::size_t>(sheet.height) * 4u,
        0u);
    for (std::size_t index = 0; index < images.size(); ++index) {
        const FixtureImage& image = images[index].second;
        if (image.empty()) {
            continue;
        }
        const int cellX =
            static_cast<int>(index % static_cast<std::size_t>(columns)) *
            cellWidth;
        const int cellY =
            static_cast<int>(index / static_cast<std::size_t>(columns)) *
            cellHeight;
        for (int y = 0; y < cellHeight; ++y) {
            for (int x = 0; x < cellWidth; ++x) {
                const bool border =
                    x == 0 || y == 0 || x == cellWidth - 1 ||
                    y == cellHeight - 1;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(cellY + y) *
                         static_cast<std::size_t>(sheet.width) +
                     static_cast<std::size_t>(cellX + x)) * 4u;
                if (border) {
                    sheet.rgba[destinationOffset + 0u] = 24u;
                    sheet.rgba[destinationOffset + 1u] = 24u;
                    sheet.rgba[destinationOffset + 2u] = 28u;
                    sheet.rgba[destinationOffset + 3u] = 255u;
                    continue;
                }
                const int sourceX = std::min(
                    image.width - 1,
                    static_cast<int>(
                        static_cast<double>(x) /
                        static_cast<double>(cellWidth) *
                        static_cast<double>(image.width)));
                const int sourceY = std::min(
                    image.height - 1,
                    static_cast<int>(
                        static_cast<double>(y) /
                        static_cast<double>(cellHeight) *
                        static_cast<double>(image.height)));
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceY) *
                         static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(sourceX)) * 4u;
                std::copy_n(
                    image.rgba.begin() +
                        static_cast<std::ptrdiff_t>(sourceOffset),
                    4,
                    sheet.rgba.begin() +
                        static_cast<std::ptrdiff_t>(destinationOffset));
            }
        }
    }
    return sheet;
}

} // namespace engine::animation_fixture::verification
