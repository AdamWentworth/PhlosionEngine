// tests/TestMaterialFixture.cpp
//
// CPU contract for the public runtime/editor material fixture. It authors the
// shared sample inputs through the sample's own PNG writer, loads them through
// the production glTF loader boundary and the five existing CPU decode helpers,
// and exercises the verification guard's authored expectations, hermetic
// synthetic captures and handshake validator. It never creates a graphics
// context.

#include "MaterialFixtureAssets.h"
#include "MaterialFixtureCase.h"
#include "MaterialFixtureGuard.h"
#include "MaterialFixturePresentation.h"

#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfTextures.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

using engine::material_fixture::FixtureCase;
using engine::material_fixture::FixturePatch;
using engine::material_fixture::FixtureRole;
using engine::material_fixture::verification::PatchHalf;
using engine::material_fixture::verification::FixtureImage;
using engine::material_fixture::verification::ExpectedContentResult;
using engine::material_fixture::verification::HandshakeCheckResult;
using engine::material_fixture::verification::ImageCompareMetrics;
using engine::material_fixture::verification::SynthDefect;

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& prefix) {
        std::random_device device;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const std::uint64_t token =
                (static_cast<std::uint64_t>(device()) << 32u) ^ device();
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(token));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    bool ready() const { return !path_.empty(); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool expect(bool condition, std::string message, std::string& outFail) {
    if (!condition) {
        outFail = std::move(message);
        return false;
    }
    return true;
}

// Process-local environment setter for the diagnostic-scope contract. Tests may
// manipulate the environment; the sample must not depend on test utilities.
void setProcessEnv(const char* name, const char* value) {
#if defined(_MSC_VER)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value == nullptr) {
        ::unsetenv(name);
    } else {
        ::setenv(name, value, 1);
    }
#endif
}

bool expectPixels(const ::Model::CPUTexture& actual,
                  const std::vector<std::uint8_t>& expected,
                  std::size_t width,
                  std::size_t height,
                  const std::string& context,
                  std::string& outFail) {
    if (!expect(actual.rgba.size() == expected.size(),
                context + ": decoded byte count " +
                    std::to_string(actual.rgba.size()) + " != " +
                    std::to_string(expected.size()),
                outFail)) {
        return false;
    }
    if (!expect(actual.width == width && actual.height == height,
                context + ": decoded size does not match the authored map",
                outFail)) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual.rgba[index] != expected[index]) {
            return expect(false,
                          context + ": byte " + std::to_string(index) + " is " +
                              std::to_string(actual.rgba[index]) + " != " +
                              std::to_string(expected[index]),
                          outFail);
        }
    }
    return true;
}

std::vector<std::uint8_t> roleFallbackPixels(FixtureRole role) {
    switch (role) {
        case FixtureRole::Emissive:
            return {0u, 0u, 0u, 255u};
        case FixtureRole::Normal:
            return {128u, 128u, 255u, 255u};
        default:
            return {255u, 255u, 255u, 255u};
    }
}

// Writes the same handshake schema the hosts produce. A schema drift in the
// sample makes this contract fail instead of silently accepting the change.
bool writeHandshakeDocument(const std::filesystem::path& path,
                            FixtureCase fixtureCase,
                            const std::string& host,
                            const std::string& api,
                            int width,
                            int height,
                            std::string& outFail) {
    using namespace engine::material_fixture;
    const FixtureCaseInfo& info = fixtureCaseInfo(fixtureCase);
    const FixtureMaterialFactors factors = fixtureMaterialFactors(fixtureCase);
    const bool authored = fixtureCase == FixtureCase::AuthoredMaps;
    nlohmann::json document;
    document["schema"] = "phlosion-material-fixture-handshake-v1";
    document["sample_identity"] =
        std::string("phlosion-material-fixture-v1/") + info.name;
    document["host"] = host;
    document["case"] = info.name;
    document["requested_backend"] = api;
    document["actual_backend"] = api;
    document["gpu"] = "cpu-contract";
    document["gpu_discrete"] = false;
    document["surface"] = {{"width", width}, {"height", height}};
    document["logical_frame"] = info.logicalFrame;
    document["asset_directory"] =
        (std::filesystem::temp_directory_path() /
         ("phlosion-material-fixture-contract-" + std::string(info.name)))
            .string();
    document["asset_file_count"] = authored ? 6 : 2;
    document["material"] = {
        {"mode", 2},
        {"presence_flags", 0xF},
        {"alpha_mode", 0},
        {"metallic_factor", factors.metallicFactor},
        {"roughness_factor", factors.roughnessFactor},
        {"normal_scale", factors.normalScale},
        {"occlusion_strength", factors.occlusionStrength},
        {"emissive_factor", factors.emissiveFactor}};
    nlohmann::json roles = nlohmann::json::array();
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        const FixtureRole role = static_cast<FixtureRole>(index);
        const std::string roleName = fixtureRoleName(role);
        const auto contract =
            engine::material_fixture::verification::expectedRoleMetadata(
                fixtureCase, role);
        roles.push_back({
            {"role", roleName},
            {"origin", fixtureRoleOrigin(fixtureCase, role)},
            {"width", contract.width},
            {"height", contract.height},
            {"texture_srgb", contract.textureSrgb},
            {"content_hash", contract.contentHash},
            {"key", contract.key},
            {"cache_key", contract.key},
            {"wrap_s", contract.wrapS},
            {"wrap_t", contract.wrapT},
            {"min_filter", contract.minFilter},
            {"mag_filter", contract.magFilter},
            {"first_pixel", contract.firstPixel},
            {"pixel_bytes", contract.width * contract.height * 4}});
    }
    document["roles"] = std::move(roles);
    nlohmann::json patches = nlohmann::json::array();
    const std::map<std::string, FixturePatchRect> rects =
        computeFixturePatchRects(width, height);
    for (int index = 0; index < kFixturePatchCount; ++index) {
        const FixturePatch patch = static_cast<FixturePatch>(index);
        const std::string patchName = fixturePatchName(patch);
        patches.push_back({
            {"patch", patchName},
            {"debug_view", fixturePatchDebugView(patch)},
            {"geometry_key",
             std::string("fixture/mat/") + info.name + "/patch" +
                 std::to_string(index) + "/contract"},
            {"rect", rects.at(patchName)}});
    }
    document["patches"] = std::move(patches);
    document["presentation"] = {{"debug_state_reset", true}, {"draw_calls", 9}};
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return expect(false, "could not write the contract handshake", outFail);
    }
    output << document.dump(2) << '\n';
    return expect(output.good(), "the contract handshake did not write", outFail);
}

bool caseAuthoredRoleInputs(const std::filesystem::path& root, std::string& outFail) {
    using namespace engine::material_fixture;
    const FixtureCase cases[3] = {FixtureCase::AuthoredMaps,
                                  FixtureCase::EmptyImageBytes,
                                  FixtureCase::AbsentMaps};
    for (const FixtureCase fixtureCase : cases) {
        const std::string label = fixtureCaseName(fixtureCase);
        FixtureTextureSet textures;
        std::string error;
        if (!expect(decodeFixtureTextures(fixtureCase, textures, &error),
                    std::string("decodeFixtureTextures(") + label + ") failed: " +
                        error,
                    outFail)) {
            return false;
        }
        if (!expect(textures.roles.size() ==
                        static_cast<std::size_t>(kFixtureRoleCount),
                    std::string(label) + " does not carry five role textures",
                    outFail)) {
            return false;
        }
        if (!expect(textures.presenceFlags() == 0xFu,
                    std::string(label) +
                        " does not report all five role textures",
                    outFail)) {
            return false;
        }
        for (int index = 0; index < kFixtureRoleCount; ++index) {
            const FixtureRole role = static_cast<FixtureRole>(index);
            const FixtureRoleTexture& entry = textures.role(role);
            const std::vector<std::uint8_t> expected =
                fixtureCase == FixtureCase::AuthoredMaps
                ? authoredRolePixels(role)
                : roleFallbackPixels(role);
            const std::size_t width =
                fixtureCase == FixtureCase::AuthoredMaps ? 8u : 1u;
            const std::size_t height = width;
            if (!expectPixels(entry.cpu,
                              expected,
                              width,
                              height,
                              std::string(label) + " role " + fixtureRoleName(role),
                              outFail)) {
                return false;
            }
            if (!expect(entry.textureSrgb == authoredRoleMap(role).textureSrgb,
                        std::string(label) + " role " + fixtureRoleName(role) +
                            " color-space flag changed",
                        outFail)) {
                return false;
            }
            if (!expect(entry.contentHash != 0u &&
                            entry.key.find(fixtureRoleName(role)) !=
                                std::string::npos &&
                            entry.key.find(label) != std::string::npos &&
                            entry.cacheKey == entry.key,
                        std::string(label) + " role " + fixtureRoleName(role) +
                            " cache identity is not content dependent",
                        outFail)) {
                return false;
            }
        }
        // The scalar factors the sample submits come from the loaded glTF
        // through the production material resolver.
        const FixtureMaterialFactors authored = fixtureMaterialFactors(fixtureCase);
        if (!expect(std::abs(textures.factors.metallicFactor -
                             authored.metallicFactor) <= 1.0e-6f &&
                        std::abs(textures.factors.roughnessFactor -
                                 authored.roughnessFactor) <= 1.0e-6f &&
                        std::abs(textures.factors.normalScale -
                                 authored.normalScale) <= 1.0e-6f &&
                        std::abs(textures.factors.occlusionStrength -
                                 authored.occlusionStrength) <= 1.0e-6f,
                    std::string(label) +
                        " scalar factors did not resolve from the authored glTF",
                    outFail)) {
            return false;
        }
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            if (!expect(std::abs(textures.factors.emissiveFactor[channel] -
                                 authored.emissiveFactor[channel]) <= 1.0e-5f,
                        std::string(label) +
                            " emissive factor did not resolve from the authored glTF",
                        outFail)) {
                return false;
            }
        }
        // Role content hashes are distinct, so a swapped role binding would
        // change the submitted identity.
        for (int left = 0; left < kFixtureRoleCount; ++left) {
            for (int right = left + 1; right < kFixtureRoleCount; ++right) {
                if (!expect(
                        textures.role(static_cast<FixtureRole>(left)).contentHash !=
                            textures.role(static_cast<FixtureRole>(right))
                                .contentHash,
                        std::string(label) +
                            " role content hashes are not distinct",
                        outFail)) {
                    return false;
                }
            }
        }
        // The sample stages its own temporary directory; nothing is read from a
        // repository path at run time.
        if (!expect(textures.assetDirectory().string().find(
                        "phlosion-material-fixture") != std::string::npos,
                    std::string(label) +
                        " did not stage its inputs in a run-owned directory",
                    outFail)) {
            return false;
        }
        // The submitted metadata is the guard's bounded literal contract, which
        // re-derives the content hash from its own authored pixels.
        for (int index = 0; index < kFixtureRoleCount; ++index) {
            const FixtureRole role = static_cast<FixtureRole>(index);
            const FixtureRoleTexture& entry = textures.role(role);
            const auto contract =
                engine::material_fixture::verification::expectedRoleMetadata(
                    fixtureCase, role);
            if (!expect(static_cast<int>(entry.cpu.width) == contract.width &&
                            static_cast<int>(entry.cpu.height) == contract.height &&
                            entry.textureSrgb == contract.textureSrgb &&
                            entry.cpu.wrapS == contract.wrapS &&
                            entry.cpu.wrapT == contract.wrapT &&
                            entry.cpu.minF == contract.minFilter &&
                            entry.cpu.magF == contract.magFilter &&
                            entry.contentHash == contract.contentHash &&
                            entry.key == contract.key &&
                            entry.cacheKey == contract.key,
                        std::string(label) + " role " + fixtureRoleName(role) +
                            " metadata does not match the bounded contract",
                        outFail)) {
                return false;
            }
        }
    }
    (void)root;
    return true;
}

// The staged PNG payloads are genuine files written by the existing
// stb_image_write dependency; an independent decoder must read back exactly the
// authored pixels.
bool caseStagedPngPayloads(std::string& outFail) {
    using namespace engine::material_fixture;
    FixtureTextureSet textures;
    std::string error;
    if (!expect(decodeFixtureTextures(FixtureCase::AuthoredMaps, textures, &error),
                "could not stage the authored maps: " + error, outFail)) {
        return false;
    }
    const std::filesystem::path directory = textures.assetDirectory();
    if (!expect(!directory.empty() &&
                    std::filesystem::is_directory(directory),
                "the sample did not stage its inputs into an owned directory",
                outFail)) {
        return false;
    }
    for (int index = 0; index < kFixtureRoleCount; ++index) {
        const FixtureRole role = static_cast<FixtureRole>(index);
        const std::filesystem::path path =
            directory / authoredRoleMap(role).fileName;
        engine::material_fixture::verification::FixtureImage image;
        if (!expect(engine::material_fixture::verification::loadFixturePng(
                        path.string(), image, &error),
                    std::string("an independent decoder rejected the staged PNG "
                                "for role ") +
                        fixtureRoleName(role) + ": " + error,
                    outFail)) {
            return false;
        }
        const std::vector<std::uint8_t> expected = authoredRolePixels(role);
        if (!expect(image.width == 8 && image.height == 8 &&
                        image.rgba == expected,
                    std::string("the authored PNG for role ") +
                        fixtureRoleName(role) +
                        " does not decode to the authored pixels",
                    outFail)) {
            return false;
        }
    }
    return true;
}

bool casePatchLayoutAndExpectations(std::string& outFail) {
    using namespace engine::material_fixture;
    // The layout is ordered, non-overlapping and inside the viewport.
    const std::map<std::string, FixturePatchRect> rects =
        computeFixturePatchRects(640, 360);
    if (!expect(rects.size() == static_cast<std::size_t>(kFixturePatchCount),
                "the patch layout does not describe eight patches", outFail)) {
        return false;
    }
    float previousRight = -1.0f;
    for (int index = 0; index < kFixturePatchCount; ++index) {
        const std::string name = fixturePatchName(static_cast<FixturePatch>(index));
        const FixturePatchRect& rect = rects.at(name);
        if (!expect(rect[0] >= 0.0f && rect[1] >= 0.0f && rect[2] > 0.0f &&
                        rect[3] > 0.0f && rect[0] + rect[2] <= 1.0f &&
                        rect[1] + rect[3] <= 1.0f,
                    "patch '" + name + "' is outside the viewport", outFail)) {
            return false;
        }
        if (!expect(rect[0] > previousRight,
                    "patch '" + name + "' overlaps the previous patch",
                    outFail)) {
            return false;
        }
        previousRight = rect[0] + rect[2];
    }
    // Debug views follow the documented one-to-one mapping and the lit patch is
    // the ordinary path.
    const int expectedViews[kFixturePatchCount] = {1, 2, 3, 4, 5, 6, 7, 0};
    for (int index = 0; index < kFixturePatchCount; ++index) {
        if (!expect(fixturePatchDebugView(static_cast<FixturePatch>(index)) ==
                        expectedViews[index],
                    "patch " + std::to_string(index) +
                        " uses the wrong material debug view",
                    outFail)) {
            return false;
        }
    }
    // Geometry keys are unique per patch and depend on the case and content.
    std::map<std::string, int> keys;
    for (int index = 0; index < kFixturePatchCount; ++index) {
        const std::string key =
            fixturePatchGeometryKey(FixtureCase::AuthoredMaps,
                                    static_cast<std::size_t>(index),
                                    0x1234ABCDu);
        ++keys[key];
        if (!expect(key.find("authored-maps") != std::string::npos,
                    "a geometry key does not carry its case", outFail)) {
            return false;
        }
    }
    if (!expect(keys.size() == static_cast<std::size_t>(kFixturePatchCount),
                "geometry keys are not unique per patch", outFail)) {
        return false;
    }
    if (!expect(
            fixturePatchGeometryKey(FixtureCase::AuthoredMaps, 0u, 1u) !=
                fixturePatchGeometryKey(FixtureCase::AuthoredMaps, 0u, 2u),
            "geometry keys do not depend on decoded content", outFail)) {
        return false;
    }

    // The authored expectations describe the documented color treatment.
    const std::vector<engine::material_fixture::verification::PatchExpectation>
        authored = engine::material_fixture::verification::fixturePatchExpectations(
            FixtureCase::AuthoredMaps);
    for (const auto& expectation : authored) {
        if (expectation.patch == "lit") {
            continue;
        }
        if (!expect(expectation.present,
                    "the authored case has no expectation for patch '" +
                        expectation.patch + "'",
                    outFail)) {
            return false;
        }
        const bool srgbRole = expectation.patch == "base" ||
            expectation.patch == "albedo" || expectation.patch == "emissive";
        if (!expect(expectation.srgbView == srgbRole,
                    "patch '" + expectation.patch +
                        "' reports the wrong color-space treatment",
                    outFail)) {
            return false;
        }
        if (!expect(expectation.left != expectation.right,
                    "patch '" + expectation.patch +
                        "' has no authored spatial variation",
                    outFail)) {
            return false;
        }
    }
    // The metallic-roughness expectations invert between halves, which is the
    // structural swap detector.
    const auto findByPatch = [&authored](const std::string& name) {
        for (const auto& expectation : authored) {
            if (expectation.patch == name) {
                return expectation;
            }
        }
        return engine::material_fixture::verification::PatchExpectation{};
    };
    const auto roughness = findByPatch("roughness");
    const auto metallic = findByPatch("metallic");
    if (!expect(roughness.right[0] > roughness.left[0] &&
                    metallic.right[0] < metallic.left[0],
                "the authored metallic-roughness expectations do not invert "
                "between halves",
                outFail)) {
        return false;
    }
    // The corrected unavailable-bytes placeholders are visible in the
    // expectation, not black.
    const std::vector<engine::material_fixture::verification::PatchExpectation>
        empty = engine::material_fixture::verification::fixturePatchExpectations(
            FixtureCase::EmptyImageBytes);
    for (const auto& expectation : empty) {
        if (expectation.patch == "normal") {
            if (!expect(expectation.left[0] >= 120 && expectation.left[2] >= 200,
                        "the empty-bytes normal expectation is not the flat "
                        "placeholder",
                        outFail)) {
                return false;
            }
        }
        if (expectation.patch == "roughness" || expectation.patch == "metallic" ||
            expectation.patch == "occlusion") {
            if (!expect(expectation.left[0] >= 200,
                        "the empty-bytes " + expectation.patch +
                            " expectation is not the opaque-white placeholder",
                        outFail)) {
                return false;
            }
        }
    }
    return true;
}

bool caseSyntheticCaptureVerdicts(const std::filesystem::path& root, std::string& outFail) {
    using namespace engine::material_fixture;
    using engine::material_fixture::verification::validateFixtureExpectedContent;
    const FixtureCase cases[3] = {FixtureCase::AuthoredMaps,
                                  FixtureCase::EmptyImageBytes,
                                  FixtureCase::AbsentMaps};
    const SynthDefect defects[12] = {SynthDefect::None,
                                     SynthDefect::Blank,
                                     SynthDefect::MissingPatch,
                                     SynthDefect::SwappedMetallicRoughness,
                                     SynthDefect::NeutralizedNormal,
                                     SynthDefect::NeutralizedAo,
                                     SynthDefect::NeutralizedEmissive,
                                     SynthDefect::MissingLitRegion,
                                     SynthDefect::OldBlackFallback,
                                     SynthDefect::WrongColorSpace,
                                     SynthDefect::SameMeanCorruption,
                                     SynthDefect::FrozenLitAcrossCases};
    for (const FixtureCase fixtureCase : cases) {
        for (const SynthDefect defect : defects) {
            FixtureImage image;
            std::string error;
            if (!expect(engine::material_fixture::verification::
                            buildSyntheticFixtureCapture(fixtureCase,
                                                         defect,
                                                         320,
                                                         180,
                                                         image,
                                                         &error),
                        "synthetic capture failed: " + error, outFail)) {
                return false;
            }
            ExpectedContentResult result;
            if (!expect(validateFixtureExpectedContent(image, fixtureCase, result,
                                                       &error),
                        "synthetic validation failed: " + error, outFail)) {
                return false;
            }
            const bool authoredCase = fixtureCase == FixtureCase::AuthoredMaps;
            // The authored case distinguishes every modelled defect. On the two
            // fallback cases the corrected placeholders already agree with a
            // neutralized normal/AO/emissive map and with a swapped
            // roughness/metallic routing, so only the defects that change the
            // placeholder or the color treatment are distinguishable there.
            const bool distinguishable =
                defect != SynthDefect::None &&
                // The frozen lit region is only visible in the targeted
                // lit-region cross-case comparison, never in the per-entry
                // validator.
                defect != SynthDefect::FrozenLitAcrossCases &&
                (authoredCase ||
                 (defect != SynthDefect::NeutralizedNormal &&
                  defect != SynthDefect::NeutralizedAo &&
                  defect != SynthDefect::NeutralizedEmissive &&
                  defect != SynthDefect::SwappedMetallicRoughness));
            if (defect == SynthDefect::None) {
                if (!expect(result.ok,
                            std::string(fixtureCaseName(fixtureCase)) +
                                " pristine synthetic capture was rejected: " +
                                (result.failures.empty()
                                     ? std::string{}
                                     : result.failures.front()),
                            outFail)) {
                    return false;
                }
                continue;
            }
            if (distinguishable) {
                if (!expect(!result.ok,
                            std::string(fixtureCaseName(fixtureCase)) +
                                " synthetic defect " +
                                engine::material_fixture::verification::
                                    synthDefectName(defect) +
                                " was accepted by the expected-content validator",
                            outFail)) {
                    return false;
                }
            } else if (!expect(result.ok,
                               std::string(fixtureCaseName(fixtureCase)) +
                                   " synthetic defect " +
                                   engine::material_fixture::verification::
                                       synthDefectName(defect) +
                                   " is not distinguishable on this case but was "
                                   "rejected: " +
                                   (result.failures.empty() ? std::string{}
                                                            : result.failures.front()),
                               outFail)) {
                return false;
            }
        }
    }
    (void)root;
    return true;
}

// The composed lit region must itself differ between the authored case and each
// corrected fallback case, and a fallback capture that freezes the authored lit
// appearance must be rejected by that targeted comparison even though its
// diagnostic patches are correct.
bool caseLitRegionCrossCase(std::string& outFail) {
    using namespace engine::material_fixture;
    using engine::material_fixture::verification::compareLitRegions;
    using engine::material_fixture::verification::kLitCrossCaseChangedRatioMinimum;
    FixtureImage authored;
    std::string error;
    if (!expect(engine::material_fixture::verification::buildSyntheticFixtureCapture(
                    FixtureCase::AuthoredMaps,
                    SynthDefect::None,
                    320,
                    180,
                    authored,
                    &error),
                "could not build the authored synthetic capture: " + error,
                outFail)) {
        return false;
    }
    const FixtureCase fallbackCases[2] = {FixtureCase::EmptyImageBytes,
                                          FixtureCase::AbsentMaps};
    for (const FixtureCase fallback : fallbackCases) {
        FixtureImage pristine;
        if (!expect(engine::material_fixture::verification::
                        buildSyntheticFixtureCapture(fallback,
                                                     SynthDefect::None,
                                                     320,
                                                     180,
                                                     pristine,
                                                     &error),
                    "could not build the fallback synthetic capture: " + error,
                    outFail)) {
            return false;
        }
        ImageCompareMetrics metrics;
        if (!expect(compareLitRegions(authored, pristine, 8, metrics, &error),
                    "lit-region comparison failed: " + error,
                    outFail)) {
            return false;
        }
        if (!expect(metrics.changedPixelRatio >=
                        kLitCrossCaseChangedRatioMinimum,
                    std::string("the pristine ") + fixtureCaseName(fallback) +
                        " lit region is not distinguishable from the authored "
                        "one",
                    outFail)) {
            return false;
        }
        FixtureImage frozen;
        if (!expect(engine::material_fixture::verification::
                        buildSyntheticFixtureCapture(
                            fallback,
                            SynthDefect::FrozenLitAcrossCases,
                            320,
                            180,
                            frozen,
                            &error),
                    "could not build the frozen-lit capture: " + error,
                    outFail)) {
            return false;
        }
        ImageCompareMetrics frozenMetrics;
        if (!expect(compareLitRegions(authored, frozen, 8, frozenMetrics, &error),
                    "frozen lit-region comparison failed: " + error,
                    outFail)) {
            return false;
        }
        if (!expect(frozenMetrics.changedPixelRatio <
                        kLitCrossCaseChangedRatioMinimum,
                    std::string("a ") + fixtureCaseName(fallback) +
                        " capture with a frozen lit region would pass the "
                        "targeted cross-case check",
                    outFail)) {
            return false;
        }
    }
    return true;
}

// Staging ownership: distinct live owners, failure cleanup, a survivor that is
// not removed, and final cleanup by the last shared owner.
bool caseStagingOwnership(std::string& outFail) {
    using namespace engine::material_fixture;
    constexpr const char* kPrefix = "phlosion-material-fixture-contract";
    auto first = std::make_unique<FixtureStagingDirectory>(kPrefix);
    auto second = std::make_unique<FixtureStagingDirectory>(kPrefix);
    if (!expect(first->valid() && second->valid(),
                "could not reserve two staging directories", outFail)) {
        return false;
    }
    if (!expect(first->path() != second->path(),
                "two live staging owners share one directory", outFail)) {
        return false;
    }
    if (!expect(std::filesystem::is_directory(first->path()) &&
                    std::filesystem::is_directory(second->path()),
                "a reserved staging directory does not exist", outFail)) {
        return false;
    }
    if (!expect(first->contains(first->file("material_fixture.gltf")) &&
                    !first->contains(second->path()) &&
                    !first->contains(first->path() / ".." /
                                     "phlosion-material-fixture-elsewhere"),
                "staging containment is not component aware", outFail)) {
        return false;
    }
    // A failed staging run leaves nothing behind once its owner is released,
    // and the other owner's directory survives.
    const std::filesystem::path failedPath = second->path();
    std::error_code error;
    std::filesystem::create_directory(failedPath / "base_color.png", error);
    std::string stageError;
    if (!expect(!stageFixtureAssets(FixtureCase::AuthoredMaps,
                                    failedPath,
                                    &stageError),
                "staging into a directory that cannot hold the payloads was "
                "reported as successful",
                outFail)) {
        return false;
    }
    second.reset();
    if (!expect(!std::filesystem::exists(failedPath),
                "a released staging owner left its directory behind", outFail)) {
        return false;
    }
    if (!expect(std::filesystem::is_directory(first->path()),
                "releasing one staging owner removed another owner's directory",
                outFail)) {
        return false;
    }
    const std::filesystem::path survivor = first->path();
    first.reset();
    if (!expect(!std::filesystem::exists(survivor),
                "the last staging owner did not remove its directory", outFail)) {
        return false;
    }
    // A decoded texture set owns its staged inputs and shares them with copies;
    // the last owner performs the cleanup.
    FixtureTextureSet textures;
    std::string decodeError;
    if (!expect(decodeFixtureTextures(FixtureCase::AuthoredMaps,
                                      textures,
                                      &decodeError),
                "could not decode the authored case: " + decodeError, outFail)) {
        return false;
    }
    const std::filesystem::path staged = textures.assetDirectory();
    if (!expect(!staged.empty() && std::filesystem::is_directory(staged),
                "the decoded texture set does not own its staged inputs",
                outFail)) {
        return false;
    }
    {
        const FixtureTextureSet copy = textures;
        if (!expect(copy.assetDirectory() == staged &&
                        std::filesystem::is_directory(staged),
                    "copying the texture set moved or deleted the staged inputs",
                    outFail)) {
            return false;
        }
    }
    if (!expect(std::filesystem::is_directory(staged),
                "a released copy removed the staged inputs", outFail)) {
        return false;
    }
    textures.staging.reset();
    return expect(!std::filesystem::exists(staged),
                  "the last owner did not remove the staged inputs", outFail);
}

// The decoder's diagnostic environment is scoped off for the sample's decode and
// the caller's ambient values are restored exactly.
bool caseDecoderDiagnosticsScope(std::string& outFail) {
    using namespace engine::material_fixture;
    const char* names[3] = {"PHLOSION_GLTF_DEBUG_ALL",
                            "PHLOSION_GLTF_DEBUG_MATCH",
                            "PHLOSION_GLTF_DUMP_TEXTURES"};
    const char* ambient[3] = {"1", "material_fixture", "1"};
    std::array<std::optional<std::string>, 3> previous;
    for (std::size_t index = 0u; index < 3u; ++index) {
        previous[index] = engine::env::get(names[index]);
        setProcessEnv(names[index], ambient[index]);
    }
    FixtureTextureSet textures;
    std::string decodeError;
    const bool decoded =
        decodeFixtureTextures(FixtureCase::AuthoredMaps, textures, &decodeError);
    bool restored = true;
    for (std::size_t index = 0u; index < 3u; ++index) {
        const std::optional<std::string> current = engine::env::get(names[index]);
        restored = restored && current.has_value() &&
            *current == std::string(ambient[index]);
        if (previous[index].has_value()) {
            setProcessEnv(names[index], previous[index]->c_str());
        } else {
            setProcessEnv(names[index], nullptr);
        }
    }
    if (!expect(decoded, "the decode failed: " + decodeError, outFail)) {
        return false;
    }
    return expect(restored,
                  "the sample did not restore the caller's decoder diagnostic "
                  "environment",
                  outFail);
}

bool caseHandshakeValidator(const std::filesystem::path& root, std::string& outFail) {
    using namespace engine::material_fixture;
    using engine::material_fixture::verification::validateFixtureHandshake;
    const std::filesystem::path path = root / "contract-handshake.json";
    if (!writeHandshakeDocument(path,
                                FixtureCase::AuthoredMaps,
                                "runtime",
                                "vulkan",
                                640,
                                360,
                                outFail)) {
        return false;
    }
    HandshakeCheckResult result;
    std::string error;
    if (!expect(validateFixtureHandshake(path.string(),
                                         FixtureCase::AuthoredMaps,
                                         640,
                                         360,
                                         result,
                                         &error,
                                         "vulkan",
                                         "runtime"),
                "handshake validation failed: " + error, outFail)) {
        return false;
    }
    if (!expect(result.ok,
                "the contract handshake was rejected: " +
                    (result.failures.empty() ? std::string{}
                                             : result.failures.front()),
                outFail)) {
        return false;
    }
    // Negative: the wrong host, the wrong backend, a wrong role color space, a
    // wrong debug view and a non run-owned asset directory are all rejected.
    struct Mutation {
        const char* label;
        void (*apply)(nlohmann::json&);
    };
    const Mutation mutations[] = {
        {"host",
         [](nlohmann::json& document) { document["host"] = "editor"; }},
        {"backend",
         [](nlohmann::json& document) {
             document["actual_backend"] = "opengl";
         }},
        {"color space",
         [](nlohmann::json& document) {
             for (auto& role : document["roles"]) {
                 if (role["role"] == "normal") {
                     role["texture_srgb"] = 1;
                 }
             }
         }},
        {"debug view",
         [](nlohmann::json& document) {
             for (auto& patch : document["patches"]) {
                 if (patch["patch"] == "roughness") {
                     patch["debug_view"] = 5;
                 }
             }
         }},
        {"sample identity",
         [](nlohmann::json& document) {
             document["sample_identity"] = "phlosion-material-fixture-v1/absent-maps";
         }},
        {"temporary directory",
         [](nlohmann::json& document) {
             document["asset_directory"] =
                 "D:/Projects/Phlosion/PhlosionEngine/examples/material_fixture";
         }},
        {"role origin",
         [](nlohmann::json& document) {
             for (auto& role : document["roles"]) {
                 if (role["role"] == "baseColor") {
                     role["origin"] = "base-color-factor";
                 }
             }
         }},
        {"material factor",
         [](nlohmann::json& document) {
             document["material"]["normal_scale"] = 2.5;
         }},
        {"fractional frame",
         [](nlohmann::json& document) {
             document["logical_frame"] = 0.5;
         }},
        {"oversized surface",
         [](nlohmann::json& document) {
             document["surface"]["width"] = 100000000;
         }},
        {"unknown patch name",
         [](nlohmann::json& document) {
             document["patches"][0]["patch"] = "bogus";
         }},
        {"wrong factor type",
         [](nlohmann::json& document) {
             document["material"]["roughness_factor"] = "0.8";
         }},
        {"invented hash",
         [](nlohmann::json& document) {
             for (auto& role : document["roles"]) {
                 if (role["role"] == "normal") {
                     role["content_hash"] = 12345;
                 }
             }
         }},
        {"invented key",
         [](nlohmann::json& document) {
             for (auto& role : document["roles"]) {
                 if (role["role"] == "baseColor") {
                     role["key"] = "fixture/mat/authored-maps/baseColor/999";
                 }
             }
         }},
        {"wrong first pixel type",
         [](nlohmann::json& document) {
             document["roles"][0]["first_pixel"][0] = "200";
         }},
        {"malformed role element",
         [](nlohmann::json& document) {
             document["roles"][1] = 7;
         }},
        {"hash plus 2^32",
         [](nlohmann::json& document) {
             const std::int64_t expected = static_cast<std::int64_t>(
                 engine::material_fixture::verification::expectedRoleMetadata(
                     FixtureCase::AuthoredMaps, FixtureRole::Normal)
                     .contentHash);
             for (auto& role : document["roles"]) {
                 if (role["role"] == "normal") {
                     role["content_hash"] = expected + 4294967296LL;
                 }
             }
         }},
        {"hash uint64 max",
         [](nlohmann::json& document) {
             for (auto& role : document["roles"]) {
                 if (role["role"] == "normal") {
                     role["content_hash"] = std::numeric_limits<std::uint64_t>::max();
                 }
             }
         }},
    };
    for (const Mutation& mutation : mutations) {
        nlohmann::json document;
        {
            std::ifstream input(path, std::ios::binary);
            input >> document;
        }
        mutation.apply(document);
        const std::filesystem::path mutatedPath =
            root / (std::string("mutated-") + mutation.label + ".json");
        {
            std::ofstream output(mutatedPath, std::ios::trunc);
            output << document.dump(2) << '\n';
        }
        HandshakeCheckResult mutatedResult;
        std::string mutatedError;
        if (!expect(validateFixtureHandshake(mutatedPath.string(),
                                             FixtureCase::AuthoredMaps,
                                             640,
                                             360,
                                             mutatedResult,
                                             &mutatedError,
                                             "vulkan",
                                             "runtime"),
                    std::string("mutated handshake validation failed to run: ") +
                        mutatedError,
                    outFail)) {
            return false;
        }
        if (!expect(!mutatedResult.ok,
                    std::string("a wrong ") + mutation.label +
                        " in the handshake was accepted",
                    outFail)) {
            return false;
        }
    }
    return true;
}

bool caseImagePrimitives(const std::filesystem::path& root, std::string& outFail) {
    using namespace engine::material_fixture::verification;
    if (!expect(sha256Hex("abc") ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "the guard SHA-256 does not match the published vector", outFail)) {
        return false;
    }
    if (!expect(
            sha256Hex("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "the guard SHA-256 of an empty input is wrong", outFail)) {
        return false;
    }
    FixtureImage image;
    if (!buildSyntheticFixtureCapture(FixtureCase::AuthoredMaps,
                                      SynthDefect::None,
                                      320,
                                      180,
                                      image,
                                      &outFail)) {
        return false;
    }
    const std::filesystem::path png = root / "primitives.png";
    std::string error;
    if (!expect(saveFixturePng(png.string(), image, &error),
                "could not save a synthetic PNG: " + error, outFail)) {
        return false;
    }
    FixtureImage reloaded;
    if (!expect(loadFixturePng(png.string(), reloaded, &error),
                "could not reload a synthetic PNG: " + error, outFail)) {
        return false;
    }
    if (!expect(reloaded.width == image.width && reloaded.height == image.height,
                "the PNG round trip changed the image size", outFail)) {
        return false;
    }
    FixtureCrop crop;
    if (!expect(detectFixtureViewport(reloaded, crop, &error),
                "viewport detection failed on a synthetic capture: " + error,
                outFail)) {
        return false;
    }
    if (!expect(crop.x == 0 && crop.y == 0 && crop.width == reloaded.width &&
                    crop.height == reloaded.height,
                "a full-surface capture did not resolve to itself", outFail)) {
        return false;
    }
    ImageCompareMetrics metrics;
    if (!expect(compareFixtureImages(reloaded, image, 0, metrics, &error),
                "self comparison failed: " + error, outFail)) {
        return false;
    }
    if (!expect(metrics.normalizedMae == 0.0 && metrics.maxChannelDiff == 0,
                "an image does not compare equal to itself", outFail)) {
        return false;
    }
    const FixtureImage sheet = buildFixtureContactSheet({{"a", image}, {"b", image}},
                                                       2,
                                                       80,
                                                       45);
    return expect(sheet.width == 160 && sheet.height == 45,
                  "the contact sheet layout is wrong", outFail);
}

} // namespace

bool test_material_fixture_contract(std::string& outFail) {
    outFail.clear();
    const TemporaryDirectory root("phlosion-material-fixture-contract");
    if (!expect(root.ready(), "could not create the contract directory", outFail)) {
        return false;
    }
    if (!caseStagedPngPayloads(outFail)) {
        return false;
    }
    if (!caseAuthoredRoleInputs(root.path(), outFail)) {
        return false;
    }
    if (!casePatchLayoutAndExpectations(outFail)) {
        return false;
    }
    if (!caseSyntheticCaptureVerdicts(root.path(), outFail)) {
        return false;
    }
    if (!caseLitRegionCrossCase(outFail)) {
        return false;
    }
    if (!caseStagingOwnership(outFail)) {
        return false;
    }
    if (!caseDecoderDiagnosticsScope(outFail)) {
        return false;
    }
    if (!caseHandshakeValidator(root.path(), outFail)) {
        return false;
    }
    return caseImagePrimitives(root.path(), outFail);
}
