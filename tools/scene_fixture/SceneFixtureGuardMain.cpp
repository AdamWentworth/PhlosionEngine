// Verification CLI for the glTF scene fixture: expected-content checks, matrix
// validation and hermetic synthetic captures for the CPU-only adversarial
// harness.

#include "SceneFixtureGuard.h"

#include "FixtureImageGuard.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using engine::fixture::FixtureImage;
using engine::scene_fixture::FixtureCase;

struct Options {
    std::string command;
    std::string caseName;
    std::string host = "runtime";
    std::string api = "opengl";
    std::string outputPath;
    std::string handshakePath;
    std::string manifestPath;
    int width = engine::scene_fixture::fixtureDefaultSurfaceWidth();
    int height = engine::scene_fixture::fixtureDefaultSurfaceHeight();
    engine::scene_fixture::verification::ProbeDefect probeDefect =
        engine::scene_fixture::verification::ProbeDefect::None;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        const auto valueAfter = [&](const char* prefix) -> std::optional<std::string> {
            const std::string prefixText(prefix);
            if (argument.rfind(prefixText, 0u) == 0u) {
                return argument.substr(prefixText.size());
            }
            return std::nullopt;
        };
        if (const auto caseValue = valueAfter("--case=")) {
            options.caseName = *caseValue;
        } else if (const auto hostValue = valueAfter("--host=")) {
            options.host = *hostValue;
        } else if (const auto apiValue = valueAfter("--api=")) {
            options.api = *apiValue;
        } else if (const auto outValue = valueAfter("--out=")) {
            options.outputPath = *outValue;
        } else if (const auto handshakeValue = valueAfter("--handshake=")) {
            options.handshakePath = *handshakeValue;
        } else if (const auto manifestValue = valueAfter("--manifest=")) {
            options.manifestPath = *manifestValue;
        } else if (const auto widthValue = valueAfter("--width=")) {
            options.width = std::stoi(*widthValue);
        } else if (const auto heightValue = valueAfter("--height=")) {
            options.height = std::stoi(*heightValue);
        } else if (const auto defectValue = valueAfter("--defect=")) {
            if (!engine::scene_fixture::verification::parseProbeDefect(
                    *defectValue, options.probeDefect)) {
                throw std::runtime_error("unknown probe defect: " +
                                         *defectValue);
            }
        } else if (options.command.empty()) {
            options.command = argument;
        } else if (options.manifestPath.empty()) {
            options.manifestPath = argument;
        } else {
            throw std::runtime_error("unexpected scene fixture guard argument: " +
                                     argument);
        }
    }
    return options;
}

int fail(const std::string& message) {
    std::cerr << "[SceneFixtureGuard] FAILED " << message << '\n';
    return 1;
}

int runMatrix(const Options& options) {
    if (options.manifestPath.empty()) {
        return fail("matrix needs a manifest path");
    }
    engine::scene_fixture::verification::GuardResult result;
    std::string error;
    if (!engine::scene_fixture::verification::validateMatrix(
            options.manifestPath, result, &error)) {
        return fail(error);
    }
    for (const std::string& failure : result.failures) {
        std::cerr << "[SceneFixtureGuard] " << failure << '\n';
    }
    std::cout << "[SceneFixtureGuard] matrix ok=" << (result.ok ? 1 : 0)
              << " failures=" << result.failures.size() << '\n';
    return result.ok ? 0 : 1;
}

int runSynth(const Options& options) {
    const std::optional<FixtureCase> parsedCase =
        engine::scene_fixture::parseFixtureCase(options.caseName);
    if (!parsedCase) {
        return fail("synth needs a known --case");
    }
    if (options.outputPath.empty()) {
        return fail("synth needs --out");
    }
    FixtureImage image;
    std::string error;
    if (!engine::scene_fixture::verification::buildSyntheticCapture(
            *parsedCase, options.probeDefect, options.width, options.height,
            image, &error)) {
        return fail(error);
    }
    if (!engine::fixture::saveFixturePng(options.outputPath, image, &error)) {
        return fail(error);
    }
    if (!options.handshakePath.empty() &&
        !engine::scene_fixture::verification::writeSyntheticHandshake(
            options.handshakePath, *parsedCase, options.host, options.api,
            options.width, options.height, &error)) {
        return fail(error);
    }
    return 0;
}

int runExpect(const Options& options) {
    const std::optional<FixtureCase> parsedCase =
        engine::scene_fixture::parseFixtureCase(options.caseName);
    if (!parsedCase) {
        return fail("expect needs a known --case");
    }
    if (options.outputPath.empty()) {
        return fail("expect needs --out (the capture to inspect)");
    }
    FixtureImage image;
    std::string error;
    if (!engine::fixture::loadFixturePng(options.outputPath, image, &error)) {
        return fail(error);
    }
    engine::scene_fixture::verification::GuardResult result;
    if (!engine::scene_fixture::verification::validateCaptureContent(
            image, *parsedCase, result, &error)) {
        return fail(error);
    }
    for (const std::string& failure : result.failures) {
        std::cerr << "[SceneFixtureGuard] " << failure << '\n';
    }
    std::cout << "[SceneFixtureGuard] expect ok=" << (result.ok ? 1 : 0) << '\n';
    return result.ok ? 0 : 1;
}

// Writes a background-only capture. The adversarial harness uses it to prove a
// capture that lost its scene content is rejected.
int runBlank(const Options& options) {
    if (options.outputPath.empty()) {
        return fail("blank needs --out");
    }
    FixtureImage image;
    std::string error;
    if (!engine::scene_fixture::verification::buildSyntheticCapture(
            FixtureCase::Unloaded, options.width, options.height, image,
            &error)) {
        return fail(error);
    }
    if (!engine::fixture::saveFixturePng(options.outputPath, image, &error)) {
        return fail(error);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.command == "matrix") {
            return runMatrix(options);
        }
        if (options.command == "synth") {
            return runSynth(options);
        }
        if (options.command == "expect") {
            return runExpect(options);
        }
        if (options.command == "blank") {
            return runBlank(options);
        }
        if (options.command == "echo") {
            std::cout << "[SceneFixtureGuard] echo\n";
            return 0;
        }
        std::cerr << "usage: SceneFixtureGuard <matrix|synth|expect|blank|echo> "
                     "[--case=] [--host=] [--api=] [--out=] [--handshake=] "
                     "[--manifest=] [--width=] [--height=]\n";
        return 2;
    } catch (const std::exception& exception) {
        return fail(exception.what());
    }
}
