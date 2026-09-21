// Verification CLI for the material fixture: image comparison, authored
// expected-content checks, the native capture matrix and hermetic synthetic
// captures used by the CPU and adversarial tests.

#include "MaterialFixtureGuard.h"

#include "MaterialFixtureAssets.h"
#include "MaterialFixtureCase.h"
#include "MaterialFixturePresentation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using engine::material_fixture::FixtureCase;
using engine::material_fixture::authoredRoleMap;
using engine::material_fixture::FixtureMaterialFactors;
using engine::material_fixture::FixtureRole;
using engine::material_fixture::fixtureCaseInfo;
using engine::material_fixture::fixtureCaseCatalog;
using engine::material_fixture::fixtureCaseName;
using engine::material_fixture::fixtureMaterialFactors;
using engine::material_fixture::fixturePatchDebugView;
using engine::material_fixture::fixturePatchName;
using engine::material_fixture::fixtureRoleName;
using engine::material_fixture::fixtureRoleOrigin;
using engine::material_fixture::kFixturePatchCount;
using engine::material_fixture::kFixtureRoleCount;
using engine::material_fixture::parseFixtureCase;
using engine::material_fixture::verification::buildFixtureContactSheet;
using engine::material_fixture::verification::compareLitRegions;
using engine::material_fixture::verification::buildSyntheticFixtureCapture;
using engine::material_fixture::verification::compareFixtureImages;
using engine::material_fixture::verification::cropFixtureImage;
using engine::material_fixture::verification::detectFixtureViewport;
using engine::material_fixture::verification::ExpectedContentResult;
using engine::material_fixture::verification::FixtureCrop;
using engine::material_fixture::verification::FixtureImage;
using engine::material_fixture::verification::HandshakeCheckResult;
using engine::material_fixture::verification::ImageCompareMetrics;
using engine::material_fixture::verification::loadFixturePng;
using engine::material_fixture::verification::kLitCrossCaseChangedRatioMinimum;
using engine::material_fixture::verification::parseSynthDefect;
using engine::material_fixture::verification::saveFixturePng;
using engine::material_fixture::verification::sha256FileHex;
using engine::material_fixture::verification::SynthDefect;
using engine::material_fixture::verification::synthDefectName;
using engine::material_fixture::verification::validateFixtureExpectedContent;
using engine::material_fixture::verification::validateFixtureHandshake;

// Approved capture-contract parity limits. A manifest may request stricter
// values but never looser ones.
constexpr double kApprovedMae = 0.01;
constexpr double kApprovedRmse = 0.08;
constexpr double kApprovedChangedRatio = 0.05;
constexpr int kApprovedChannelTolerance = 8;
// Authored content differences between the material cases, in changed ratio.
constexpr double kAuthoredVsFallbackMinimum = 0.01;

struct Thresholds {
    double mae = kApprovedMae;
    double rmse = kApprovedRmse;
    double changedRatio = kApprovedChangedRatio;
    int channelTolerance = kApprovedChannelTolerance;
};

int fail(const std::string& message) {
    std::cerr << "[MaterialFixture][Guard] FAILED " << message << '\n';
    return 1;
}

std::optional<std::string> optionValue(const std::vector<std::string>& arguments,
                                       const std::string& prefix) {
    for (const std::string& argument : arguments) {
        if (argument.rfind(prefix, 0u) == 0u) {
            return argument.substr(prefix.size());
        }
    }
    return std::nullopt;
}

bool loadJson(const std::string& path,
              nlohmann::json& outDocument,
              std::string& outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outError = "could not open JSON file: " + path;
        return false;
    }
    try {
        input >> outDocument;
    } catch (const nlohmann::json::exception& exception) {
        outError = "JSON parse failed for " + path + ": " + exception.what();
        return false;
    }
    return true;
}

bool writeJson(const std::string& path,
               const nlohmann::json& document,
               std::string& outError) {
    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            outError = "could not open JSON output: " + path;
            return false;
        }
        output << document.dump(2) << '\n';
    } catch (const std::exception& exception) {
        outError = std::string("could not write JSON output: ") + exception.what();
        return false;
    }
    return true;
}

bool readTextFile(const std::string& path,
                  std::string& outText,
                  std::string& outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outError = "could not open log file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    outText = buffer.str();
    return true;
}

std::string normalizedPath(const std::string& value) {
    std::string out = value;
    std::replace(out.begin(), out.end(), '\\', '/');
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool isHex(const std::string& value, std::size_t minimum, std::size_t maximum) {
    if (value.size() < minimum || value.size() > maximum) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

// JSON integers must be real integers inside a bounded range, so a fractional or
// oversized value becomes a diagnostic instead of a truncating or overflowing
// cast.
bool jsonInt(const nlohmann::json& document,
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

// The same bounded-integer contract for one element of a JSON array. The value is
// read as int64 and range-checked before any narrowing cast.
bool jsonBoundedArrayElement(const nlohmann::json& array,
                             std::size_t index,
                             std::int64_t minimum,
                             std::int64_t maximum,
                             int& outValue) {
    if (!array.is_array() || index >= array.size()) {
        return false;
    }
    const nlohmann::json& element = array[index];
    if (!element.is_number_integer()) {
        return false;
    }
    std::int64_t value = 0;
    try {
        value = element.get<std::int64_t>();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (value < minimum || value > maximum) {
        return false;
    }
    outValue = static_cast<int>(value);
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

struct ScreenshotRecord {
    std::string backend;
    std::string status;
    std::string path;
    int width = 0;
    int height = 0;
    int frame = -1;
};

std::vector<ScreenshotRecord> parseScreenshotRecords(const std::string& text) {
    std::vector<ScreenshotRecord> records;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string prefix = "[Screenshot][";
        if (line.rfind(prefix, 0u) != 0u) {
            continue;
        }
        const std::size_t backendStart = prefix.size();
        const std::size_t backendEnd = line.find(']', backendStart);
        if (backendEnd == std::string::npos) {
            continue;
        }
        ScreenshotRecord record;
        record.backend = line.substr(backendStart, backendEnd - backendStart);
        std::size_t statusStart = backendEnd + 1u;
        while (statusStart < line.size() && line[statusStart] == ' ') {
            ++statusStart;
        }
        const std::size_t statusEnd = line.find(' ', statusStart);
        if (statusEnd == std::string::npos) {
            continue;
        }
        record.status = line.substr(statusStart, statusEnd - statusStart);
        std::string remainder = line.substr(statusEnd + 1u);
        const std::size_t sizePos = remainder.find("size=");
        const std::size_t framePos = remainder.find("frame=");
        if (sizePos == std::string::npos || framePos == std::string::npos) {
            continue;
        }
        record.path = remainder.substr(0u, sizePos);
        while (!record.path.empty() && record.path.back() == ' ') {
            record.path.pop_back();
        }
        const std::string sizeText =
            remainder.substr(sizePos + 5u, remainder.find(' ', sizePos + 5u) -
                                               (sizePos + 5u));
        const std::size_t cross = sizeText.find('x');
        if (cross == std::string::npos) {
            continue;
        }
        try {
            record.width = std::stoi(sizeText.substr(0u, cross));
            record.height = std::stoi(sizeText.substr(cross + 1u));
            const std::string frameText = remainder.substr(framePos + 6u);
            record.frame = std::stoi(frameText);
        } catch (const std::exception&) {
            continue;
        }
        records.push_back(std::move(record));
    }
    return records;
}

bool parseThresholds(const nlohmann::json& manifest,
                     Thresholds& outThresholds,
                     std::vector<std::string>& outFailures) {
    outThresholds = Thresholds{};
    const auto found = manifest.find("thresholds");
    if (found == manifest.end() || !found->is_object()) {
        outFailures.push_back("manifest has no thresholds object");
        return false;
    }
    const struct {
        const char* key;
        double limit;
        double* target;
    } limits[] = {
        {"mae", kApprovedMae, &outThresholds.mae},
        {"rmse", kApprovedRmse, &outThresholds.rmse},
        {"changed_ratio", kApprovedChangedRatio, &outThresholds.changedRatio},
    };
    bool ok = true;
    for (const auto& limit : limits) {
        const auto value = found->find(limit.key);
        if (value == found->end() || !value->is_number()) {
            outFailures.push_back(std::string("manifest threshold ") + limit.key +
                                  " is missing");
            ok = false;
            continue;
        }
        const double requested = value->get<double>();
        if (requested < 0.0 || requested > limit.limit) {
            outFailures.push_back(std::string("manifest threshold ") + limit.key +
                                  " is looser than the approved limit");
            ok = false;
        }
        *limit.target = requested;
    }
    const auto tolerance = found->find("channel_tolerance");
    if (tolerance == found->end() || !tolerance->is_number()) {
        outFailures.push_back("manifest channel tolerance is missing");
        ok = false;
    } else {
        const int requested = tolerance->get<int>();
        if (requested < 0 || requested > kApprovedChannelTolerance) {
            outFailures.push_back(
                "manifest channel tolerance is looser than the approved limit");
            ok = false;
        }
        outThresholds.channelTolerance = requested;
    }
    return ok;
}

void thresholdsPass(const ImageCompareMetrics& metrics,
                    const Thresholds& thresholds,
                    std::vector<std::string>& outFailures,
                    const std::string& label) {
    if (metrics.normalizedMae > thresholds.mae) {
        outFailures.push_back(label + " normalized MAE exceeds the limit");
    }
    if (metrics.normalizedRmse > thresholds.rmse) {
        outFailures.push_back(label + " normalized RMSE exceeds the limit");
    }
    if (metrics.changedPixelRatio > thresholds.changedRatio) {
        outFailures.push_back(label + " changed pixel ratio exceeds the limit");
    }
}

struct Entry {
    std::string host;
    std::string api;
    std::string caseName;
    std::string png;
    std::string handshake;
    std::string stdoutPath;
    std::string stderrPath;
    std::string metricsPath;
    int exitCode = -1;
    int requestedCaptureFrame = -1;
    int recordedObservedFrame = -1;
    std::string screenshotBackend;
    int observedCaptureFrame = -1;
    int pngWidth = 0;
    int pngHeight = 0;
    int recordedScreenshotWidth = 0;
    int recordedScreenshotHeight = 0;
    int observedCaptureWidth = 0;
    int observedCaptureHeight = 0;
    std::string observedCaptureBackend;
    // Canonical, run-bounded resolutions of the recorded artifact paths.
    std::filesystem::path resolvedPng;
    std::filesystem::path resolvedHandshake;
    std::filesystem::path resolvedStdout;
    std::filesystem::path resolvedStderr;
    std::filesystem::path resolvedMetrics;
    FixtureImage crop;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
};

void checkBinaryIdentities(const nlohmann::json& manifest,
                           std::vector<std::string>& outFailures) {
    const auto binaries = manifest.find("binaries");
    if (binaries == manifest.end() || !binaries->is_object()) {
        outFailures.push_back("manifest has no binaries object");
        return;
    }
    for (const char* name : {"runtime", "editor", "plugin", "guard"}) {
        const auto record = binaries->find(name);
        if (record == binaries->end() || !record->is_object()) {
            outFailures.push_back(std::string("manifest has no ") + name +
                                  " binary identity");
            continue;
        }
        std::string path;
        std::string recordedHash;
        double recordedBytes = -1.0;
        if (record->find("path") == record->end() ||
            !record->at("path").is_string() ||
            record->find("sha256") == record->end() ||
            !record->at("sha256").is_string() ||
            record->find("bytes") == record->end() ||
            !record->at("bytes").is_number()) {
            outFailures.push_back(std::string("manifest ") + name +
                                  " binary identity is incomplete");
            continue;
        }
        path = record->at("path").get<std::string>();
        recordedHash = record->at("sha256").get<std::string>();
        recordedBytes = record->at("bytes").get<double>();
        if (!std::filesystem::exists(path)) {
            outFailures.push_back(std::string("manifest ") + name +
                                  " binary is missing: " + path);
            continue;
        }
        std::string actualHash;
        std::string error;
        if (!sha256FileHex(path, actualHash, &error)) {
            outFailures.push_back(std::string("could not hash ") + name +
                                  " binary: " + error);
            continue;
        }
        if (actualHash != recordedHash) {
            outFailures.push_back(std::string("manifest ") + name +
                                  " binary hash is stale for the final binary");
        }
        const double actualBytes = static_cast<double>(
            std::filesystem::file_size(path));
        if (static_cast<std::uint64_t>(actualBytes) !=
            static_cast<std::uint64_t>(recordedBytes)) {
            outFailures.push_back(std::string("manifest ") + name +
                                  " binary size does not match");
        }
    }
}

int runDetect(const std::vector<std::string>& arguments) {
    const auto path = optionValue(arguments, "--png=");
    if (!path) {
        return fail("detect requires --png=<path>");
    }
    FixtureImage image;
    std::string error;
    if (!loadFixturePng(*path, image, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    std::cout << "[MaterialFixture][Guard] viewport x=" << crop.x
              << " y=" << crop.y << " width=" << crop.width
              << " height=" << crop.height << " source=" << image.width << "x"
              << image.height << '\n';
    return 0;
}

int runCompare(const std::vector<std::string>& arguments) {
    const auto leftPath = optionValue(arguments, "--left=");
    const auto rightPath = optionValue(arguments, "--right=");
    if (!leftPath || !rightPath) {
        return fail("compare requires --left=<png> and --right=<png>");
    }
    Thresholds thresholds;
    if (const auto tolerance = optionValue(arguments, "--channel-tolerance=")) {
        try {
            thresholds.channelTolerance = std::stoi(*tolerance);
        } catch (const std::exception&) {
            return fail("invalid --channel-tolerance");
        }
    }
    FixtureImage left;
    FixtureImage right;
    std::string error;
    if (!loadFixturePng(*leftPath, left, &error)) {
        return fail(error);
    }
    if (!loadFixturePng(*rightPath, right, &error)) {
        return fail(error);
    }
    FixtureCrop leftCrop;
    FixtureCrop rightCrop;
    if (!detectFixtureViewport(left, leftCrop, &error)) {
        return fail(error);
    }
    if (!detectFixtureViewport(right, rightCrop, &error)) {
        return fail(error);
    }
    const FixtureImage leftViewport = cropFixtureImage(left, leftCrop);
    const FixtureImage rightViewport = cropFixtureImage(right, rightCrop);
    ImageCompareMetrics metrics;
    if (!compareFixtureImages(leftViewport,
                              rightViewport,
                              thresholds.channelTolerance,
                              metrics,
                              &error)) {
        return fail(error);
    }
    std::vector<std::string> failures;
    thresholdsPass(metrics, thresholds, failures, "comparison");
    if (const auto out = optionValue(arguments, "--out=")) {
        nlohmann::json document;
        document["normalized_mae"] = metrics.normalizedMae;
        document["normalized_rmse"] = metrics.normalizedRmse;
        document["changed_pixel_ratio"] = metrics.changedPixelRatio;
        document["max_channel_diff"] = metrics.maxChannelDiff;
        document["failures"] = failures;
        std::string writeError;
        if (!writeJson(*out, document, writeError)) {
            return fail(writeError);
        }
    }
    std::cout << "[MaterialFixture][Guard] compare mae=" << metrics.normalizedMae
              << " rmse=" << metrics.normalizedRmse
              << " changed=" << metrics.changedPixelRatio
              << " maxDiff=" << metrics.maxChannelDiff << '\n';
    for (const std::string& failure : failures) {
        std::cerr << "[MaterialFixture][Guard] FAIL " << failure << '\n';
    }
    return failures.empty() ? 0 : 1;
}

int runExpect(const std::vector<std::string>& arguments) {
    const auto path = optionValue(arguments, "--png=");
    const auto caseText = optionValue(arguments, "--case=");
    if (!path || !caseText) {
        return fail("expect requires --png=<path> and --case=<name>");
    }
    const std::optional<FixtureCase> fixtureCase = parseFixtureCase(*caseText);
    if (!fixtureCase) {
        return fail("unknown material fixture case: " + *caseText);
    }
    FixtureImage image;
    std::string error;
    if (!loadFixturePng(*path, image, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    const FixtureImage viewport = cropFixtureImage(image, crop);
    ExpectedContentResult result;
    if (!validateFixtureExpectedContent(viewport, *fixtureCase, result, &error)) {
        return fail(error);
    }
    if (const auto out = optionValue(arguments, "--out=")) {
        nlohmann::json document;
        document["ok"] = result.ok;
        document["failures"] = result.failures;
        document["metrics"] = result.metrics;
        std::string writeError;
        if (!writeJson(*out, document, writeError)) {
            return fail(writeError);
        }
    }
    std::cout << "[MaterialFixture][Guard] expect case=" << *caseText
              << " ok=" << (result.ok ? "true" : "false")
              << " failures=" << result.failures.size() << '\n';
    for (const std::string& failure : result.failures) {
        std::cerr << "[MaterialFixture][Guard] FAIL " << failure << '\n';
    }
    return result.ok ? 0 : 1;
}

// A hermetic handshake with the same schema as the hosts'. It exists so the CPU
// and adversarial tests can exercise the validator and its mutations without a
// graphics device; it is never presented as native evidence.
bool writeSyntheticHandshake(const std::string& path,
                             FixtureCase fixtureCase,
                             const std::string& host,
                             const std::string& api,
                             int width,
                             int height,
                             std::string& outError) {
    const engine::material_fixture::FixtureCaseInfo& info =
        fixtureCaseInfo(fixtureCase);
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
    document["gpu"] = "synthetic";
    document["gpu_discrete"] = false;
    document["surface"] = {{"width", width}, {"height", height}};
    document["logical_frame"] = info.logicalFrame;
    std::error_code error;
    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path(error);
    document["asset_directory"] =
        (tempRoot / "phlosion-material-fixture-synth" / info.name).string();
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
        // The synthetic handshake carries the guard's own bounded literal role
        // contract, so it never invents hashes or first pixels.
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
    const std::map<std::string,
                   engine::material_fixture::FixturePatchRect> rects =
        engine::material_fixture::computeFixturePatchRects(width, height);
    for (int index = 0; index < kFixturePatchCount; ++index) {
        const engine::material_fixture::FixturePatch patch =
            static_cast<engine::material_fixture::FixturePatch>(index);
        const std::string patchName = fixturePatchName(patch);
        patches.push_back({
            {"patch", patchName},
            {"debug_view", fixturePatchDebugView(patch)},
            {"geometry_key",
             std::string("fixture/mat/") + info.name + "/patch" +
                 std::to_string(index) + "/synthetic"},
            {"rect", rects.at(patchName)}});
    }
    document["patches"] = std::move(patches);
    document["presentation"] = {{"debug_state_reset", true}, {"draw_calls", 9}};
    return writeJson(path, document, outError);
}

int runHandshake(const std::vector<std::string>& arguments) {
    const auto path = optionValue(arguments, "--path=");
    const auto caseText = optionValue(arguments, "--case=");
    const auto surfaceText = optionValue(arguments, "--surface=");
    if (!path || !caseText || !surfaceText) {
        return fail("handshake requires --path=<json> --case=<name> "
                    "--surface=WxH");
    }
    const std::optional<FixtureCase> fixtureCase = parseFixtureCase(*caseText);
    if (!fixtureCase) {
        return fail("unknown material fixture case: " + *caseText);
    }
    const std::size_t cross = surfaceText->find('x');
    if (cross == std::string::npos) {
        return fail("--surface must look like 640x360");
    }
    int width = 0;
    int height = 0;
    try {
        width = std::stoi(surfaceText->substr(0u, cross));
        height = std::stoi(surfaceText->substr(cross + 1u));
    } catch (const std::exception&) {
        return fail("--surface must look like 640x360");
    }
    HandshakeCheckResult result;
    std::string error;
    if (!validateFixtureHandshake(path->empty() ? std::string{} : *path,
                                  *fixtureCase,
                                  width,
                                  height,
                                  result,
                                  &error,
                                  optionValue(arguments, "--api=").value_or(
                                      std::string{}),
                                  optionValue(arguments, "--host=").value_or(
                                      std::string{}))) {
        return fail(error);
    }
    if (const auto out = optionValue(arguments, "--out=")) {
        nlohmann::json document;
        document["ok"] = result.ok;
        document["failures"] = result.failures;
        std::string writeError;
        if (!writeJson(*out, document, writeError)) {
            return fail(writeError);
        }
    }
    std::cout << "[MaterialFixture][Guard] handshake ok="
              << (result.ok ? "true" : "false")
              << " failures=" << result.failures.size() << '\n';
    for (const std::string& failure : result.failures) {
        std::cerr << "[MaterialFixture][Guard] FAIL " << failure << '\n';
    }
    return result.ok ? 0 : 1;
}

int runSynth(const std::vector<std::string>& arguments) {
    const auto path = optionValue(arguments, "--out-png=");
    const auto caseText = optionValue(arguments, "--case=");
    const auto defectText = optionValue(arguments, "--defect=");
    const auto surfaceText = optionValue(arguments, "--surface=");
    if (!path || !caseText || !defectText || !surfaceText) {
        return fail(
            "synth requires --out-png, --case, --defect and --surface=WxH");
    }
    const std::optional<FixtureCase> fixtureCase = parseFixtureCase(*caseText);
    if (!fixtureCase) {
        return fail("unknown material fixture case: " + *caseText);
    }
    SynthDefect defect = SynthDefect::None;
    if (!parseSynthDefect(*defectText, defect)) {
        return fail("unknown synth defect: " + *defectText);
    }
    const std::size_t cross = surfaceText->find('x');
    if (cross == std::string::npos) {
        return fail("--surface must look like 640x360");
    }
    int width = 0;
    int height = 0;
    try {
        width = std::stoi(surfaceText->substr(0u, cross));
        height = std::stoi(surfaceText->substr(cross + 1u));
    } catch (const std::exception&) {
        return fail("--surface must look like 640x360");
    }
    FixtureImage image;
    std::string error;
    if (!buildSyntheticFixtureCapture(*fixtureCase, defect, width, height, image,
                                      &error)) {
        return fail(error);
    }
    if (!saveFixturePng(*path, image, &error)) {
        return fail(error);
    }
    if (const auto handshakePath = optionValue(arguments, "--out-handshake=")) {
        const std::string host =
            optionValue(arguments, "--host=").value_or(std::string("runtime"));
        const std::string api =
            optionValue(arguments, "--api=").value_or(std::string("opengl"));
        std::string handshakeError;
        if (!writeSyntheticHandshake(*handshakePath,
                                     *fixtureCase,
                                     host,
                                     api,
                                     width,
                                     height,
                                     handshakeError)) {
            return fail(handshakeError);
        }
    }
    std::cout << "[MaterialFixture][Synth] wrote " << *path
              << " case=" << *caseText << " defect=" << synthDefectName(defect)
              << " size=" << width << "x" << height << '\n';
    return 0;
}

int runEcho(const std::vector<std::string>& arguments) {
    const auto text = optionValue(arguments, "--text=");
    const auto exitCode = optionValue(arguments, "--exit=");
    std::cout << "[MaterialFixture][Echo] " << text.value_or(std::string{})
              << '\n';
    if (exitCode) {
        try {
            return std::stoi(*exitCode);
        } catch (const std::exception&) {
            return fail("invalid --exit value");
        }
    }
    return 0;
}

int runMatrix(const std::vector<std::string>& arguments) {
    const auto manifestPath = optionValue(arguments, "--manifest=");
    if (!manifestPath) {
        return fail("matrix requires --manifest=<path>");
    }
    const bool requireComplete =
        std::find(arguments.begin(), arguments.end(), "--require-complete") !=
        arguments.end();
    nlohmann::json manifest;
    std::string error;
    if (!loadJson(*manifestPath, manifest, error)) {
        return fail(error);
    }
    std::vector<std::string> manifestFailures;
    Thresholds thresholds;
    parseThresholds(manifest, thresholds, manifestFailures);
    checkBinaryIdentities(manifest, manifestFailures);

    std::string revision;
    std::string baseline;
    if (manifest.find("source_revision") == manifest.end() ||
        !manifest.at("source_revision").is_string()) {
        manifestFailures.push_back("manifest has no source revision");
    } else {
        revision = manifest.at("source_revision").get<std::string>();
        if (!isHex(revision, 7u, 40u)) {
            manifestFailures.push_back("manifest source revision is not a hash");
        }
    }
    if (manifest.find("source_baseline") == manifest.end() ||
        !manifest.at("source_baseline").is_string()) {
        manifestFailures.push_back("manifest has no source baseline");
    } else {
        baseline = manifest.at("source_baseline").get<std::string>();
        if (!isHex(baseline, 7u, 40u)) {
            manifestFailures.push_back("manifest source baseline is not a hash");
        }
    }
    if (!revision.empty() && !baseline.empty() && revision != baseline) {
        manifestFailures.push_back(
            "manifest source revision does not match the source baseline");
    }

    std::error_code directoryError;
    const std::filesystem::path runDirectory = std::filesystem::weakly_canonical(
        std::filesystem::path(*manifestPath).parent_path(), directoryError);
    if (directoryError) {
        return fail("could not resolve the run directory: " +
                    directoryError.message());
    }
    const std::string manifestName =
        std::filesystem::path(*manifestPath).filename().string();
    // Both output paths are validated BEFORE anything is read from or written to
    // the run, so an outside --out or --sheet cannot be created as a side effect
    // of a run that then reports a failure.
    std::filesystem::path resolvedOut;
    std::filesystem::path resolvedSheet;
    for (const auto& output :
         {std::make_pair(std::string("--out"), optionValue(arguments, "--out=")),
          std::make_pair(std::string("--sheet"),
                         optionValue(arguments, "--sheet="))}) {
        if (!output.second) {
            continue;
        }
        std::string pathError;
        std::filesystem::path resolved;
        if (!engine::material_fixture::verification::resolveWithinRoot(
                *output.second, runDirectory, resolved, pathError)) {
            return fail(output.first + " is not a valid run path: " + pathError);
        }
        if (output.first == "--out") {
            resolvedOut = resolved;
        } else {
            resolvedSheet = resolved;
        }
    }

    const auto entriesFound = manifest.find("entries");
    if (entriesFound == manifest.end() || !entriesFound->is_array()) {
        manifestFailures.push_back("manifest has no entries array");
    }
    std::vector<Entry> entries;
    std::set<std::string> seen;
    std::set<std::string> hosts;
    std::set<std::string> apis;
    std::set<std::string> cases;
    const std::set<std::string> allowedHosts{"runtime", "editor"};
    const std::set<std::string> allowedApis{"opengl", "vulkan", "d3d12"};
    std::map<std::string, int> captureFrames;
    if (manifest.find("capture_frames") != manifest.end() &&
        manifest.at("capture_frames").is_object()) {
        for (const char* host : {"runtime", "editor"}) {
            const auto found = manifest.at("capture_frames").find(host);
            int frame = 0;
            if (found == manifest.at("capture_frames").end() ||
                !jsonInt(manifest.at("capture_frames"), host, 1, 100000,
                                frame)) {
                manifestFailures.push_back(
                    std::string("manifest has no valid capture frame for ") + host);
                continue;
            }
            captureFrames[host] = frame;
        }
    } else {
        manifestFailures.push_back("manifest has no capture_frames object");
    }
    int editorFrames = 0;
    if (!jsonInt(manifest, "editor_frames", 1, 100000, editorFrames)) {
        manifestFailures.push_back(
            "manifest editor_frames is missing or not a bounded integer");
    }

    if (entriesFound != manifest.end() && entriesFound->is_array()) {
        for (const nlohmann::json& record : *entriesFound) {
            Entry entry;
            const auto readString = [&record](const char* key,
                                              std::string& out) -> bool {
                const auto found = record.find(key);
                if (found == record.end() || !found->is_string()) {
                    return false;
                }
                out = found->get<std::string>();
                return true;
            };
            if (!readString("host", entry.host) ||
                !readString("api", entry.api) ||
                !readString("case", entry.caseName) ||
                !readString("png", entry.png) ||
                !readString("handshake", entry.handshake) ||
                !readString("stdout", entry.stdoutPath) ||
                !readString("stderr", entry.stderrPath)) {
                entry.failures.push_back("entry is missing a required field");
                entries.push_back(std::move(entry));
                continue;
            }
            if (record.find("metrics") != record.end() &&
                record.at("metrics").is_string()) {
                entry.metricsPath = record.at("metrics").get<std::string>();
            }
            jsonInt(record, "exit_code", -4096, 4096, entry.exitCode);
            jsonInt(record,
                    "requested_capture_frame",
                    1,
                    100000,
                    entry.requestedCaptureFrame);
            jsonInt(record,
                    "observed_capture_frame",
                    -1,
                    100000,
                    entry.recordedObservedFrame);
            entry.observedCaptureFrame = entry.recordedObservedFrame;
            if (record.find("screenshot_backend") != record.end() &&
                record.at("screenshot_backend").is_string()) {
                entry.screenshotBackend =
                    record.at("screenshot_backend").get<std::string>();
            }
            int recordedWidth = 0;
            int recordedHeight = 0;
            const auto sizeFound = record.find("screenshot_size");
            if (sizeFound != record.end() && sizeFound->is_array() &&
                sizeFound->size() == 2u) {
                // Both elements are read as int64 and range-checked before any
                // narrowing, so a value such as the correct width + 2^32 (or
                // UINT64_MAX) is rejected instead of truncating into range.
                const bool ok = jsonBoundedArrayElement(*sizeFound,
                                                        0u,
                                                        1,
                                                        16384,
                                                        recordedWidth) &&
                    jsonBoundedArrayElement(*sizeFound,
                                            1u,
                                            1,
                                            16384,
                                            recordedHeight);
                if (!ok) {
                    entry.failures.push_back(
                        "entry screenshot_size is not a bounded integer pair");
                    recordedWidth = 0;
                    recordedHeight = 0;
                }
            } else {
                entry.failures.push_back("entry has no screenshot_size pair");
            }
            entry.recordedScreenshotWidth = recordedWidth;
            entry.recordedScreenshotHeight = recordedHeight;

            const std::string tuple =
                entry.host + "/" + entry.api + "/" + entry.caseName;
            if (!seen.insert(tuple).second) {
                entry.failures.push_back("duplicate matrix tuple " + tuple);
            }
            hosts.insert(entry.host);
            apis.insert(entry.api);
            cases.insert(entry.caseName);
            if (allowedHosts.find(entry.host) == allowedHosts.end()) {
                entry.failures.push_back("entry host is unknown: " + entry.host);
            }
            if (allowedApis.find(entry.api) == allowedApis.end()) {
                entry.failures.push_back("entry API is unknown: " + entry.api);
            }
            if (!parseFixtureCase(entry.caseName)) {
                entry.failures.push_back("entry case is unknown: " + entry.caseName);
            }
            // Every recorded artifact path is resolved against the run
            // directory and must stay inside it after canonicalization before it
            // is read.
            bool pathsUsable = true;
            const struct {
                std::string* value;
                std::filesystem::path* resolved;
                const char* label;
            } entryPaths[] = {
                {&entry.png, &entry.resolvedPng, "png"},
                {&entry.handshake, &entry.resolvedHandshake, "handshake"},
                {&entry.stdoutPath, &entry.resolvedStdout, "stdout"},
                {&entry.stderrPath, &entry.resolvedStderr, "stderr"},
                {&entry.metricsPath, &entry.resolvedMetrics, "metrics"},
            };
            for (const auto& path : entryPaths) {
                if (path.value->empty()) {
                    continue;
                }
                std::string pathError;
                if (!engine::material_fixture::verification::resolveWithinRoot(
                        *path.value, runDirectory, *path.resolved, pathError)) {
                    entry.failures.push_back(
                        std::string("entry ") + path.label + " path is not a "
                        "valid run path: " + pathError);
                    pathsUsable = false;
                }
            }
            if (entry.host == "editor" && entry.metricsPath.empty()) {
                entry.failures.push_back(
                    "editor entry has no metrics path");
                pathsUsable = false;
            }
            if (entry.exitCode != 0) {
                entry.failures.push_back("entry exit code is " +
                                         std::to_string(entry.exitCode));
            }
            if (captureFrames.find(entry.host) != captureFrames.end() &&
                entry.requestedCaptureFrame != captureFrames[entry.host]) {
                entry.failures.push_back(
                    "entry requested capture frame disagrees with the manifest "
                    "capture frame");
            }

            std::string combined;
            std::string logError;
            bool haveLogs = true;
            for (const std::filesystem::path* path :
                 {&entry.resolvedStdout, &entry.resolvedStderr}) {
                if (path->empty()) {
                    haveLogs = false;
                    continue;
                }
                std::string text;
                if (!readTextFile(path->string(), text, logError)) {
                    haveLogs = false;
                    continue;
                }
                combined += text;
                combined += '\n';
            }
            if (!haveLogs) {
                entry.failures.push_back("entry log file could not be read");
            }
            const std::string display = backendDisplayName(entry.api);
            const std::vector<ScreenshotRecord> records =
                parseScreenshotRecords(combined);
            // Exactly one screenshot record may be written by the whole entry,
            // and it must be the requested backend, frame, path and size.
            int wroteRecords = 0;
            for (const ScreenshotRecord& screenshot : records) {
                if (screenshot.status == "FAILED") {
                    entry.failures.push_back("entry reported a screenshot failure");
                }
                if (screenshot.status != "WROTE") {
                    continue;
                }
                ++wroteRecords;
                if (screenshot.backend != display) {
                    entry.failures.push_back(
                        "entry screenshot backend is " + screenshot.backend +
                        " not " + display);
                }
                if (screenshot.frame != entry.requestedCaptureFrame) {
                    entry.failures.push_back(
                        "entry screenshot frame does not match the request");
                }
                if (normalizedPath(screenshot.path) !=
                    normalizedPath(entry.resolvedPng.string())) {
                    entry.failures.push_back(
                        "entry screenshot record path does not match the "
                        "recorded capture path");
                }
                entry.observedCaptureFrame = screenshot.frame;
                entry.observedCaptureWidth = screenshot.width;
                entry.observedCaptureHeight = screenshot.height;
                entry.observedCaptureBackend = screenshot.backend;
            }
            if (wroteRecords != 1) {
                entry.failures.push_back(
                    "entry did not produce exactly one screenshot WROTE record");
            }
            if (entry.recordedObservedFrame != entry.requestedCaptureFrame) {
                entry.failures.push_back(
                    "entry recorded observed capture frame does not match the "
                    "request");
            }
            if (combined.find("[ParityContract][" + display + "] PASS") ==
                std::string::npos) {
                entry.failures.push_back(
                    "entry did not report a passing parity contract for " + display);
            }
            if (combined.find("[ParityContract][" + display + "] FAIL") !=
                std::string::npos) {
                entry.failures.push_back(
                    "entry reported a failing parity contract for " + display);
            }
            if (combined.find("using OpenGL compatibility mode") !=
                std::string::npos) {
                entry.failures.push_back(
                    "entry reported a renderer fallback to OpenGL");
            }

            FixtureImage image;
            std::string imageError;
            if (entry.resolvedPng.empty() ||
                !std::filesystem::exists(entry.resolvedPng)) {
                entry.failures.push_back("entry produced no capture");
            } else if (!loadFixturePng(entry.resolvedPng.string(), image,
                                       &imageError)) {
                entry.failures.push_back(imageError);
            } else {
                entry.pngWidth = image.width;
                entry.pngHeight = image.height;
                if (entry.recordedScreenshotWidth != image.width ||
                    entry.recordedScreenshotHeight != image.height) {
                    entry.failures.push_back(
                        "entry recorded screenshot size does not match the PNG");
                }
                if (entry.observedCaptureWidth != image.width ||
                    entry.observedCaptureHeight != image.height) {
                    entry.failures.push_back(
                        "entry screenshot log dimensions do not match the PNG");
                }
                if (!entry.observedCaptureBackend.empty() &&
                    entry.screenshotBackend != entry.observedCaptureBackend) {
                    entry.failures.push_back(
                        "entry recorded screenshot backend does not match the "
                        "log");
                }
                FixtureCrop crop;
                if (!detectFixtureViewport(image, crop, &imageError)) {
                    entry.failures.push_back(imageError);
                } else {
                    entry.crop = cropFixtureImage(image, crop);
                }
            }

            if (entry.crop.empty()) {
                entry.failures.push_back("entry capture has no fixture viewport");
            }
            if (entry.exitCode == 0 &&
                entry.observedCaptureFrame != entry.requestedCaptureFrame) {
                entry.failures.push_back(
                    "entry observed capture frame does not match the request");
            }

            const std::optional<FixtureCase> fixtureCase =
                parseFixtureCase(entry.caseName);
            if (fixtureCase && !entry.resolvedHandshake.empty()) {
                HandshakeCheckResult handshake;
                std::string handshakeError;
                const int surfaceWidth =
                    entry.crop.width > 0 ? entry.crop.width : entry.pngWidth;
                const int surfaceHeight =
                    entry.crop.height > 0 ? entry.crop.height : entry.pngHeight;
                if (!validateFixtureHandshake(entry.resolvedHandshake.string(),
                                              *fixtureCase,
                                              surfaceWidth,
                                              surfaceHeight,
                                              handshake,
                                              &handshakeError,
                                              entry.api,
                                              entry.host)) {
                    entry.failures.push_back(handshakeError);
                } else {
                    for (const std::string& failure : handshake.failures) {
                        entry.failures.push_back("handshake: " + failure);
                    }
                }
                if (!entry.crop.empty()) {
                    ExpectedContentResult content;
                    if (!validateFixtureExpectedContent(entry.crop,
                                                        *fixtureCase,
                                                        content,
                                                        &handshakeError)) {
                        entry.failures.push_back(handshakeError);
                    } else {
                        for (const std::string& failure : content.failures) {
                            entry.failures.push_back("content: " + failure);
                        }
                        for (const auto& metric : content.metrics) {
                            entry.metrics[metric.first] = metric.second;
                        }
                    }
                }
            }

            if (entry.host == "editor") {
                // The plugin can only be considered activated when its metrics
                // file exists, names the requested renderer and reports at least
                // the requested frame count. A missing metrics field or file is a
                // failure, never a skipped check.
                if (entry.resolvedMetrics.empty()) {
                    entry.failures.push_back(
                        "editor entry has no usable metrics path");
                } else if (!std::filesystem::exists(entry.resolvedMetrics)) {
                    entry.failures.push_back("editor entry produced no metrics");
                } else {
                    nlohmann::json metrics;
                    std::string metricsError;
                    if (!loadJson(entry.resolvedMetrics.string(), metrics,
                                  metricsError)) {
                        entry.failures.push_back(metricsError);
                    } else {
                        const auto rendererFound = metrics.find("renderer");
                        if (rendererFound != metrics.end() &&
                            rendererFound->is_object() &&
                            rendererFound->find("backend") != rendererFound->end() &&
                            rendererFound->at("backend").is_string()) {
                            const std::string backend =
                                rendererFound->at("backend").get<std::string>();
                            if (backend != entry.api) {
                                entry.failures.push_back(
                                    "editor metrics backend is " + backend);
                            }
                        } else {
                            entry.failures.push_back(
                                "editor metrics have no renderer backend");
                        }
                        const auto captureFound = metrics.find("capture");
                        int frames = 0;
                        if (captureFound != metrics.end() &&
                            captureFound->is_object() &&
                            jsonInt(*captureFound, "frames", 1, 1000000, frames)) {
                            if (frames < editorFrames) {
                                entry.failures.push_back(
                                    "editor metrics frame count is below the "
                                    "requested frames");
                            }
                        } else {
                            entry.failures.push_back(
                                "editor metrics have no bounded capture frame "
                                "count");
                        }
                    }
                }
            }
            entries.push_back(std::move(entry));
        }
    }

    if (requireComplete) {
        const std::size_t expected = allowedHosts.size() * allowedApis.size() *
            fixtureCaseCatalog().size();
        if (seen.size() != expected) {
            manifestFailures.push_back(
                "matrix is missing tuples: found " + std::to_string(seen.size()) +
                " of " + std::to_string(expected));
        }
    }

    // Cross-API and cross-host comparisons for every case.
    nlohmann::json pairwise = nlohmann::json::array();
    for (const auto& left : entries) {
        if (left.crop.empty()) {
            continue;
        }
        for (const auto& right : entries) {
            if (right.crop.empty() || left.caseName != right.caseName) {
                continue;
            }
            // Cross-API parity pairs only. Runtime/editor parity is checked on
            // per-patch interior means because the two hosts render at
            // different viewport sizes.
            const bool sameHost = left.host == right.host &&
                left.api < right.api;
            if (!sameHost) {
                continue;
            }
            if (left.crop.width != right.crop.width ||
                left.crop.height != right.crop.height) {
                pairwise.push_back({
                    {"left", left.host + "/" + left.api + "/" + left.caseName},
                    {"right", right.host + "/" + right.api + "/" + right.caseName},
                    {"failures",
                     {"viewport dimensions do not match across the pair"}}});
                continue;
            }
            ImageCompareMetrics metrics;
            std::string compareError;
            std::vector<std::string> failures;
            if (!compareFixtureImages(left.crop,
                                      right.crop,
                                      thresholds.channelTolerance,
                                      metrics,
                                      &compareError)) {
                failures.push_back(compareError);
            } else {
                const std::string label = left.host + "/" + left.api + "/" +
                    left.caseName + " vs " + right.host + "/" + right.api;
                thresholdsPass(metrics, thresholds, failures, label);
            }
            pairwise.push_back({
                {"left", left.host + "/" + left.api + "/" + left.caseName},
                {"right", right.host + "/" + right.api + "/" + right.caseName},
                {"normalized_mae", metrics.normalizedMae},
                {"normalized_rmse", metrics.normalizedRmse},
                {"changed_pixel_ratio", metrics.changedPixelRatio},
                {"max_channel_diff", metrics.maxChannelDiff},
                {"failures", failures}});
        }
    }

    // Authored content must differ materially from both fallback cases, and the
    // two fallback cases must agree with each other because they present the
    // same corrected role inputs.
    nlohmann::json crossCase = nlohmann::json::array();
    for (const auto& left : entries) {
        if (left.crop.empty()) {
            continue;
        }
        for (const auto& right : entries) {
            if (right.crop.empty() || left.host != right.host ||
                left.api != right.api || left.caseName >= right.caseName) {
                continue;
            }
            if (left.crop.width != right.crop.width ||
                left.crop.height != right.crop.height) {
                crossCase.push_back({
                    {"left", left.caseName},
                    {"right", right.caseName},
                    {"failures", {"viewport dimensions do not match"}}});
                continue;
            }
            ImageCompareMetrics metrics;
            std::string compareError;
            std::vector<std::string> failures;
            if (!compareFixtureImages(left.crop,
                                      right.crop,
                                      thresholds.channelTolerance,
                                      metrics,
                                      &compareError)) {
                failures.push_back(compareError);
            } else {
                const bool authoredInvolved =
                    left.caseName == "authored-maps" ||
                    right.caseName == "authored-maps";
                const std::string label = left.host + "/" + left.api + " " +
                    left.caseName + " vs " + right.caseName;
                if (authoredInvolved) {
                    if (metrics.changedPixelRatio < kAuthoredVsFallbackMinimum) {
                        failures.push_back(label +
                                           " authored and fallback content do not "
                                           "differ");
                    }
                } else {
                    thresholdsPass(metrics, thresholds, failures, label);
                }
            }
            crossCase.push_back({
                {"left", left.caseName},
                {"right", right.caseName},
                {"host", left.host},
                {"api", left.api},
                {"normalized_mae", metrics.normalizedMae},
                {"normalized_rmse", metrics.normalizedRmse},
                {"changed_pixel_ratio", metrics.changedPixelRatio},
                {"max_channel_diff", metrics.maxChannelDiff},
                {"failures", failures}});
        }
    }

    // The composed lit region must itself differ between the authored case and
    // each corrected fallback case. A frozen lit region cannot hide behind correct
    // diagnostic tiles, because this comparison looks only at the lit patch.
    nlohmann::json crossLit = nlohmann::json::array();
    for (const auto& left : entries) {
        if (left.crop.empty()) {
            continue;
        }
        for (const auto& right : entries) {
            if (right.crop.empty() || left.host != right.host ||
                left.api != right.api || left.caseName >= right.caseName) {
                continue;
            }
            const bool authoredInvolved =
                left.caseName == "authored-maps" ||
                right.caseName == "authored-maps";
            if (!authoredInvolved) {
                continue;
            }
            std::vector<std::string> failures;
            ImageCompareMetrics metrics;
            std::string compareError;
            if (left.crop.width != right.crop.width ||
                left.crop.height != right.crop.height) {
                failures.push_back(
                    "the composed lit region cannot be compared at different "
                    "viewport sizes");
            } else if (!compareLitRegions(left.crop,
                                          right.crop,
                                          thresholds.channelTolerance,
                                          metrics,
                                          &compareError)) {
                failures.push_back(compareError);
            } else if (metrics.changedPixelRatio <
                       kLitCrossCaseChangedRatioMinimum) {
                failures.push_back(
                    left.host + "/" + left.api + ": the composed lit region is "
                    "frozen between " + left.caseName + " and " + right.caseName);
            }
            crossLit.push_back({
                {"host", left.host},
                {"api", left.api},
                {"left", left.caseName},
                {"right", right.caseName},
                {"normalized_mae", metrics.normalizedMae},
                {"normalized_rmse", metrics.normalizedRmse},
                {"changed_pixel_ratio", metrics.changedPixelRatio},
                {"max_channel_diff", metrics.maxChannelDiff},
                {"failures", failures}});
        }
    }

    // Runtime/editor parity is a whole-image comparison at matched viewport
    // sizes: the capture runner derives the editor viewport first and records the
    // standalone host at exactly those dimensions, so the same approved limits
    // (MAE 0.01, RMSE 0.08, changed ratio 0.05 at channel tolerance 8) gate the
    // nine host pairs as well.
    nlohmann::json crossHost = nlohmann::json::array();
    for (const char* api : {"opengl", "vulkan", "d3d12"}) {
        for (const auto& info : fixtureCaseCatalog()) {
            const Entry* runtimeEntry = nullptr;
            const Entry* editorEntry = nullptr;
            for (const Entry& entry : entries) {
                if (entry.api != api || entry.caseName != info.name) {
                    continue;
                }
                if (entry.host == "runtime") {
                    runtimeEntry = &entry;
                } else if (entry.host == "editor") {
                    editorEntry = &entry;
                }
            }
            std::vector<std::string> failures;
            if (runtimeEntry == nullptr || editorEntry == nullptr) {
                failures.push_back(
                    std::string("missing host pair for ") + api + " " + info.name);
            } else if (runtimeEntry->crop.empty() || editorEntry->crop.empty()) {
                failures.push_back(
                    std::string("host pair for ") + api + " " + info.name +
                    " has no measurable viewport");
            } else if (runtimeEntry->crop.width != editorEntry->crop.width ||
                       runtimeEntry->crop.height != editorEntry->crop.height) {
                failures.push_back(
                    std::string("host pair for ") + api + " " + info.name +
                    " rendered at different viewport sizes (" +
                    std::to_string(runtimeEntry->crop.width) + "x" +
                    std::to_string(runtimeEntry->crop.height) + " vs " +
                    std::to_string(editorEntry->crop.width) + "x" +
                    std::to_string(editorEntry->crop.height) + ")");
            } else {
                ImageCompareMetrics metrics;
                std::string compareError;
                if (!compareFixtureImages(runtimeEntry->crop,
                                          editorEntry->crop,
                                          thresholds.channelTolerance,
                                          metrics,
                                          &compareError)) {
                    failures.push_back(compareError);
                } else {
                    thresholdsPass(metrics,
                                   thresholds,
                                   failures,
                                   std::string("host pair ") + api + " " +
                                       info.name);
                }
                crossHost.push_back({{"api", api},
                                     {"case", info.name},
                                     {"normalized_mae", metrics.normalizedMae},
                                     {"normalized_rmse", metrics.normalizedRmse},
                                     {"changed_pixel_ratio",
                                      metrics.changedPixelRatio},
                                     {"max_channel_diff", metrics.maxChannelDiff},
                                     {"failures", failures}});
                continue;
            }
            crossHost.push_back({{"api", api},
                                 {"case", info.name},
                                 {"normalized_mae", 0.0},
                                 {"normalized_rmse", 0.0},
                                 {"changed_pixel_ratio", 0.0},
                                 {"max_channel_diff", 0},
                                 {"failures", failures}});
        }
    }
    std::size_t crossHostFailures = 0u;
    for (const auto& pair : crossHost) {
        if (!pair.at("failures").get<std::vector<std::string>>().empty()) {
            ++crossHostFailures;
        }
    }

    bool ok = manifestFailures.empty();
    for (const Entry& entry : entries) {
        if (!entry.failures.empty()) {
            ok = false;
        }
    }
    std::size_t pairwiseFailures = 0u;
    for (const auto& pair : pairwise) {
        if (!pair.at("failures").get<std::vector<std::string>>().empty()) {
            ++pairwiseFailures;
        }
    }
    std::size_t crossFailures = 0u;
    for (const auto& cross : crossCase) {
        if (!cross.at("failures").get<std::vector<std::string>>().empty()) {
            ++crossFailures;
        }
    }
    std::size_t crossLitFailures = 0u;
    for (const auto& pair : crossLit) {
        if (!pair.at("failures").get<std::vector<std::string>>().empty()) {
            ++crossLitFailures;
        }
    }
    if (pairwiseFailures > 0u || crossFailures > 0u || crossHostFailures > 0u ||
        crossLitFailures > 0u) {
        ok = false;
    }

    if (!resolvedOut.empty()) {
        nlohmann::json document;
        document["manifest"] = *manifestPath;
        document["manifest_name"] = manifestName;
        document["source_revision"] = revision;
        document["source_baseline"] = baseline;
        document["thresholds"] = {
            {"mae", thresholds.mae},
            {"rmse", thresholds.rmse},
            {"changed_ratio", thresholds.changedRatio},
            {"channel_tolerance", thresholds.channelTolerance}};
        document["manifest_failures"] = manifestFailures;
        nlohmann::json entryArray = nlohmann::json::array();
        for (const Entry& entry : entries) {
            nlohmann::json record;
            record["host"] = entry.host;
            record["api"] = entry.api;
            record["case"] = entry.caseName;
            record["exit_code"] = entry.exitCode;
            record["requested_capture_frame"] = entry.requestedCaptureFrame;
            record["observed_capture_frame"] = entry.observedCaptureFrame;
            record["png"] = entry.png;
            record["screenshot_size"] = {entry.pngWidth, entry.pngHeight};
            record["viewport_size"] = {entry.crop.width, entry.crop.height};
            record["metrics"] = entry.metrics;
            record["failures"] = entry.failures;
            entryArray.push_back(std::move(record));
        }
        document["entries"] = std::move(entryArray);
        document["pairwise"] = pairwise;
        document["cross_case"] = crossCase;
        document["cross_lit"] = crossLit;
        document["cross_lit_changed_ratio_minimum"] =
            kLitCrossCaseChangedRatioMinimum;
        document["cross_host"] = crossHost;
        document["ok"] = ok;
        std::string writeError;
        if (!writeJson(resolvedOut.string(), document, writeError)) {
            return fail(writeError);
        }
    }
    if (!resolvedSheet.empty()) {
        std::vector<std::pair<std::string, FixtureImage>> images;
        for (const Entry& entry : entries) {
            if (!entry.crop.empty()) {
                images.push_back({entry.host + "/" + entry.api + "/" + entry.caseName,
                                  entry.crop});
            }
        }
        if (!images.empty()) {
            const FixtureImage contactSheet =
                buildFixtureContactSheet(images, 3, images.front().second.width,
                                         images.front().second.height);
            std::string sheetError;
            if (!saveFixturePng(resolvedSheet.string(), contactSheet, &sheetError)) {
                std::cerr << "[MaterialFixture][Guard] FAIL " << sheetError << '\n';
                ok = false;
            }
        }
    }

    std::cout << "[MaterialFixture][Guard] matrix entries=" << entries.size()
              << " pairwise=" << pairwise.size()
              << " cross_case=" << crossCase.size()
              << " cross_lit=" << crossLit.size()
              << " cross_host=" << crossHost.size()
              << " ok=" << (ok ? "true" : "false") << '\n';
    for (const std::string& failure : manifestFailures) {
        std::cerr << "[MaterialFixture][Guard] FAIL " << failure << '\n';
    }
    for (const Entry& entry : entries) {
        for (const std::string& failure : entry.failures) {
            std::cerr << "[MaterialFixture][Guard] FAIL " << entry.host << "/"
                      << entry.api << "/" << entry.caseName << ": " << failure
                      << '\n';
        }
    }
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.push_back(argv[index] ? argv[index] : "");
    }
    if (arguments.empty()) {
        return fail("usage: MaterialFixtureGuard <detect|compare|expect|synth|"
                    "handshake|matrix|echo> [options]");
    }
    const std::string command = arguments.front();
    const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    try {
        if (command == "detect") {
            return runDetect(rest);
        }
        if (command == "compare") {
            return runCompare(rest);
        }
        if (command == "expect") {
            return runExpect(rest);
        }
        if (command == "handshake") {
            return runHandshake(rest);
        }
        if (command == "synth") {
            return runSynth(rest);
        }
        if (command == "matrix") {
            return runMatrix(rest);
        }
        if (command == "echo") {
            return runEcho(rest);
        }
        return fail("unknown material fixture guard command: " + command);
    } catch (const std::exception& exception) {
        // A malformed input document must produce a diagnostic verdict instead
        // of an escaping exception.
        return fail(std::string("guard command '") + command +
                    "' failed on malformed input: " + exception.what());
    }
}
