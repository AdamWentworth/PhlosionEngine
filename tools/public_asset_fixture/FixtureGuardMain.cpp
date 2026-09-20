// Image/content guard for the public fixture capture matrix. It validates
// expected content, pairwise native-API parity inside each host and the
// transition-specific image relationships, then writes a machine-readable
// matrix and a contact sheet.

#include "FixtureImageGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

using engine::fixture::FixtureCase;
using engine::fixture::FixtureCrop;
using engine::fixture::FixtureImage;
using engine::fixture::ImageCompareMetrics;

struct Thresholds {
    double mae = 0.01;
    double rmse = 0.08;
    double changedRatio = 0.05;
    int channelTolerance = 8;
};

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
    FixtureCase fixtureCase = FixtureCase::AFrame0;
    FixtureImage full;
    FixtureImage viewport;
    FixtureCrop crop;
    nlohmann::json handshake;
    engine::fixture::ExpectedContentResult expected;
    bool ok = false;
    std::vector<std::string> failures;
};

// Approved capture-contract thresholds. A manifest may request stricter
// values but never looser ones.
constexpr double kApprovedMae = 0.01;
constexpr double kApprovedRmse = 0.08;
constexpr double kApprovedChangedRatio = 0.05;
constexpr int kApprovedChannelTolerance = 8;

struct ScreenshotRecord {
    std::string backend;
    std::string status;
    std::string path;
    int width = 0;
    int height = 0;
    int frame = -1;
};

int fail(const std::string& message) {
    std::cerr << "[PublicAssetFixture][Guard] FAILED " << message << '\n';
    return 1;
}

std::optional<std::string> optionValue(
    const std::vector<std::string>& arguments,
    const std::string& prefix) {
    for (const std::string& argument : arguments) {
        if (argument.rfind(prefix, 0u) == 0u) {
            return argument.substr(prefix.size());
        }
    }
    return std::nullopt;
}

bool loadJson(
    const std::string& path,
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

bool writeJson(
    const std::string& path,
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

std::string normalizedPath(const std::string& value) {
    std::string out = value;
    std::replace(out.begin(), out.end(), '\\', '/');
    std::transform(
        out.begin(),
        out.end(),
        out.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return out;
}

bool isHex(const std::string& value, std::size_t minimum, std::size_t maximum) {
    if (value.size() < minimum || value.size() > maximum) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

bool readTextFile(
    const std::string& path,
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

std::vector<ScreenshotRecord> parseScreenshotRecords(const std::string& text) {
    std::vector<ScreenshotRecord> records;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
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
        record.backend = line.substr(
            backendStart, backendEnd - backendStart);
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
            framePosition == std::string::npos ||
            framePosition < sizePosition) {
            continue;
        }
        std::string path = line.substr(
            statusEnd + 1u, sizePosition - (statusEnd + 1u));
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

bool parseThresholds(
    const nlohmann::json& manifest,
    Thresholds& outThresholds,
    std::string& outError) {
    if (!manifest.contains("thresholds") ||
        !manifest.at("thresholds").is_object()) {
        outError = "manifest is missing the thresholds object";
        return false;
    }
    const auto& values = manifest.at("thresholds");
    for (const char* key : {"mae", "rmse", "changed_ratio", "channel_tolerance"}) {
        if (!values.contains(key) || !values.at(key).is_number()) {
            outError = std::string("manifest threshold '") + key + "' is missing or not a number";
            return false;
        }
    }
    outThresholds.mae = values.at("mae").get<double>();
    outThresholds.rmse = values.at("rmse").get<double>();
    outThresholds.changedRatio = values.at("changed_ratio").get<double>();
    outThresholds.channelTolerance = values.at("channel_tolerance").get<int>();
    const auto finite = [](double value) {
        return std::isfinite(value);
    };
    if (!finite(outThresholds.mae) ||
        !finite(outThresholds.rmse) ||
        !finite(outThresholds.changedRatio)) {
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

// Validates the per-entry process, capture-frame, parity-contract and editor
// metrics evidence. The runner records the raw logs; this verifies them rather
// than trusting the recorded fields.
void checkEntryProvenance(
    Entry& entry,
    const nlohmann::json& manifest,
    const Thresholds& thresholds) {
    (void)thresholds;
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
        entry.failures.push_back(label + " produced a screenshot FAILURE record");
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
                    label + " editor metrics backend does not match the requested API");
            }
            const auto& capture = metrics.at("capture");
            const int frames = capture.at("frames").get<int>();
            const int expectedFrames = manifest.value("editor_frames", 0);
            if (frames < expectedFrames) {
                entry.failures.push_back(
                    label + " editor metrics frame count is below the requested frames");
            }
            if (!entry.handshake.contains("surface")) {
                entry.failures.push_back(
                    label + " handshake has no surface dimensions for metrics cross-check");
            } else {
                const int surfaceWidth =
                    entry.handshake.at("surface").at("width").get<int>();
                const int surfaceHeight =
                    entry.handshake.at("surface").at("height").get<int>();
                if (capture.at("viewport_width").get<int>() != surfaceWidth ||
                    capture.at("viewport_height").get<int>() != surfaceHeight) {
                    entry.failures.push_back(
                        label + " editor metrics viewport does not match the handshake surface");
                }
            }
        } catch (const nlohmann::json::exception& exception) {
            entry.failures.push_back(
                label + " editor metrics fields are invalid: " +
                exception.what());
        }
    }
}

void checkManifestProvenance(
    const nlohmann::json& manifest,
    std::vector<std::string>& outFailures) {
    const auto expect = [&outFailures](bool condition, const std::string& message) {
        if (!condition) {
            outFailures.push_back(message);
        }
    };
    try {
        expect(
            manifest.contains("source_revision") &&
                manifest.at("source_revision").is_string() &&
                isHex(manifest.at("source_revision").get<std::string>(), 7u, 40u),
            "manifest is missing a hexadecimal source revision");
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
        expect(
            manifest.contains("binaries") &&
                manifest.at("binaries").is_object(),
            "manifest is missing binary identities");
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
            if (manifest.contains("editor_frames") &&
                manifest.at("capture_frames").contains("editor")) {
                expect(
                    manifest.at("editor_frames").get<int>() >
                        manifest.at("capture_frames").at("editor").get<int>(),
                    "editor frame count must exceed the requested capture frame");
            }
        }
        if (manifest.contains("binaries") &&
            manifest.at("binaries").is_object()) {
            for (const char* name : {"runtime", "editor", "plugin", "guard"}) {
                expect(
                    manifest.at("binaries").contains(name) &&
                        manifest.at("binaries").at(name).is_object(),
                    std::string("manifest is missing the binary identity '") +
                        name + "'");
                if (!manifest.at("binaries").contains(name)) {
                    continue;
                }
                const auto& identity = manifest.at("binaries").at(name);
                expect(
                    identity.contains("sha256") && identity.at("sha256").is_string() &&
                        isHex(identity.at("sha256").get<std::string>(), 64u, 64u),
                    std::string("manifest binary '") + name +
                        "' is missing a SHA-256 digest");
                expect(
                    identity.contains("path") && identity.at("path").is_string() &&
                        identity.contains("bytes") &&
                        identity.at("bytes").is_number_unsigned(),
                    std::string("manifest binary '") + name +
                        "' is missing its path or byte count");
                if (identity.contains("path") && identity.at("path").is_string() &&
                    identity.contains("bytes") &&
                    identity.at("bytes").is_number_unsigned()) {
                    std::error_code sizeError;
                    const auto size = std::filesystem::file_size(
                        identity.at("path").get<std::string>(), sizeError);
                    expect(
                        !sizeError &&
                            size == identity.at("bytes").get<std::uint64_t>(),
                        std::string("manifest binary '") + name +
                            "' no longer matches the recorded file size");
                }
            }
        }
    } catch (const nlohmann::json::exception& exception) {
        outFailures.push_back(
            std::string("manifest provenance fields are invalid: ") +
            exception.what());
    }
}

bool handshakeMatchesCase(
    const Entry& entry,
    std::vector<std::string>& outFailures) {
    const nlohmann::json& handshake = entry.handshake;
    const std::string expectedRoot =
        engine::fixture::fixtureCaseExpectedActiveRoot(entry.fixtureCase);
    const std::string expectedContent =
        engine::fixture::fixtureCaseExpectedContentId(entry.fixtureCase);
    const int expectedFrame =
        engine::fixture::fixtureCaseLogicalFrame(entry.fixtureCase);
    const auto expect = [&](bool condition, const std::string& message) {
        if (!condition) {
            outFailures.push_back(message);
        }
    };
    try {
        expect(
            handshake.at("schema").get<std::string>() ==
                "phlosion-public-asset-fixture-handshake-v1",
            "handshake schema is unknown");
        expect(
            handshake.at("host").get<std::string>() == entry.host,
            "handshake host does not match the manifest entry");
        expect(
            handshake.at("case").get<std::string>() == entry.caseName,
            "handshake case does not match the manifest entry");
        expect(
            handshake.at("logical_frame").get<int>() == expectedFrame,
            "handshake logical frame does not match the case");
        expect(
            handshake.at("requested_backend").get<std::string>() == entry.api,
            "handshake requested backend does not match the manifest entry");
        expect(
            handshake.at("actual_backend").get<std::string>() == entry.api,
            "handshake actual backend does not match the requested backend");
        expect(
            handshake.at("preparation_complete").get<bool>(),
            "handshake reports incomplete preparation");
        const auto& active = handshake.at("active");
        expect(
            active.at("root").get<std::string>() == expectedRoot,
            "handshake active root does not match the expected case root");
        expect(
            active.at("content_id").get<std::string>() == expectedContent,
            "handshake active content does not match the expected case content");
        expect(
            !active.at("content_hash").get<std::string>().empty(),
            "handshake active content hash is empty");
        expect(
            active.at("activation_count").get<int>() ==
                (entry.fixtureCase == FixtureCase::ValidReplacementB ? 2 : 1),
            "handshake activation count does not match the case lifecycle");

        const auto& transition = handshake.at("transition");
        if (engine::fixture::fixtureCaseExpectsReplacementFailure(entry.fixtureCase)) {
            expect(
                transition.at("attempted").get<bool>(),
                "failed-replacement case did not attempt a replacement");
            expect(
                transition.at("failed").get<bool>(),
                "failed-replacement case did not report a failed candidate");
            expect(
                !transition.at("succeeded").get<bool>(),
                "failed-replacement case reported a successful candidate");
            expect(
                transition.at("outcome").get<std::string>() == "graph_failed",
                "failed-replacement outcome is not graph_failed");
            expect(
                !transition.at("error").get<std::string>().empty(),
                "failed-replacement diagnostic is empty");
        } else if (entry.fixtureCase == FixtureCase::ValidReplacementB) {
            expect(
                transition.at("attempted").get<bool>(),
                "valid-replacement case did not attempt a replacement");
            expect(
                transition.at("succeeded").get<bool>(),
                "valid-replacement case did not activate the candidate");
            expect(
                !transition.at("failed").get<bool>(),
                "valid-replacement case reported a failure");
            expect(
                transition.at("outcome").get<std::string>() == "activated",
                "valid-replacement outcome is not activated");
        } else {
            expect(
                !transition.at("attempted").get<bool>(),
                "initial-only case attempted a replacement");
        }

        const auto& surface = handshake.at("surface");
        const int surfaceWidth = surface.at("width").get<int>();
        const int surfaceHeight = surface.at("height").get<int>();
        if (entry.crop.width == entry.full.width &&
            entry.crop.height == entry.full.height) {
            expect(
                surfaceWidth == entry.full.width &&
                    surfaceHeight == entry.full.height,
                "standalone handshake surface does not match the capture size");
        } else {
            expect(
                surfaceWidth == entry.crop.width &&
                    surfaceHeight == entry.crop.height,
                "editor handshake viewport size does not match the detected crop");
        }
        const auto& objects = handshake.at("objects");
        for (const char* role :
             {"background", "board", "occluder", "hidden", "blend", "marker"}) {
            expect(
                objects.contains(role) && objects.at(role).contains("rect"),
                std::string("handshake is missing role rect '") + role + "'");
        }
    } catch (const nlohmann::json::exception& exception) {
        outFailures.push_back(
            std::string("handshake fields are invalid: ") + exception.what());
    }
    return outFailures.empty();
}

std::map<std::string, engine::fixture::FixtureRoleRect> roleRectsOf(
    const nlohmann::json& handshake) {
    std::map<std::string, engine::fixture::FixtureRoleRect> rects;
    const auto& objects = handshake.at("objects");
    for (const char* role :
         {"background", "board", "occluder", "hidden", "blend", "marker"}) {
        if (!objects.contains(role)) {
            continue;
        }
        const auto& values = objects.at(role).at("rect");
        if (!values.is_array() || values.size() != 4u) {
            continue;
        }
        rects[role] = engine::fixture::FixtureRoleRect{
            values.at(0).get<float>(),
            values.at(1).get<float>(),
            values.at(2).get<float>(),
            values.at(3).get<float>()};
    }
    return rects;
}

bool thresholdsPass(
    const ImageCompareMetrics& metrics,
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
            "changed-pixel ratio " +
            std::to_string(metrics.changedPixelRatio) +
            " exceeds " + std::to_string(thresholds.changedRatio));
    }
    return outFailures.empty();
}

std::string cropText(const FixtureCrop& crop) {
    return std::to_string(crop.x) + "," + std::to_string(crop.y) + "," +
        std::to_string(crop.width) + "," + std::to_string(crop.height);
}

bool parseCrop(const std::string& text, FixtureCrop& outCrop) {
    int values[4] = {0, 0, 0, 0};
    std::size_t begin = 0u;
    for (int index = 0; index < 4; ++index) {
        const std::size_t comma = text.find(',', begin);
        const std::string part = text.substr(
            begin,
            comma == std::string::npos ? std::string::npos : comma - begin);
        try {
            values[index] = std::stoi(part);
        } catch (const std::exception&) {
            return false;
        }
        if (index < 3 && comma == std::string::npos) {
            return false;
        }
        begin = comma == std::string::npos ? text.size() : comma + 1u;
    }
    outCrop = FixtureCrop{values[0], values[1], values[2], values[3]};
    return outCrop.width > 0 && outCrop.height > 0;
}

int runDetect(const std::vector<std::string>& arguments) {
    const auto png = optionValue(arguments, "--png=");
    const auto out = optionValue(arguments, "--out=");
    if (!png || !out) {
        return fail("detect requires --png= and --out=");
    }
    FixtureImage image;
    std::string error;
    if (!engine::fixture::loadFixturePng(*png, image, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!engine::fixture::detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    nlohmann::json document{
        {"png", *png},
        {"image", {{"width", image.width}, {"height", image.height}}},
        {"crop",
         {{"x", crop.x},
          {"y", crop.y},
          {"width", crop.width},
          {"height", crop.height}}}};
    if (!writeJson(*out, document, error)) {
        return fail(error);
    }
    std::cout << "[PublicAssetFixture][Guard] crop=" << cropText(crop) << '\n';
    return 0;
}

int runCompare(const std::vector<std::string>& arguments) {
    const auto left = optionValue(arguments, "--left=");
    const auto right = optionValue(arguments, "--right=");
    const auto cropTextValue = optionValue(arguments, "--crop=");
    const auto out = optionValue(arguments, "--out=");
    const bool expectEqual =
        std::find(arguments.begin(), arguments.end(), "--expect-equal") !=
        arguments.end();
    if (!left || !right) {
        return fail("compare requires --left= and --right=");
    }
    FixtureImage leftImage;
    FixtureImage rightImage;
    std::string error;
    if (!engine::fixture::loadFixturePng(*left, leftImage, &error) ||
        !engine::fixture::loadFixturePng(*right, rightImage, &error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!cropTextValue || *cropTextValue == "auto") {
        if (!engine::fixture::detectFixtureViewport(leftImage, crop, &error)) {
            return fail(error);
        }
    } else if (!parseCrop(*cropTextValue, crop)) {
        return fail("compare crop must be x,y,w,h or auto");
    }
    const FixtureImage leftViewport =
        engine::fixture::cropFixtureImage(leftImage, crop);
    const FixtureImage rightViewport =
        engine::fixture::cropFixtureImage(rightImage, crop);
    Thresholds thresholds;
    ImageCompareMetrics metrics;
    if (!engine::fixture::compareFixtureImages(
            leftViewport,
            rightViewport,
            thresholds.channelTolerance,
            metrics,
            &error)) {
        return fail(error);
    }
    std::vector<std::string> failures;
    if (expectEqual) {
        if (metrics.maxChannelDiff != 0) {
            failures.push_back("images are not identical");
        }
    } else {
        thresholdsPass(metrics, thresholds, failures);
    }
    nlohmann::json document{
        {"left", *left},
        {"right", *right},
        {"crop",
         {{"x", crop.x},
          {"y", crop.y},
          {"width", crop.width},
          {"height", crop.height}}},
        {"normalized_mae", metrics.normalizedMae},
        {"normalized_rmse", metrics.normalizedRmse},
        {"changed_pixel_ratio", metrics.changedPixelRatio},
        {"max_channel_diff", metrics.maxChannelDiff},
        {"expect_equal", expectEqual},
        {"ok", failures.empty()},
        {"failures", failures}};
    if (out && !writeJson(*out, document, error)) {
        return fail(error);
    }
    if (!failures.empty()) {
        return fail(failures.front());
    }
    return 0;
}

int runExpect(const std::vector<std::string>& arguments) {
    const auto png = optionValue(arguments, "--png=");
    const auto handshakePath = optionValue(arguments, "--handshake=");
    const auto out = optionValue(arguments, "--out=");
    if (!png || !handshakePath) {
        return fail("expect requires --png= and --handshake=");
    }
    FixtureImage image;
    nlohmann::json handshake;
    std::string error;
    if (!engine::fixture::loadFixturePng(*png, image, &error) ||
        !loadJson(*handshakePath, handshake, error)) {
        return fail(error);
    }
    FixtureCrop crop;
    if (!engine::fixture::detectFixtureViewport(image, crop, &error)) {
        return fail(error);
    }
    const FixtureImage viewport =
        engine::fixture::cropFixtureImage(image, crop);
    const std::string content =
        handshake.at("active").at("content_id").get<std::string>();
    const int frame = handshake.at("logical_frame").get<int>();
    engine::fixture::ExpectedContentResult result;
    if (!engine::fixture::validateFixtureExpectedContent(
            viewport,
            content,
            frame,
            roleRectsOf(handshake),
            result,
            &error)) {
        return fail(error);
    }
    nlohmann::json document{
        {"png", *png},
        {"crop",
         {{"x", crop.x},
          {"y", crop.y},
          {"width", crop.width},
          {"height", crop.height}}},
        {"ok", result.ok},
        {"failures", result.failures},
        {"metrics", result.metrics}};
    if (out && !writeJson(*out, document, error)) {
        return fail(error);
    }
    if (!result.ok) {
        return fail(result.failures.empty() ? "expected content failed" : result.failures.front());
    }
    return 0;
}

int runMatrix(const std::vector<std::string>& arguments) {
    const auto manifestPath = optionValue(arguments, "--manifest=");
    const auto outPath = optionValue(arguments, "--out=");
    const auto sheetPath = optionValue(arguments, "--sheet=");
    const bool requireComplete =
        std::find(
            arguments.begin(),
            arguments.end(),
            "--require-complete") != arguments.end();
    if (!manifestPath || !outPath) {
        return fail("matrix requires --manifest= and --out=");
    }
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

    // Phase 1: validate the matrix tuple set before reading per-entry
    // evidence, so duplicate, unknown and incomplete manifests are rejected
    // for the right reason.
    std::vector<std::string> tupleKeys;
    {
        std::size_t index = 0u;
        for (const auto& entryJson : manifest.at("entries")) {
            try {
                const std::string host =
                    entryJson.at("host").get<std::string>();
                const std::string api =
                    entryJson.at("api").get<std::string>();
                const std::string caseName =
                    entryJson.at("case").get<std::string>();
                if (host != "runtime" && host != "editor") {
                    return fail("manifest host is unknown: " + host);
                }
                if (api != "opengl" && api != "vulkan" && api != "d3d12") {
                    return fail("manifest API is unknown: " + api);
                }
                if (!engine::fixture::parseFixtureCase(caseName)) {
                    return fail("manifest case is unknown: " + caseName);
                }
                tupleKeys.push_back(host + "|" + api + "|" + caseName);
            } catch (const nlohmann::json::exception& exception) {
                return fail(
                    "manifest entry " + std::to_string(index) +
                    " tuple is invalid: " + exception.what());
            }
            ++index;
        }
    }
    if (tupleKeys.empty()) {
        return fail("manifest contains no entries");
    }
    {
        std::vector<std::string> sortedKeys = tupleKeys;
        std::sort(sortedKeys.begin(), sortedKeys.end());
        const auto duplicate = std::adjacent_find(
            sortedKeys.begin(), sortedKeys.end());
        if (duplicate != sortedKeys.end()) {
            return fail(
                "manifest contains a duplicate matrix tuple: " + *duplicate);
        }
    }
    const std::array<std::string, 2> expectedHosts{"runtime", "editor"};
    const std::array<std::string, 3> expectedApis{
        "opengl", "vulkan", "d3d12"};
    const std::array<FixtureCase, 4> expectedCases{
        FixtureCase::AFrame0,
        FixtureCase::AFrame30,
        FixtureCase::FailedReplacement,
        FixtureCase::ValidReplacementB};
    if (requireComplete) {
        std::vector<std::string> missing;
        for (const std::string& host : expectedHosts) {
            for (const std::string& api : expectedApis) {
                for (const FixtureCase fixtureCase : expectedCases) {
                    const std::string key =
                        host + "|" + api + "|" +
                        std::string(engine::fixture::fixtureCaseName(fixtureCase));
                    if (std::find(tupleKeys.begin(), tupleKeys.end(), key) ==
                        tupleKeys.end()) {
                        missing.push_back(key);
                    }
                }
            }
        }
        if (tupleKeys.size() !=
            expectedHosts.size() * expectedApis.size() * expectedCases.size()) {
            return fail(
                "complete matrix requires exactly 24 unique entries, found " +
                std::to_string(tupleKeys.size()));
        }
        if (!missing.empty()) {
            return fail(
                "complete matrix is missing " +
                std::to_string(missing.size()) +
                " tuple(s), first: " + missing.front());
        }
    }

    // Phase 2: parse the per-entry evidence now that the tuple set is exact.
    std::vector<Entry> entries;
    entries.reserve(tupleKeys.size());
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
            entry.exitCode = entryJson.at("exit_code").get<int>();
            entry.requestedCaptureFrame =
                entryJson.at("requested_capture_frame").get<int>();
            if (entry.requestedCaptureFrame < 1) {
                return fail(
                    "manifest requested capture frame must be positive for " +
                    entry.host + "/" + entry.api + "/" + entry.caseName);
            }
            entry.fixtureCase =
                *engine::fixture::parseFixtureCase(entry.caseName);
            entries.push_back(std::move(entry));
        }
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            std::string("manifest entries are invalid: ") + exception.what());
    }

    // Load captures and handshakes, then resolve one crop per host so every
    // case and API on that host is compared inside the same viewport.
    std::map<std::string, FixtureCrop> hostCrops;
    for (Entry& entry : entries) {
        if (!engine::fixture::loadFixturePng(entry.pngPath, entry.full, &error)) {
            return fail(error);
        }
        if (!loadJson(entry.handshakePath, entry.handshake, error)) {
            return fail(error);
        }
        FixtureCrop crop;
        if (!engine::fixture::detectFixtureViewport(entry.full, crop, &error)) {
            return fail(entry.pngPath + ": " + error);
        }
        const auto found = hostCrops.find(entry.host);
        if (found == hostCrops.end()) {
            hostCrops.emplace(entry.host, crop);
        } else if (found->second.x != crop.x ||
                   found->second.y != crop.y ||
                   found->second.width != crop.width ||
                   found->second.height != crop.height) {
            return fail(
                "host '" + entry.host +
                "' produced inconsistent viewport crops across cases");
        }
        entry.crop = crop;
        entry.viewport = engine::fixture::cropFixtureImage(entry.full, crop);
    }

    int failureCount = 0;
    {
        std::vector<std::string> provenanceFailures;
        checkManifestProvenance(manifest, provenanceFailures);
        failureCount += static_cast<int>(provenanceFailures.size());
        if (!provenanceFailures.empty()) {
            std::cerr << "[PublicAssetFixture][Guard] manifest provenance:\n";
            for (const std::string& failure : provenanceFailures) {
                std::cerr << "  " << failure << '\n';
            }
        }
    }
    for (Entry& entry : entries) {
        handshakeMatchesCase(entry, entry.failures);
        checkEntryProvenance(entry, manifest, thresholds);
        try {
            if (!engine::fixture::validateFixtureExpectedContent(
                    entry.viewport,
                    engine::fixture::fixtureCaseExpectedContentId(entry.fixtureCase),
                    engine::fixture::fixtureCaseLogicalFrame(entry.fixtureCase),
                    roleRectsOf(entry.handshake),
                    entry.expected,
                    &error)) {
                entry.failures.push_back(error);
            } else {
                for (const std::string& failure : entry.expected.failures) {
                    entry.failures.push_back(failure);
                }
            }
        } catch (const nlohmann::json::exception& exception) {
            entry.failures.push_back(
                std::string("handshake role rects are invalid: ") +
                exception.what());
        }
        entry.ok = entry.failures.empty();
        if (!entry.ok) {
            failureCount += static_cast<int>(entry.failures.size());
        }
    }

    const auto findEntry = [&entries](
                               const std::string& host,
                               const std::string& api,
                               FixtureCase fixtureCase) -> const Entry* {
        const auto found = std::find_if(
            entries.begin(),
            entries.end(),
            [&](const Entry& entry) {
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
        for (const FixtureCase fixtureCase :
             {FixtureCase::AFrame0,
              FixtureCase::AFrame30,
              FixtureCase::FailedReplacement,
              FixtureCase::ValidReplacementB}) {
            for (const auto& [leftApi, rightApi] : apiPairs) {
                const Entry* left = findEntry(host, leftApi, fixtureCase);
                const Entry* right = findEntry(host, rightApi, fixtureCase);
                if (!left || !right) {
                    std::vector<std::string> missing;
                    if (!left) {
                        missing.push_back(leftApi);
                    }
                    if (!right) {
                        missing.push_back(rightApi);
                    }
                    failureCount += static_cast<int>(missing.size());
                    std::vector<std::string> failures;
                    for (const std::string& api : missing) {
                        failures.push_back(
                            "missing matrix tuple " + host + "/" + api + "/" +
                            std::string(
                                engine::fixture::fixtureCaseName(fixtureCase)));
                    }
                    pairwise.push_back({
                        {"host", host},
                        {"case", std::string(engine::fixture::fixtureCaseName(fixtureCase))},
                        {"left_api", leftApi},
                        {"right_api", rightApi},
                        {"ok", false},
                        {"failures", failures}});
                    continue;
                }
                ImageCompareMetrics metrics;
                if (!engine::fixture::compareFixtureImages(
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
                    {"case", std::string(engine::fixture::fixtureCaseName(fixtureCase))},
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
            const Entry* a0 = findEntry(host, api, FixtureCase::AFrame0);
            const Entry* a30 = findEntry(host, api, FixtureCase::AFrame30);
            const Entry* failed =
                findEntry(host, api, FixtureCase::FailedReplacement);
            const Entry* b30 =
                findEntry(host, api, FixtureCase::ValidReplacementB);
            if (!a0 || !a30 || !failed || !b30) {
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
                                    bool expectEqual,
                                    double minimumChanged) {
                ImageCompareMetrics metrics;
                if (!engine::fixture::compareFixtureImages(
                        left.viewport,
                        right.viewport,
                        thresholds.channelTolerance,
                        metrics,
                        &error)) {
                    ++failureCount;
                    return nlohmann::json{{"ok", false}, {"error", error}};
                }
                std::vector<std::string> failures;
                if (expectEqual) {
                    if (metrics.maxChannelDiff != 0) {
                        failures.push_back(
                            "failed replacement changed the visible frame");
                    }
                } else {
                    if (metrics.changedPixelRatio < minimumChanged) {
                        failures.push_back(
                            "visible difference is smaller than the expected minimum");
                    }
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
                "a30_vs_failed_a30", *a30, *failed, true, 0.0));
            crossCase.push_back(record(
                "a0_vs_a30", *a0, *a30, false, 0.001));
            crossCase.push_back(record(
                "a30_vs_b30", *a30, *b30, false, 0.05));
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
            {"expected_content", entry.expected.metrics}});
    }

    const std::size_t pairwiseCount = pairwise.size();
    const std::size_t crossCaseCount = crossCase.size();
    nlohmann::json document{
        {"schema", "phlosion-public-asset-fixture-matrix-v1"},
        {"run_id", manifest.value("run_id", std::string{})},
        {"thresholds",
         {{"normalized_mae_max", thresholds.mae},
          {"normalized_rmse_max", thresholds.rmse},
          {"changed_pixel_ratio_max", thresholds.changedRatio},
          {"channel_tolerance", thresholds.channelTolerance},
          {"a0_vs_a30_changed_ratio_min", 0.001},
          {"a30_vs_b30_changed_ratio_min", 0.05}}},
        {"entries", std::move(entryJson)},
        {"pairwise", std::move(pairwise)},
        {"cross_case", std::move(crossCase)},
        {"failure_count", failureCount},
        {"ok", failureCount == 0}};
    if (!writeJson(*outPath, document, error)) {
        return fail(error);
    }

    if (sheetPath) {
        std::vector<std::pair<std::string, FixtureImage>> sheetImages;
        for (const Entry& entry : entries) {
            sheetImages.emplace_back(
                entry.host + "/" + entry.api + "/" + entry.caseName,
                entry.viewport);
        }
        const FixtureImage sheet =
            engine::fixture::buildFixtureContactSheet(
                sheetImages,
                4,
                320,
                180);
        if (sheet.empty() ||
            !engine::fixture::saveFixturePng(*sheetPath, sheet, &error)) {
            return fail(
                error.empty() ? "contact sheet is empty" : error);
        }
    }

    if (failureCount != 0) {
        return fail(
            std::to_string(failureCount) +
            " capture-matrix check(s) failed; see " + *outPath);
    }
    std::cout
        << "[PublicAssetFixture][Guard] matrix=ok entries=" << entries.size()
        << " pairwise=" << pairwiseCount
        << " cross_case=" << crossCaseCount << '\n';
    return 0;
}

// Writes a hermetic synthetic capture and matching handshake. The CLI negative
// tests use it so validator coverage does not depend on GPU artifacts.
int runSynth(const std::vector<std::string>& arguments) {
    const auto outPng = optionValue(arguments, "--out-png=");
    const auto outHandshake = optionValue(arguments, "--out-handshake=");
    const auto host = optionValue(arguments, "--host=");
    const auto api = optionValue(arguments, "--api=");
    const auto caseName = optionValue(arguments, "--case=");
    if (!outPng || !outHandshake || !host || !api || !caseName) {
        return fail(
            "synth requires --out-png=, --out-handshake=, --host=, --api= and --case=");
    }
    if (*host != "runtime" && *host != "editor") {
        return fail("synth host must be runtime or editor");
    }
    if (*api != "opengl" && *api != "vulkan" && *api != "d3d12") {
        return fail("synth API must be opengl, vulkan or d3d12");
    }
    const std::optional<FixtureCase> parsedCase =
        engine::fixture::parseFixtureCase(*caseName);
    if (!parsedCase) {
        return fail("synth case is unknown: " + *caseName);
    }
    int width = 320;
    int height = 180;
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
    const std::string contentId =
        engine::fixture::fixtureCaseExpectedContentId(*parsedCase);
    const int logicalFrame =
        engine::fixture::fixtureCaseLogicalFrame(*parsedCase);
    FixtureImage image;
    std::map<std::string, engine::fixture::FixtureRoleRect> roleRects;
    std::string error;
    if (!engine::fixture::buildSyntheticFixtureCapture(
            contentId,
            logicalFrame,
            width,
            height,
            image,
            roleRects,
            &error)) {
        return fail(error);
    }
    if (std::find(arguments.begin(), arguments.end(), "--blank") !=
        arguments.end()) {
        for (std::size_t index = 0u; index < image.rgba.size(); index += 4u) {
            image.rgba[index + 0u] = 30u;
            image.rgba[index + 1u] = 30u;
            image.rgba[index + 2u] = 30u;
            image.rgba[index + 3u] = 255u;
        }
    }
    if (!engine::fixture::saveFixturePng(*outPng, image, &error)) {
        return fail(error);
    }

    engine::fixture::FixtureHandshake handshake;
    handshake.host = *host;
    handshake.caseName = *caseName;
    handshake.requestedBackend = *api;
    handshake.actualBackend = *api;
    handshake.gpu = "synthetic";
    handshake.gpuDiscrete = false;
    handshake.logicalFrame = logicalFrame;
    handshake.surfaceWidth = width;
    handshake.surfaceHeight = height;
    handshake.activeRoot =
        engine::fixture::fixtureCaseExpectedActiveRoot(*parsedCase);
    handshake.activeContentId = contentId;
    handshake.activeContentHash = "synthetic-" + contentId;
    handshake.archiveFileCount = 7u;
    handshake.activationCount =
        *parsedCase == FixtureCase::ValidReplacementB ? 2 : 1;
    handshake.triangleCount = 12u;
    handshake.preparationComplete = true;
    handshake.roleRects = std::move(roleRects);
    if (engine::fixture::fixtureCaseExpectsReplacementFailure(*parsedCase)) {
        handshake.transition.attempted = true;
        handshake.transition.failed = true;
        handshake.transition.requestedRoot =
            engine::fixture::fixtureCaseReplacementRoot(*parsedCase);
        handshake.transition.outcome = "graph_failed";
        handshake.transition.error = "synthetic missing required dependency";
    } else if (*parsedCase == FixtureCase::ValidReplacementB) {
        handshake.transition.attempted = true;
        handshake.transition.succeeded = true;
        handshake.transition.requestedRoot =
            engine::fixture::fixtureCaseReplacementRoot(*parsedCase);
        handshake.transition.outcome = "activated";
    }
    if (!engine::fixture::writeFixtureHandshake(
            *outHandshake, handshake, &error)) {
        return fail(error);
    }
    std::cout
        << "[PublicAssetFixture][Guard] synth=" << *outPng
        << " host=" << *host << " api=" << *api << " case=" << *caseName
        << '\n';
    return 0;
}

// Minimal deterministic child used by the harness negative tests to prove
// argument quoting, exit-code propagation and bounded termination.
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
    std::cout << "[PublicAssetFixture][Echo] " << *text << '\n' << std::flush;
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
            "usage: PublicAssetFixtureGuard detect|compare|expect|matrix|synth|echo ...");
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
