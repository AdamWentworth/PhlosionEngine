#include "MaterialFixtureGuard.h"

#include "MaterialFixtureAssets.h"
#include "MaterialFixturePresentation.h"

#include <nlohmann/json.hpp>

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace engine::material_fixture::verification {
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
         static_cast<std::size_t>(x)) *
        4u;
    return Pixel{static_cast<int>(image.rgba[offset + 0u]),
                 static_cast<int>(image.rgba[offset + 1u]),
                 static_cast<int>(image.rgba[offset + 2u])};
}

// The fixture's background band is a saturated light green
// (128, 213, 114) after shading. The class test intentionally accepts a band of
// that, so editor chrome and a black clear colour never satisfy it.
bool isBackgroundClass(const Pixel& pixel) {
    return pixel.g >= 100 && pixel.g > pixel.r + 25 && pixel.g > pixel.b + 25;
}

bool validCrop(const FixtureImage& image, const FixtureCrop& crop) {
    return crop.x >= 0 && crop.y >= 0 && crop.width > 0 && crop.height > 0 &&
        crop.x + crop.width <= image.width &&
        crop.y + crop.height <= image.height;
}

bool validRegion(const FixtureImage& image, const FixturePatchRegion& region) {
    return region.x >= 0 && region.y >= 0 && region.width > 0 &&
        region.height > 0 && region.x + region.width <= image.width &&
        region.y + region.height <= image.height;
}

// The documented debug-view transfer: role views whose texture format is linear
// are written in linear light and then encoded once for display, which is the
// sRGB transfer applied to the raw texel. Role views submitted as sRGB textures
// round-trip back to the authored byte.
double srgbEncodeDouble(double linear) {
    const double clamped = (std::max)(0.0, (std::min)(1.0, linear));
    if (clamped <= 0.0031308) {
        return clamped * 12.92;
    }
    return 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

int srgbEncodedByte(int raw) {
    const double linear = static_cast<double>(raw) / 255.0;
    return static_cast<int>(std::lround(srgbEncodeDouble(linear) * 255.0));
}

double linearFromSrgbByte(int encoded) {
    const double value = static_cast<double>(encoded) / 255.0;
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
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
        pixels + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(width) *
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
        const int wrote = stbi_write_png(path.c_str(),
                                         image.width,
                                         image.height,
                                         4,
                                         image.rgba.data(),
                                         image.width * 4);
        if (wrote == 0) {
            return fail(outError, "could not write fixture PNG: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(outError,
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
    out.rgba.resize(static_cast<std::size_t>(crop.width) *
                    static_cast<std::size_t>(crop.height) * 4u);
    for (int y = 0; y < crop.height; ++y) {
        const std::size_t sourceOffset =
            (static_cast<std::size_t>(crop.y + y) *
                 static_cast<std::size_t>(image.width) +
             static_cast<std::size_t>(crop.x)) *
            4u;
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(crop.width) * 4u;
        std::copy_n(image.rgba.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                    static_cast<std::ptrdiff_t>(crop.width) * 4,
                    out.rgba.begin() +
                        static_cast<std::ptrdiff_t>(destinationOffset));
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
    std::size_t backgroundPixels = 0u;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (!isBackgroundClass(pixelAt(image, x, y))) {
                continue;
            }
            ++backgroundPixels;
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
    }
    const std::size_t total = static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height);
    if (backgroundPixels == 0u || maximumX < minimumX || maximumY < minimumY) {
        return fail(outError,
                    "fixture viewport detection found no background pixels");
    }
    const bool fullExtent = minimumX == 0 && minimumY == 0 &&
        maximumX == image.width - 1 && maximumY == image.height - 1;
    if (fullExtent || backgroundPixels >= total * 2u / 5u) {
        // The standalone host fills the surface; the editor chrome case still
        // resolves to the full viewport because the background band frames every
        // patch.
        outCrop = FixtureCrop{0, 0, image.width, image.height};
        if (outError) {
            outError->clear();
        }
        return true;
    }
    const FixtureCrop crop{minimumX,
                           minimumY,
                           maximumX - minimumX + 1,
                           maximumY - minimumY + 1};
    if (crop.width < image.width / 5 || crop.height < image.height / 5) {
        return fail(
            outError,
            "fixture viewport detection found a background region that is too small");
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
    const std::size_t pixelCount = static_cast<std::size_t>(left.width) *
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
    outMetrics.normalizedRmse = std::sqrt(squaredError / channelCount) / 255.0;
    outMetrics.changedPixelRatio =
        static_cast<double>(changedPixels) / static_cast<double>(pixelCount);
    outMetrics.maxChannelDiff = maximumDifference;
    if (outError) {
        outError->clear();
    }
    return true;
}

FixturePatchRegion patchRegion(const FixtureImage& viewport,
                               const std::string& patchName) {
    FixturePatchRegion region;
    const std::map<std::string, FixturePatchRect> rects =
        computeFixturePatchRects(viewport.width, viewport.height);
    const auto found = rects.find(patchName);
    if (found == rects.end()) {
        return region;
    }
    const FixturePatchRect& rect = found->second;
    const int x0 = static_cast<int>(
        std::lround(static_cast<double>(rect[0]) * viewport.width));
    const int y0 = static_cast<int>(
        std::lround(static_cast<double>(rect[1]) * viewport.height));
    const int x1 = static_cast<int>(
        std::lround((static_cast<double>(rect[0]) + static_cast<double>(rect[2])) *
                    viewport.width));
    const int y1 = static_cast<int>(
        std::lround((static_cast<double>(rect[1]) + static_cast<double>(rect[3])) *
                    viewport.height));
    region.x = std::max(0, x0);
    region.y = std::max(0, y0);
    region.width = std::max(0, std::min(viewport.width, x1) - region.x);
    region.height = std::max(0, std::min(viewport.height, y1) - region.y);
    return region;
}

bool compareLitRegions(const FixtureImage& left,
                       const FixtureImage& right,
                       int channelTolerance,
                       ImageCompareMetrics& outMetrics,
                       std::string* outError) {
    outMetrics = ImageCompareMetrics{};
    if (left.empty() || right.empty()) {
        return fail(outError, "lit-region comparison requires two images");
    }
    if (left.width != right.width || left.height != right.height) {
        return fail(outError,
                    "lit-region comparison requires identical viewport sizes");
    }
    const FixturePatchRegion region = patchRegion(left, "lit");
    if (!validRegion(left, region)) {
        return fail(outError, "the composed lit region is not measurable");
    }
    const FixtureCrop crop{region.x, region.y, region.width, region.height};
    const FixtureImage leftLit = cropFixtureImage(left, crop);
    const FixtureImage rightLit = cropFixtureImage(right, crop);
    return compareFixtureImages(leftLit,
                                rightLit,
                                channelTolerance,
                                outMetrics,
                                outError);
}

namespace {

// Interior sampling window of one patch half: 20% vertical inset and 12% of the
// half width on each side, so native filtering at object edges never dominates a
// measurement, while every remaining pixel stays inside the authored region.
struct InteriorRect {
    int startX = 0;
    int startY = 0;
    int endX = 0;
    int endY = 0;

    bool valid() const noexcept { return endX > startX && endY > startY; }
    int width() const noexcept { return endX - startX; }
    int height() const noexcept { return endY - startY; }
};

InteriorRect patchHalfInterior(const FixtureImage& viewport,
                               const FixturePatchRegion& region,
                               PatchHalf half) {
    InteriorRect rect;
    if (!validRegion(viewport, region) || region.width < 4) {
        return rect;
    }
    const int verticalInset =
        std::max(1, static_cast<int>(std::lround(region.height * 0.20)));
    const int halfWidth = region.width / 2;
    const int horizontalInset =
        std::max(1, static_cast<int>(std::lround(halfWidth * 0.12)));
    const int startX = half == PatchHalf::Left
        ? region.x + horizontalInset
        : region.x + halfWidth + horizontalInset;
    const int endX = half == PatchHalf::Left
        ? region.x + halfWidth - horizontalInset
        : region.x + region.width - horizontalInset;
    const int startY = region.y + verticalInset;
    const int endY = region.y + region.height - verticalInset;
    if (endX <= startX || endY <= startY) {
        return InteriorRect{};
    }
    rect.startX = std::clamp(startX, 0, viewport.width);
    rect.endX = std::clamp(endX, 0, viewport.width);
    rect.startY = std::clamp(startY, 0, viewport.height);
    rect.endY = std::clamp(endY, 0, viewport.height);
    return rect;
}

} // namespace

ChannelMean samplePatchHalf(const FixtureImage& viewport,
                            const FixturePatchRegion& region,
                            PatchHalf half) {
    ChannelMean mean;
    const InteriorRect rect = patchHalfInterior(viewport, region, half);
    if (!rect.valid()) {
        return mean;
    }
    double sums[3] = {0.0, 0.0, 0.0};
    std::size_t count = 0u;
    for (int y = rect.startY; y < rect.endY; ++y) {
        for (int x = rect.startX; x < rect.endX; ++x) {
            const Pixel pixel = pixelAt(viewport, x, y);
            sums[0] += pixel.r;
            sums[1] += pixel.g;
            sums[2] += pixel.b;
            ++count;
        }
    }
    if (count == 0u) {
        return mean;
    }
    mean.samples = count;
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        mean.rgb[channel] = sums[channel] / static_cast<double>(count);
    }
    return mean;
}

std::vector<PatchExpectation> fixturePatchExpectations(FixtureCase fixtureCase) {
    std::vector<PatchExpectation> expectations;
    expectations.reserve(static_cast<std::size_t>(kFixturePatchCount));
    for (int index = 0; index < kFixturePatchCount; ++index) {
        const FixturePatch patch = static_cast<FixturePatch>(index);
        PatchExpectation expectation;
        expectation.patch = fixturePatchName(patch);
        for (const PatchHalf half : {PatchHalf::Left, PatchHalf::Right}) {
            PatchSample sample;
            if (!fixturePatchSample(fixtureCase, patch, half, sample)) {
                continue;
            }
            expectation.present = true;
            expectation.srgbView = sample.srgbView;
            expectation.source = sample.source;
            std::array<int, 3>& rawTarget =
                half == PatchHalf::Left ? expectation.rawLeft
                                        : expectation.rawRight;
            std::array<int, 3>& target =
                half == PatchHalf::Left ? expectation.left : expectation.right;
            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                const int raw = static_cast<int>(sample.rgb[channel]);
                rawTarget[channel] = raw;
                target[channel] = sample.srgbView ? raw : srgbEncodedByte(raw);
            }
        }
        expectations.push_back(std::move(expectation));
    }
    return expectations;
}

std::array<std::uint8_t, 4> fixtureRolePlaceholderPixel(FixtureRole role) {
    switch (role) {
        case FixtureRole::Emissive:
            return {0u, 0u, 0u, 255u};
        case FixtureRole::Normal:
            return {128u, 128u, 255u, 255u};
        case FixtureRole::BaseColor:
        case FixtureRole::MetallicRoughness:
        case FixtureRole::Occlusion:
        default:
            return {255u, 255u, 255u, 255u};
    }
}

namespace {

std::uint8_t authoredHalfChannel(const AuthoredRoleMap& map,
                                 bool left,
                                 std::size_t channel) {
    const std::array<std::uint8_t, 4>& half = left ? map.left : map.right;
    return half[channel];
}

// The pixels the decoder is contracted to hand the renderer for a role.
std::vector<std::uint8_t> expectedDecodedRolePixels(FixtureCase fixtureCase,
                                                    FixtureRole role) {
    if (fixtureCase == FixtureCase::AuthoredMaps) {
        return authoredRolePixels(role);
    }
    // The placeholder is a single object: taking begin() and end() from two
    // separate temporaries would build a range from mismatched iterators.
    const std::array<std::uint8_t, 4> placeholder =
        fixtureRolePlaceholderPixel(role);
    return std::vector<std::uint8_t>(placeholder.begin(), placeholder.end());
}

int expectedRoleSize(FixtureCase fixtureCase) {
    return fixtureCase == FixtureCase::AuthoredMaps ? 8 : 1;
}

// The guard's own FNV-1a over the case, role and independently authored pixels.
std::uint32_t expectedContentHash(FixtureCase fixtureCase, FixtureRole role) {
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](std::uint8_t byte) {
        hash ^= static_cast<std::uint32_t>(byte);
        hash *= 16777619u;
    };
    for (const char character : std::string(fixtureCaseName(fixtureCase))) {
        mix(static_cast<std::uint8_t>(character));
    }
    for (const char character : std::string(fixtureRoleName(role))) {
        mix(static_cast<std::uint8_t>(character));
    }
    for (const std::uint8_t byte : expectedDecodedRolePixels(fixtureCase, role)) {
        mix(byte);
    }
    return hash;
}

} // namespace

RoleMetadataContract expectedRoleMetadata(FixtureCase fixtureCase,
                                          FixtureRole role) {
    RoleMetadataContract contract;
    const AuthoredRoleMap& map = authoredRoleMap(role);
    const bool authored = fixtureCase == FixtureCase::AuthoredMaps;
    contract.width = expectedRoleSize(fixtureCase);
    contract.height = contract.width;
    contract.textureSrgb = map.textureSrgb;
    contract.wrapS = 10497;  // GL_REPEAT
    contract.wrapT = 10497;
    contract.minFilter = authored ? 9987 : 9729;
    contract.magFilter = 9729;
    if (authored) {
        contract.firstPixel = map.left;
    } else {
        contract.firstPixel = fixtureRolePlaceholderPixel(role);
    }
    contract.contentHash = expectedContentHash(fixtureCase, role);
    contract.key = std::string("fixture/mat/") +
        fixtureCaseName(fixtureCase) + "/" + fixtureRoleName(role) + "/" +
        std::to_string(contract.contentHash);
    return contract;
}

bool fixturePatchSample(FixtureCase fixtureCase,
                        FixturePatch patch,
                        PatchHalf half,
                        PatchSample& outSample) {
    outSample = PatchSample{};
    if (patch == FixturePatch::Lit) {
        return false;
    }
    const bool left = half == PatchHalf::Left;
    const bool authored = fixtureCase == FixtureCase::AuthoredMaps;
    const auto fill = [&outSample](const std::array<std::uint8_t, 3>& rgb) {
        outSample.rgb = rgb;
    };
    switch (patch) {
        case FixturePatch::BaseColor:
        case FixturePatch::Albedo: {
            const AuthoredRoleMap& map = authoredRoleMap(FixtureRole::BaseColor);
            outSample.srgbView = map.textureSrgb != 0u;
            outSample.source = authored ? "authored-base"
                : (fixtureCase == FixtureCase::EmptyImageBytes
                       ? "empty-image-bytes"
                       : "base-color-factor");
            if (authored) {
                fill({authoredHalfChannel(map, left, 0u),
                      authoredHalfChannel(map, left, 1u),
                      authoredHalfChannel(map, left, 2u)});
            } else {
                const auto placeholder = fixtureRolePlaceholderPixel(
                    FixtureRole::BaseColor);
                fill({placeholder[0], placeholder[1], placeholder[2]});
            }
            return true;
        }
        case FixturePatch::Emissive: {
            const AuthoredRoleMap& map = authoredRoleMap(FixtureRole::Emissive);
            outSample.srgbView = map.textureSrgb != 0u;
            outSample.source = authored ? "authored-emissive" : "role-fallback";
            if (authored) {
                fill({authoredHalfChannel(map, left, 0u),
                      authoredHalfChannel(map, left, 1u),
                      authoredHalfChannel(map, left, 2u)});
            } else {
                const auto placeholder =
                    fixtureRolePlaceholderPixel(FixtureRole::Emissive);
                fill({placeholder[0], placeholder[1], placeholder[2]});
            }
            return true;
        }
        case FixturePatch::Normal: {
            const AuthoredRoleMap& map = authoredRoleMap(FixtureRole::Normal);
            outSample.srgbView = map.textureSrgb != 0u;
            outSample.source = authored ? "authored-normal" : "role-fallback";
            if (authored) {
                fill({authoredHalfChannel(map, left, 0u),
                      authoredHalfChannel(map, left, 1u),
                      authoredHalfChannel(map, left, 2u)});
            } else {
                const auto placeholder =
                    fixtureRolePlaceholderPixel(FixtureRole::Normal);
                fill({placeholder[0], placeholder[1], placeholder[2]});
            }
            return true;
        }
        case FixturePatch::Roughness:
        case FixturePatch::Metallic: {
            const AuthoredRoleMap& map =
                authoredRoleMap(FixtureRole::MetallicRoughness);
            const std::size_t channelIndex =
                patch == FixturePatch::Roughness ? 1u : 2u;
            outSample.srgbView = map.textureSrgb != 0u;
            outSample.source =
                authored ? "authored-metallic-roughness" : "role-fallback";
            const std::uint8_t value = authored
                ? authoredHalfChannel(map, left, channelIndex)
                : fixtureRolePlaceholderPixel(FixtureRole::MetallicRoughness)
                      [channelIndex];
            fill({value, value, value});
            return true;
        }
        case FixturePatch::Occlusion: {
            const AuthoredRoleMap& map = authoredRoleMap(FixtureRole::Occlusion);
            outSample.srgbView = map.textureSrgb != 0u;
            outSample.source = authored ? "authored-occlusion" : "role-fallback";
            const std::uint8_t value = authored
                ? authoredHalfChannel(map, left, 0u)
                : fixtureRolePlaceholderPixel(FixtureRole::Occlusion)[0];
            fill({value, value, value});
            return true;
        }
        default:
            return false;
    }
}

namespace {

void requireCondition(ExpectedContentResult& result,
                      bool condition,
                      std::string message) {
    if (!condition) {
        result.failures.push_back(std::move(message));
    }
}

double channelDistance(const ChannelMean& left, const ChannelMean& right) {
    return (std::abs(left.rgb[0] - right.rgb[0]) +
            std::abs(left.rgb[1] - right.rgb[1]) +
            std::abs(left.rgb[2] - right.rgb[2])) /
        3.0;
}

std::string channelText(const ChannelMean& mean) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << "(" << mean.rgb[0] << ", " << mean.rgb[1] << ", " << mean.rgb[2]
           << ")";
    return stream.str();
}

std::string rgbText(const std::array<int, 3>& rgb) {
    return "(" + std::to_string(rgb[0]) + ", " + std::to_string(rgb[1]) + ", " +
        std::to_string(rgb[2]) + ")";
}

} // namespace

bool validateFixtureExpectedContent(const FixtureImage& viewport,
                                    FixtureCase fixtureCase,
                                    ExpectedContentResult& outResult,
                                    std::string* outError) {
    outResult = ExpectedContentResult{};
    if (viewport.empty()) {
        return fail(outError, "material fixture validation requires a viewport");
    }
    const std::vector<PatchExpectation> expectations =
        fixturePatchExpectations(fixtureCase);
    if (expectations.size() != static_cast<std::size_t>(kFixturePatchCount)) {
        return fail(outError, "the patch catalog is incomplete");
    }

    std::map<std::string, ChannelMean> leftMeans;
    std::map<std::string, ChannelMean> rightMeans;
    std::map<std::string, FixturePatchRegion> regions;
    for (const PatchExpectation& expectation : expectations) {
        const FixturePatchRegion region =
            patchRegion(viewport, expectation.patch);
        regions[expectation.patch] = region;
        requireCondition(outResult,
                         validRegion(viewport, region) && region.width >= 4 &&
                             region.height >= 8,
                         "patch '" + expectation.patch +
                             "' has no measurable interior region");
        const ChannelMean left = samplePatchHalf(viewport, region, PatchHalf::Left);
        const ChannelMean right =
            samplePatchHalf(viewport, region, PatchHalf::Right);
        leftMeans[expectation.patch] = left;
        rightMeans[expectation.patch] = right;
        requireCondition(outResult,
                         left.samples > 0u && right.samples > 0u,
                         "patch '" + expectation.patch + "' has no samples");
    }

    // Background band: the row above every patch must be the green background.
    ChannelMean background;
    {
        const int bandHeight = std::max(1, viewport.height / 48);
        FixturePatchRegion band;
        band.x = 0;
        band.y = 0;
        band.width = viewport.width;
        band.height = std::min(viewport.height, bandHeight);
        double sums[3] = {0.0, 0.0, 0.0};
        std::size_t count = 0u;
        for (int y = band.y; y < band.y + band.height; ++y) {
            for (int x = 0; x < viewport.width; ++x) {
                const Pixel pixel = pixelAt(viewport, x, y);
                sums[0] += pixel.r;
                sums[1] += pixel.g;
                sums[2] += pixel.b;
                ++count;
            }
        }
        background.samples = count;
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            background.rgb[channel] =
                count > 0u ? sums[channel] / static_cast<double>(count) : 0.0;
        }
        const Pixel backgroundPixel{
            static_cast<int>(std::lround(background.rgb[0])),
            static_cast<int>(std::lround(background.rgb[1])),
            static_cast<int>(std::lround(background.rgb[2]))};
        requireCondition(outResult,
                         isBackgroundClass(backgroundPixel),
                         "the fixture background band is not the expected green " +
                             channelText(background));
        outResult.metrics["background_r"] = background.rgb[0];
        outResult.metrics["background_g"] = background.rgb[1];
        outResult.metrics["background_b"] = background.rgb[2];
    }

    for (const PatchExpectation& expectation : expectations) {
        const ChannelMean& left = leftMeans[expectation.patch];
        const ChannelMean& right = rightMeans[expectation.patch];
        outResult.metrics[expectation.patch + "_left_r"] = left.rgb[0];
        outResult.metrics[expectation.patch + "_left_g"] = left.rgb[1];
        outResult.metrics[expectation.patch + "_left_b"] = left.rgb[2];
        outResult.metrics[expectation.patch + "_right_r"] = right.rgb[0];
        outResult.metrics[expectation.patch + "_right_g"] = right.rgb[1];
        outResult.metrics[expectation.patch + "_right_b"] = right.rgb[2];

        requireCondition(
            outResult,
            channelDistance(left, background) >= kPatchBackgroundMinimum,
            "patch '" + expectation.patch +
                "' left half matches the background band " + channelText(left));
        requireCondition(
            outResult,
            channelDistance(right, background) >= kPatchBackgroundMinimum,
            "patch '" + expectation.patch +
                "' right half matches the background band " + channelText(right));

        if (!expectation.present) {
            // The composed lit region has no authored per-channel value; it is
            // checked for presence, difference from the diagnostic base patch
            // and non-black content below.
            continue;
        }
        const int tolerance = expectation.srgbView ? kPatchSrgbTolerance
                                                   : kPatchLinearTolerance;
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            requireCondition(
                outResult,
                std::abs(left.rgb[channel] -
                         static_cast<double>(expectation.left[channel])) <=
                    tolerance,
                "patch '" + expectation.patch + "' left half channel " +
                    std::to_string(channel) + " is " +
                    std::to_string(static_cast<int>(std::lround(left.rgb[channel]))) +
                    " but the authored " + expectation.source + " value is " +
                    std::to_string(expectation.left[channel]));
            requireCondition(
                outResult,
                std::abs(right.rgb[channel] -
                         static_cast<double>(expectation.right[channel])) <=
                    tolerance,
                "patch '" + expectation.patch + "' right half channel " +
                    std::to_string(channel) + " is " +
                    std::to_string(static_cast<int>(std::lround(right.rgb[channel]))) +
                    " but the authored " + expectation.source + " value is " +
                    std::to_string(expectation.right[channel]));
        }
        // Spatial variation: the authored halves differ, so the measured halves
        // must differ too.
        const int authoredSeparation = (std::max)(
            std::abs(expectation.left[0] - expectation.right[0]),
            (std::max)(std::abs(expectation.left[1] - expectation.right[1]),
                       std::abs(expectation.left[2] - expectation.right[2])));
        if (authoredSeparation >= kHalfSeparationMinimum) {
            const int measuredSeparation = static_cast<int>(std::lround(
                channelDistance(left, right)));
            requireCondition(
                outResult,
                measuredSeparation >= kHalfSeparationMinimum / 3,
                "patch '" + expectation.patch +
                    "' shows no spatial variation between its halves (" +
                    channelText(left) + " vs " + channelText(right) + ")");
        }
        // Color-space treatment: an sRGB role view must not be rendered as if it
        // were linear, and a linear role view must carry the single sRGB encode.
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            const int rawValues[2] = {expectation.rawLeft[channel],
                                      expectation.rawRight[channel]};
            const double measured[2] = {left.rgb[channel], right.rgb[channel]};
            const int expectedValues[2] = {expectation.left[channel],
                                           expectation.right[channel]};
            const char* halves[2] = {"left", "right"};
            for (int half = 0; half < 2; ++half) {
                // The wrong interpretation of the same texel: a linear role
                // view rendered without its sRGB encode, or an sRGB role view
                // rendered through an extra encode.
                const int alternative = expectation.srgbView
                    ? srgbEncodedByte(rawValues[half])
                    : rawValues[half];
                if (std::abs(expectedValues[half] - alternative) < 24) {
                    continue;
                }
                requireCondition(
                    outResult,
                    std::abs(measured[half] - static_cast<double>(alternative)) >
                        12.0,
                    "patch '" + expectation.patch + "' " + halves[half] +
                        " half channel " + std::to_string(channel) +
                        " carries the wrong color-space treatment: " +
                        std::to_string(static_cast<int>(std::lround(measured[half]))) +
                        " is consistent with the untransformed value " +
                        std::to_string(alternative) + " instead of the authored " +
                        std::to_string(expectedValues[half]));
            }
        }

        // Per-pixel interior agreement against the literal expected colour.
        // Every interior pixel must match within the tight tolerance, so a
        // corruption that preserves the half mean (checkerboards, mirrored
        // halves, stripes) is rejected even though the mean still agrees.
        for (const PatchHalf half : {PatchHalf::Left, PatchHalf::Right}) {
            const InteriorRect interior =
                patchHalfInterior(viewport, regions.at(expectation.patch), half);
            const std::array<int, 3>& expected =
                half == PatchHalf::Left ? expectation.left : expectation.right;
            const char* halfName =
                half == PatchHalf::Left ? "left" : "right";
            if (!interior.valid()) {
                requireCondition(outResult,
                                 false,
                                 "patch '" + expectation.patch + "' " + halfName +
                                     " half has no interior pixels");
                continue;
            }
            int maximumDeviation = 0;
            int violations = 0;
            int worstX = -1;
            int worstY = -1;
            for (int y = interior.startY; y < interior.endY; ++y) {
                for (int x = interior.startX; x < interior.endX; ++x) {
                    const Pixel pixel = pixelAt(viewport, x, y);
                    const int values[3] = {pixel.r, pixel.g, pixel.b};
                    for (std::size_t channel = 0u; channel < 3u; ++channel) {
                        const int deviation =
                            std::abs(values[channel] - expected[channel]);
                        if (deviation > maximumDeviation) {
                            maximumDeviation = deviation;
                            worstX = x;
                            worstY = y;
                        }
                        if (deviation > kInteriorPixelTolerance) {
                            ++violations;
                        }
                    }
                }
            }
            outResult.metrics[expectation.patch + "_" + halfName + "_maxdev"] =
                static_cast<double>(maximumDeviation);
            requireCondition(
                outResult,
                violations == 0,
                "patch '" + expectation.patch + "' " + halfName +
                    " half has " + std::to_string(violations) +
                    " interior channel samples beyond the " +
                    std::to_string(kInteriorPixelTolerance) +
                    " count tolerance (worst " +
                    std::to_string(maximumDeviation) + " at " +
                    std::to_string(worstX) + "," + std::to_string(worstY) +
                    ", expected " + rgbText(expected) + ")");
        }
    }

    // Diagnostic role routing for the metallic-roughness pair: roughness is the
    // green channel and metallic the blue channel of the same map, and the
    // authored map inverts which of the two is larger between its halves.
    if (fixtureCase == FixtureCase::AuthoredMaps) {
        const ChannelMean& roughnessLeft = leftMeans.at("roughness");
        const ChannelMean& roughnessRight = rightMeans.at("roughness");
        const ChannelMean& metallicLeft = leftMeans.at("metallic");
        const ChannelMean& metallicRight = rightMeans.at("metallic");
        requireCondition(outResult,
                         roughnessRight.rgb[0] - roughnessLeft.rgb[0] >=
                             kHalfSeparationMinimum,
                         "the roughness patch does not rise from its left to its "
                         "right half: " + channelText(roughnessLeft) + " -> " +
                             channelText(roughnessRight));
        requireCondition(outResult,
                         metallicLeft.rgb[0] - metallicRight.rgb[0] >=
                             kHalfSeparationMinimum,
                         "the metallic patch does not fall from its left to its "
                         "right half: " + channelText(metallicLeft) + " -> " +
                             channelText(metallicRight));
        requireCondition(outResult,
                         std::abs(roughnessLeft.rgb[0] - metallicLeft.rgb[0]) >=
                             kHalfSeparationMinimum,
                         "roughness and metallic are not separated in the left "
                         "half: " + channelText(roughnessLeft) + " vs " +
                             channelText(metallicLeft));
        requireCondition(outResult,
                         std::abs(roughnessRight.rgb[0] - metallicRight.rgb[0]) >=
                             kHalfSeparationMinimum,
                         "roughness and metallic are not separated in the right "
                         "half: " + channelText(roughnessRight) + " vs " +
                             channelText(metallicRight));
        // The measured display values must unpick to the authored linear
        // channels, which is a channel-routing check independent of the sRGB
        // transfer being applied exactly.
        const AuthoredRoleMap& mr =
            authoredRoleMap(FixtureRole::MetallicRoughness);
        const double roughnessLeftLinear =
            linearFromSrgbByte(static_cast<int>(std::lround(roughnessLeft.rgb[0])));
        const double metallicLeftLinear =
            linearFromSrgbByte(static_cast<int>(std::lround(metallicLeft.rgb[0])));
        requireCondition(
            outResult,
            std::abs(roughnessLeftLinear - mr.left[1] / 255.0) <= 0.08,
            "the roughness patch does not carry the metallic-roughness green "
            "channel: linear " + std::to_string(roughnessLeftLinear) + " vs " +
                std::to_string(mr.left[1] / 255.0));
        requireCondition(
            outResult,
            std::abs(metallicLeftLinear - mr.left[2] / 255.0) <= 0.08,
            "the metallic patch does not carry the metallic-roughness blue "
            "channel: linear " + std::to_string(metallicLeftLinear) + " vs " +
                std::to_string(mr.left[2] / 255.0));
    }

    // Corrected unavailable-bytes placeholders: a normal patch that is black or
    // a metallic-roughness/occlusion patch that is black is the pre-correction
    // decoder behaviour, not the corrected role fallback.
    if (fixtureCase != FixtureCase::AuthoredMaps) {
        const ChannelMean& normal = leftMeans.at("normal");
        requireCondition(outResult,
                         normal.rgb[0] >= 120.0 && normal.rgb[1] >= 120.0 &&
                             normal.rgb[2] >= 180.0,
                         "the normal patch is not the corrected flat-normal "
                         "placeholder: " + channelText(normal));
        for (const char* patch : {"roughness", "metallic", "occlusion"}) {
            const ChannelMean& mean = leftMeans.at(patch);
            requireCondition(outResult,
                             mean.rgb[0] >= 200.0 && mean.rgb[1] >= 200.0 &&
                                 mean.rgb[2] >= 200.0,
                             std::string("the ") + patch +
                                 " patch is not the corrected opaque-white "
                                 "placeholder: " + channelText(mean));
        }
        const ChannelMean& emissive = leftMeans.at("emissive");
        requireCondition(outResult,
                         emissive.rgb[0] <= 20.0 && emissive.rgb[1] <= 20.0 &&
                             emissive.rgb[2] <= 20.0,
                         "the emissive patch is not the black placeholder: " +
                             channelText(emissive));
    }

    // Composed lit region: present, different from the background band, and
    // different from the diagnostic base patch.
    {
        const ChannelMean& litLeft = leftMeans.at("lit");
        const ChannelMean& litRight = rightMeans.at("lit");
        const ChannelMean& baseLeft = leftMeans.at("base");
        const ChannelMean& baseRight = rightMeans.at("base");
        requireCondition(outResult,
                         channelDistance(litLeft, background) >=
                                 kPatchBackgroundMinimum &&
                             channelDistance(litRight, background) >=
                                 kPatchBackgroundMinimum,
                         "the composed lit patch matches the background band");
        requireCondition(outResult,
                         channelDistance(litLeft, baseLeft) >= 8.0 ||
                             channelDistance(litRight, baseRight) >= 8.0,
                         "the composed lit patch is identical to the raw base "
                         "color patch, so no lighting was applied");
        const double litLuma = (litLeft.rgb[0] + litLeft.rgb[1] + litLeft.rgb[2] +
                                litRight.rgb[0] + litRight.rgb[1] +
                                litRight.rgb[2]) /
            6.0;
        requireCondition(outResult,
                         litLuma >= 20.0,
                         "the composed lit patch is black");
        outResult.metrics["lit_luma"] = litLuma;
        outResult.metrics["lit_vs_base_left"] = channelDistance(litLeft, baseLeft);
        outResult.metrics["lit_vs_base_right"] =
            channelDistance(litRight, baseRight);
    }

    outResult.ok = outResult.failures.empty();
    if (outError) {
        outError->clear();
    }
    return true;
}

namespace {

// Fills one patch half of the synthetic image with a constant colour.
void fillPatchHalf(FixtureImage& image,
                   const std::map<std::string, FixturePatchRect>& rects,
                   const std::string& patchName,
                   PatchHalf half,
                   const std::array<int, 3>& rgb) {
    const auto found = rects.find(patchName);
    if (found == rects.end()) {
        return;
    }
    const FixturePatchRect& rect = found->second;
    const int x0 = static_cast<int>(
        std::lround(static_cast<double>(rect[0]) * image.width));
    const int y0 = static_cast<int>(
        std::lround(static_cast<double>(rect[1]) * image.height));
    const int x1 = static_cast<int>(std::lround(
        (static_cast<double>(rect[0]) + static_cast<double>(rect[2])) *
        image.width));
    const int y1 = static_cast<int>(std::lround(
        (static_cast<double>(rect[1]) + static_cast<double>(rect[3])) *
        image.height));
    const int middle = (x0 + x1) / 2 + (half == PatchHalf::Left ? 1 : 0);
    const int startX = half == PatchHalf::Left ? x0 + 1 : middle;
    const int endX = half == PatchHalf::Left ? middle : x1 - 1;
    for (int y = y0 + 1; y < y1 - 1; ++y) {
        if (y < 0 || y >= image.height) {
            continue;
        }
        for (int x = startX; x < endX; ++x) {
            if (x < 0 || x >= image.width) {
                continue;
            }
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            image.rgba[offset + 0u] = static_cast<std::uint8_t>(rgb[0]);
            image.rgba[offset + 1u] = static_cast<std::uint8_t>(rgb[1]);
            image.rgba[offset + 2u] = static_cast<std::uint8_t>(rgb[2]);
            image.rgba[offset + 3u] = 255u;
        }
    }
}

// The synthetic lit stand-ins. The validator requires the composed region to be
// present, different from the background band, different from the raw base patch
// and not black; the authored and fallback stand-ins must also differ from each
// other, because the real fixture's composed lit regions do. They are not
// measured golden colours.
const std::array<int, 3> kSynthLitLeft{186, 112, 96};
const std::array<int, 3> kSynthLitRight{72, 148, 116};
const std::array<int, 3> kSynthFallbackLit{23, 23, 23};

} // namespace

bool parseSynthDefect(const std::string& name, SynthDefect& outDefect) {
    static const std::pair<const char*, SynthDefect> kDefects[] = {
        {"none", SynthDefect::None},
        {"blank", SynthDefect::Blank},
        {"missing-patch", SynthDefect::MissingPatch},
        {"swapped-metallic-roughness", SynthDefect::SwappedMetallicRoughness},
        {"neutralized-normal", SynthDefect::NeutralizedNormal},
        {"neutralized-ao", SynthDefect::NeutralizedAo},
        {"neutralized-emissive", SynthDefect::NeutralizedEmissive},
        {"missing-lit-region", SynthDefect::MissingLitRegion},
        {"old-black-fallback", SynthDefect::OldBlackFallback},
        {"wrong-color-space", SynthDefect::WrongColorSpace},
        {"same-mean-corruption", SynthDefect::SameMeanCorruption},
        {"frozen-lit-across-cases", SynthDefect::FrozenLitAcrossCases},
    };
    for (const auto& entry : kDefects) {
        if (name == entry.first) {
            outDefect = entry.second;
            return true;
        }
    }
    return false;
}

const char* synthDefectName(SynthDefect defect) {
    switch (defect) {
        case SynthDefect::None: return "none";
        case SynthDefect::Blank: return "blank";
        case SynthDefect::MissingPatch: return "missing-patch";
        case SynthDefect::SwappedMetallicRoughness:
            return "swapped-metallic-roughness";
        case SynthDefect::NeutralizedNormal: return "neutralized-normal";
        case SynthDefect::NeutralizedAo: return "neutralized-ao";
        case SynthDefect::NeutralizedEmissive: return "neutralized-emissive";
        case SynthDefect::MissingLitRegion: return "missing-lit-region";
        case SynthDefect::OldBlackFallback: return "old-black-fallback";
        case SynthDefect::WrongColorSpace: return "wrong-color-space";
        case SynthDefect::SameMeanCorruption: return "same-mean-corruption";
        case SynthDefect::FrozenLitAcrossCases: return "frozen-lit-across-cases";
        default: return "unknown";
    }
}

bool buildSyntheticFixtureCapture(FixtureCase fixtureCase,
                                  SynthDefect defect,
                                  int width,
                                  int height,
                                  FixtureImage& outImage,
                                  std::string* outError) {
    outImage = FixtureImage{};
    if (width < 64 || height < 64) {
        return fail(outError, "synthetic captures need at least 64x64 pixels");
    }
    outImage.width = width;
    outImage.height = height;
    outImage.rgba.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0u);
    const std::array<int, 3> background{128, 213, 114};
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(width) *
             static_cast<std::size_t>(height);
         ++index) {
        outImage.rgba[index * 4u + 0u] = static_cast<std::uint8_t>(background[0]);
        outImage.rgba[index * 4u + 1u] = static_cast<std::uint8_t>(background[1]);
        outImage.rgba[index * 4u + 2u] = static_cast<std::uint8_t>(background[2]);
        outImage.rgba[index * 4u + 3u] = 255u;
    }
    if (defect == SynthDefect::Blank) {
        if (outError) {
            outError->clear();
        }
        return true;
    }

    const std::map<std::string, FixturePatchRect> rects =
        computeFixturePatchRects(width, height);
    for (const PatchExpectation& expectation :
         fixturePatchExpectations(fixtureCase)) {
        if (!expectation.present) {
            continue;
        }
        fillPatchHalf(outImage, rects, expectation.patch, PatchHalf::Left,
                      expectation.left);
        fillPatchHalf(outImage, rects, expectation.patch, PatchHalf::Right,
                      expectation.right);
    }
    fillPatchHalf(outImage, rects, "lit", PatchHalf::Left, kSynthLitLeft);
    fillPatchHalf(outImage, rects, "lit", PatchHalf::Right, kSynthLitRight);
    if (fixtureCase != FixtureCase::AuthoredMaps) {
        // The corrected fallback material composes to a very dark region; the
        // pristine synthetic capture reflects that required distinction.
        fillPatchHalf(outImage, rects, "lit", PatchHalf::Left,
                      kSynthFallbackLit);
        fillPatchHalf(outImage, rects, "lit", PatchHalf::Right,
                      kSynthFallbackLit);
    }

    switch (defect) {
        case SynthDefect::None:
            break;
        case SynthDefect::Blank:
            break;
        case SynthDefect::MissingPatch: {
            const std::array<int, 3> green = background;
            fillPatchHalf(outImage, rects, "occlusion", PatchHalf::Left, green);
            fillPatchHalf(outImage, rects, "occlusion", PatchHalf::Right, green);
            break;
        }
        case SynthDefect::SwappedMetallicRoughness: {
            std::array<int, 3> roughnessLeft{0, 0, 0};
            std::array<int, 3> roughnessRight{0, 0, 0};
            std::array<int, 3> metallicLeft{0, 0, 0};
            std::array<int, 3> metallicRight{0, 0, 0};
            for (const PatchExpectation& expectation :
                 fixturePatchExpectations(fixtureCase)) {
                if (expectation.patch == "roughness") {
                    roughnessLeft = expectation.left;
                    roughnessRight = expectation.right;
                } else if (expectation.patch == "metallic") {
                    metallicLeft = expectation.left;
                    metallicRight = expectation.right;
                }
            }
            fillPatchHalf(outImage, rects, "roughness", PatchHalf::Left,
                          metallicLeft);
            fillPatchHalf(outImage, rects, "roughness", PatchHalf::Right,
                          metallicRight);
            fillPatchHalf(outImage, rects, "metallic", PatchHalf::Left,
                          roughnessLeft);
            fillPatchHalf(outImage, rects, "metallic", PatchHalf::Right,
                          roughnessRight);
            break;
        }
        case SynthDefect::NeutralizedNormal: {
            const std::array<int, 3> flat{srgbEncodedByte(128),
                                          srgbEncodedByte(128),
                                          srgbEncodedByte(255)};
            fillPatchHalf(outImage, rects, "normal", PatchHalf::Left, flat);
            fillPatchHalf(outImage, rects, "normal", PatchHalf::Right, flat);
            break;
        }
        case SynthDefect::NeutralizedAo: {
            const std::array<int, 3> white{255, 255, 255};
            fillPatchHalf(outImage, rects, "occlusion", PatchHalf::Left, white);
            fillPatchHalf(outImage, rects, "occlusion", PatchHalf::Right, white);
            break;
        }
        case SynthDefect::NeutralizedEmissive: {
            const std::array<int, 3> black{0, 0, 0};
            fillPatchHalf(outImage, rects, "emissive", PatchHalf::Left, black);
            fillPatchHalf(outImage, rects, "emissive", PatchHalf::Right, black);
            break;
        }
        case SynthDefect::MissingLitRegion: {
            const std::array<int, 3> green = background;
            fillPatchHalf(outImage, rects, "lit", PatchHalf::Left, green);
            fillPatchHalf(outImage, rects, "lit", PatchHalf::Right, green);
            break;
        }
        case SynthDefect::OldBlackFallback: {
            const std::array<int, 3> black{0, 0, 0};
            for (const char* patch : {"normal", "roughness", "metallic",
                                      "occlusion", "emissive"}) {
                fillPatchHalf(outImage, rects, patch, PatchHalf::Left, black);
                fillPatchHalf(outImage, rects, patch, PatchHalf::Right, black);
            }
            break;
        }
        case SynthDefect::WrongColorSpace: {
            // Render the linear role views without their single sRGB encode.
            for (const char* patch : {"normal", "roughness", "metallic",
                                      "occlusion"}) {
                for (const PatchHalf half :
                     {PatchHalf::Left, PatchHalf::Right}) {
                    const AuthoredRoleMap* map = nullptr;
                    if (std::string(patch) == "normal") {
                        map = &authoredRoleMap(FixtureRole::Normal);
                    } else if (std::string(patch) == "roughness" ||
                               std::string(patch) == "metallic") {
                        map = &authoredRoleMap(FixtureRole::MetallicRoughness);
                    } else {
                        map = &authoredRoleMap(FixtureRole::Occlusion);
                    }
                    const std::array<std::uint8_t, 4>& authored =
                        half == PatchHalf::Left ? map->left : map->right;
                    int channel = 0;
                    if (std::string(patch) == "roughness") {
                        channel = 1;
                    } else if (std::string(patch) == "metallic") {
                        channel = 2;
                    }
                    const int value = static_cast<int>(
                        authored[static_cast<std::size_t>(channel)]);
                    if (std::string(patch) == "normal") {
                        fillPatchHalf(
                            outImage,
                            rects,
                            patch,
                            half,
                            {static_cast<int>(authored[0]),
                             static_cast<int>(authored[1]),
                             static_cast<int>(authored[2])});
                    } else {
                        fillPatchHalf(outImage, rects, patch, half,
                                      {value, value, value});
                    }
                }
            }
            break;
        }
        case SynthDefect::SameMeanCorruption: {
            // Corrupt every authored diagnostic half spatially while keeping its
            // mean: alternate +/-D around the expected value, clipped so the two
            // extremes stay in range for every channel with headroom.
            for (const PatchExpectation& expectation :
                 fixturePatchExpectations(fixtureCase)) {
                if (!expectation.present) {
                    continue;
                }
                const auto found = rects.find(expectation.patch);
                if (found == rects.end()) {
                    continue;
                }
                const int x0 = static_cast<int>(
                    std::lround(static_cast<double>(found->second[0]) * width));
                const int y0 = static_cast<int>(
                    std::lround(static_cast<double>(found->second[1]) * height));
                const int x1 = static_cast<int>(std::lround(
                    (static_cast<double>(found->second[0]) +
                     static_cast<double>(found->second[2])) *
                    width));
                const int y1 = static_cast<int>(std::lround(
                    (static_cast<double>(found->second[1]) +
                     static_cast<double>(found->second[3])) *
                    height));
                int amplitudes[3] = {0, 0, 0};
                for (std::size_t channel = 0u; channel < 3u; ++channel) {
                    const int expected =
                        (expectation.left[channel] + expectation.right[channel]) /
                        2;
                    amplitudes[channel] = (std::min)(30, (std::min)(expected,
                                                                    255 - expected));
                }
                for (int y = y0 + 1; y < y1 - 1 && y < height; ++y) {
                    for (int x = x0 + 1; x < x1 - 1 && x < width; ++x) {
                        if (x < 0 || y < 0) {
                            continue;
                        }
                        const bool odd = ((x + y) & 1) != 0;
                        const std::size_t offset =
                            (static_cast<std::size_t>(y) *
                                 static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4u;
                        for (std::size_t channel = 0u; channel < 3u; ++channel) {
                            const int expected =
                                (expectation.left[channel] +
                                 expectation.right[channel]) /
                                2;
                            const int value = std::clamp(
                                expected +
                                    (odd ? amplitudes[channel]
                                         : -amplitudes[channel]),
                                0,
                                255);
                            outImage.rgba[offset + channel] =
                                static_cast<std::uint8_t>(value);
                        }
                    }
                }
            }
            break;
        }
        case SynthDefect::FrozenLitAcrossCases: {
            // Hold the composed lit region at the authored appearance. The
            // diagnostic patches stay correct, so only the targeted lit-region
            // cross-case comparison can reject this.
            fillPatchHalf(outImage, rects, "lit", PatchHalf::Left, kSynthLitLeft);
            fillPatchHalf(outImage, rects, "lit", PatchHalf::Right,
                          kSynthLitRight);
            break;
        }
        default:
            break;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

namespace {

std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

const std::uint32_t kSha256RoundConstants[64] = {
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
            rotateRight(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3u);
        const std::uint32_t s1 = rotateRight(schedule[index - 2], 17) ^
            rotateRight(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10u);
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
    std::uint32_t state[8] = {0x6a09e667u,
                              0xbb67ae85u,
                              0x3c6ef372u,
                              0xa54ff53au,
                              0x510e527fu,
                              0x9b05688cu,
                              0x1f83d9abu,
                              0x5be0cd19u};
    std::vector<unsigned char> padded(bytes.begin(), bytes.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
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
    std::string bytes((std::istreambuf_iterator<char>(input)),
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

namespace {

std::string normalizedComponent(const std::filesystem::path& component) {
    std::string text = component.string();
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

} // namespace

bool resolveWithinRoot(const std::string& value,
                       const std::filesystem::path& root,
                       std::filesystem::path& outResolved,
                       std::string& outError) {
    if (value.empty()) {
        outError = "path is empty";
        return false;
    }
    std::error_code error;
    const std::filesystem::path input(value);
    const std::filesystem::path candidate =
        input.is_absolute() ? input : root / input;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(root, error);
    if (error) {
        outError = "could not resolve the run directory: " + error.message();
        return false;
    }
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        outError = "could not resolve path: " + value + ": " + error.message();
        return false;
    }
    // Component-aware comparison: a sibling whose name merely starts with the
    // run directory's name, or a traversal that escapes it, is rejected.
    auto rootPart = canonicalRoot.begin();
    auto candidatePart = canonical.begin();
    bool inside = true;
    while (rootPart != canonicalRoot.end()) {
        if (candidatePart == canonical.end() ||
            normalizedComponent(*rootPart) != normalizedComponent(*candidatePart)) {
            inside = false;
            break;
        }
        ++rootPart;
        ++candidatePart;
    }
    if (!inside) {
        outError = "path is outside the run directory: " + value;
        return false;
    }
    outResolved = canonical;
    return true;
}

namespace {

bool jsonNumber(const nlohmann::json& document,
                const char* key,
                double& outValue) {
    const auto found = document.find(key);
    if (found == document.end() || !found->is_number()) {
        return false;
    }
    outValue = found->get<double>();
    return true;
}

// Integer fields must be JSON integers inside the bounded range, so a fractional
// or oversized value is a diagnostic instead of a truncating or overflowing
// cast.
bool jsonBoundedInt(const nlohmann::json& document,
                    const char* key,
                    std::int64_t minimum,
                    std::int64_t maximum,
                    int& outValue) {
    const auto found = document.find(key);
    if (found == document.end() || !found->is_number_integer()) {
        return false;
    }
    std::int64_t value = 0;
    try {
        value = found->get<std::int64_t>();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (value < minimum || value > maximum) {
        return false;
    }
    outValue = static_cast<int>(value);
    return true;
}

bool jsonFiniteNumber(const nlohmann::json& document,
                      const char* key,
                      double& outValue) {
    if (!jsonNumber(document, key, outValue)) {
        return false;
    }
    return std::isfinite(outValue);
}

template <typename T>
bool jsonRequired(const nlohmann::json& document,
                  const char* key,
                  T& outValue) {
    const auto found = document.find(key);
    if (found == document.end()) {
        return false;
    }
    try {
        outValue = found->get<T>();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return true;
}

} // namespace

bool validateFixtureHandshakeImpl(const std::string& handshakePath,
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
        return fail(outError,
                    "could not open material fixture handshake: " + handshakePath);
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& exception) {
        return fail(outError,
                    "material fixture handshake is not valid JSON: " +
                        std::string(exception.what()));
    }
    const auto require = [&outResult](bool condition, std::string message) {
        if (!condition) {
            outResult.failures.push_back(std::move(message));
        }
    };

    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);
    std::string schema;
    require(jsonRequired(document, "schema", schema) &&
                schema == "phlosion-material-fixture-handshake-v1",
            "handshake schema is not the material fixture schema");
    std::string caseName;
    require(jsonRequired(document, "case", caseName) && caseName == info.name,
            "handshake case '" + caseName + "' is not '" + info.name + "'");
    std::string sampleIdentity;
    require(jsonRequired(document, "sample_identity", sampleIdentity) &&
                sampleIdentity == std::string("phlosion-material-fixture-v1/") +
                    info.name,
            "handshake sample identity is not the shared sample identity");
    std::string host;
    require(jsonRequired(document, "host", host), "handshake host is missing");
    if (!expectedHost.empty()) {
        require(host == expectedHost,
                "handshake host '" + host + "' is not '" + expectedHost + "'");
    }
    std::string requested;
    require(jsonRequired(document, "requested_backend", requested) &&
                !requested.empty(),
            "handshake requested backend is missing");
    std::string actual;
    require(jsonRequired(document, "actual_backend", actual),
            "handshake actual backend is missing");
    require(!actual.empty() && actual == requested,
            "handshake requested backend '" + requested +
                "' resolved to '" + actual + "'");
    if (!expectedApi.empty()) {
        require(requested == expectedApi,
                "handshake requested backend '" + requested + "' is not '" +
                    expectedApi + "'");
        require(actual == expectedApi,
                "handshake actual backend '" + actual + "' is not '" +
                    expectedApi + "'");
    }
    int logicalFrame = -1;
    require(jsonBoundedInt(document, "logical_frame", 0, 1000000, logicalFrame) &&
                logicalFrame == info.logicalFrame,
            "handshake logical frame is missing, not a bounded integer or does "
            "not match the case catalog");
    const nlohmann::json* surface = nullptr;
    const auto surfaceFound = document.find("surface");
    if (surfaceFound != document.end() && surfaceFound->is_object()) {
        surface = &*surfaceFound;
    }
    require(surface != nullptr, "handshake surface is missing");
    if (surface != nullptr) {
        int width = -1;
        int height = -1;
        require(jsonBoundedInt(*surface, "width", 1, 16384, width) &&
                    width == surfaceWidth,
                "handshake surface width is missing, not a bounded integer or "
                "does not match the capture");
        require(jsonBoundedInt(*surface, "height", 1, 16384, height) &&
                    height == surfaceHeight,
                "handshake surface height is missing, not a bounded integer or "
                "does not match the capture");
    }

    std::string assetDirectory;
    require(jsonRequired(document, "asset_directory", assetDirectory) &&
                !assetDirectory.empty(),
            "handshake asset directory is missing");
    if (!assetDirectory.empty()) {
        const std::filesystem::path directory(assetDirectory);
        std::error_code error;
        const std::filesystem::path tempRoot =
            std::filesystem::temp_directory_path(error);
        std::filesystem::path resolved;
        std::string pathError;
        bool insideTemp = !error && directory.is_absolute() &&
            resolveWithinRoot(assetDirectory, tempRoot, resolved, pathError);
        if (insideTemp) {
            // The run-owned component is the first path component beneath the
            // temporary root; the check is on that whole component, never on a
            // string prefix of the path.
            const std::filesystem::path canonicalTemp =
                std::filesystem::weakly_canonical(tempRoot, error);
            insideTemp = !error;
            if (insideTemp) {
                const std::filesystem::path relative =
                    resolved.lexically_relative(canonicalTemp);
                const auto first = relative.begin();
                insideTemp = first != relative.end() &&
                    normalizedComponent(*first).rfind(
                        "phlosion-material-fixture", 0u) == 0u;
            }
        }
        require(insideTemp,
                "handshake asset directory is not a run-owned temporary directory: " +
                    assetDirectory);
    }
    int assetFiles = 0;
    require(jsonBoundedInt(document, "asset_file_count", 1, 64, assetFiles),
            "handshake asset file count is missing, out of range or empty");

    const auto materialFound = document.find("material");
    require(materialFound != document.end() && materialFound->is_object(),
            "handshake material record is missing");
    if (materialFound != document.end() && materialFound->is_object()) {
        int mode = -1;
        require(jsonBoundedInt(*materialFound, "mode", 0, 255, mode) && mode == 2,
                "handshake material mode is missing, out of range or not the "
                "debug-capable world mode");
        int presence = -1;
        require(jsonBoundedInt(*materialFound, "presence_flags", 0, 0xFFFF,
                               presence) &&
                    presence == 0xF,
                "handshake presence flags are missing, out of range or do not "
                "bind all five roles");
        const FixtureMaterialFactors authored = fixtureMaterialFactors(fixtureCase);
        const char* factorKeys[4] = {"metallic_factor",
                                     "roughness_factor",
                                     "normal_scale",
                                     "occlusion_strength"};
        const double authoredFactors[4] = {authored.metallicFactor,
                                           authored.roughnessFactor,
                                           authored.normalScale,
                                           authored.occlusionStrength};
        for (int index = 0; index < 4; ++index) {
            double value = 0.0;
            require(jsonFiniteNumber(*materialFound, factorKeys[index], value) &&
                        std::abs(value - authoredFactors[index]) <= 1.0e-5,
                    std::string("handshake material factor ") + factorKeys[index] +
                        " is missing, not finite or does not match the authored "
                        "value");
        }
        const auto emissiveFound = materialFound->find("emissive_factor");
        require(emissiveFound != materialFound->end() &&
                    emissiveFound->is_array() && emissiveFound->size() == 3u,
                "handshake emissive factor is missing");
        if (emissiveFound != materialFound->end() && emissiveFound->is_array() &&
            emissiveFound->size() == 3u) {
            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                const nlohmann::json& element = (*emissiveFound)[channel];
                const bool number = element.is_number();
                const double value = number ? element.get<double>() : 0.0;
                require(number && std::isfinite(value) &&
                            std::abs(value - info.emissiveFactor[channel]) <=
                                1.0e-5,
                        "handshake emissive factor channel " +
                            std::to_string(channel) +
                            " is not a finite number or does not match the "
                            "authored value");
            }
        }
    }

    const auto rolesFound = document.find("roles");
    require(rolesFound != document.end() && rolesFound->is_array() &&
                rolesFound->size() ==
                    static_cast<std::size_t>(kFixtureRoleCount),
            "handshake must carry exactly five role records");
    if (rolesFound != document.end() && rolesFound->is_array()) {
        for (std::size_t index = 0u; index < rolesFound->size(); ++index) {
            const nlohmann::json& role = (*rolesFound)[index];
            const FixtureRole expectedRole = static_cast<FixtureRole>(index);
            if (!role.is_object()) {
                require(false,
                        "handshake role " + std::to_string(index) +
                            " is not an object");
                continue;
            }
            // The submitted metadata is validated against the guard's own
            // literal contract, and the content hash and cache identity are
            // re-derived from the guard's independently authored role pixels.
            const RoleMetadataContract contract =
                expectedRoleMetadata(fixtureCase, expectedRole);
            std::string roleName;
            require(jsonRequired(role, "role", roleName) &&
                        roleName == fixtureRoleName(expectedRole),
                    "handshake role " + std::to_string(index) +
                        " is not '" + fixtureRoleName(expectedRole) + "'");
            std::string origin;
            require(jsonRequired(role, "origin", origin) &&
                        origin == fixtureRoleOrigin(fixtureCase, expectedRole),
                    "handshake role '" + roleName +
                        "' records the wrong input origin '" + origin + "'");
            int width = 0;
            int height = 0;
            int srgb = -1;
            int pixelBytes = -1;
            std::int64_t contentHash = -1;
            require(jsonBoundedInt(role, "width", 1, 4096, width) &&
                        width == contract.width,
                    "handshake role '" + roleName +
                        "' width is missing, out of range or not the authored size");
            require(jsonBoundedInt(role, "height", 1, 4096, height) &&
                        height == contract.height,
                    "handshake role '" + roleName +
                        "' height is missing, out of range or not the authored size");
            require(jsonBoundedInt(role, "pixel_bytes", 4, 4096 * 4096 * 4,
                                   pixelBytes) &&
                        pixelBytes == contract.width * contract.height * 4,
                    "handshake role '" + roleName +
                        "' pixel byte count is missing, out of range or does not "
                        "match its size");
            require(jsonBoundedInt(role, "texture_srgb", 0, 1, srgb) &&
                        srgb == static_cast<int>(contract.textureSrgb),
                    "handshake role '" + roleName +
                        "' color space flag is missing, out of range or not the "
                        "authored one");
            int wrapS = -1;
            int wrapT = -1;
            int minFilter = -1;
            int magFilter = -1;
            require(jsonBoundedInt(role, "wrap_s", 0, 100000, wrapS) &&
                        wrapS == contract.wrapS,
                    "handshake role '" + roleName +
                        "' wrap_s is missing, out of range or not the authored one");
            require(jsonBoundedInt(role, "wrap_t", 0, 100000, wrapT) &&
                        wrapT == contract.wrapT,
                    "handshake role '" + roleName +
                        "' wrap_t is missing, out of range or not the authored one");
            require(jsonBoundedInt(role, "min_filter", 0, 100000, minFilter) &&
                        minFilter == contract.minFilter,
                    "handshake role '" + roleName +
                        "' min filter is missing, out of range or not the "
                        "authored one");
            require(jsonBoundedInt(role, "mag_filter", 0, 100000, magFilter) &&
                        magFilter == contract.magFilter,
                    "handshake role '" + roleName +
                        "' mag filter is missing, out of range or not the "
                        "authored one");
            const auto firstPixelFound = role.find("first_pixel");
            bool firstPixelOk = firstPixelFound != role.end() &&
                firstPixelFound->is_array() && firstPixelFound->size() == 4u;
            if (firstPixelOk) {
                for (std::size_t channel = 0u; channel < 4u; ++channel) {
                    const nlohmann::json& value = (*firstPixelFound)[channel];
                    if (!value.is_number_integer()) {
                        firstPixelOk = false;
                        break;
                    }
                    std::int64_t byte = 0;
                    try {
                        byte = value.get<std::int64_t>();
                    } catch (const nlohmann::json::exception&) {
                        firstPixelOk = false;
                        break;
                    }
                    if (byte != static_cast<std::int64_t>(
                                    contract.firstPixel[channel])) {
                        firstPixelOk = false;
                        break;
                    }
                }
            }
            require(firstPixelOk,
                    "handshake role '" + roleName +
                        "' first pixel is missing or not the contracted value");
            const auto hashFound = role.find("content_hash");
            bool hashOk = hashFound != role.end() &&
                hashFound->is_number_integer();
            if (hashOk) {
                try {
                    contentHash = hashFound->get<std::int64_t>();
                } catch (const nlohmann::json::exception&) {
                    hashOk = false;
                }
            }
            // The identity is compared exactly, in int64, against the expected
            // 32-bit hash promoted to int64. A value outside the uint32 range is
            // rejected before any narrowing cast, so a forged hash such as
            // expected + 2^32 cannot truncate into an accepted identity.
            const std::int64_t expectedHash =
                static_cast<std::int64_t>(contract.contentHash);
            require(hashOk && contentHash == expectedHash,
                    "handshake role '" + roleName +
                        "' content hash is missing or does not match the "
                        "independently authored role pixels");
            std::string key;
            std::string cacheKey;
            require(jsonRequired(role, "key", key) && key == contract.key,
                    "handshake role '" + roleName +
                        "' key is not the exact content-dependent role key");
            require(jsonRequired(role, "cache_key", cacheKey) &&
                        cacheKey == key,
                    "handshake role '" + roleName +
                        "' key and cache key disagree");
        }
    }

    const auto patchesFound = document.find("patches");
    require(patchesFound != document.end() && patchesFound->is_array() &&
                patchesFound->size() ==
                    static_cast<std::size_t>(kFixturePatchCount),
            "handshake must carry exactly eight patch records");
    const std::map<std::string, FixturePatchRect> expectedRects =
        computeFixturePatchRects(surfaceWidth, surfaceHeight);
    if (patchesFound != document.end() && patchesFound->is_array()) {
        for (std::size_t index = 0u; index < patchesFound->size(); ++index) {
            const nlohmann::json& patch = (*patchesFound)[index];
            const FixturePatch expectedPatch = static_cast<FixturePatch>(index);
            const std::string expectedName = fixturePatchName(expectedPatch);
            if (!patch.is_object()) {
                require(false,
                        "handshake patch " + std::to_string(index) +
                            " is not an object");
                continue;
            }
            std::string patchName;
            require(jsonRequired(patch, "patch", patchName) &&
                        patchName == expectedName,
                    "handshake patch " + std::to_string(index) +
                        " is not '" + expectedName + "'");
            int debugView = -1;
            require(jsonBoundedInt(patch, "debug_view", 0, 7, debugView) &&
                        debugView == fixturePatchDebugView(expectedPatch),
                    "handshake patch '" + expectedName +
                        "' debug view is missing, out of range or not the "
                        "authored one");
            std::string geometryKey;
            require(jsonRequired(patch, "geometry_key", geometryKey) &&
                        geometryKey.find(info.name) != std::string::npos,
                    "handshake patch '" + expectedName +
                        "' geometry key is not case dependent");
            const auto rectFound = patch.find("rect");
            require(rectFound != patch.end() && rectFound->is_array() &&
                        rectFound->size() == 4u,
                    "handshake patch '" + expectedName + "' has no rect");
            if (rectFound != patch.end() && rectFound->is_array() &&
                rectFound->size() == 4u) {
                // The lookup uses the authored patch name, never the parsed one,
                // so an unknown name can only produce a diagnostic.
                const FixturePatchRect& expected = expectedRects.at(expectedName);
                for (std::size_t element = 0u; element < 4u; ++element) {
                    const nlohmann::json& value = (*rectFound)[element];
                    const bool number = value.is_number();
                    const double parsed = number ? value.get<double>() : 0.0;
                    require(number &&
                                std::isfinite(parsed) &&
                                std::abs(parsed - expected[element]) <= 1.0e-4,
                            "handshake patch '" + expectedName +
                                "' rect element " + std::to_string(element) +
                                " is not a number or does not match the authored "
                                "layout");
                }
            }
        }
    }

    const auto presentationFound = document.find("presentation");
    require(presentationFound != document.end() &&
                presentationFound->is_object(),
            "handshake presentation record is missing");
    if (presentationFound != document.end() &&
        presentationFound->is_object()) {
        bool debugReset = false;
        require(jsonRequired(*presentationFound, "debug_state_reset", debugReset) &&
                    debugReset,
                "handshake did not reset the transient debug state");
    }

    outResult.ok = outResult.failures.empty();
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
    // A malformed handshake (wrong field types, unknown names, non-numeric
    // entries) must produce a diagnostic verdict, never an escaping exception.
    try {
        return validateFixtureHandshakeImpl(handshakePath,
                                            fixtureCase,
                                            surfaceWidth,
                                            surfaceHeight,
                                            outResult,
                                            outError,
                                            expectedApi,
                                            expectedHost);
    } catch (const std::exception& exception) {
        outResult = HandshakeCheckResult{};
        outResult.failures.push_back(
            std::string("handshake validation raised: ") + exception.what());
        outResult.ok = false;
        if (outError) {
            outError->clear();
        }
        return true;
    }
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
    sheet.rgba.assign(static_cast<std::size_t>(sheet.width) *
                          static_cast<std::size_t>(sheet.height) * 4u,
                      0u);
    for (std::size_t index = 0; index < images.size(); ++index) {
        const FixtureImage& image = images[index].second;
        if (image.empty()) {
            continue;
        }
        const int cellX =
            static_cast<int>(index % static_cast<std::size_t>(columns)) * cellWidth;
        const int cellY =
            static_cast<int>(index / static_cast<std::size_t>(columns)) * cellHeight;
        for (int y = 0; y < cellHeight; ++y) {
            for (int x = 0; x < cellWidth; ++x) {
                const bool border = x == 0 || y == 0 || x == cellWidth - 1 ||
                    y == cellHeight - 1;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(cellY + y) *
                         static_cast<std::size_t>(sheet.width) +
                     static_cast<std::size_t>(cellX + x)) *
                    4u;
                if (border) {
                    sheet.rgba[destinationOffset + 0u] = 24u;
                    sheet.rgba[destinationOffset + 1u] = 24u;
                    sheet.rgba[destinationOffset + 2u] = 28u;
                    sheet.rgba[destinationOffset + 3u] = 255u;
                    continue;
                }
                const int sourceX = std::min(
                    image.width - 1,
                    static_cast<int>(static_cast<double>(x) /
                                     static_cast<double>(cellWidth) *
                                     static_cast<double>(image.width)));
                const int sourceY = std::min(
                    image.height - 1,
                    static_cast<int>(static_cast<double>(y) /
                                     static_cast<double>(cellHeight) *
                                     static_cast<double>(image.height)));
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceY) *
                         static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(sourceX)) *
                    4u;
                std::copy_n(image.rgba.begin() +
                                static_cast<std::ptrdiff_t>(sourceOffset),
                            4,
                            sheet.rgba.begin() +
                                static_cast<std::ptrdiff_t>(destinationOffset));
            }
        }
    }
    return sheet;
}

} // namespace engine::material_fixture::verification
