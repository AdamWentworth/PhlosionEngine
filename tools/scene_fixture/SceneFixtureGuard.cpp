#include "SceneFixtureGuard.h"

#include "SceneFixtureCase.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace engine::scene_fixture::verification {
namespace {

using engine::fixture::FixtureCrop;
using engine::fixture::FixtureImage;
using engine::fixture::buildFixtureContactSheet;
using engine::fixture::compareFixtureImages;
using engine::fixture::cropFixtureImage;
using engine::fixture::loadFixturePng;
using engine::fixture::saveFixturePng;

constexpr float kNonBackgroundChannelDelta = 12.0f;

// --- documented display transfer (mirrors the shared world shader policy) ---

glm::vec3 rrtAndOdtFit(const glm::vec3& value) {
    const glm::vec3 a = value * (value + glm::vec3(0.0245786f)) -
                        glm::vec3(0.000090537f);
    const glm::vec3 b = value * (0.983729f * value + glm::vec3(0.4329510f)) +
                        glm::vec3(0.238081f);
    return a / b;
}

glm::vec3 tonemapAcesFilmic(const glm::vec3& color, float exposure) {
    const glm::mat3 inputTransform(
        glm::vec3(0.59719f, 0.07600f, 0.02840f),
        glm::vec3(0.35458f, 0.90834f, 0.13383f),
        glm::vec3(0.04823f, 0.01566f, 0.83777f));
    const glm::mat3 outputTransform(
        glm::vec3(1.60475f, -0.10208f, -0.00327f),
        glm::vec3(-0.53108f, 1.10813f, -0.07276f),
        glm::vec3(-0.07367f, -0.00605f, 1.07602f));
    glm::vec3 mapped = color * (exposure / 0.6f);
    mapped = inputTransform * mapped;
    mapped = rrtAndOdtFit(mapped);
    return glm::clamp(outputTransform * mapped, glm::vec3(0.0f), glm::vec3(1.0f));
}

glm::vec3 linearToSrgb(const glm::vec3& color) {
    const glm::vec3 clamped = glm::max(color, glm::vec3(0.0f));
    const glm::vec3 low = clamped * 12.92f;
    const glm::vec3 high =
        1.055f * glm::pow(clamped, glm::vec3(1.0f / 2.4f)) - 0.055f;
    return glm::mix(low, high, glm::step(glm::vec3(0.0031308f), clamped));
}

std::uint8_t toByte(double value) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0, 255.0)));
}

// --- authored expectations -------------------------------------------------

const std::array<CaseExpectation, kFixtureCaseCount> kCaseExpectations{{
    {SceneVariant::A, 0.0f, true, false, true, true, false, false, false, false},
    {SceneVariant::A, 1.0f, false, false, true, true, false, false, false, false},
    {SceneVariant::A, 1.0f, false, false, true, true, true, true, false, false},
    {SceneVariant::B, 1.0f, false, false, true, true, true, false, true, false},
    {SceneVariant::A, 1.0f, false, true, true, true, false, false, false, false},
    {SceneVariant::A, 1.0f, false, false, false, true, false, false, false, true},
}};

std::array<float, 4> normalizedRegion(const FixtureImage& image,
                                      const std::array<float, 4>& region) {
    return {region[0] * static_cast<float>(image.width),
            region[1] * static_cast<float>(image.height),
            region[2] * static_cast<float>(image.width),
            region[3] * static_cast<float>(image.height)};
}

FixtureCrop toCrop(const FixtureImage& image,
                   const std::array<float, 4>& region) {
    const std::array<float, 4> pixels = normalizedRegion(image, region);
    FixtureCrop crop;
    crop.x = static_cast<int>(std::floor(pixels[0]));
    crop.y = static_cast<int>(std::floor(pixels[1]));
    crop.width = static_cast<int>(std::ceil(pixels[2]));
    crop.height = static_cast<int>(std::ceil(pixels[3]));
    crop.x = std::clamp(crop.x, 0, std::max(0, image.width - 1));
    crop.y = std::clamp(crop.y, 0, std::max(0, image.height - 1));
    crop.width = std::clamp(crop.width, 0, image.width - crop.x);
    crop.height = std::clamp(crop.height, 0, image.height - crop.y);
    return crop;
}

bool pixelDiffersFromBackground(const FixtureImage& image,
                                int x,
                                int y,
                                const std::array<int, 3>& background) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) *
        4u;
    float difference = 0.0f;
    for (int channel = 0; channel < 3; ++channel) {
        difference = std::max(
            difference,
            std::fabs(static_cast<float>(image.rgba[offset +
                                                    static_cast<std::size_t>(
                                                        channel)]) -
                      static_cast<float>(background[static_cast<std::size_t>(
                          channel)])));
    }
    return difference > kNonBackgroundChannelDelta;
}

std::array<float, 4> expandBounds(const std::array<float, 4>& bounds,
                                  int surfaceWidth,
                                  int surfaceHeight) {
    const float marginX = 0.02f * static_cast<float>(surfaceWidth);
    const float marginY = 0.02f * static_cast<float>(surfaceHeight);
    return {std::clamp(bounds[0] - marginX, 0.0f,
                       static_cast<float>(surfaceWidth)),
            std::clamp(bounds[1] - marginY, 0.0f,
                       static_cast<float>(surfaceHeight)),
            std::clamp(bounds[2] + marginX, 0.0f,
                       static_cast<float>(surfaceWidth)),
            std::clamp(bounds[3] + marginY, 0.0f,
                       static_cast<float>(surfaceHeight))};
}

// Fraction of pixels inside a disc around a projected authored position that
// differ from the expected background.
double discContentRatio(const FixtureImage& image,
                        const glm::vec2& center,
                        float radius) {
    if (image.empty() || !std::isfinite(center.x) || !std::isfinite(center.y)) {
        return 0.0;
    }
    const std::array<int, 3> background = expectedBackgroundRgb();
    const int x0 = static_cast<int>(std::floor(center.x - radius));
    const int x1 = static_cast<int>(std::ceil(center.x + radius));
    const int y0 = static_cast<int>(std::floor(center.y - radius));
    const int y1 = static_cast<int>(std::ceil(center.y + radius));
    std::size_t content = 0u;
    std::size_t total = 0u;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - center.x;
            const float dy = static_cast<float>(y) + 0.5f - center.y;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
                ++total;
                continue;
            }
            ++total;
            content += pixelDiffersFromBackground(image, x, y, background) ? 1u
                                                                          : 0u;
        }
    }
    if (total == 0u) {
        return 0.0;
    }
    return static_cast<double>(content) / static_cast<double>(total);
}

std::array<float, 4> boundsToRegion(const std::array<float, 4>& bounds,
                                    int surfaceWidth,
                                    int surfaceHeight) {
    if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return {bounds[0] / static_cast<float>(surfaceWidth),
            bounds[1] / static_cast<float>(surfaceHeight),
            (bounds[2] - bounds[0]) / static_cast<float>(surfaceWidth),
            (bounds[3] - bounds[1]) / static_cast<float>(surfaceHeight)};
}

std::array<float, 4> projectPointBounds(const std::vector<glm::vec3>& points,
                                        const glm::mat4& viewProjection,
                                        int surfaceWidth,
                                        int surfaceHeight) {
    std::array<float, 4> bounds{std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max()};
    for (const glm::vec3& point : points) {
        const glm::vec2 projected = fixtureProjectToPixels(
            viewProjection, point, surfaceWidth, surfaceHeight);
        bounds[0] = std::min(bounds[0], projected.x);
        bounds[1] = std::min(bounds[1], projected.y);
        bounds[2] = std::max(bounds[2], projected.x);
        bounds[3] = std::max(bounds[3], projected.y);
    }
    return bounds;
}

std::map<std::string, std::array<float, 4>> analyticRegions(
    SceneVariant variant,
    float poseBlend,
    int surfaceWidth,
    int surfaceHeight) {
    const glm::mat4 viewProjection =
        fixtureViewProjection(surfaceWidth, surfaceHeight);
    std::map<std::string, std::array<float, 4>> regions;
    regions["character"] = boundsToRegion(
        expandBounds(projectPointBounds(authoredCharacterPoints(variant, poseBlend),
                                       viewProjection, surfaceWidth,
                                       surfaceHeight),
                     surfaceWidth, surfaceHeight),
        surfaceWidth, surfaceHeight);
    regions["limb"] = boundsToRegion(
        expandBounds(projectPointBounds(authoredLimbPoints(variant, poseBlend),
                                       viewProjection, surfaceWidth,
                                       surfaceHeight),
                     surfaceWidth, surfaceHeight),
        surfaceWidth, surfaceHeight);
    regions["head"] = boundsToRegion(
        expandBounds(projectPointBounds(authoredHeadPoints(variant),
                                       viewProjection, surfaceWidth,
                                       surfaceHeight),
                     surfaceWidth, surfaceHeight),
        surfaceWidth, surfaceHeight);

    const std::array<float, 4> characterBounds = projectPointBounds(
        authoredCharacterPoints(variant, poseBlend), viewProjection,
        surfaceWidth, surfaceHeight);
    std::array<float, 4> floorBounds = projectPointBounds(
        authoredFloorPoints(), viewProjection, surfaceWidth, surfaceHeight);
    floorBounds[1] = std::max(
        floorBounds[1],
        characterBounds[3] + 0.02f * static_cast<float>(surfaceHeight));
    regions["floor"] = boundsToRegion(expandBounds(floorBounds, surfaceWidth,
                                                   surfaceHeight),
                                      surfaceWidth, surfaceHeight);
    std::array<float, 4> wallBounds = projectPointBounds(
        authoredWallPoints(variant), viewProjection, surfaceWidth, surfaceHeight);
    wallBounds[2] = std::min(
        wallBounds[2],
        characterBounds[0] - 0.02f * static_cast<float>(surfaceWidth));
    regions["wall"] = boundsToRegion(expandBounds(wallBounds, surfaceWidth,
                                                  surfaceHeight),
                                     surfaceWidth, surfaceHeight);
    regions["probe"] = boundsToRegion(
        expandBounds(projectPointBounds(authoredProbePoints(), viewProjection,
                                       surfaceWidth, surfaceHeight),
                     surfaceWidth, surfaceHeight),
        surfaceWidth, surfaceHeight);
    return regions;
}

bool hexDigestEquals(const std::string& value, const std::string& expected) {
    if (value.size() != expected.size() || value.empty()) {
        return false;
    }
    for (std::size_t index = 0u; index < value.size(); ++index) {
        const char a = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[index])));
        const char b = static_cast<char>(
            std::tolower(static_cast<unsigned char>(expected[index])));
        if (a != b) {
            return false;
        }
    }
    return value.size() == 64u;
}

// --- SHA-256 (self-contained; verification-only) ---------------------------

struct Sha256 {
    std::array<std::uint32_t, 8> state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                       0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                       0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer{};
    std::size_t bufferLength = 0u;
    std::uint64_t totalBytes = 0u;

    static std::uint32_t rotr(std::uint32_t value, std::uint32_t count) {
        return (value >> count) | (value << (32u - count));
    }

    void transform(const std::uint8_t* block) {
        static const std::uint32_t k[64] = {
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
        std::uint32_t words[64] = {};
        for (int index = 0; index < 16; ++index) {
            words[index] =
                (static_cast<std::uint32_t>(block[index * 4 + 0]) << 24u) |
                (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16u) |
                (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8u) |
                static_cast<std::uint32_t>(block[index * 4 + 3]);
        }
        for (int index = 16; index < 64; ++index) {
            const std::uint32_t s0 = rotr(words[index - 15], 7) ^
                                     rotr(words[index - 15], 18) ^
                                     (words[index - 15] >> 3u);
            const std::uint32_t s1 = rotr(words[index - 2], 17) ^
                                     rotr(words[index - 2], 19) ^
                                     (words[index - 2] >> 10u);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (int index = 0; index < 64; ++index) {
            const std::uint32_t s1 =
                rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + ch + k[index] + words[index];
            const std::uint32_t s0 =
                rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
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

    void update(const std::uint8_t* data, std::size_t length) {
        totalBytes += length;
        while (length > 0u) {
            const std::size_t chunk = std::min(length, buffer.size() - bufferLength);
            std::memcpy(buffer.data() + bufferLength, data, chunk);
            bufferLength += chunk;
            data += chunk;
            length -= chunk;
            if (bufferLength == buffer.size()) {
                transform(buffer.data());
                bufferLength = 0u;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bitLength = totalBytes * 8u;
        std::uint8_t padding = 0x80u;
        update(&padding, 1u);
        padding = 0u;
        while (bufferLength != 56u) {
            update(&padding, 1u);
        }
        std::uint8_t lengthBytes[8] = {};
        for (int index = 0; index < 8; ++index) {
            lengthBytes[7 - index] = static_cast<std::uint8_t>(
                (bitLength >> (index * 8)) & 0xFFu);
        }
        update(lengthBytes, 8u);
        std::ostringstream stream;
        for (const std::uint32_t word : state) {
            stream << std::hex << std::setw(8) << std::setfill('0') << word;
        }
        return stream.str();
    }
};

}  // namespace

const CaseExpectation& caseExpectation(FixtureCase fixtureCase) {
    return kCaseExpectations[static_cast<std::size_t>(fixtureCase)];
}

std::array<int, 3> expectedBackgroundRgb() {
    const std::array<float, 3> linear = fixtureBackgroundLinearColor();
    const glm::vec3 tonemapped = tonemapAcesFilmic(
        glm::vec3(linear[0], linear[1], linear[2]), 1.15f);
    const glm::vec3 encoded = linearToSrgb(tonemapped);
    return {static_cast<int>(toByte(encoded.x * 255.0)),
            static_cast<int>(toByte(encoded.y * 255.0)),
            static_cast<int>(toByte(encoded.z * 255.0))};
}

std::array<std::uint8_t, 3> expectedProbePixel(
    const std::array<std::uint8_t, 4>& texel) {
    const AuthoredProbe& probe = authoredProbe();
    const auto srgbToLinear = [](std::uint8_t byte) {
        const float value = static_cast<float>(byte) / 255.0f;
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    const auto linearToByte = [](float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        const float encoded =
            clamped <= 0.0031308f
                ? clamped * 12.92f
                : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(
            std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
    };
    const float linear[3] = {srgbToLinear(texel[0]), srgbToLinear(texel[1]),
                             srgbToLinear(texel[2])};
    return {linearToByte(linear[0] * probe.baseColorFactor[0]),
            linearToByte(linear[1] * probe.baseColorFactor[1]),
            linearToByte(linear[2] * probe.baseColorFactor[2])};
}

std::array<std::uint8_t, 3> probeDefectPixel(
    const std::array<std::uint8_t, 4>& texel,
    ProbeDefect defect) {
    if (defect == ProbeDefect::None) {
        return expectedProbePixel(texel);
    }
    if (defect == ProbeDefect::NoFactor) {
        return {texel[0], texel[1], texel[2]};
    }
    const AuthoredProbe& probe = authoredProbe();
    const auto srgbToLinear = [](std::uint8_t byte) {
        const float value = static_cast<float>(byte) / 255.0f;
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    std::array<std::uint8_t, 3> out{};
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        float linear = srgbToLinear(texel[channel]);
        float factor = probe.baseColorFactor[channel];
        if (defect == ProbeDefect::DoubleFactor) {
            linear = std::clamp(linear * factor * factor, 0.0f, 1.0f);
            out[channel] = static_cast<std::uint8_t>(
                std::lround(std::clamp(
                    linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f,
                    0.0f, 1.0f) *
                    255.0f));
        } else {
            // Wrong transfer: the authored linear value written without the
            // documented sRGB encode.
            const float value = std::clamp(linear * factor, 0.0f, 1.0f);
            out[channel] = static_cast<std::uint8_t>(std::lround(value * 255.0f));
        }
    }
    return out;
}

bool parseProbeDefect(const std::string& name, ProbeDefect& outDefect) {
    if (name == "none") {
        outDefect = ProbeDefect::None;
        return true;
    }
    if (name == "probe-no-factor") {
        outDefect = ProbeDefect::NoFactor;
        return true;
    }
    if (name == "probe-double-factor") {
        outDefect = ProbeDefect::DoubleFactor;
        return true;
    }
    if (name == "probe-wrong-transfer") {
        outDefect = ProbeDefect::WrongTransfer;
        return true;
    }
    return false;
}

const char* probeDefectName(ProbeDefect defect) {
    switch (defect) {
        case ProbeDefect::None:
            return "none";
        case ProbeDefect::NoFactor:
            return "probe-no-factor";
        case ProbeDefect::DoubleFactor:
            return "probe-double-factor";
        case ProbeDefect::WrongTransfer:
            return "probe-wrong-transfer";
        default:
            return "unknown";
    }
}

bool analyticCharacterBounds(SceneVariant variant,
                             float poseBlend,
                             int surfaceWidth,
                             int surfaceHeight,
                             std::array<float, 4>& outCharacter,
                             std::array<float, 4>& outLimb,
                             std::array<float, 4>& outHead) {
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return false;
    }
    const glm::mat4 viewProjection =
        fixtureViewProjection(surfaceWidth, surfaceHeight);
    outCharacter = projectPointBounds(authoredCharacterPoints(variant, poseBlend),
                                      viewProjection, surfaceWidth, surfaceHeight);
    outLimb = projectPointBounds(authoredLimbPoints(variant, poseBlend),
                                 viewProjection, surfaceWidth, surfaceHeight);
    outHead = projectPointBounds(authoredHeadPoints(variant), viewProjection,
                                 surfaceWidth, surfaceHeight);
    return true;
}

bool measuredContentBounds(const FixtureImage& viewport,
                           const std::array<float, 4>& region,
                           std::array<float, 4>& outBounds,
                           double* outCoverage,
                           std::string* outError) {
    if (viewport.empty()) {
        if (outError != nullptr) {
            *outError = "measured bounds need a valid image";
        }
        return false;
    }
    const FixtureCrop crop = toCrop(viewport, region);
    if (crop.width <= 0 || crop.height <= 0) {
        if (outError != nullptr) {
            *outError = "measured bounds region is empty";
        }
        return false;
    }
    const std::array<int, 3> background = expectedBackgroundRgb();
    std::array<float, 4> bounds{std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max()};
    std::size_t content = 0u;
    for (int y = crop.y; y < crop.y + crop.height; ++y) {
        for (int x = crop.x; x < crop.x + crop.width; ++x) {
            if (!pixelDiffersFromBackground(viewport, x, y, background)) {
                continue;
            }
            ++content;
            bounds[0] = std::min(bounds[0], static_cast<float>(x));
            bounds[1] = std::min(bounds[1], static_cast<float>(y));
            bounds[2] = std::max(bounds[2], static_cast<float>(x + 1));
            bounds[3] = std::max(bounds[3], static_cast<float>(y + 1));
        }
    }
    if (content == 0u) {
        bounds = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (outCoverage != nullptr) {
        *outCoverage = static_cast<double>(content) /
                       (static_cast<double>(crop.width) *
                        static_cast<double>(crop.height));
    }
    outBounds = bounds;
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

RegionMean regionMean(const FixtureImage& viewport,
                      const std::array<float, 4>& region,
                      float insetFraction) {
    RegionMean mean;
    if (viewport.empty()) {
        return mean;
    }
    const FixtureCrop crop = toCrop(viewport, region);
    const int insetX = static_cast<int>(
        static_cast<float>(crop.width) * std::clamp(insetFraction, 0.0f, 0.49f));
    const int insetY = static_cast<int>(
        static_cast<float>(crop.height) * std::clamp(insetFraction, 0.0f, 0.49f));
    const int x0 = crop.x + insetX;
    const int y0 = crop.y + insetY;
    const int x1 = crop.x + crop.width - insetX;
    const int y1 = crop.y + crop.height - insetY;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(viewport.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            mean.r += static_cast<double>(viewport.rgba[offset + 0u]);
            mean.g += static_cast<double>(viewport.rgba[offset + 1u]);
            mean.b += static_cast<double>(viewport.rgba[offset + 2u]);
            ++mean.samples;
        }
    }
    if (mean.samples == 0u) {
        return mean;
    }
    const double inverse = 1.0 / static_cast<double>(mean.samples);
    mean.r *= inverse;
    mean.g *= inverse;
    mean.b *= inverse;
    mean.luminance = 0.2126 * mean.r + 0.7152 * mean.g + 0.0722 * mean.b;
    return mean;
}

double changedPixelRatioInRegion(const FixtureImage& left,
                                 const FixtureImage& right,
                                 const std::array<float, 4>& region,
                                 int channelTolerance) {
    if (left.width != right.width || left.height != right.height ||
        left.empty() || right.empty()) {
        return 0.0;
    }
    const FixtureCrop crop = toCrop(left, region);
    if (crop.width <= 0 || crop.height <= 0) {
        return 0.0;
    }
    std::size_t changed = 0u;
    std::size_t total = 0u;
    for (int y = crop.y; y < crop.y + crop.height; ++y) {
        for (int x = crop.x; x < crop.x + crop.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(left.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            bool differs = false;
            for (int channel = 0; channel < 3; ++channel) {
                const int difference = std::abs(
                    static_cast<int>(left.rgba[offset +
                                               static_cast<std::size_t>(
                                                   channel)]) -
                    static_cast<int>(right.rgba[offset +
                                                static_cast<std::size_t>(
                                                    channel)]));
                if (difference > channelTolerance) {
                    differs = true;
                    break;
                }
            }
            ++total;
            changed += differs ? 1u : 0u;
        }
    }
    if (total == 0u) {
        return 0.0;
    }
    return static_cast<double>(changed) / static_cast<double>(total);
}

bool validateCaptureContent(const FixtureImage& viewport,
                            FixtureCase fixtureCase,
                            GuardResult& outResult,
                            std::string* outError) {
    GuardResult result;
    if (viewport.empty()) {
        if (outError != nullptr) {
            *outError = "capture content needs a valid image";
        }
        return false;
    }
    const CaseExpectation& expectation = caseExpectation(fixtureCase);
    const std::array<int, 3> background = expectedBackgroundRgb();
    const std::array<float, 4> characterRegion =
        analyticRegions(expectation.variant, expectation.poseBlend, viewport.width,
                        viewport.height)
            .at("character");
    const std::array<float, 4> floorRegion =
        analyticRegions(expectation.variant, expectation.poseBlend, viewport.width,
                        viewport.height)
            .at("floor");
    const std::array<float, 4> wallRegion =
        analyticRegions(expectation.variant, expectation.poseBlend, viewport.width,
                        viewport.height)
            .at("wall");
    const std::array<float, 4> headRegion =
        analyticRegions(expectation.variant, expectation.poseBlend, viewport.width,
                        viewport.height)
            .at("head");

    // Background consistency across the whole viewport.
    std::size_t backgroundPixels = 0u;
    for (int y = 0; y < viewport.height; ++y) {
        for (int x = 0; x < viewport.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(viewport.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            bool matches = true;
            for (int channel = 0; channel < 3; ++channel) {
                if (std::abs(static_cast<int>(
                                 viewport.rgba[offset +
                                               static_cast<std::size_t>(channel)]) -
                             background[static_cast<std::size_t>(channel)]) >
                    kBackgroundTolerance) {
                    matches = false;
                    break;
                }
            }
            backgroundPixels += matches ? 1u : 0u;
        }
    }
    const double backgroundRatio =
        static_cast<double>(backgroundPixels) /
        (static_cast<double>(viewport.width) *
         static_cast<double>(viewport.height));
    result.metrics["background_ratio"] = backgroundRatio;

    std::array<float, 4> characterBounds{};
    double characterCoverage = 0.0;
    std::string boundsError;
    if (!measuredContentBounds(viewport, characterRegion, characterBounds,
                               &characterCoverage, &boundsError)) {
        if (outError != nullptr) {
            *outError = boundsError;
        }
        return false;
    }
    result.metrics["character_coverage"] = characterCoverage;

    if (!expectation.scenePresent) {
        if (backgroundRatio < kUnloadedBackgroundMinimum) {
            result.failures.push_back(
                "unloaded case is not background-only (background ratio " +
                std::to_string(backgroundRatio) + ")");
        }
        if (characterCoverage > 0.02) {
            result.failures.push_back(
                "unloaded case still shows scene content (coverage " +
                std::to_string(characterCoverage) + ")");
        }
        result.ok = result.failures.empty();
        outResult = std::move(result);
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }

    if (backgroundRatio < 0.05) {
        result.failures.push_back(
            "expected background is not visible (background ratio " +
            std::to_string(backgroundRatio) + ")");
    }
    if (characterCoverage < kCharacterCoverageMinimum) {
        result.failures.push_back(
            "character region is missing content (coverage " +
            std::to_string(characterCoverage) + ")");
    }

    std::array<float, 4> analyticCharacter{};
    std::array<float, 4> analyticLimb{};
    std::array<float, 4> analyticHead{};
    analyticCharacterBounds(expectation.variant, expectation.poseBlend,
                            viewport.width, viewport.height, analyticCharacter,
                            analyticLimb, analyticHead);
    for (int edge = 0; edge < 4; ++edge) {
        result.metrics["character_edge_" + std::to_string(edge)] =
            characterBounds[static_cast<std::size_t>(edge)];
        result.metrics["analytic_edge_" + std::to_string(edge)] =
            analyticCharacter[static_cast<std::size_t>(edge)];
    }

    // Weighted deformation against the authored analytic rig: the authored
    // limb tip must land where the analytic palette composition places it. The
    // environment can overlap the character's bounding box, so this checks a
    // small disc at the authored position instead of a silhouette hull.
    {
        const std::vector<glm::vec3> limb = authoredLimbPoints(
            expectation.variant, expectation.poseBlend);
        const glm::mat4 viewProjection =
            fixtureViewProjection(viewport.width, viewport.height);
        const auto project = [&](std::size_t index) {
            return fixtureProjectToPixels(viewProjection, limb.at(index),
                                          viewport.width, viewport.height);
        };
        // Sample just inside the authored tip edge so the disc is mostly
        // character pixels regardless of the ribbon's orientation.
        const glm::vec2 tipMid = (project(4u) + project(5u)) * 0.5f;
        const glm::vec2 previousMid = (project(2u) + project(3u)) * 0.5f;
        const glm::vec2 along = tipMid - previousMid;
        const float length = glm::length(along);
        const glm::vec2 sample =
            length > 1e-3f ? tipMid - (along / length) * 4.0f : tipMid;
        const double tipCoverage = discContentRatio(viewport, sample, 4.0f);
        result.metrics["limb_tip_coverage"] = tipCoverage;
        if (tipCoverage < 0.5) {
            result.failures.push_back(
                "the authored weighted limb tip is not where the analytic rig "
                "places it (coverage " + std::to_string(tipCoverage) + ")");
        }
    }

    double floorCoverage = 0.0;
    std::array<float, 4> floorBounds{};
    if (!measuredContentBounds(viewport, floorRegion, floorBounds,
                               &floorCoverage, nullptr)) {
        result.failures.push_back("the authored floor region is empty");
    }
    result.metrics["floor_coverage"] = floorCoverage;
    if (floorCoverage < kEnvironmentCoverageMinimum) {
        result.failures.push_back(
            "the authored floor is missing (coverage " +
            std::to_string(floorCoverage) + ")");
    }

    double wallCoverage = 0.0;
    std::array<float, 4> wallBounds{};
    if (!measuredContentBounds(viewport, wallRegion, wallBounds,
                               &wallCoverage, nullptr)) {
        result.failures.push_back("the authored wall region is empty");
    }
    result.metrics["wall_coverage"] = wallCoverage;
    if (wallCoverage < kEnvironmentCoverageMinimum) {
        result.failures.push_back(
            "the authored wall is missing (coverage " +
            std::to_string(wallCoverage) + ")");
    }

    double headCoverage = 0.0;
    std::array<float, 4> headBounds{};
    if (!measuredContentBounds(viewport, headRegion, headBounds, &headCoverage,
                               nullptr)) {
        result.failures.push_back("the authored head region is empty");
    }
    result.metrics["head_coverage"] = headCoverage;
    if (headCoverage < 0.5) {
        result.failures.push_back(
            "the emissive-factor-only surface is missing (coverage " +
            std::to_string(headCoverage) + ")");
    }

    // Per-capture authored emission band: the emissive-factor-only surface must
    // be bright when the authored factor is present and dark when it is zero.
    {
        const RegionMean head = regionMean(
            viewport,
            analyticRegions(expectation.variant, expectation.poseBlend,
                            viewport.width, viewport.height)
                .at("head"),
            0.2f);
        result.metrics["head_luminance"] = head.luminance;
        if (expectation.emissionOff) {
            if (!(head.luminance <= kEmissionOffMaximumLuminance)) {
                result.failures.push_back(
                    "an emission-off capture is brighter than the authored "
                    "emission-off band (luminance " +
                    std::to_string(head.luminance) + ")");
            }
        } else if (!(head.luminance >= kEmissionOnMinimumLuminance)) {
            result.failures.push_back(
                "an emission-on capture is darker than the authored emission-on "
                "band (luminance " +
                std::to_string(head.luminance) + ")");
        }
    }

    // Authored albedo inspection surface: the displayed interior must be the
    // authored source bytes multiplied exactly once by the authored linear
    // base-color factor and encoded through the documented transfer.
    {
        const AuthoredProbe& probe = authoredProbe();
        const std::array<float, 4> probeRegion =
            analyticRegions(expectation.variant, expectation.poseBlend,
                            viewport.width, viewport.height)
                .at("probe");
        if (!expectation.scenePresent) {
            double probeCoverage = 0.0;
            std::array<float, 4> probeBounds{};
            if (measuredContentBounds(viewport, probeRegion, probeBounds,
                                      &probeCoverage, nullptr) &&
                probeCoverage > 0.05) {
                result.failures.push_back(
                    "the unloaded case still shows the albedo inspection "
                    "surface (coverage " + std::to_string(probeCoverage) + ")");
            }
        } else {
            // Left and right thirds of the authored panel, inset so the panel's
            // border filtering cannot dominate the measurement.
            const std::array<float, 4> leftRegion{
                probeRegion[0] + probeRegion[2] * 0.12f,
                probeRegion[1] + probeRegion[3] * 0.20f,
                probeRegion[2] * 0.36f,
                probeRegion[3] * 0.60f};
            const std::array<float, 4> rightRegion{
                probeRegion[0] + probeRegion[2] * 0.52f,
                probeRegion[1] + probeRegion[3] * 0.20f,
                probeRegion[2] * 0.36f,
                probeRegion[3] * 0.60f};
            const RegionMean left = regionMean(viewport, leftRegion, 0.1f);
            const RegionMean right = regionMean(viewport, rightRegion, 0.1f);
            const std::array<std::uint8_t, 3> expectedLeft =
                authoredProbeExpectedPixel(probe.map.left[0], probe.map.left[1],
                                           probe.map.left[2]);
            const std::array<std::uint8_t, 3> expectedRight =
                authoredProbeExpectedPixel(probe.map.right[0], probe.map.right[1],
                                           probe.map.right[2]);
            result.metrics["probe_left_r"] = left.r;
            result.metrics["probe_left_g"] = left.g;
            result.metrics["probe_left_b"] = left.b;
            result.metrics["probe_right_r"] = right.r;
            result.metrics["probe_right_g"] = right.g;
            result.metrics["probe_right_b"] = right.b;
            const auto checkProbeHalf = [&](const RegionMean& measured,
                                            const std::array<std::uint8_t, 3>& expected,
                                            const char* half) {
                // Interior point samples cannot hide incorrect pixels behind
                // a coincidentally correct region average.
                const float direction = std::string(half) == "left" ? -0.5f : 0.5f;
                const glm::vec3 point = probe.center + glm::vec3(direction * probe.halfExtent.x, 0.0f, 0.0f);
                const auto center = fixtureProjectToPixels(
                    fixtureViewProjection(viewport.width, viewport.height), point,
                    viewport.width, viewport.height);
                std::size_t correct = 0u;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int x = static_cast<int>(center.x) + dx;
                        const int y = static_cast<int>(center.y) + dy;
                        if (x < 0 || y < 0 || x >= viewport.width || y >= viewport.height) continue;
                        const auto offset = (static_cast<std::size_t>(y) * viewport.width + x) * 4u;
                        bool matches = true;
                        for (std::size_t channel = 0u; channel < 3u; ++channel)
                            matches = matches && std::abs(static_cast<int>(viewport.rgba[offset + channel]) -
                                static_cast<int>(expected[channel])) <= 3;
                        if (matches) ++correct;
                    }
                }
                result.metrics[std::string("probe_") + half + "_pixel_matches"] = static_cast<double>(correct);
                if (correct != 25u) result.failures.push_back(
                    std::string("albedo probe ") + half + " interior pixels disagree with the linear-factor oracle");
                if (measured.samples == 0u) {
                    result.failures.push_back(
                        std::string("the albedo inspection surface ") + half +
                        " half has no measurable pixels");
                    return;
                }
                const double values[3] = {measured.r, measured.g, measured.b};
                for (int channel = 0; channel < 3; ++channel) {
                    if (std::fabs(values[channel] -
                                  static_cast<double>(expected[static_cast<std::size_t>(
                                      channel)])) >
                        static_cast<double>(kProbeChannelTolerance)) {
                        result.failures.push_back(
                            std::string("the albedo inspection surface ") + half +
                            " half does not match the authored source bytes "
                            "multiplied once by the authored linear factor "
                            "(channel " + std::to_string(channel) +
                            ": measured " + std::to_string(values[channel]) +
                            ", expected " +
                            std::to_string(
                                expected[static_cast<std::size_t>(channel)]) +
                            ")");
                        break;
                    }
                }
            };
            checkProbeHalf(left, expectedLeft, "left");
            checkProbeHalf(right, expectedRight, "right");
        }
    }

    result.ok = result.failures.empty();
    outResult = std::move(result);
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

bool sha256FileHex(const std::string& path,
                   std::string& outHex,
                   std::string* outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (outError != nullptr) {
            *outError = "could not open file for hashing: " + path;
        }
        return false;
    }
    Sha256 hash;
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            hash.update(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                        static_cast<std::size_t>(read));
        }
    }
    outHex = hash.finish();
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

bool resolveWithinRoot(const std::string& value,
                       const std::filesystem::path& root,
                       std::filesystem::path& outResolved,
                       std::string& outError) {
    if (value.empty()) {
        outError = "recorded path is empty";
        return false;
    }
    std::error_code error;
    std::filesystem::path candidate(value);
    if (candidate.is_relative()) {
        candidate = root / candidate;
    }
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(root, error);
    if (error) {
        outError = "could not canonicalize the run directory: " + root.string();
        return false;
    }
    const std::filesystem::path canonicalCandidate =
        std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        outError = "could not canonicalize the recorded path: " + value;
        return false;
    }
    auto rootPart = canonicalRoot.begin();
    auto candidatePart = canonicalCandidate.begin();
    while (rootPart != canonicalRoot.end()) {
        if (candidatePart == canonicalCandidate.end() ||
            *rootPart != *candidatePart) {
            outError = "recorded path escapes the run directory: " + value;
            return false;
        }
        ++rootPart;
        ++candidatePart;
    }
    outResolved = canonicalCandidate;
    return true;
}

// ---------------------------------------------------------------------------
// Matrix validation
// ---------------------------------------------------------------------------

namespace {

struct MatrixRow {
    FixtureCase fixtureCase = FixtureCase::ABind;
    std::string host;
    std::string api;
    std::filesystem::path imagePath;
    std::filesystem::path handshakePath;
    FixtureImage image;
    nlohmann::json handshake;
};

bool evidenceNumbersValid(const nlohmann::json& value, const std::string& key) {
    const bool wide = key == "bytes" || key == "png_bytes" || key == "generation" ||
        key == "generation_before_transition" || key == "retained_texture_bytes";
    const bool integer = wide || key == "width" || key == "height" ||
        key == "viewport_width" || key == "viewport_height" || key == "logical_frame" ||
        key == "requested_capture_frame" || key == "render_frame" || key == "transition_frame" ||
        key == "exit_code" || key == "frames" ||
        key == "editor_frames" || key == "draw_calls" || key == "triangles" ||
        key == "palette_matrices" || key == "probe_draw_calls" || key == "debug_view";
    if (integer && !value.is_number_integer()) return false;
    if (value.is_number_integer()) {
        if (value.is_number_unsigned() && value.get<std::uint64_t>() >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
        const auto number = value.get<std::int64_t>();
        if (wide) return number >= 0;
        return number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max();
    }
    if (value.is_number_float()) return std::isfinite(value.get<double>());
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            if (!evidenceNumbersValid(it.value(), it.key())) return false;
    } else if (value.is_array()) {
        for (const auto& element : value)
            if (!evidenceNumbersValid(element, "")) return false;
    }
    return true;
}

bool loadJsonFile(const std::filesystem::path& path,
                  nlohmann::json& outJson,
                  std::string& outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outError = "could not open JSON evidence: " + path.string();
        return false;
    }
    try {
        input >> outJson;
        if (!evidenceNumbersValid(outJson, "")) {
            outError = "out-of-range or non-integer numeric evidence: " + path.string();
            return false;
        }
    } catch (const std::exception& exception) {
        outError = std::string("invalid JSON evidence (") + exception.what() +
                   "): " + path.string();
        return false;
    }
    return true;
}

std::string rowKey(FixtureCase fixtureCase,
                   const std::string& host,
                   const std::string& api) {
    return std::string(fixtureCaseName(fixtureCase)) + "|" + host + "|" + api;
}

bool thresholdsAreApproved(const nlohmann::json& thresholds,
                           std::string& outError) {
    if (!thresholds.is_object()) {
        outError = "manifest has no thresholds object";
        return false;
    }
    const auto read = [&thresholds](const char* name, double& out) {
        if (!thresholds.contains(name) || !thresholds[name].is_number()) {
            return false;
        }
        out = thresholds[name].get<double>();
        return true;
    };
    double mae = 0.0;
    double rmse = 0.0;
    double changed = 0.0;
    double tolerance = 0.0;
    if (!read("normalized_mae", mae) || !read("normalized_rmse", rmse) ||
        !read("changed_ratio", changed) || !read("channel_tolerance", tolerance)) {
        outError = "manifest thresholds are incomplete";
        return false;
    }
    if (std::fabs(mae - kMaxNormalizedMae) > 1e-9 ||
        std::fabs(rmse - kMaxNormalizedRmse) > 1e-9 ||
        std::fabs(changed - kMaxChangedRatio) > 1e-9 ||
        std::fabs(tolerance - static_cast<double>(kChannelTolerance)) > 1e-9) {
        outError =
            "manifest thresholds are not the approved parity thresholds";
        return false;
    }
    return true;
}

// Checked integer extraction: a JSON number must be an integer that fits a
// signed 64-bit value before anything narrows it.
bool readJsonInt(const nlohmann::json& object,
                 const char* key,
                 std::int64_t& out,
                 std::string& outError) {
    if (!object.contains(key)) {
        outError = std::string("evidence is missing '") + key + "'";
        return false;
    }
    const nlohmann::json& value = object.at(key);
    if (value.is_number_unsigned()) {
        const std::uint64_t raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
            outError = std::string("evidence '") + key + "' overflows";
            return false;
        }
        out = static_cast<std::int64_t>(raw);
        return true;
    }
    if (value.is_number_integer()) {
        out = value.get<std::int64_t>();
        return true;
    }
    outError = std::string("evidence '") + key + "' is not an integer";
    return false;
}

bool readJsonString(const nlohmann::json& object,
                    const char* key,
                    std::string& out,
                    std::string& outError) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        outError = std::string("evidence is missing string '") + key + "'";
        return false;
    }
    out = object.at(key).get<std::string>();
    return true;
}

std::string backendDisplayName(const std::string& api) {
    if (api == "opengl") {
        return "OpenGL";
    }
    if (api == "vulkan") {
        return "Vulkan";
    }
    if (api == "d3d12") {
        return "D3D12";
    }
    return api;
}

// Locates the authored viewport inside a host window capture. Standalone hosts
// fill the window, so the expected size is returned unchanged; editor hosts
// render the fixture inside their chrome, so the viewport is found by matching
// the authored background along its top row and rejecting a match that is not
// bounded by host chrome.
bool detectSceneViewport(const FixtureImage& image,
                         int expectedWidth,
                         int expectedHeight,
                         FixtureCrop& outCrop,
                         std::string& outError) {
    if (expectedWidth <= 0 || expectedHeight <= 0) {
        outError = "expected viewport size is invalid";
        return false;
    }
    if (image.width == expectedWidth && image.height == expectedHeight) {
        outCrop = FixtureCrop{0, 0, image.width, image.height};
        return true;
    }
    if (image.width < expectedWidth || image.height < expectedHeight) {
        outError = "capture is smaller than the expected viewport";
        return false;
    }
    const std::array<int, 3> background = expectedBackgroundRgb();
    const auto matchesBackground = [&](int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(image.width) +
             static_cast<std::size_t>(x)) *
            4u;
        for (int channel = 0; channel < 3; ++channel) {
            if (std::abs(static_cast<int>(
                             image.rgba[offset +
                                        static_cast<std::size_t>(channel)]) -
                         background[static_cast<std::size_t>(channel)]) >
                kBackgroundTolerance + 4) {
                return false;
            }
        }
        return true;
    };
    // The authored fixture paints its background along the viewport's left
    // column and top row, so the exact origin is the first position whose left
    // and top neighbours are host chrome.
    for (int y = 1; y + expectedHeight <= image.height; ++y) {
        for (int x = 1; x + expectedWidth <= image.width; ++x) {
            const bool leftEdge = matchesBackground(x, y + 2) &&
                                  matchesBackground(x, y + expectedHeight / 2) &&
                                  !matchesBackground(x - 1, y + 2) &&
                                  !matchesBackground(x - 1, y + expectedHeight / 2);
            if (!leftEdge) {
                continue;
            }
            const bool topEdge =
                matchesBackground(x + expectedWidth / 2, y) &&
                matchesBackground(x + expectedWidth - 1, y) &&
                !matchesBackground(x + expectedWidth / 2, y - 1) &&
                !matchesBackground(x + expectedWidth - 1, y - 1);
            if (!topEdge) {
                continue;
            }
            outCrop = FixtureCrop{x, y, expectedWidth, expectedHeight};
            return true;
        }
    }
    outError = "could not locate the authored viewport inside the capture";
    return false;
}

void fillRect(FixtureImage& image,
              int x0,
              int y0,
              int x1,
              int y1,
              const std::array<std::uint8_t, 3>& color) {
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            image.rgba[offset + 0u] = color[0];
            image.rgba[offset + 1u] = color[1];
            image.rgba[offset + 2u] = color[2];
            image.rgba[offset + 3u] = 255u;
        }
    }
}

void fillTriangle(FixtureImage& image,
                  const glm::vec2& a,
                  const glm::vec2& b,
                  const glm::vec2& c,
                  const std::array<std::uint8_t, 3>& color) {
    const int minX = static_cast<int>(std::floor(
        std::min(a.x, std::min(b.x, c.x))));
    const int maxX = static_cast<int>(std::ceil(
        std::max(a.x, std::max(b.x, c.x))));
    const int minY = static_cast<int>(std::floor(
        std::min(a.y, std::min(b.y, c.y))));
    const int maxY = static_cast<int>(std::ceil(
        std::max(a.y, std::max(b.y, c.y))));
    const auto edge = [](const glm::vec2& p0,
                         const glm::vec2& p1,
                         const glm::vec2& point) {
        return (p1.x - p0.x) * (point.y - p0.y) -
               (p1.y - p0.y) * (point.x - p0.x);
    };
    const float area = edge(a, b, c);
    if (std::fabs(area) < 1e-6f) {
        return;
    }
    const float sign = area > 0.0f ? 1.0f : -1.0f;
    for (int y = std::max(0, minY); y <= std::min(image.height - 1, maxY); ++y) {
        for (int x = std::max(0, minX); x <= std::min(image.width - 1, maxX); ++x) {
            const glm::vec2 point(static_cast<float>(x) + 0.5f,
                                  static_cast<float>(y) + 0.5f);
            const float w0 = edge(b, c, point) * sign;
            const float w1 = edge(c, a, point) * sign;
            const float w2 = edge(a, b, point) * sign;
            if (w0 < -0.5f || w1 < -0.5f || w2 < -0.5f) {
                continue;
            }
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) *
                4u;
            image.rgba[offset + 0u] = color[0];
            image.rgba[offset + 1u] = color[1];
            image.rgba[offset + 2u] = color[2];
            image.rgba[offset + 3u] = 255u;
        }
    }
}

void fillProjectedBox(FixtureImage& image,
                      const std::vector<glm::vec3>& points,
                      const glm::mat4& viewProjection,
                      const std::array<std::uint8_t, 3>& color) {
    const std::array<float, 4> bounds =
        projectPointBounds(points, viewProjection, image.width, image.height);
    fillRect(image,
             static_cast<int>(std::floor(bounds[0])),
             static_cast<int>(std::floor(bounds[1])),
             static_cast<int>(std::ceil(bounds[2])) + 1,
             static_cast<int>(std::ceil(bounds[3])) + 1,
             color);
}

}  // namespace

bool validateMatrix(const std::string& manifestPath,
                    GuardResult& outResult,
                    std::string* outError) try {
    GuardResult result;
    const auto hardFail = [&](std::string message) {
        if (outError != nullptr) {
            *outError = std::move(message);
        }
        return false;
    };
    if (manifestPath.empty()) {
        return hardFail("matrix validation needs a manifest path");
    }
    std::error_code pathError;
    const std::filesystem::path manifestFile =
        std::filesystem::absolute(manifestPath, pathError);
    if (pathError) {
        return hardFail("could not resolve the manifest path: " + manifestPath);
    }
    const std::filesystem::path runDirectory = manifestFile.parent_path();

    nlohmann::json manifest;
    std::string error;
    if (!loadJsonFile(manifestFile, manifest, error)) {
        return hardFail(error);
    }
    if (manifest.value("schema", std::string{}) !=
        "phlosion-scene-fixture-matrix-v1") {
        result.failures.push_back("manifest schema is not the scene fixture matrix schema");
    }
    if (!manifest.contains("thresholds") ||
        !thresholdsAreApproved(manifest["thresholds"], error)) {
        result.failures.push_back(
            error.empty() ? "manifest thresholds are missing" : error);
    }

    // Recorded binaries must match the files actually on disk.
    const std::array<const char*, 4> kRequiredRoles{"runtime", "editor",
                                                    "guard", "plugin"};
    if (!manifest.contains("binaries") || !manifest["binaries"].is_array()) {
        result.failures.push_back("manifest records no binary identities");
    } else {
        if (manifest["binaries"].size() != kRequiredRoles.size()) {
            result.failures.push_back("expected exactly four unique binary roles");
        }
        for (const char* role : kRequiredRoles) {
            const nlohmann::json* entry = nullptr;
            std::size_t matches = 0;
            for (const auto& candidate : manifest["binaries"]) {
                if (candidate.value("role", std::string{}) == role) {
                    entry = &candidate;
                    ++matches;
                }
            }
            if (matches > 1u) result.failures.push_back("duplicate binary role: " + std::string(role));
            if (entry == nullptr) {
                result.failures.push_back(std::string("manifest has no binary "
                                                      "identity for role ") +
                                          role);
                continue;
            }
            const std::string path = entry->value("path", std::string{});
            const std::string recordedHash =
                entry->value("sha256", std::string{});
            const std::uint64_t recordedBytes =
                entry->value("bytes", static_cast<std::uint64_t>(0u));
            if (path.empty() || !std::filesystem::exists(path)) {
                result.failures.push_back(std::string("recorded binary is "
                                                      "missing for role ") +
                                          role + ": " + path);
                continue;
            }
            std::error_code sizeError;
            const std::uint64_t actualBytes = static_cast<std::uint64_t>(
                std::filesystem::file_size(path, sizeError));
            if (sizeError || actualBytes != recordedBytes) {
                result.failures.push_back(std::string("binary size changed for "
                                                      "role ") +
                                          role);
            }
            std::string actualHash;
            if (!sha256FileHex(path, actualHash, &error)) {
                result.failures.push_back(error);
            } else if (!hexDigestEquals(recordedHash, actualHash)) {
                result.failures.push_back(std::string("binary hash changed for "
                                                      "role ") +
                                          role + ": recorded " + recordedHash +
                                          " actual " + actualHash);
            }
        }
    }

    std::vector<MatrixRow> rows;
    std::map<std::string, std::size_t> rowIndex;
    std::int64_t editorFrames = 0;
    std::int64_t runtimeCaptureFrame = 0;
    std::int64_t editorCaptureFrame = 0;
    if (!readJsonInt(manifest, "editor_frames", editorFrames, error) ||
        !manifest.contains("capture_frames") ||
        !readJsonInt(manifest.at("capture_frames"), "runtime",
                     runtimeCaptureFrame, error) ||
        !readJsonInt(manifest.at("capture_frames"), "editor",
                     editorCaptureFrame, error)) {
        result.failures.push_back(
            error.empty() ? "manifest capture frames are missing" : error);
    }
    if (runtimeCaptureFrame != 6 || editorCaptureFrame != 6 || editorFrames < 6) {
        result.failures.push_back("capture must observe authored final state at frame 6");
    }
    if (!manifest.contains("captures") || !manifest["captures"].is_array()) {
        result.failures.push_back("manifest records no captures");
    } else {
        for (const auto& entry : manifest["captures"]) {
            const std::string caseName = entry.value("case", std::string{});
            const std::optional<FixtureCase> parsedCase =
                parseFixtureCase(caseName);
            if (!parsedCase) {
                result.failures.push_back("capture records unknown case: " +
                                          caseName);
                continue;
            }
            const std::string host = entry.value("host", std::string{});
            const std::string api = entry.value("api", std::string{});
            if (host != "runtime" && host != "editor") {
                result.failures.push_back("capture records unknown host: " +
                                          host);
                continue;
            }
            if (api != "opengl" && api != "vulkan" && api != "d3d12") {
                result.failures.push_back("capture records unknown api: " + api);
                continue;
            }
            const std::string key = rowKey(*parsedCase, host, api);
            if (rowIndex.find(key) != rowIndex.end()) {
                result.failures.push_back("duplicate capture row: " + key);
                continue;
            }
            MatrixRow row;
            row.fixtureCase = *parsedCase;
            row.host = host;
            row.api = api;
            std::int64_t checkedFrame = 0;
            if (!readJsonInt(entry, "logical_frame", checkedFrame, error)) {
                result.failures.push_back("capture frame evidence rejected (" +
                                          key + "): " + error);
            } else if (checkedFrame !=
                       static_cast<std::int64_t>(
                           fixtureCaseLogicalFrame(*parsedCase))) {
                result.failures.push_back(
                    "capture logical frame does not match the authored case: " +
                    key);
            }
            std::int64_t checkedExitCode = 1;
            if (!readJsonInt(entry, "exit_code", checkedExitCode, error)) {
                result.failures.push_back("capture exit evidence rejected (" +
                                          key + "): " + error);
            } else if (checkedExitCode != 0) {
                result.failures.push_back("capture process failed: " + key);
            }
            std::int64_t requestedFrame = 0;
            if (!readJsonInt(entry, "requested_capture_frame", requestedFrame,
                             error)) {
                result.failures.push_back("capture frame evidence rejected (" +
                                          key + "): " + error);
            } else if (requestedFrame != (host == "runtime" ? runtimeCaptureFrame
                                                             : editorCaptureFrame)) {
                result.failures.push_back(
                    "capture requested frame does not match the manifest: " +
                    key);
            }
            std::string resolveError;
            if (!resolveWithinRoot(entry.value("png", std::string{}), runDirectory,
                                   row.imagePath, resolveError)) {
                result.failures.push_back("capture image path rejected (" + key +
                                          "): " + resolveError);
                continue;
            }
            if (!resolveWithinRoot(entry.value("handshake", std::string{}),
                                   runDirectory, row.handshakePath,
                                   resolveError)) {
                result.failures.push_back("capture handshake path rejected (" +
                                          key + "): " + resolveError);
                continue;
            }
            if (!loadFixturePng(row.imagePath.string(), row.image, &error)) {
                result.failures.push_back("capture image could not be read (" +
                                          key + "): " + error);
                continue;
            }
            const int windowWidth = row.image.width;
            const int windowHeight = row.image.height;
            if (!loadJsonFile(row.handshakePath, row.handshake, error)) {
                result.failures.push_back(error + " (" + key + ")");
                continue;
            }
            std::string imageHash;
            if (!sha256FileHex(row.imagePath.string(), imageHash, &error)) {
                result.failures.push_back(error);
            } else if (!hexDigestEquals(
                           entry.value("png_sha256", std::string{}), imageHash)) {
                result.failures.push_back(
                    "capture image hash does not match the recorded evidence: " +
                    key);
            }
            std::error_code sizeError;
            const std::uint64_t imageBytes = static_cast<std::uint64_t>(
                std::filesystem::file_size(row.imagePath, sizeError));
            const std::uint64_t recordedImageBytes =
                entry.value("png_bytes", static_cast<std::uint64_t>(0u));
            if (sizeError || imageBytes != recordedImageBytes) {
                result.failures.push_back("capture image size does not match "
                                          "the recorded evidence: " + key);
            }
            const int recordedWidth = entry.contains("surface")
                                          ? entry["surface"].value("width", 0)
                                          : 0;
            const int recordedHeight = entry.contains("surface")
                                           ? entry["surface"].value("height", 0)
                                           : 0;
            // A host window capture is cropped to the authored viewport, which
            // is the surface size both hosts record in their handshake.
            const int handshakeWidth =
                row.handshake.contains("surface")
                    ? row.handshake["surface"].value("width", 0)
                    : 0;
            const int handshakeHeight =
                row.handshake.contains("surface")
                    ? row.handshake["surface"].value("height", 0)
                    : 0;
            FixtureCrop viewportCrop{};
            if (!detectSceneViewport(row.image, handshakeWidth, handshakeHeight,
                                     viewportCrop, error)) {
                result.failures.push_back("capture viewport rejected (" + key +
                                          "): " + error);
                continue;
            }
            if (viewportCrop.x != 0 || viewportCrop.y != 0 ||
                viewportCrop.width != row.image.width ||
                viewportCrop.height != row.image.height) {
                row.image = cropFixtureImage(row.image, viewportCrop);
            }
            if (recordedWidth != handshakeWidth ||
                recordedHeight != handshakeHeight ||
                row.image.width != handshakeWidth ||
                row.image.height != handshakeHeight) {
                result.failures.push_back(
                    "capture dimensions do not match the recorded surface: " +
                    key);
            }
            const CaseExpectation& expectation = caseExpectation(*parsedCase);
            const auto requireEqual = [&](const char* label, auto actual,
                                          auto expectedValue) {
                if (!(actual == expectedValue)) {
                    result.failures.push_back(std::string("handshake ") + label +
                                              " mismatch: " + key);
                }
            };
            requireEqual("schema",
                         row.handshake.value("schema", std::string{}),
                         std::string("phlosion-scene-fixture-handshake-v1"));
            requireEqual("sample identity",
                         row.handshake.value("sample_identity", std::string{}),
                         std::string("phlosion-scene-fixture-v1/") + caseName);
            requireEqual("case", row.handshake.value("case", std::string{}),
                         caseName);
            requireEqual("host", row.handshake.value("host", std::string{}),
                         host);
            requireEqual("requested backend",
                         row.handshake.value("requested_backend", std::string{}),
                         api);
            requireEqual("actual backend",
                         row.handshake.value("actual_backend", std::string{}),
                         api);
            requireEqual("logical frame",
                         row.handshake.value("logical_frame", -1),
                         fixtureCaseLogicalFrame(*parsedCase));
            requireEqual("render frame", row.handshake.value("render_frame", -1), requestedFrame);
            requireEqual("transition frame", row.handshake.value("transition_frame", -1), fixtureTransitionFrame());
            requireEqual("variant",
                         row.handshake.value("variant", std::string{}),
                         std::string(sceneVariantName(expectation.variant)));
            requireEqual("surface width",
                         row.handshake["surface"].value("width", -1),
                         recordedWidth);
            requireEqual("surface height",
                         row.handshake["surface"].value("height", -1),
                         recordedHeight);
            requireEqual("bind pose",
                         row.handshake["pose"].value("bind", false),
                         expectation.bindPose);
            requireEqual("emission off",
                         row.handshake["pose"].value("emission_off", false),
                         expectation.emissionOff);
            requireEqual("scene unloaded",
                         row.handshake["scene"].value("unloaded", false),
                         expectation.unloaded);
            requireEqual("scene drawn",
                         row.handshake["scene"].value("drawn", false), true);
            requireEqual("replacement attempted",
                         row.handshake["replacement"].value("attempted", false),
                         expectation.replacementAttempted);
            requireEqual("replacement failed",
                         row.handshake["replacement"].value("failed", false),
                         expectation.replacementFailed);
            requireEqual("replacement succeeded",
                         row.handshake["replacement"].value("succeeded", false),
                         expectation.replacementSucceeded);
            if (expectation.replacementFailed &&
                row.handshake["replacement"]
                    .value("diagnostic", std::string{})
                    .empty()) {
                result.failures.push_back(
                    "failed replacement has no diagnostic: " + key);
            }
            if (expectation.scenePresent) {
                if (row.handshake["scene"].value("identity", std::string{}).empty()) {
                    result.failures.push_back(
                        "active scene has no published identity: " + key);
                }
                if (row.handshake["stats"].value("draw_calls", 0) < 2) {
                    result.failures.push_back(
                        "scene capture recorded no scene submission: " + key);
                }
            } else {
                requireEqual("draw calls",
                             row.handshake["stats"].value("draw_calls", -1), 1);
                requireEqual("scene identity",
                             row.handshake["scene"].value("identity", std::string{}),
                             std::string{});
            }

            // Pose transition evidence must describe a real before/after pair.
            if (!row.handshake.contains("pose")) {
                result.failures.push_back("handshake has no pose record: " + key);
            } else {
                const nlohmann::json& pose = row.handshake["pose"];
                const bool transitionExpected =
                    *parsedCase == FixtureCase::APose;
                requireEqual("pose transition", pose.value("transition_applied", false),
                             transitionExpected);
                requireEqual("pose bind before transition",
                             pose.value("bind_before_transition", false),
                             transitionExpected ? true : expectation.bindPose);
                const double beforeTime = pose.value("time_before_transition", -1.0);
                const double afterTime = pose.value("time_sec", -1.0);
                const double expectedAfter =
                    (expectation.bindPose || expectation.unloaded)
                        ? 0.0
                        : static_cast<double>(fixturePoseTimeSec());
                if (std::fabs(afterTime - expectedAfter) > 1e-4) {
                    result.failures.push_back(
                        "handshake pose time does not match the authored pose: " +
                        key);
                }
                const double expectedBefore =
                    transitionExpected ? 0.0 : (expectation.bindPose ? 0.0 : static_cast<double>(fixturePoseTimeSec()));
                if (std::fabs(beforeTime - expectedBefore) > 1e-4) {
                    result.failures.push_back(
                        "handshake pre-transition pose time does not match the "
                        "authored start state: " + key);
                }
                const std::string beforeIdentity =
                    pose.value("identity_before_transition", std::string{});
                if (transitionExpected && beforeIdentity != row.handshake["scene"].value(
                                          "identity", std::string{})) {
                    result.failures.push_back(
                        "handshake pre-transition asset identity does not match "
                        "the active asset: " + key);
                }
            }

            // The albedo inspection surface must be presented through the
            // generic adapter with the documented debug view, and the recorded
            // expectation must agree with the guard's own authored math.
            if (!row.handshake.contains("probe")) {
                result.failures.push_back("handshake has no probe record: " + key);
            } else {
                const nlohmann::json& probe = row.handshake["probe"];
                requireEqual("probe presence", probe.value("present", false),
                             expectation.scenePresent);
                requireEqual("probe debug view", probe.value("debug_view", -1), 2);
                requireEqual("probe debug reset",
                             probe.value("debug_state_reset", false),
                             expectation.scenePresent);
                if (expectation.scenePresent) {
                    requireEqual("probe draw calls",
                                 row.handshake["stats"].value("probe_draw_calls",
                                                              -1),
                                 1);
                    if (probe.value("identity", std::string{}).empty()) {
                        result.failures.push_back(
                            "probe has no published identity: " + key);
                    }
                    const AuthoredProbe& authored = authoredProbe();
                    const std::array<std::uint8_t, 3> expectedLeft =
                        expectedProbePixel(authored.map.left);
                    const std::array<std::uint8_t, 3> expectedRight =
                        expectedProbePixel(authored.map.right);
                    if (probe.value("expected_left_rgb", expectedLeft) !=
                            expectedLeft ||
                        probe.value("expected_right_rgb", expectedRight) !=
                            expectedRight) {
                        result.failures.push_back(
                            "handshake probe expectation does not match the "
                            "authored source bytes and factor: " + key);
                    }
                } else {
                    requireEqual("probe draw calls",
                                 row.handshake["stats"].value("probe_draw_calls",
                                                              -1),
                                 0);
                }
            }

            // The sample's recorded regions must match the guard's analytic
            // regions within two pixels.
            const std::map<std::string, std::array<float, 4>> analytic =
                analyticRegions(expectation.variant, expectation.poseBlend,
                                row.image.width, row.image.height);
            if (!row.handshake.contains("regions") ||
                !row.handshake["regions"].is_object()) {
                result.failures.push_back("handshake has no region records: " +
                                          key);
            } else {
                for (const auto& analyticEntry : analytic) {
                    if (!row.handshake["regions"].contains(analyticEntry.first)) {
                        result.failures.push_back("handshake has no region '" +
                                                  analyticEntry.first +
                                                  "': " + key);
                        continue;
                    }
                    const auto recorded =
                        row.handshake["regions"][analyticEntry.first]
                            .get<std::array<float, 4>>();
                    for (int element = 0; element < 4; ++element) {
                        const float scale =
                            element % 2 == 0 ? static_cast<float>(row.image.width)
                                        : static_cast<float>(row.image.height);
                        const float difference = std::fabs(
                            (recorded[static_cast<std::size_t>(element)] -
                             analyticEntry.second[static_cast<std::size_t>(
                                 element)]) *
                            scale);
                        if (difference > 2.0f) {
                            result.failures.push_back(
                                "handshake region '" + analyticEntry.first +
                                "' differs from the authored analytic region: " +
                                key);
                            break;
                        }
                    }
                }
            }

            GuardResult content;
            if (!validateCaptureContent(row.image, *parsedCase, content, &error)) {
                result.failures.push_back("capture content check failed (" + key +
                                          "): " + error);
            } else {
                for (const std::string& failure : content.failures) {
                    result.failures.push_back(key + ": " + failure);
                }
                for (const auto& metric : content.metrics) {
                    result.metrics[key + "/" + metric.first] = metric.second;
                }
            }

            // Full process evidence agreement: log files, the native screenshot
            // record, the parity contract line and, for the editor host, the
            // editor metrics and real plugin activation.
            {
                const auto resolveEvidence =
                    [&](const char* field, std::filesystem::path& outPath) {
                        std::string value;
                        if (!readJsonString(entry, field, value, error)) {
                            result.failures.push_back(
                                std::string("capture evidence rejected (") + key +
                                "): " + error);
                            return false;
                        }
                        std::string resolveError;
                        if (!resolveWithinRoot(value, runDirectory, outPath,
                                               resolveError)) {
                            result.failures.push_back(
                                std::string("capture evidence path rejected (") +
                                key + "): " + resolveError);
                            return false;
                        }
                        return true;
                    };
                std::filesystem::path stdoutPath;
                std::filesystem::path stderrPath;
                std::filesystem::path metricsPath;
                bool evidenceOk = resolveEvidence("stdout", stdoutPath) &&
                                  resolveEvidence("stderr", stderrPath) &&
                                  (host != "editor" || resolveEvidence("metrics", metricsPath));
                std::string handshakeDigest;
                if (!readJsonString(entry, "handshake_sha256", handshakeDigest,
                                    error)) {
                    result.failures.push_back(
                        std::string("capture evidence rejected (") + key +
                        "): " + error);
                    evidenceOk = false;
                } else {
                    std::string actualDigest;
                    if (!sha256FileHex(row.handshakePath.string(), actualDigest,
                                       &error)) {
                        result.failures.push_back(error);
                        evidenceOk = false;
                    } else if (!hexDigestEquals(handshakeDigest, actualDigest)) {
                        result.failures.push_back(
                            "handshake hash does not match the recorded "
                            "evidence: " + key);
                        evidenceOk = false;
                    }
                }
                std::string stdoutText;
                std::string stderrText;
                if (evidenceOk) {
                    const auto readFile = [&](const std::filesystem::path& path,
                                              std::string& out) {
                        std::ifstream input(path, std::ios::binary);
                        if (!input) {
                            result.failures.push_back(
                                "capture log could not be read: " + key);
                            return false;
                        }
                        out.assign(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
                        return true;
                    };
                    evidenceOk = readFile(stdoutPath, stdoutText) &&
                                 readFile(stderrPath, stderrText);
                }
                if (evidenceOk) {
                    const std::string display = backendDisplayName(api);
                    const std::string combined = stdoutText + "\n" + stderrText;
                    const std::string expectedSuffix =
                        "[" + display + "]";
                    std::size_t wroteRecords = 0u;
                    std::size_t failedRecords = 0u;
                    bool recordMatched = false;
                    std::size_t position = 0u;
                    while ((position = combined.find("[Screenshot]", position)) !=
                           std::string::npos) {
                        const std::size_t lineEnd = combined.find('\n', position);
                        const std::string line = combined.substr(
                            position,
                            lineEnd == std::string::npos
                                ? std::string::npos
                                : lineEnd - position);
                        position += 1u;
                        if (line.find("FAILED") != std::string::npos) {
                            ++failedRecords;
                            continue;
                        }
                        if (line.find("WROTE") == std::string::npos) {
                            continue;
                        }
                        ++wroteRecords;
                        if (line.find(expectedSuffix) == std::string::npos) continue;
                        const std::size_t sizeAt = line.find("size=");
                        const std::size_t frameAt = line.find("frame=");
                        const std::string expectedPath =
                            std::filesystem::absolute(row.imagePath).string();
                        if (line.find("WROTE " + expectedPath + " size=") == std::string::npos ||
                            sizeAt == std::string::npos ||
                            frameAt == std::string::npos) {
                            continue;
                        }
                        const std::string sizeText = line.substr(
                            sizeAt + 5u, line.find(' ', sizeAt + 5u) -
                                             (sizeAt + 5u));
                        const std::string frameText = line.substr(frameAt + 6u);
                        const std::string expectedSize =
                            std::to_string(windowWidth) + "x" +
                            std::to_string(windowHeight);
                        std::int64_t loggedFrame = -1;
                        try {
                            loggedFrame = std::stoll(frameText);
                        } catch (const std::exception&) {
                            loggedFrame = -1;
                        }
                        if (sizeText == expectedSize &&
                            loggedFrame == requestedFrame) {
                            recordMatched = true;
                        }
                    }
                    if (failedRecords != 0u) {
                        result.failures.push_back(
                            "capture reported a screenshot failure: " + key);
                    }
                    if (wroteRecords != 1u || !recordMatched) {
                        result.failures.push_back(
                            "capture did not report exactly one matching "
                            "screenshot record for the requested frame: " +
                            key);
                    }
                    if (combined.find("[ParityContract][" + display +
                                      "] PASS") == std::string::npos) {
                        result.failures.push_back(
                            "capture did not report a passing parity contract: " +
                            key);
                    }
                    if (combined.find("[ParityContract][" + display +
                                      "] FAIL") != std::string::npos) {
                        result.failures.push_back(
                            "capture reported a failing parity contract: " + key);
                    }
                    if (combined.find("using OpenGL compatibility mode") !=
                        std::string::npos) {
                        result.failures.push_back(
                            "capture reported a renderer fallback: " + key);
                    }
                }
                if (evidenceOk && host == "editor") {
                    nlohmann::json metrics;
                    if (!loadJsonFile(metricsPath, metrics, error)) {
                        result.failures.push_back(error + " (" + key + ")");
                    } else {
                        std::int64_t frames = 0;
                        std::int64_t viewportWidth = 0;
                        std::int64_t viewportHeight = 0;
                        if (!readJsonInt(metrics.at("capture"), "frames", frames,
                                         error) ||
                            !readJsonInt(metrics.at("capture"),
                                         "viewport_width", viewportWidth,
                                         error) ||
                            !readJsonInt(metrics.at("capture"),
                                         "viewport_height", viewportHeight,
                                         error)) {
                            result.failures.push_back(
                                "editor metrics rejected (" + key + "): " +
                                error);
                        } else if (frames < editorFrames ||
                                   viewportWidth != handshakeWidth ||
                                   viewportHeight != handshakeHeight) {
                            result.failures.push_back(
                                "editor metrics do not match the requested "
                                "frames/viewport: " + key);
                        }
                        if (metrics.value("schema", std::string{}) !=
                            "phlosion-editor-baseline-metrics-v1") {
                            result.failures.push_back(
                                "editor metrics schema is unexpected: " + key);
                        }
                        if (!metrics.contains("renderer") ||
                            metrics["renderer"].value("backend", std::string{}) !=
                                api) {
                            result.failures.push_back(
                                "editor metrics backend does not match: " + key);
                        }
                        if (!metrics.contains("project") ||
                            metrics["project"].value("id", std::string{}) !=
                                "phlosion-scene-fixture") {
                            result.failures.push_back(
                                "editor plugin activation is not recorded: " +
                                key);
                        }
                    }
                }
            }

            rowIndex[key] = rows.size();
            rows.push_back(std::move(row));
        }
    }

    // Exact Cartesian coverage: 6 cases x 2 hosts x 3 APIs.
    std::map<FixtureCase, std::vector<std::size_t>> byCase;
    const std::array<const char*, 2> kHosts{"runtime", "editor"};
    const std::array<const char*, 3> kApis{"opengl", "vulkan", "d3d12"};
    for (const FixtureCaseInfo& info : fixtureCaseCatalog()) {
        for (const char* host : kHosts) {
            for (const char* api : kApis) {
                const std::string key = rowKey(info.fixtureCase, host, api);
                const auto found = rowIndex.find(key);
                if (found == rowIndex.end()) {
                    result.failures.push_back("missing capture row: " + key);
                    continue;
                }
                byCase[info.fixtureCase].push_back(found->second);
            }
        }
    }
    result.metrics["capture_rows"] = static_cast<double>(rows.size());
    if (rows.size() != 36u) {
        result.failures.push_back("expected exactly 36 captures, found " +
                                  std::to_string(rows.size()));
    }

    // Every capture of a case must agree across hosts and APIs.
    for (const auto& entry : byCase) {
        const std::vector<std::size_t>& indices = entry.second;
        for (std::size_t left = 0u; left < indices.size(); ++left) {
            for (std::size_t right = left + 1u; right < indices.size(); ++right) {
                const MatrixRow& a = rows[indices[left]];
                const MatrixRow& b = rows[indices[right]];
                if (a.image.width != b.image.width ||
                    a.image.height != b.image.height) {
                    result.failures.push_back(
                        "capture dimensions differ for case " +
                        std::string(fixtureCaseName(a.fixtureCase)));
                    continue;
                }
                ImageCompareMetrics metrics;
                if (!compareFixtureImages(a.image, b.image, kChannelTolerance,
                                          metrics, &error)) {
                    result.failures.push_back(error);
                    continue;
                }
                const std::string label =
                    std::string(fixtureCaseName(a.fixtureCase)) + "/" + a.host +
                    "-" + a.api + "/" + b.host + "-" + b.api;
                if (metrics.normalizedMae > kMaxNormalizedMae ||
                    metrics.normalizedRmse > kMaxNormalizedRmse ||
                    metrics.changedPixelRatio > kMaxChangedRatio) {
                    result.failures.push_back(
                        "host/API parity failure for " + label +
                        " (mae " + std::to_string(metrics.normalizedMae) +
                        ", rmse " + std::to_string(metrics.normalizedRmse) +
                        ", changed " +
                        std::to_string(metrics.changedPixelRatio) + ")");
                }
            }
        }
    }

    // Authored cross-case expectations.
    // Independent per host/API effect checks: a missing or wrong effect on one
    // backend must fail even when the OpenGL reference looks correct.
    const auto findRow = [&](FixtureCase fixtureCase, const char* host,
                             const char* api) -> const MatrixRow* {
        const auto found = rowIndex.find(rowKey(fixtureCase, host, api));
        return found == rowIndex.end() ? nullptr : &rows[found->second];
    };
    for (const char* host : kHosts) {
        for (const char* api : kApis) {
            const MatrixRow* bind = findRow(FixtureCase::ABind, host, api);
            const MatrixRow* posed = findRow(FixtureCase::APose, host, api);
            const MatrixRow* failed =
                findRow(FixtureCase::FailedReplacement, host, api);
            const MatrixRow* replacement =
                findRow(FixtureCase::ReplacementB, host, api);
            const MatrixRow* emission =
                findRow(FixtureCase::EmissionOff, host, api);
            if (bind == nullptr || posed == nullptr || failed == nullptr ||
                replacement == nullptr || emission == nullptr) {
                continue;
            }
            const std::string label = std::string(host) + "/" + api;
            if (bind->image.width == posed->image.width &&
                bind->image.height == posed->image.height &&
                bind->image.width > 0) {
                const auto regions =
                    analyticRegions(SceneVariant::A, 1.0f, posed->image.width,
                                    posed->image.height);
                const double changed = changedPixelRatioInRegion(
                    bind->image, posed->image, regions.at("limb"),
                    kChannelTolerance);
                result.metrics["deformation_changed_ratio/" + label] = changed;
                if (changed < kDeformationChangedRatioMinimum) {
                    result.failures.push_back(
                        "the authored weighted limb does not differ between the "
                        "bind pose and the animated pose on " +
                        label);
                }
            }
            ImageCompareMetrics preserved;
            if (compareFixtureImages(posed->image, failed->image,
                                     kChannelTolerance, preserved, &error)) {
                if (preserved.normalizedMae > kMaxNormalizedMae ||
                    preserved.normalizedRmse > kMaxNormalizedRmse ||
                    preserved.changedPixelRatio > kMaxChangedRatio) {
                    result.failures.push_back(
                        "a failed replacement changed the presented A pose on " +
                        label);
                }
            } else {
                result.failures.push_back(error);
            }
            if (posed->image.width == replacement->image.width &&
                posed->image.height == replacement->image.height &&
                posed->image.width > 0) {
                const auto regions =
                    analyticRegions(SceneVariant::B, 1.0f,
                                    replacement->image.width,
                                    replacement->image.height);
                const double changed = changedPixelRatioInRegion(
                    posed->image, replacement->image, regions.at("character"),
                    kChannelTolerance);
                result.metrics["replacement_changed_ratio/" + label] = changed;
                if (changed < kReplacementChangedRatioMinimum) {
                    result.failures.push_back(
                        "the replacement content is not visibly different on " +
                        label);
                }
            }
            if (posed->image.width == emission->image.width &&
                posed->image.height == emission->image.height &&
                posed->image.width > 0) {
                const auto regions =
                    analyticRegions(SceneVariant::A, 1.0f, posed->image.width,
                                    posed->image.height);
                const RegionMean on =
                    regionMean(posed->image, regions.at("head"), 0.2f);
                const RegionMean off =
                    regionMean(emission->image, regions.at("head"), 0.2f);
                result.metrics["emission_on_luminance/" + label] = on.luminance;
                result.metrics["emission_off_luminance/" + label] = off.luminance;
                if (on.samples == 0u || off.samples == 0u) {
                    result.failures.push_back(
                        "the emissive-factor-only region has no measurable "
                        "pixels on " + label);
                } else if (on.luminance - off.luminance <
                           kEmissionLuminanceMinimum) {
                    result.failures.push_back(
                        "the authored emissive factor does not change the "
                        "emissive surface on " + label);
                }
            }
        }
    }

    const auto referenceImage = [&](FixtureCase fixtureCase) -> const FixtureImage* {
        const FixtureImage* fallback = nullptr;
        const auto found = byCase.find(fixtureCase);
        if (found == byCase.end()) {
            return nullptr;
        }
        for (const std::size_t index : found->second) {
            if (fallback == nullptr) {
                fallback = &rows[index].image;
            }
            if (rows[index].host == "runtime" && rows[index].api == "opengl") {
                return &rows[index].image;
            }
        }
        return fallback;
    };
    const FixtureImage* bindImage = referenceImage(FixtureCase::ABind);
    const FixtureImage* poseImage = referenceImage(FixtureCase::APose);
    const FixtureImage* failedImage = referenceImage(FixtureCase::FailedReplacement);
    const FixtureImage* replacementImage = referenceImage(FixtureCase::ReplacementB);
    const FixtureImage* emissionImage = referenceImage(FixtureCase::EmissionOff);
    if (bindImage != nullptr && poseImage != nullptr) {
        const auto regions = analyticRegions(SceneVariant::A, 1.0f,
                                             poseImage->width, poseImage->height);
        const double changed = changedPixelRatioInRegion(
            *bindImage, *poseImage, regions.at("limb"), kChannelTolerance);
        result.metrics["deformation_changed_ratio"] = changed;
        if (changed < kDeformationChangedRatioMinimum) {
            result.failures.push_back(
                "the authored weighted limb does not differ between the bind "
                "pose and the animated pose (changed ratio " +
                std::to_string(changed) + ")");
        }
    }
    if (poseImage != nullptr && failedImage != nullptr) {
        ImageCompareMetrics metrics;
        if (compareFixtureImages(*poseImage, *failedImage, kChannelTolerance,
                                 metrics, &error)) {
            if (metrics.normalizedMae > kMaxNormalizedMae ||
                metrics.normalizedRmse > kMaxNormalizedRmse ||
                metrics.changedPixelRatio > kMaxChangedRatio) {
                result.failures.push_back(
                    "a failed replacement changed the presented A pose");
            }
        } else {
            result.failures.push_back(error);
        }
    }
    if (poseImage != nullptr && replacementImage != nullptr) {
        const auto regions = analyticRegions(SceneVariant::B, 1.0f,
                                             replacementImage->width,
                                             replacementImage->height);
        const double changed = changedPixelRatioInRegion(
            *poseImage, *replacementImage, regions.at("character"),
            kChannelTolerance);
        result.metrics["replacement_changed_ratio"] = changed;
        if (changed < kReplacementChangedRatioMinimum) {
            result.failures.push_back(
                "the replacement content is not visibly different (changed "
                "ratio " + std::to_string(changed) + ")");
        }
    }
    if (poseImage != nullptr && emissionImage != nullptr) {
        const auto regions = analyticRegions(SceneVariant::A, 1.0f,
                                             poseImage->width, poseImage->height);
        const RegionMean on = regionMean(*poseImage, regions.at("head"), 0.2f);
        const RegionMean off = regionMean(*emissionImage, regions.at("head"), 0.2f);
        result.metrics["emission_on_luminance"] = on.luminance;
        result.metrics["emission_off_luminance"] = off.luminance;
        if (on.samples == 0u || off.samples == 0u) {
            result.failures.push_back(
                "the emissive-factor-only region has no measurable pixels");
        } else if (on.luminance - off.luminance < kEmissionLuminanceMinimum) {
            result.failures.push_back(
                "the authored emissive factor does not change the emissive "
                "surface (on " + std::to_string(on.luminance) + " off " +
                std::to_string(off.luminance) + ")");
        }
    }

    // Contact sheet over every capture.
    if (!rows.empty() && rows.front().image.width > 0) {
        std::vector<std::pair<std::string, FixtureImage>> labelled;
        labelled.reserve(rows.size());
        for (const MatrixRow& row : rows) {
            labelled.emplace_back(rowKey(row.fixtureCase, row.host, row.api),
                                  row.image);
        }
        const FixtureImage sheet = buildFixtureContactSheet(
            labelled, 6, rows.front().image.width, rows.front().image.height);
        if (sheet.empty()) {
            result.failures.push_back("contact sheet could not be built");
        } else if (!saveFixturePng(
                       (runDirectory / "contact-sheet.png").string(), sheet,
                       &error)) {
            result.failures.push_back("contact sheet could not be written: " +
                                      error);
        }
    }

    result.ok = result.failures.empty();
    nlohmann::json validation;
    validation["schema"] = "phlosion-scene-fixture-validation-v1";
    validation["ok"] = result.ok;
    validation["failures"] = result.failures;
    validation["metrics"] = result.metrics;
    std::ofstream output(runDirectory / "validation.json", std::ios::trunc);
    if (output) {
        output << validation.dump(2) << '\n';
    }

    outResult = std::move(result);
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

catch (const std::exception& exception) {
    outResult.ok = false;
    if (outError != nullptr) *outError = std::string("invalid matrix evidence: ") + exception.what();
    return false;
}

bool buildSyntheticCapture(FixtureCase fixtureCase,
                           int width,
                           int height,
                           FixtureImage& outImage,
                           std::string* outError) {
    return buildSyntheticCapture(fixtureCase, ProbeDefect::None, width, height,
                                 outImage, outError);
}

bool buildSyntheticCapture(FixtureCase fixtureCase,
                           ProbeDefect defect,
                           int width,
                           int height,
                           FixtureImage& outImage,
                           std::string* outError) {
    if (width <= 0 || height <= 0) {
        if (outError != nullptr) {
            *outError = "synthetic capture needs positive dimensions";
        }
        return false;
    }
    const CaseExpectation& expectation = caseExpectation(fixtureCase);
    FixtureImage image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 4u,
                      255u);
    const std::array<int, 3> background = expectedBackgroundRgb();
    for (std::size_t offset = 0u; offset + 3u < image.rgba.size(); offset += 4u) {
        image.rgba[offset + 0u] = static_cast<std::uint8_t>(background[0]);
        image.rgba[offset + 1u] = static_cast<std::uint8_t>(background[1]);
        image.rgba[offset + 2u] = static_cast<std::uint8_t>(background[2]);
        image.rgba[offset + 3u] = 255u;
    }
    if (!expectation.scenePresent) {
        outImage = std::move(image);
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }
    const glm::mat4 viewProjection = fixtureViewProjection(width, height);

    // Environment: floor and wall quads, then the character over them.
    {
        const std::vector<glm::vec3> floorPoints = authoredFloorPoints();
        std::array<glm::vec2, 4> projected{};
        for (std::size_t index = 0u; index < floorPoints.size(); ++index) {
            projected[index] = fixtureProjectToPixels(
                viewProjection, floorPoints[index], width, height);
        }
        fillTriangle(image, projected[0], projected[1], projected[2],
                     {110u, 120u, 110u});
        fillTriangle(image, projected[0], projected[2], projected[3],
                     {110u, 120u, 110u});
    }
    {
        const std::vector<glm::vec3> wallPoints = authoredWallPoints(expectation.variant);
        std::array<glm::vec2, 4> projected{};
        for (std::size_t index = 0u; index < wallPoints.size(); ++index) {
            projected[index] = fixtureProjectToPixels(
                viewProjection, wallPoints[index], width, height);
        }
        fillTriangle(image, projected[0], projected[1], projected[2],
                     {70u, 80u, 95u});
        fillTriangle(image, projected[0], projected[2], projected[3],
                     {70u, 80u, 95u});
    }
    {
        // Authored albedo inspection surface, with the optional oracle defect
        // used by the adversarial harness.
        const AuthoredProbe& probe = authoredProbe();
        const std::array<float, 4> bounds = projectPointBounds(
            authoredProbePoints(), viewProjection, width, height);
        const int x0 = static_cast<int>(std::floor(bounds[0]));
        const int x1 = static_cast<int>(std::ceil(bounds[2]));
        const int y0 = static_cast<int>(std::floor(bounds[1]));
        const int y1 = static_cast<int>(std::ceil(bounds[3]));
        const int middle = x0 + (x1 - x0) / 2;
        const std::array<std::uint8_t, 3> left =
            probeDefectPixel(probe.map.left, defect);
        const std::array<std::uint8_t, 3> right =
            probeDefectPixel(probe.map.right, defect);
        fillRect(image, x0, y0, middle, y1, left);
        fillRect(image, middle, y0, x1, y1, right);
    }
    fillProjectedBox(image, authoredTorsoPoints(expectation.variant),
                     viewProjection, {90u, 120u, 160u});
    const std::array<std::uint8_t, 3> headColor =
        expectation.emissionOff ? std::array<std::uint8_t, 3>{60u, 66u, 80u}
                                : std::array<std::uint8_t, 3>{200u, 180u, 110u};
    fillProjectedBox(image, authoredHeadPoints(expectation.variant),
                     viewProjection, headColor);
    fillProjectedBox(image, authoredLeftArmPoints(expectation.variant),
                     viewProjection, {80u, 105u, 145u});
    {
        const std::vector<glm::vec3> limb =
            authoredLimbPoints(expectation.variant, expectation.poseBlend);
        std::vector<glm::vec2> projected;
        projected.reserve(limb.size());
        for (std::size_t index = 0u; index < limb.size(); ++index) {
            projected.push_back(fixtureProjectToPixels(
                viewProjection, limb[index], width, height));
        }
        const std::vector<std::uint16_t>& indices = authoredLimbIndices();
        for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u) {
            fillTriangle(
                image,
                projected[static_cast<std::size_t>(indices[index + 0u])],
                projected[static_cast<std::size_t>(indices[index + 1u])],
                projected[static_cast<std::size_t>(indices[index + 2u])],
                {110u, 140u, 190u});
        }
    }

    outImage = std::move(image);
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

bool writeSyntheticHandshake(const std::string& path,
                             FixtureCase fixtureCase,
                             const std::string& host,
                             const std::string& api,
                             int width,
                             int height,
                             std::string* outError) {
    const CaseExpectation& expectation = caseExpectation(fixtureCase);
    nlohmann::json document;
    document["schema"] = "phlosion-scene-fixture-handshake-v1";
    document["sample_identity"] =
        std::string("phlosion-scene-fixture-v1/") + fixtureCaseName(fixtureCase);
    document["host"] = host;
    document["case"] = fixtureCaseName(fixtureCase);
    document["variant"] = sceneVariantName(expectation.variant);
    document["requested_backend"] = api;
    document["actual_backend"] = api;
    document["gpu"] = "synthetic";
    document["gpu_discrete"] = false;
    document["logical_frame"] = fixtureCaseLogicalFrame(fixtureCase);
    document["render_frame"] = 6;
    document["transition_frame"] = fixtureTransitionFrame();
    document["surface"] = {{"width", width}, {"height", height}};
    document["pose"] = {{"bind", expectation.bindPose},
                        {"bind_before_transition",
                         fixtureCase == FixtureCase::APose
                             ? true
                             : expectation.bindPose},
                        {"transition_applied",
                         fixtureCase == FixtureCase::APose},
                        {"time_sec", (expectation.bindPose || expectation.unloaded) ? 0.0
                                                          : static_cast<double>(
                                                                fixturePoseTimeSec())},
                        {"time_before_transition",
                         fixtureCase == FixtureCase::APose
                             ? 0.0
                             : (expectation.bindPose ? 0.0
                                                     : static_cast<double>(
                                                           fixturePoseTimeSec()))},
                        {"identity_before_transition",
                         expectation.scenePresent
                             ? std::string("synthetic-") +
                                   sceneVariantName(expectation.variant)
                             : std::string{}},
                        {"generation_before_transition",
                         expectation.scenePresent ? 1 : 0},
                        {"emission_off", expectation.emissionOff}};
    document["scene"] = {{"drawn", expectation.sceneDrawn},
                         {"unloaded", expectation.unloaded},
                         {"identity",
                          expectation.scenePresent
                              ? std::string("synthetic-") +
                                    sceneVariantName(expectation.variant)
                              : std::string{}},
                         {"generation", expectation.scenePresent ? 1 : 0},
                         {"source", "synthetic"},
                         {"texture_count", expectation.scenePresent ? 5 : 0},
                         {"primitive_count", expectation.scenePresent ? 7 : 0},
                         {"retained_texture_bytes", 0},
                         {"load_seconds", 0.0},
                         {"pose_seconds", 0.0}};
    document["replacement"] = {
        {"attempted", expectation.replacementAttempted},
        {"failed", expectation.replacementFailed},
        {"succeeded", expectation.replacementSucceeded},
        {"diagnostic", expectation.replacementFailed ? "synthetic rejection"
                                                     : std::string{}}};
    document["stats"] = {{"draw_calls", expectation.scenePresent ? 8 : 1},
                         {"triangles", expectation.scenePresent ? 60 : 2},
                         {"palette_matrices",
                          expectation.scenePresent ? 2 : 0},
                         {"probe_draw_calls",
                          expectation.scenePresent ? 1 : 0}};
    const AuthoredProbe& probe = authoredProbe();
    document["probe"] = {
        {"present", expectation.scenePresent},
        {"identity", expectation.scenePresent
                         ? std::string("synthetic-probe")
                         : std::string{}},
        {"generation", expectation.scenePresent ? 1 : 0},
        {"debug_view", 2},
        {"debug_state_reset", expectation.scenePresent},
        {"base_color_factor", probe.baseColorFactor},
        {"expected_left_rgb", expectedProbePixel(probe.map.left)},
        {"expected_right_rgb", expectedProbePixel(probe.map.right)}};
    nlohmann::json regions = nlohmann::json::object();
    for (const auto& entry : analyticRegions(expectation.variant,
                                             expectation.poseBlend, width,
                                             height)) {
        regions[entry.first] = entry.second;
    }
    document["regions"] = std::move(regions);

    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            if (outError != nullptr) {
                *outError = "could not open the synthetic handshake path: " + path;
            }
            return false;
        }
        output << document.dump(2) << '\n';
        if (!output) {
            if (outError != nullptr) {
                *outError = "could not write the synthetic handshake: " + path;
            }
            return false;
        }
    } catch (const std::exception& exception) {
        if (outError != nullptr) {
            *outError = std::string("synthetic handshake write failed: ") +
                        exception.what();
        }
        return false;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

}  // namespace engine::scene_fixture::verification
