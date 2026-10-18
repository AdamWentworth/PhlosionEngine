#include "engine/render/RendererPolicy.h"
#include "engine/render/LegacyOpenGL.h"
// Standalone animation fixture host: creates a hidden native window, selects
// the requested backend without fallback, presents the bind pose and then the
// requested case's pose through the same geometry and palette storage.

#define SDL_MAIN_HANDLED

#include "AnimationFixturePresentation.h"

#include "engine/core/Environment.h"
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using engine::animation_fixture::FixtureCase;
using engine::animation_fixture::FixturePaletteStorage;
using engine::animation_fixture::FixturePaletteTransition;
using engine::animation_fixture::FixturePose;

constexpr int kBindFrames = 2;

struct Arguments {
    std::string api = "auto";
    std::string caseName = "bind";
    std::string handshakePath;
    int width = 640;
    int height = 360;
    std::optional<int> logicalFrame;
    int screenshotFrame = 6;
    bool hidden = true;
};

std::string envOrEmpty(const char* name) {
    const std::optional<std::string> value = engine::env::get(name);
    return value ? *value : std::string{};
}

int parseInt(const std::string& text, const char* name) {
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        throw std::runtime_error(
            std::string("invalid integer for ") + name + ": " + text);
    }
}

Arguments parseArguments(int argc, char** argv) {
    Arguments arguments;
    if (const std::string envApi = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_API");
        !envApi.empty()) {
        arguments.api = envApi;
    }
    if (const std::string envCase = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_CASE");
        !envCase.empty()) {
        arguments.caseName = envCase;
    }
    if (const std::string envHandshake =
            envOrEmpty("PHLOSION_ANIMATION_FIXTURE_HANDSHAKE");
        !envHandshake.empty()) {
        arguments.handshakePath = envHandshake;
    }
    if (const std::string envWidth = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_WIDTH");
        !envWidth.empty()) {
        arguments.width = parseInt(envWidth, "PHLOSION_ANIMATION_FIXTURE_WIDTH");
    }
    if (const std::string envHeight = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_HEIGHT");
        !envHeight.empty()) {
        arguments.height = parseInt(envHeight, "PHLOSION_ANIMATION_FIXTURE_HEIGHT");
    }
    if (const std::string envFrame =
            envOrEmpty("PHLOSION_ANIMATION_FIXTURE_LOGICAL_FRAME");
        !envFrame.empty()) {
        arguments.logicalFrame =
            parseInt(envFrame, "PHLOSION_ANIMATION_FIXTURE_LOGICAL_FRAME");
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        const auto valueAfter = [&](const char* prefix) -> std::optional<std::string> {
            const std::string prefixText(prefix);
            if (argument.rfind(prefixText, 0u) == 0u) {
                return argument.substr(prefixText.size());
            }
            return std::nullopt;
        };
        if (const auto apiValue = valueAfter("--api=")) {
            arguments.api = *apiValue;
        } else if (const auto caseValue = valueAfter("--case=")) {
            arguments.caseName = *caseValue;
        } else if (const auto handshakeValue = valueAfter("--handshake=")) {
            arguments.handshakePath = *handshakeValue;
        } else if (const auto widthValue = valueAfter("--width=")) {
            arguments.width = parseInt(*widthValue, "--width");
        } else if (const auto heightValue = valueAfter("--height=")) {
            arguments.height = parseInt(*heightValue, "--height");
        } else if (const auto frameValue = valueAfter("--logical-frame=")) {
            arguments.logicalFrame = parseInt(*frameValue, "--logical-frame");
        } else if (const auto screenshotValue = valueAfter("--screenshot-frame=")) {
            arguments.screenshotFrame =
                parseInt(*screenshotValue, "--screenshot-frame");
        } else if (argument == "--visible") {
            arguments.hidden = false;
        } else if (!argument.empty()) {
            throw std::runtime_error(
                "unknown animation fixture runtime argument: " + argument);
        }
    }
    arguments.width = std::clamp(arguments.width, 64, 4096);
    arguments.height = std::clamp(arguments.height, 64, 4096);
    arguments.screenshotFrame = std::clamp(arguments.screenshotFrame, 1, 10000);
    return arguments;
}

struct Graphics {
    std::unique_ptr<Window> window;
    std::unique_ptr<IRenderBackend> renderer;
};

Graphics createGraphics(const Arguments& arguments) {
    Graphics graphics;
    if (arguments.api == "d3d12") {
#if defined(_WIN32)
        graphics.window = std::make_unique<Window>(
            "Phlosion Animation Fixture",
            arguments.width,
            arguments.height,
            Window::GraphicsApi::Native,
            false,
            !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<D3D12RenderBackend>(
            graphics.window->getSDLWindow(),
            std::max(1, width),
            std::max(1, height),
            false);
        return graphics;
#else
        throw std::runtime_error("D3D12 is only available on Windows.");
#endif
    }
    if (arguments.api == "vulkan") {
        graphics.window = std::make_unique<Window>(
            "Phlosion Animation Fixture",
            arguments.width,
            arguments.height,
            Window::GraphicsApi::Vulkan,
            false,
            !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<VulkanRenderBackend>(
            graphics.window->getSDLWindow(),
            std::max(1, width),
            std::max(1, height),
            false);
        return graphics;
    }
    if (arguments.api != "opengl") {
        throw std::runtime_error(
            "unknown animation fixture backend: " + arguments.api);
    }
    graphics.window = std::make_unique<Window>(
        "Phlosion Animation Fixture",
        arguments.width,
        arguments.height,
        Window::GraphicsApi::OpenGL,
        false,
        !arguments.hidden);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error(
            "Failed to initialize GLAD for the animation fixture runtime.");
    }
    graphics.renderer = engine::render::createLegacyOpenGLBackend();
    return graphics;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Arguments arguments = parseArguments(argc, argv);
        arguments.api = engine::render::rendererCandidates(engine::render::defaultRendererPolicy(), arguments.api).front();
        const std::optional<FixtureCase> parsedCase =
            engine::animation_fixture::parseFixtureCase(arguments.caseName);
        if (!parsedCase) {
            throw std::runtime_error(
                "unknown animation fixture case: " + arguments.caseName);
        }
        const int logicalFrame = arguments.logicalFrame.value_or(
            engine::animation_fixture::fixtureCaseLogicalFrame(*parsedCase));
        if (logicalFrame < 0) {
            throw std::runtime_error(
                "animation fixture logical frame must be non-negative.");
        }

        Graphics graphics = createGraphics(arguments);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        width = std::max(1, width);
        height = std::max(1, height);
        graphics.renderer->onResize(width, height);
        graphics.renderer->prewarmWorldRenderAssets();

        const std::string actualBackend =
            engine::animation_fixture::fixtureBackendName(*graphics.renderer);
        if (actualBackend != arguments.api) {
            throw std::runtime_error(
                "requested backend " + arguments.api + " resolved to " +
                actualBackend);
        }

        FixturePose bindPose;
        FixturePose requestedPose;
        std::string error;
        if (!engine::animation_fixture::buildFixturePose(
                FixtureCase::Bind, bindPose, &error)) {
            throw std::runtime_error(
                "animation fixture bind pose failed: " + error);
        }
        if (!engine::animation_fixture::buildFixturePose(
                *parsedCase, requestedPose, &error)) {
            throw std::runtime_error(
                "animation fixture pose failed: " + error);
        }

        // One palette allocation is reused for the whole run; the hosts only
        // rewrite its values between the bind and requested-pose draws.
        FixturePaletteStorage drawPalette(
            engine::animation_fixture::fixtureRig()
                .skins[engine::animation_fixture::fixtureRig().skinIndex]
                .joints.size());
        FixturePaletteTransition transition;
        drawPalette.setFrom(bindPose);
        const std::vector<float> bindPalette = drawPalette.snapshot();

        const int totalFrames = arguments.screenshotFrame + 2;
        int poseFrames = 0;
        bool bindDrawRecorded = false;
        bool poseDrawRecorded = false;
        for (int frame = 1; frame <= totalFrames; ++frame) {
            const bool presentPose = frame > kBindFrames;
            if (!presentPose) {
                drawPalette.setFrom(bindPose);
                if (!bindDrawRecorded) {
                    transition.recordBindDraw(drawPalette);
                    bindDrawRecorded = true;
                }
            } else {
                drawPalette.setFrom(requestedPose);
                if (!poseDrawRecorded) {
                    transition.recordPoseDraw(drawPalette, bindPalette);
                    poseDrawRecorded = true;
                }
                ++poseFrames;
            }
            graphics.renderer->beginFrame(0.02f, 0.02f, 0.02f, 1.0f);
            graphics.renderer->beginWorldSceneColorPass(width, height);
            std::string renderError;
            engine::animation_fixture::FixtureDrawStats stats;
            if (!engine::animation_fixture::renderAnimationFixture(
                    engine::animation_fixture::fixtureRig(),
                    drawPalette,
                    *graphics.renderer,
                    width,
                    height,
                    &stats,
                    &renderError)) {
                throw std::runtime_error(
                    "animation fixture render failed: " + renderError);
            }
            graphics.renderer->endWorldSceneColorPass();
            graphics.renderer->endFrame();
            if (!graphics.renderer->handlesPresentation()) {
                graphics.window->swapBuffers();
            }
        }
        if (poseFrames <= 0) {
            throw std::runtime_error(
                "animation fixture runtime never presented the requested pose.");
        }

        const engine::animation_fixture::FixtureHandshake handshake =
            engine::animation_fixture::buildFixtureHandshake(
                "runtime",
                arguments.api,
                *graphics.renderer,
                requestedPose,
                bindPose,
                transition,
                width,
                height,
                kBindFrames,
                poseFrames);
        if (!arguments.handshakePath.empty() &&
            !engine::animation_fixture::writeFixtureHandshake(
                arguments.handshakePath, handshake, &error)) {
            throw std::runtime_error(
                "animation fixture handshake write failed: " + error);
        }
        std::cout
            << "[AnimationFixture][Runtime] api=" << handshake.actualBackend
            << " case=" << handshake.caseName
            << " time=" << handshake.sampleTimeSec
            << " frame=" << handshake.logicalFrame
            << " size=" << width << "x" << height
            << " gpu=\"" << handshake.gpu << "\"\n";
        graphics.renderer->shutdown();
        graphics.renderer.reset();
        graphics.window.reset();
        SDL_Quit();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[AnimationFixture][Runtime] FAILED " << exception.what()
                  << '\n';
        SDL_Quit();
        return 1;
    }
}
