// Standalone glTF scene fixture host: creates a hidden native window, selects
// the requested backend without fallback, drives the shared scene session and
// writes the capture handshake. The host owns only presentation and lifecycle
// choices; CPU import, palette composition and draw binding come from the
// engine scene path.

#define SDL_MAIN_HANDLED

#include "SceneFixtureCase.h"
#include "SceneFixturePresentation.h"

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

struct Arguments {
    std::string api = "opengl";
    std::string caseName = "a-bind";
    std::string handshakePath;
    int width = engine::scene_fixture::fixtureDefaultSurfaceWidth();
    int height = engine::scene_fixture::fixtureDefaultSurfaceHeight();
    std::optional<int> logicalFrame;
    int screenshotFrame = engine::scene_fixture::fixtureCaptureFrame();
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
        throw std::runtime_error(std::string("invalid integer for ") + name +
                                 ": " + text);
    }
}

Arguments parseArguments(int argc, char** argv) {
    Arguments arguments;
    if (const std::string envApi = envOrEmpty("PHLOSION_SCENE_FIXTURE_API");
        !envApi.empty()) {
        arguments.api = envApi;
    }
    if (const std::string envCase = envOrEmpty("PHLOSION_SCENE_FIXTURE_CASE");
        !envCase.empty()) {
        arguments.caseName = envCase;
    }
    if (const std::string envHandshake =
            envOrEmpty("PHLOSION_SCENE_FIXTURE_HANDSHAKE");
        !envHandshake.empty()) {
        arguments.handshakePath = envHandshake;
    }
    if (const std::string envWidth = envOrEmpty("PHLOSION_SCENE_FIXTURE_WIDTH");
        !envWidth.empty()) {
        arguments.width = parseInt(envWidth, "PHLOSION_SCENE_FIXTURE_WIDTH");
    }
    if (const std::string envHeight = envOrEmpty("PHLOSION_SCENE_FIXTURE_HEIGHT");
        !envHeight.empty()) {
        arguments.height = parseInt(envHeight, "PHLOSION_SCENE_FIXTURE_HEIGHT");
    }
    if (const std::string envFrame =
            envOrEmpty("PHLOSION_SCENE_FIXTURE_LOGICAL_FRAME");
        !envFrame.empty()) {
        arguments.logicalFrame =
            parseInt(envFrame, "PHLOSION_SCENE_FIXTURE_LOGICAL_FRAME");
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
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
        } else if (const auto screenshotValue =
                       valueAfter("--screenshot-frame=")) {
            arguments.screenshotFrame =
                parseInt(*screenshotValue, "--screenshot-frame");
        } else if (argument == "--visible") {
            arguments.hidden = false;
        } else if (!argument.empty()) {
            throw std::runtime_error(
                "unknown scene fixture runtime argument: " + argument);
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
        graphics.window = std::make_unique<Window>("Phlosion Scene Fixture",
                                                   arguments.width,
                                                   arguments.height,
                                                   Window::GraphicsApi::Native,
                                                   false,
                                                   !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<D3D12RenderBackend>(
            graphics.window->getSDLWindow(), std::max(1, width),
            std::max(1, height), false);
        return graphics;
#else
        throw std::runtime_error("D3D12 is only available on Windows.");
#endif
    }
    if (arguments.api == "vulkan") {
        graphics.window = std::make_unique<Window>("Phlosion Scene Fixture",
                                                   arguments.width,
                                                   arguments.height,
                                                   Window::GraphicsApi::Vulkan,
                                                   false,
                                                   !arguments.hidden);
        int width = 0;
        int height = 0;
        graphics.window->getDrawableSize(width, height);
        graphics.renderer = std::make_unique<VulkanRenderBackend>(
            graphics.window->getSDLWindow(), std::max(1, width),
            std::max(1, height), false);
        return graphics;
    }
    if (arguments.api != "opengl") {
        throw std::runtime_error("unknown scene fixture backend: " +
                                 arguments.api);
    }
    graphics.window = std::make_unique<Window>("Phlosion Scene Fixture",
                                               arguments.width,
                                               arguments.height,
                                               Window::GraphicsApi::OpenGL,
                                               false,
                                               !arguments.hidden);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error(
            "Failed to initialize GLAD for the scene fixture runtime.");
    }
    graphics.renderer = std::make_unique<OpenGLRenderBackend>();
    return graphics;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parseArguments(argc, argv);
        const std::optional<engine::scene_fixture::FixtureCase> parsedCase =
            engine::scene_fixture::parseFixtureCase(arguments.caseName);
        if (!parsedCase) {
            throw std::runtime_error("unknown scene fixture case: " +
                                     arguments.caseName);
        }
        const int catalogFrame =
            engine::scene_fixture::fixtureCaseLogicalFrame(*parsedCase);
        if (arguments.logicalFrame && *arguments.logicalFrame != catalogFrame) {
            throw std::runtime_error(
                "scene fixture logical frame " +
                std::to_string(*arguments.logicalFrame) +
                " does not match the authored frame " +
                std::to_string(catalogFrame) + " for case " +
                arguments.caseName);
        }

        engine::scene_fixture::FixtureSession session;
        std::string error;
        if (!engine::scene_fixture::prepareFixtureSession(*parsedCase, session,
                                                          &error)) {
            throw std::runtime_error("scene fixture session failed: " + error);
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
            engine::scene_fixture::fixtureBackendName(*graphics.renderer);
        if (actualBackend != arguments.api) {
            throw std::runtime_error("requested backend " + arguments.api +
                                     " resolved to " + actualBackend);
        }

        engine::scene_fixture::FixtureDrawStats stats;
        const int totalFrames = arguments.screenshotFrame + 2;
        engine::scene_fixture::FixtureHandshake handshake;
        const std::array<float, 3> background =
            engine::scene_fixture::fixtureBackgroundLinearColor();
        for (int frame = 1; frame <= totalFrames; ++frame) {
            if (!engine::scene_fixture::advanceFixtureSession(session, frame,
                                                              &error)) {
                throw std::runtime_error("scene fixture lifecycle failed: " +
                                         error);
            }
            graphics.renderer->beginFrame(background[0], background[1],
                                          background[2], 1.0f);
            graphics.renderer->beginWorldSceneColorPass(width, height);
            if (!engine::scene_fixture::drawFixtureSession(
                    *graphics.renderer, session, width, height, &stats,
                    &error)) {
                throw std::runtime_error("scene fixture render failed: " +
                                         error);
            }
            graphics.renderer->endWorldSceneColorPass();
            graphics.renderer->endFrame();
            if (frame == arguments.screenshotFrame) {
                handshake = engine::scene_fixture::buildFixtureHandshake(
                    "runtime", arguments.api, session, stats, width, height,
                    *graphics.renderer);
            }
            if (!graphics.renderer->handlesPresentation()) {
                graphics.window->swapBuffers();
            }
        }

        if (!arguments.handshakePath.empty() &&
            !engine::scene_fixture::writeFixtureHandshake(
                arguments.handshakePath, handshake, &error)) {
            throw std::runtime_error("scene fixture handshake write failed: " +
                                     error);
        }
        std::cout << "[SceneFixture][Runtime] api=" << handshake.actualBackend
                  << " case=" << handshake.caseName
                  << " frame=" << handshake.logicalFrame
                  << " size=" << width << "x" << height << " scene_drawn="
                  << (handshake.sceneDrawn ? 1 : 0) << " unloaded="
                  << (handshake.unloaded ? 1 : 0) << " gpu=\""
                  << handshake.gpu << "\"\n";
        graphics.renderer->shutdown();
        graphics.renderer.reset();
        graphics.window.reset();
        SDL_Quit();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[SceneFixture][Runtime] FAILED " << exception.what()
                  << '\n';
        SDL_Quit();
        return 1;
    }
}
