// Verification CLI for the public animation fixture: image comparison, authored
// expected-content checks, the native capture matrix and hermetic synthetic
// captures used by the CPU/adversarial tests.

#include "AnimationFixtureGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using engine::animation_fixture::FixtureCase;
using engine::animation_fixture::fixtureCaseCatalog;
using engine::animation_fixture::fixtureCaseName;
using engine::animation_fixture::parseFixtureCase;
using engine::animation_fixture::verification::authoredFixtureVertices;
using engine::animation_fixture::verification::buildFixtureContactSheet;
using engine::animation_fixture::verification::buildSyntheticFixtureCapture;
using engine::animation_fixture::verification::computeFixtureExpectation;
using engine::animation_fixture::verification::compareFixtureImages;
using engine::animation_fixture::verification::cropFixtureImage;
using engine::animation_fixture::verification::detectFixtureViewport;
using engine::animation_fixture::verification::ExpectedContentResult;
using engine::animation_fixture::verification::FixtureCrop;
using engine::animation_fixture::verification::FixtureImage;
using engine::animation_fixture::verification::HandshakeCheckResult;
using engine::animation_fixture::verification::ImageCompareMetrics;
using engine::animation_fixture::verification::loadFixturePng;
using engine::animation_fixture::verification::parseSynthDefect;
using engine::animation_fixture::verification::saveFixturePng;
using engine::animation_fixture::verification::sha256FileHex;
using engine::animation_fixture::verification::sha256Hex;
using engine::animation_fixture::verification::SynthDefect;
using engine::animation_fixture::verification::synthDefectName;
using engine::animation_fixture::verification::validateFixtureExpectedContent;
using engine::animation_fixture::verification::validateFixtureHandshake;

// Approved capture-contract parity limits. A manifest may request stricter
// values but never looser ones.
constexpr double kApprovedMae = 0.01;
constexpr double kApprovedRmse = 0.08;
constexpr double kApprovedChangedRatio = 0.05;
constexpr int kApprovedChannelTolerance = 8;
// Authored content differences between cases, in changed-pixel ratio.
constexpr double kBindVsPosedMinimum = 0.01;
constexpr double kIntermediateVsEndpointMinimum = 0.003;
constexpr double kEndpointVsStepMinimum = 0.005;

struct Thresholds {
    double mae = kApprovedMae;
    double rmse = kApprovedRmse;
    double changedRatio = kApprovedChangedRatio;
    int channelTolerance = kApprovedChannelTolerance;
};

int fail(const std::string& message) {
    std::cerr << "[AnimationFixture][Guard] FAILED " << message << '\n';
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
        if (record.status != "WROTE" && record.status != "FAILED") {
            continue;
        }
        const std::size_t sizePosition = line.rfind(" size=");
        const std::size_t framePosition = line.rfind(" frame=");
        if (sizePosition == std::string::npos ||
            framePosition == std::string::npos || framePosition < sizePosition) {
            continue;
        }
        std::string path =
            line.substr(statusEnd + 1u, sizePosition - (statusEnd + 1u));
        while (!path.empty() && path.back() == ' ') {
            path.pop_back();
        }
        record.path = std::move(path);
        const std::string size = line.substr(
            sizePosition + std::string(" size=").size(),
            framePosition - (sizePosition + std::string(" size=").size()));
        const std::size_t cross = size.find('x');
        if (cross == std::string::npos) {
            continue;
        }
        try {
            record.width = std::stoi(size.substr(0u, cross));
            record.height = std::stoi(size.substr(cross + 1u));
            record.frame = std::stoi(
                line.substr(framePosition + std::string(" frame=").size()));
        } catch (const std::exception&) {
            continue;
        }
        records.push_back(std::move(record));
    }
    return records;
}

bool parseThresholds(const nlohmann::json& manifest,
                     Thresholds& outThresholds,
                     std::string& outError) {
    if (!manifest.contains("thresholds") ||
        !manifest.at("thresholds").is_object()) {
        outError = "manifest is missing the thresholds object";
        return false;
    }
    const auto& values = manifest.at("thresholds");
    for (const char* key :
         {"mae", "rmse", "changed_ratio", "channel_tolerance"}) {
        if (!values.contains(key) || !values.at(key).is_number()) {
            outError = std::string("manifest threshold '") + key +
                "' is missing or not a number";
            return false;
        }
    }
    outThresholds.mae = values.at("mae").get<double>();
    outThresholds.rmse = values.at("rmse").get<double>();
    outThresholds.changedRatio = values.at("changed_ratio").get<double>();
    outThresholds.channelTolerance = values.at("channel_tolerance").get<int>();
    if (!std::isfinite(outThresholds.mae) ||
        !std::isfinite(outThresholds.rmse) ||
        !std::isfinite(outThresholds.changedRatio)) {
        outError = "manifest thresholds must be finite";
        return false;
    }
    if (outThresholds.mae <= 0.0 || outThresholds.rmse <= 0.0 ||
        outThresholds.changedRatio <= 0.0) {
        outError = "manifest thresholds must be positive";
        return false;
    }
    if (outThresholds.mae > kApprovedMae ||
        outThresholds.rmse > kApprovedRmse ||
        outThresholds.changedRatio > kApprovedChangedRatio) {
        outError =
            "manifest thresholds are looser than the approved capture contract";
        return false;
    }
    if (outThresholds.channelTolerance <= 0 ||
        outThresholds.channelTolerance > kApprovedChannelTolerance) {
        outError =
            "manifest channel tolerance is outside the approved capture contract";
        return false;
    }
    return true;
}

void thresholdsPass(const ImageCompareMetrics& metrics,
                    const Thresholds& thresholds,
                    std::vector<std::string>& outFailures) {
    if (metrics.normalizedMae > thresholds.mae) {
        outFailures.push_back(
            "normalized MAE " + std::to_string(metrics.normalizedMae) +
            " exceeds " + std::to_string(thresholds.mae));
    }
    if (metrics.normalizedRmse > thresholds.rmse) {
        outFailures.push_back(
            "normalized RMSE " + std::to_string(metrics.normalizedRmse) +
            " exceeds " + std::to_string(thresholds.rmse));
    }
    if (metrics.changedPixelRatio > thresholds.changedRatio) {
        outFailures.push_back(
            "changed pixel ratio " + std::to_string(metrics.changedPixelRatio) +
            " exceeds " + std::to_string(thresholds.changedRatio));
    }
}

struct Entry {
    std::string host;
    std::string api;
    std::string caseName;
    std::string pngPath;
    std::string handshakePath;
    std::string stdoutPath;
    std::string stderrPath;
    std::string metricsPath;
    int exitCode = -1;
    int requestedCaptureFrame = 0;
    int observedCaptureFrame = -1;
    FixtureCase fixtureCase = FixtureCase::Bind;
    FixtureImage full;
    FixtureImage viewport;
    FixtureCrop crop;
    std::vector<std::string> failures;
    std::map<std::string, double> expectedMetrics;
    std::map<std::string, double> expectedGeometry;
    bool ok = false;
};

// Validates one entry's process, capture, parity and editor-metrics evidence by
// re-reading the raw logs instead of trusting the recorded fields.
void checkEntryProvenance(Entry& entry, const nlohmann::json& manifest) {
    const std::string label =
        entry.host + "/" + entry.api + "/" + entry.caseName;
    if (entry.exitCode != 0) {
        entry.failures.push_back(
            label + " exit code is not zero: " + std::to_string(entry.exitCode));
    }
    std::string stdoutText;
    std::string stderrText;
    std::string error;
    if (!readTextFile(entry.stdoutPath, stdoutText, error)) {
        entry.failures.push_back(label + ": " + error);
        stdoutText.clear();
    }
    if (!readTextFile(entry.stderrPath, stderrText, error)) {
        entry.failures.push_back(label + ": " + error);
        stderrText.clear();
    }
    const std::string combined = stdoutText + "\n" + stderrText;
    const std::vector<ScreenshotRecord> records =
        parseScreenshotRecords(combined);
    const std::string display = backendDisplayName(entry.api);
    int wroteCount = 0;
    int failureCount = 0;
    bool matchedWrite = false;
    for (const ScreenshotRecord& record : records) {
        if (record.status == "FAILED") {
            ++failureCount;
            if (record.backend == display) {
                entry.failures.push_back(
                    label + " reported a backend capture failure");
            }
            continue;
        }
        ++wroteCount;
        if (record.backend != display) {
            entry.failures.push_back(
                label + " screenshot backend is " + record.backend +
                " but the manifest requests " + display);
            continue;
        }
        if (record.frame != entry.requestedCaptureFrame) {
            entry.failures.push_back(
                label + " observed capture frame " +
                std::to_string(record.frame) + " does not match the requested " +
                std::to_string(entry.requestedCaptureFrame));
        }
        entry.observedCaptureFrame = record.frame;
        if (normalizedPath(record.path) != normalizedPath(entry.pngPath)) {
            entry.failures.push_back(
                label + " screenshot path does not match the manifest entry");
        }
        if (record.width != entry.full.width ||
            record.height != entry.full.height) {
            entry.failures.push_back(
                label + " screenshot dimensions do not match the capture");
        }
        matchedWrite = true;
    }
    if (failureCount != 0) {
        entry.failures.push_back(
            label + " produced a screenshot FAILURE record");
    }
    if (wroteCount != 1 || !matchedWrite) {
        entry.failures.push_back(
            label + " did not produce exactly one matching screenshot WROTE record");
    }
    if (combined.find("[ParityContract][" + display + "] PASS") ==
        std::string::npos) {
        entry.failures.push_back(
            label + " did not report a passing parity contract for " + display);
    }
    if (combined.find("[ParityContract][" + display + "] FAIL") !=
        std::string::npos) {
        entry.failures.push_back(
            label + " reported a failing parity contract for " + display);
    }
    if (combined.find("using OpenGL compatibility mode") != std::string::npos) {
        entry.failures.push_back(
            label + " reported a renderer fallback to OpenGL");
    }

    if (entry.host == "editor") {
        nlohmann::json metrics;
        if (!loadJson(entry.metricsPath, metrics, error)) {
            entry.failures.push_back(label + ": " + error);
            return;
        }
        try {
            if (metrics.at("renderer").at("backend").get<std::string>() !=
                entry.api) {
                entry.failures.push_back(
                    label +
                    " editor metrics backend does not match the requested API");
            }
            const int frames = metrics.at("capture").at("frames").get<int>();
            const int expectedFrames = manifest.value("editor_frames", 0);
            if (frames < expectedFrames) {
                entry.failures.push_back(
                    label +
                    " editor metrics frame count is below the requested frames");
            }
        } catch (const nlohmann::json::exception& exception) {
            entry.failures.push_back(
                label + " editor metrics fields are invalid: " +
                exception.what());
        }
    }
}

void checkManifestProvenance(const nlohmann::json& manifest,
                             std::vector<std::string>& outFailures) {
    const auto expect = [&outFailures](bool condition, const std::string& message) {
        if (!condition) {
            outFailures.push_back(message);
        }
    };
    try {
        const std::string revision = manifest.value("source_revision", std::string{});
        expect(
            isHex(revision, 7u, 40u),
            "manifest is missing a hexadecimal source revision");
        const std::string baseline = manifest.value("source_baseline", std::string{});
        expect(
            isHex(baseline, 7u, 40u),
            "manifest is missing the recorded source baseline");
        expect(
            baseline == revision,
            "manifest source revision is not the recorded baseline revision");
        expect(
            manifest.contains("source_dirty") &&
                manifest.at("source_dirty").is_boolean(),
            "manifest is missing the source dirty state");
        expect(
            manifest.contains("configuration") &&
                manifest.at("configuration").is_string() &&
                !manifest.at("configuration").get<std::string>().empty(),
            "manifest is missing the build configuration");
        expect(
            manifest.contains("editor_frames") &&
                manifest.at("editor_frames").is_number_integer() &&
                manifest.at("editor_frames").get<int>() > 0,
            "manifest is missing the requested editor frame count");
        expect(
            manifest.contains("capture_frames") &&
                manifest.at("capture_frames").is_object(),
            "manifest is missing the requested capture frames");
        if (manifest.contains("capture_frames") &&
            manifest.at("capture_frames").is_object()) {
            for (const char* host : {"runtime", "editor"}) {
                expect(
                    manifest.at("capture_frames").contains(host) &&
                        manifest.at("capture_frames").at(host).is_number_integer() &&
                        manifest.at("capture_frames").at(host).get<int>() >= 3,
                    std::string("manifest capture frame for '") + host +
                        "' must be at least 3");
            }
        }
        // Binary identities are mandatory evidence: a manifest without the
        // binaries object (or without one of its entries) fails outright.
        expect(
            manifest.contains("binaries") &&
                manifest.at("binaries").is_object(),
            "manifest is missing the binaries object");
        for (const char* name : {"runtime", "editor", "plugin", "guard"}) {
            expect(
                manifest.contains("binaries") &&
                    manifest.at("binaries").is_object() &&
                    manifest.at("binaries").contains(name) &&
                    manifest.at("binaries").at(name).is_object(),
                std::string("manifest is missing the binary identity '") +
                    name + "'");
            if (!manifest.contains("binaries") ||
                !manifest.at("binaries").is_object() ||
                !manifest.at("binaries").contains(name) ||
                !manifest.at("binaries").at(name).is_object()) {
                continue;
            }
            const auto& identity = manifest.at("binaries").at(name);
            const std::string recordedHash =
                identity.value("sha256", std::string{});
            expect(
                isHex(recordedHash, 64u, 64u),
                std::string("manifest binary '") + name +
                    "' is missing a SHA-256 digest");
            const std::string path = identity.value("path", std::string{});
            expect(
                !path.empty(),
                std::string("manifest binary '") + name +
                    "' is missing its path");
            if (!path.empty()) {
                std::string actualHash;
                std::string error;
                if (!sha256FileHex(path, actualHash, &error)) {
                    outFailures.push_back(
                        std::string("manifest binary '") + name +
                        "' could not be re-hashed: " + error);
                } else if (actualHash != recordedHash) {
                    outFailures.push_back(
                        std::string("manifest binary '") + name +
                        "' hash does not match the file on disk");
                }
            }
            if (identity.contains("bytes") &&
                identity.at("bytes").is_number_unsigned() && !path.empty()) {
                std::error_code sizeError;
                const auto size = std::filesystem::file_size(path, sizeError);
                expect(
                    !sizeError &&
                        size == identity.at("bytes").get<std::uint64_t>(),
                    std::string("manifest binary '") + name +
                        "' no longer matches the recorded file size");
            }
        }
    } catch (const nlohmann::json::exception& exception) {
        outFailures.push_back(
            std::string("manifest provenance fields are invalid: ") +
            exception.what());
    }
}

int runDetect(const std::vector<std::string>& arguments) {
    const auto png = optionValue(arguments, "--png=");
    if (!png) {
        return fail("detect requires --png=");
    }
    FixtureImage image;
    std::string error;
    if (!loadFixturePng(*png, image, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    if (const auto out = optionValue(arguments, "--out=")) {
        nlohmann::json document{
            {"png", *png},
            {"image", {{"width", image.width}, {"height", image.height}}},
            {"viewport",
             {{"x", crop.x},
              {"y", crop.y},
              {"width", crop.width},
              {"height", crop.height}}}};
        if (!writeJson(*out, document, error)) {
            return fail(error);
        }
    }
    std::cout << "[AnimationFixture][Guard] detect=" << *png
              << " viewport=" << crop.width << "x" << crop.height << " at "
              << crop.x << "," << crop.y << '\n';
    return 0;
}

int runCompare(const std::vector<std::string>& arguments) {
    const auto left = optionValue(arguments, "--left=");
    const auto right = optionValue(arguments, "--right=");
    if (!left || !right) {
        return fail("compare requires --left= and --right=");
    }
    int tolerance = kApprovedChannelTolerance;
    if (const auto value = optionValue(arguments, "--tolerance=")) {
        try {
            tolerance = std::stoi(*value);
        } catch (const std::exception&) {
            return fail("compare tolerance must be an integer");
        }
    }
    FixtureImage leftImage;
    FixtureImage rightImage;
    std::string error;
    if (!loadFixturePng(*left, leftImage, &error) ||
        !loadFixturePng(*right, rightImage, &error)) {
        return fail(error);
    }
    FixtureCrop leftCrop;
    FixtureCrop rightCrop;
    if (!detectFixtureViewport(leftImage, leftCrop, &error) ||
        !detectFixtureViewport(rightImage, rightCrop, &error)) {
        return fail(error);
    }
    const FixtureImage leftViewport = cropFixtureImage(leftImage, leftCrop);
    const FixtureImage rightViewport = cropFixtureImage(rightImage, rightCrop);
    ImageCompareMetrics metrics;
    if (!compareFixtureImages(
            leftViewport, rightViewport, tolerance, metrics, &error)) {
        return fail(error);
    }
    const std::string text = nlohmann::json{
        {"left", *left},
        {"right", *right},
        {"normalized_mae", metrics.normalizedMae},
        {"normalized_rmse", metrics.normalizedRmse},
        {"changed_pixel_ratio", metrics.changedPixelRatio},
        {"max_channel_diff", metrics.maxChannelDiff}}
                                     .dump();
    if (const auto out = optionValue(arguments, "--out=")) {
        nlohmann::json document = nlohmann::json::parse(text);
        if (!writeJson(*out, document, error)) {
            return fail(error);
        }
    }
    std::cout << "[AnimationFixture][Guard] compare " << text << '\n';
    return 0;
}

int runExpect(const std::vector<std::string>& arguments) {
    const auto png = optionValue(arguments, "--png=");
    const auto caseName = optionValue(arguments, "--case=");
    if (!png || !caseName) {
        return fail("expect requires --png= and --case=");
    }
    const std::optional<FixtureCase> parsedCase = parseFixtureCase(*caseName);
    if (!parsedCase) {
        return fail("expect case is unknown: " + *caseName);
    }
    int width = 640;
    int height = 360;
    if (const auto surface = optionValue(arguments, "--surface=")) {
        const std::size_t cross = surface->find('x');
        if (cross == std::string::npos) {
            return fail("expect surface must be WIDTHxHEIGHT");
        }
        try {
            width = std::stoi(surface->substr(0u, cross));
            height = std::stoi(surface->substr(cross + 1u));
        } catch (const std::exception&) {
            return fail("expect surface must be WIDTHxHEIGHT");
        }
    }
    FixtureImage image;
    std::string error;
    if (!loadFixturePng(*png, image, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    const FixtureImage viewport = cropFixtureImage(image, crop);
    const auto expectation =
        computeFixtureExpectation(*parsedCase, viewport.width, viewport.height);
    ExpectedContentResult result;
    if (!validateFixtureExpectedContent(viewport, expectation, result, &error)) {
        return fail(error);
    }
    int failureCount = static_cast<int>(result.failures.size());
    std::vector<std::string> handshakeFailures;
    if (const auto handshake = optionValue(arguments, "--handshake=")) {
        HandshakeCheckResult handshakeResult;
        const std::string expectedApi =
            optionValue(arguments, "--api=").value_or(std::string{});
        const std::string expectedHost =
            optionValue(arguments, "--host=").value_or(std::string{});
        if (!validateFixtureHandshake(
                *handshake,
                *parsedCase,
                viewport.width,
                viewport.height,
                handshakeResult,
                &error,
                expectedApi,
                expectedHost)) {
            return fail(error);
        }
        handshakeFailures = handshakeResult.failures;
        failureCount += static_cast<int>(handshakeFailures.size());
    }
    nlohmann::json document{
        {"case", *caseName},
        {"png", *png},
        {"viewport",
         {{"x", crop.x},
          {"y", crop.y},
          {"width", crop.width},
          {"height", crop.height}}},
        {"metrics", result.metrics},
        {"failures", result.failures},
        {"handshake_failures", handshakeFailures},
        {"failure_count", failureCount},
        {"ok", failureCount == 0}};
    if (const auto out = optionValue(arguments, "--out=")) {
        if (!writeJson(*out, document, error)) {
            return fail(error);
        }
    }
    for (const std::string& message : result.failures) {
        std::cerr << "[AnimationFixture][Guard]   " << message << '\n';
    }
    for (const std::string& message : handshakeFailures) {
        std::cerr << "[AnimationFixture][Guard]   " << message << '\n';
    }
    if (failureCount != 0) {
        return fail(
            std::to_string(failureCount) + " expected-content check(s) failed for " +
            *caseName);
    }
    std::cout << "[AnimationFixture][Guard] expect=ok case=" << *caseName
              << " viewport=" << viewport.width << "x" << viewport.height << '\n';
    return 0;
}

int runSynth(const std::vector<std::string>& arguments) {
    const auto outPng = optionValue(arguments, "--out-png=");
    const auto caseName = optionValue(arguments, "--case=");
    if (!outPng || !caseName) {
        return fail("synth requires --out-png= and --case=");
    }
    const std::optional<FixtureCase> parsedCase = parseFixtureCase(*caseName);
    if (!parsedCase) {
        return fail("synth case is unknown: " + *caseName);
    }
    SynthDefect defect = SynthDefect::None;
    if (const auto name = optionValue(arguments, "--defect=")) {
        if (!parseSynthDefect(*name, defect)) {
            return fail("synth defect is unknown: " + *name);
        }
    }
    int width = 640;
    int height = 360;
    if (const auto surface = optionValue(arguments, "--surface=")) {
        const std::size_t cross = surface->find('x');
        if (cross == std::string::npos) {
            return fail("synth surface must be WIDTHxHEIGHT");
        }
        try {
            width = std::stoi(surface->substr(0u, cross));
            height = std::stoi(surface->substr(cross + 1u));
        } catch (const std::exception&) {
            return fail("synth surface must be WIDTHxHEIGHT");
        }
    }
    FixtureImage image;
    std::string error;
    if (!buildSyntheticFixtureCapture(
            *parsedCase, defect, width, height, image, &error)) {
        return fail(error);
    }
    if (!saveFixturePng(*outPng, image, &error)) {
        return fail(error);
    }

    if (const auto outHandshake = optionValue(arguments, "--out-handshake=")) {
        const auto expectation = computeFixtureExpectation(
            *parsedCase, width, height);
        const auto bindExpectation =
            computeFixtureExpectation(FixtureCase::Bind, width, height);
        nlohmann::json document;
        document["schema"] = "phlosion-animation-fixture-handshake-v1";
        document["host"] =
            optionValue(arguments, "--host=").value_or(std::string("runtime"));
        document["case"] = expectation.caseName;
        document["requested_backend"] =
            optionValue(arguments, "--api=").value_or(std::string("opengl"));
        document["actual_backend"] = document["requested_backend"];
        document["gpu"] = "synthetic";
        document["gpu_discrete"] = false;
        document["surface"] = {
            {"width", width},
            {"height", height}};
        document["animation"] = {
            {"clip", expectation.clipName},
            {"clip_index", expectation.clipIndex},
            {"sample_time_sec", expectation.sampleTimeSec},
            {"logical_frame", expectation.logicalFrame},
            {"expected_angle_degrees", expectation.angleDegrees}};
        const bool posed =
            std::abs(static_cast<double>(expectation.angleDegrees)) > 1.0;
        document["presentation"] = {
            {"bind_frames", 2},
            {"pose_frames", 4},
            {"geometry_key", "fixture/anim/strip"},
            {"gpu_skinning", true},
            {"joint_count", 2},
            {"skin_matrix_count", 2},
            {"palette_address", "0x10000"},
            {"bind_draw_palette_address", "0x10000"},
            {"pose_draw_palette_address", "0x10000"},
            {"palette_contents_changed", posed}};
        document["mesh_transform"] = expectation.meshTransform;
        document["palette"] = expectation.palette;
        document["bind_palette"] = bindExpectation.palette;
        document["node_globals"] = std::vector<float>(32u, 0.0f);
        document["joint_bind_origin"] =
            std::vector<std::array<float, 3>>{{-0.5f, 0.0f, 0.0f},
                                              {3.0f, 0.0f, 0.0f}};
        nlohmann::json vertices = nlohmann::json::array();
        for (const auto& vertex : authoredFixtureVertices()) {
            vertices.push_back({
                {"position",
                 {vertex.x, vertex.y, 0.0}},
                {"joints",
                 {vertex.joints[0],
                  vertex.joints[1],
                  vertex.joints[2],
                  vertex.joints[3]}},
                {"weights",
                 {vertex.weights[0],
                  vertex.weights[1],
                  vertex.weights[2],
                  vertex.weights[3]}}});
        }
        document["vertices"] = std::move(vertices);
        if (!writeJson(*outHandshake, document, error)) {
            return fail(error);
        }
    }
    std::cout << "[AnimationFixture][Guard] synth=" << *outPng
              << " case=" << *caseName
              << " defect=" << synthDefectName(defect) << '\n';
    return 0;
}

int runMatrix(const std::vector<std::string>& arguments) {
    const auto manifestPath = optionValue(arguments, "--manifest=");
    const auto outPath = optionValue(arguments, "--out=");
    if (!manifestPath) {
        return fail("matrix requires --manifest=");
    }
    const bool requireComplete =
        std::find(
            arguments.begin(), arguments.end(), "--require-complete") !=
        arguments.end();
    nlohmann::json manifest;
    std::string error;
    if (!loadJson(*manifestPath, manifest, error)) {
        return fail(error);
    }
    Thresholds thresholds;
    if (!parseThresholds(manifest, thresholds, error)) {
        return fail(error);
    }
    if (!manifest.contains("entries") || !manifest.at("entries").is_array()) {
        return fail("manifest is missing the entries array");
    }

    std::vector<Entry> entries;
    std::set<std::string> seenTuples;
    std::map<std::string, int> manifestCaptureFrames;
    try {
        if (manifest.contains("capture_frames") &&
            manifest.at("capture_frames").is_object()) {
            for (const char* host : {"runtime", "editor"}) {
                if (manifest.at("capture_frames").contains(host) &&
                    manifest.at("capture_frames").at(host).is_number_integer()) {
                    manifestCaptureFrames[host] =
                        manifest.at("capture_frames").at(host).get<int>();
                }
            }
        }
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            std::string("manifest capture frames are invalid: ") +
            exception.what());
    }
    try {
        for (const auto& entryJson : manifest.at("entries")) {
            Entry entry;
            entry.host = entryJson.at("host").get<std::string>();
            entry.api = entryJson.at("api").get<std::string>();
            entry.caseName = entryJson.at("case").get<std::string>();
            entry.pngPath = entryJson.at("png").get<std::string>();
            entry.handshakePath = entryJson.at("handshake").get<std::string>();
            entry.stdoutPath = entryJson.at("stdout").get<std::string>();
            entry.stderrPath = entryJson.at("stderr").get<std::string>();
            entry.metricsPath = entryJson.value("metrics", std::string{});
            entry.exitCode = entryJson.value("exit_code", -1);
            entry.requestedCaptureFrame =
                entryJson.at("requested_capture_frame").get<int>();
            const std::string label =
                entry.host + "/" + entry.api + "/" + entry.caseName;
            if (entry.host != "runtime" && entry.host != "editor") {
                return fail(label + " host is unknown: " + entry.host);
            }
            if (entry.api != "opengl" && entry.api != "vulkan" &&
                entry.api != "d3d12") {
                return fail(label + " API is unknown: " + entry.api);
            }
            const std::optional<FixtureCase> parsedCase =
                parseFixtureCase(entry.caseName);
            if (!parsedCase) {
                return fail(label + " case is unknown: " + entry.caseName);
            }
            entry.fixtureCase = *parsedCase;
            if (!seenTuples.insert(label).second) {
                return fail("duplicate matrix tuple " + label);
            }
            if (entry.requestedCaptureFrame < 3) {
                return fail(
                    label + " requested capture frame must be at least 3");
            }
            const auto manifestFrame = manifestCaptureFrames.find(entry.host);
            if (manifestFrame == manifestCaptureFrames.end()) {
                return fail(
                    label +
                    " has no matching manifest capture frame for its host");
            }
            if (entry.requestedCaptureFrame != manifestFrame->second) {
                return fail(
                    label + " requested capture frame " +
                    std::to_string(entry.requestedCaptureFrame) +
                    " does not match the manifest capture frame " +
                    std::to_string(manifestFrame->second));
            }
            entries.push_back(std::move(entry));
        }
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            std::string("manifest entries are invalid: ") + exception.what());
    }
    if (entries.empty()) {
        return fail("manifest has no entries");
    }
    if (requireComplete && entries.size() != 42u) {
        return fail(
            "complete matrix requires 42 unique entries but the manifest has " +
            std::to_string(entries.size()));
    }

    // Load captures and handshakes, then resolve one crop per host so every
    // case and API on that host is compared inside the same viewport.
    std::map<std::string, FixtureCrop> hostCrops;
    int failureCount = 0;
    for (Entry& entry : entries) {
        if (!loadFixturePng(entry.pngPath, entry.full, &error)) {
            return fail(error);
        }
        FixtureCrop crop;
        if (!detectFixtureViewport(entry.full, crop, &error)) {
            return fail(entry.pngPath + ": " + error);
        }
        const auto found = hostCrops.find(entry.host);
        if (found == hostCrops.end()) {
            hostCrops.emplace(entry.host, crop);
        } else if (found->second.x != crop.x || found->second.y != crop.y ||
                   found->second.width != crop.width ||
                   found->second.height != crop.height) {
            return fail(
                "host '" + entry.host +
                "' produced inconsistent viewport crops across cases");
        }
        entry.crop = crop;
        entry.viewport = cropFixtureImage(entry.full, crop);
    }

    {
        std::vector<std::string> provenanceFailures;
        checkManifestProvenance(manifest, provenanceFailures);
        failureCount += static_cast<int>(provenanceFailures.size());
        if (!provenanceFailures.empty()) {
            std::cerr << "[AnimationFixture][Guard] manifest provenance:\n";
            for (const std::string& failure : provenanceFailures) {
                std::cerr << "  " << failure << '\n';
            }
        }
    }

    for (Entry& entry : entries) {
        checkEntryProvenance(entry, manifest);
        HandshakeCheckResult handshakeResult;
        if (!validateFixtureHandshake(
                entry.handshakePath,
                entry.fixtureCase,
                entry.viewport.width,
                entry.viewport.height,
                handshakeResult,
                &error,
                entry.api,
                entry.host)) {
            entry.failures.push_back(error);
        } else {
            entry.failures.insert(
                entry.failures.end(),
                handshakeResult.failures.begin(),
                handshakeResult.failures.end());
        }
        const auto expectation = computeFixtureExpectation(
            entry.fixtureCase, entry.viewport.width, entry.viewport.height);
        ExpectedContentResult content;
        if (!validateFixtureExpectedContent(
                entry.viewport, expectation, content, &error)) {
            entry.failures.push_back(error);
        } else {
            entry.failures.insert(
                entry.failures.end(),
                content.failures.begin(),
                content.failures.end());
            entry.expectedMetrics = content.metrics;
            entry.expectedGeometry = content.expectedGeometry;
        }
        entry.ok = entry.failures.empty();
        if (!entry.ok) {
            failureCount += static_cast<int>(entry.failures.size());
        }
    }

    const auto findEntry = [&entries](const std::string& host,
                                      const std::string& api,
                                      FixtureCase fixtureCase) -> const Entry* {
        const auto found = std::find_if(
            entries.begin(), entries.end(), [&](const Entry& entry) {
                return entry.host == host && entry.api == api &&
                    entry.fixtureCase == fixtureCase;
            });
        return found == entries.end() ? nullptr : &*found;
    };

    nlohmann::json pairwise = nlohmann::json::array();
    const std::array<std::pair<std::string, std::string>, 3> apiPairs{{
        {"opengl", "vulkan"},
        {"opengl", "d3d12"},
        {"vulkan", "d3d12"}}};
    for (const auto& [host, crop] : hostCrops) {
        (void)crop;
        for (const FixtureCase fixtureCase : {
                 FixtureCase::Bind,
                 FixtureCase::LinearIntermediate,
                 FixtureCase::LinearNearEndpoint,
                 FixtureCase::StepState,
                 FixtureCase::CubicMidpoint,
                 FixtureCase::CubicEarly,
                 FixtureCase::CrossfadeQuarter}) {
            for (const auto& [leftApi, rightApi] : apiPairs) {
                const Entry* left = findEntry(host, leftApi, fixtureCase);
                const Entry* right = findEntry(host, rightApi, fixtureCase);
                if (!left || !right) {
                    failureCount += 1;
                    pairwise.push_back({
                        {"host", host},
                        {"case", fixtureCaseName(fixtureCase)},
                        {"left_api", leftApi},
                        {"right_api", rightApi},
                        {"ok", false},
                        {"failures",
                         {"missing matrix tuple for this host/API/case"}}});
                    continue;
                }
                ImageCompareMetrics metrics;
                if (!compareFixtureImages(
                        left->viewport,
                        right->viewport,
                        thresholds.channelTolerance,
                        metrics,
                        &error)) {
                    return fail(error);
                }
                std::vector<std::string> failures;
                thresholdsPass(metrics, thresholds, failures);
                failureCount += static_cast<int>(failures.size());
                pairwise.push_back({
                    {"host", host},
                    {"case", fixtureCaseName(fixtureCase)},
                    {"left_api", leftApi},
                    {"right_api", rightApi},
                    {"normalized_mae", metrics.normalizedMae},
                    {"normalized_rmse", metrics.normalizedRmse},
                    {"changed_pixel_ratio", metrics.changedPixelRatio},
                    {"max_channel_diff", metrics.maxChannelDiff},
                    {"ok", failures.empty()},
                    {"failures", failures}});
            }
        }
    }

    nlohmann::json crossCase = nlohmann::json::array();
    for (const auto& [host, crop] : hostCrops) {
        (void)crop;
        for (const std::string& api : {"opengl", "vulkan", "d3d12"}) {
            const Entry* bind = findEntry(host, api, FixtureCase::Bind);
            const Entry* intermediate =
                findEntry(host, api, FixtureCase::LinearIntermediate);
            const Entry* endpoint =
                findEntry(host, api, FixtureCase::LinearNearEndpoint);
            const Entry* step = findEntry(host, api, FixtureCase::StepState);
            const Entry* cubicMidpoint =
                findEntry(host, api, FixtureCase::CubicMidpoint);
            const Entry* cubicEarly =
                findEntry(host, api, FixtureCase::CubicEarly);
            const Entry* crossfade =
                findEntry(host, api, FixtureCase::CrossfadeQuarter);
            if (!bind || !intermediate || !endpoint || !step ||
                !cubicMidpoint || !cubicEarly || !crossfade) {
                ++failureCount;
                crossCase.push_back({
                    {"host", host},
                    {"api", api},
                    {"check", "required_case_present"},
                    {"ok", false},
                    {"failures",
                     {"missing one or more required case captures for this host/API"}}});
                continue;
            }
            const auto record = [&](const std::string& name,
                                    const Entry& left,
                                    const Entry& right,
                                    double minimumChanged) {
                ImageCompareMetrics metrics;
                if (!compareFixtureImages(
                        left.viewport,
                        right.viewport,
                        thresholds.channelTolerance,
                        metrics,
                        &error)) {
                    ++failureCount;
                    return nlohmann::json{{"ok", false}, {"error", error}};
                }
                std::vector<std::string> failures;
                if (metrics.changedPixelRatio < minimumChanged) {
                    failures.push_back(
                        "case difference " +
                        std::to_string(metrics.changedPixelRatio) +
                        " is smaller than the authored minimum " +
                        std::to_string(minimumChanged));
                }
                failureCount += static_cast<int>(failures.size());
                return nlohmann::json{
                    {"host", host},
                    {"api", api},
                    {"check", name},
                    {"normalized_mae", metrics.normalizedMae},
                    {"normalized_rmse", metrics.normalizedRmse},
                    {"changed_pixel_ratio", metrics.changedPixelRatio},
                    {"max_channel_diff", metrics.maxChannelDiff},
                    {"ok", failures.empty()},
                    {"failures", failures}};
            };
            crossCase.push_back(record(
                "bind_vs_linear_intermediate",
                *bind,
                *intermediate,
                kBindVsPosedMinimum));
            crossCase.push_back(record(
                "bind_vs_linear_near_endpoint",
                *bind,
                *endpoint,
                kBindVsPosedMinimum));
            crossCase.push_back(record(
                "bind_vs_step_state", *bind, *step, kBindVsPosedMinimum));
            crossCase.push_back(record(
                "linear_intermediate_vs_near_endpoint",
                *intermediate,
                *endpoint,
                kIntermediateVsEndpointMinimum));
            crossCase.push_back(record(
                "linear_near_endpoint_vs_step_state",
                *endpoint,
                *step,
                kEndpointVsStepMinimum));
            crossCase.push_back(record(
                "bind_vs_cubic_midpoint",
                *bind,
                *cubicMidpoint,
                kBindVsPosedMinimum));
            crossCase.push_back(record(
                "bind_vs_cubic_early",
                *bind,
                *cubicEarly,
                kBindVsPosedMinimum));
            crossCase.push_back(record(
                "cubic_early_vs_cubic_midpoint",
                *cubicEarly,
                *cubicMidpoint,
                kEndpointVsStepMinimum));
            crossCase.push_back(record(
                "bind_vs_crossfade_quarter",
                *bind,
                *crossfade,
                kBindVsPosedMinimum));
        }
    }

    nlohmann::json entryJson = nlohmann::json::array();
    for (const Entry& entry : entries) {
        entryJson.push_back({
            {"host", entry.host},
            {"api", entry.api},
            {"case", entry.caseName},
            {"png", entry.pngPath},
            {"handshake", entry.handshakePath},
            {"stdout", entry.stdoutPath},
            {"stderr", entry.stderrPath},
            {"metrics", entry.metricsPath},
            {"exit_code", entry.exitCode},
            {"requested_capture_frame", entry.requestedCaptureFrame},
            {"observed_capture_frame", entry.observedCaptureFrame},
            {"crop",
             {{"x", entry.crop.x},
              {"y", entry.crop.y},
              {"width", entry.crop.width},
              {"height", entry.crop.height}}},
            {"ok", entry.ok},
            {"failures", entry.failures},
            {"measured_content", entry.expectedMetrics},
            {"expected_geometry", entry.expectedGeometry}});
    }

    const std::size_t pairwiseCount = pairwise.size();
    const std::size_t crossCaseCount = crossCase.size();
    nlohmann::json document{
        {"schema", "phlosion-animation-fixture-matrix-v1"},
        {"run_id", manifest.value("run_id", std::string{})},
        {"source_revision", manifest.value("source_revision", std::string{})},
        {"source_dirty", manifest.value("source_dirty", false)},
        {"thresholds",
         {{"normalized_mae_max", thresholds.mae},
          {"normalized_rmse_max", thresholds.rmse},
          {"changed_pixel_ratio_max", thresholds.changedRatio},
          {"channel_tolerance", thresholds.channelTolerance},
          {"bind_vs_posed_changed_ratio_min", kBindVsPosedMinimum},
          {"intermediate_vs_endpoint_changed_ratio_min",
           kIntermediateVsEndpointMinimum},
          {"endpoint_vs_step_changed_ratio_min", kEndpointVsStepMinimum}}},
        {"entries", std::move(entryJson)},
        {"pairwise", std::move(pairwise)},
        {"cross_case", std::move(crossCase)},
        {"failure_count", failureCount},
        {"ok", failureCount == 0}};
    if (outPath) {
        if (!writeJson(*outPath, document, error)) {
            return fail(error);
        }
    }

    if (const auto sheetPath = optionValue(arguments, "--sheet=")) {
        std::vector<std::pair<std::string, FixtureImage>> sheetImages;
        for (const Entry& entry : entries) {
            sheetImages.emplace_back(
                entry.host + "/" + entry.api + "/" + entry.caseName,
                entry.viewport);
        }
        const FixtureImage sheet = buildFixtureContactSheet(
            sheetImages, 4, 320, 180);
        if (sheet.empty() || !saveFixturePng(*sheetPath, sheet, &error)) {
            return fail(error.empty() ? "contact sheet is empty" : error);
        }
    }

    if (failureCount != 0) {
        return fail(
            std::to_string(failureCount) +
            " capture-matrix check(s) failed" +
            (outPath ? "; see " + *outPath : std::string{}));
    }
    std::cout << "[AnimationFixture][Guard] matrix=ok entries=" << entries.size()
              << " pairwise=" << pairwiseCount
              << " cross_case=" << crossCaseCount << '\n';
    return 0;
}

int runEcho(const std::vector<std::string>& arguments) {
    const auto text = optionValue(arguments, "--text=");
    if (!text) {
        return fail("echo requires --text=");
    }
    int exitCode = 0;
    if (const auto value = optionValue(arguments, "--exit=")) {
        try {
            exitCode = std::stoi(*value);
        } catch (const std::exception&) {
            return fail("echo exit code must be an integer: " + *value);
        }
    }
    int sleepMilliseconds = 0;
    if (const auto value = optionValue(arguments, "--sleep-ms=")) {
        try {
            sleepMilliseconds = std::stoi(*value);
        } catch (const std::exception&) {
            return fail("echo sleep must be an integer: " + *value);
        }
    }
    std::cout << "[AnimationFixture][Echo] " << *text << '\n' << std::flush;
    if (sleepMilliseconds > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(sleepMilliseconds));
    }
    return exitCode;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        arguments.push_back(argv[index] ? argv[index] : "");
    }
    if (arguments.empty()) {
        return fail(
            "usage: AnimationFixtureGuard detect|compare|expect|matrix|synth|echo ...");
    }
    const std::string command = arguments.front();
    const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    if (command == "detect") {
        return runDetect(rest);
    }
    if (command == "compare") {
        return runCompare(rest);
    }
    if (command == "expect") {
        return runExpect(rest);
    }
    if (command == "matrix") {
        return runMatrix(rest);
    }
    if (command == "synth") {
        return runSynth(rest);
    }
    if (command == "echo") {
        return runEcho(rest);
    }
    return fail("unknown guard command: " + command);
}
