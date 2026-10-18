#include "engine/render/RendererPolicy.h"
#include "engine/render/LegacyOpenGL.h"
// Standalone public fixture host: creates a hidden native window, activates the
// synthetic archive through PhlosionArchiveSession and presents the decoded
// snapshot through the selected native renderer.

#define SDL_MAIN_HANDLED

#include "FixturePresentation.h"

#include "engine/core/Environment.h"
#include "engine/platform/Window.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using engine::fixture::FixtureCase;
using engine::fixture::FixtureSession;

struct Arguments {
    std::string api = "auto";
    std::string fixtureCaseName = "a-frame0";
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
        throw std::runtime_error(std::string("invalid integer for ") + name + ": " + text);
    }
}

Arguments parseArguments(int argc, char** argv) {
    Arguments arguments;
    if (const std::string envApi = envOrEmpty("PHLOSION_FIXTURE_API"); !envApi.empty()) {
        arguments.api = envApi;
    }
    if (const std::string envCase = envOrEmpty("PHLOSION_FIXTURE_CASE"); !envCase.empty()) {
        arguments.fixtureCaseName = envCase;
    }
    if (const std::string envHandshake = envOrEmpty("PHLOSION_FIXTURE_HANDSHAKE"); !envHandshake.empty()) {
        arguments.handshakePath = envHandshake;
    }
    if (const std::string envWidth = envOrEmpty("PHLOSION_FIXTURE_WIDTH"); !envWidth.empty()) {
        arguments.width = parseInt(envWidth, "PHLOSION_FIXTURE_WIDTH");
    }
    if (const std::string envHeight = envOrEmpty("PHLOSION_FIXTURE_HEIGHT"); !envHeight.empty()) {
        arguments.height = parseInt(envHeight, "PHLOSION_FIXTURE_HEIGHT");
    }
    if (const std::string envFrame = envOrEmpty("PHLOSION_FIXTURE_LOGICAL_FRAME"); !envFrame.empty()) {
        arguments.logicalFrame = parseInt(envFrame, "PHLOSION_FIXTURE_LOGICAL_FRAME");
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
            arguments.fixtureCaseName = *caseValue;
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
            throw std::runtime_error("unknown fixture runtime argument: " + argument);
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
            "Phlosion Public Asset Fixture",
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
            "Phlosion Public Asset Fixture",
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
            "unknown fixture runtime backend: " + arguments.api);
    }
    graphics.window = std::make_unique<Window>(
        "Phlosion Public Asset Fixture",
        arguments.width,
        arguments.height,
        Window::GraphicsApi::OpenGL,
        false,
        !arguments.hidden);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error("Failed to initialize GLAD for the fixture runtime.");
    }
    graphics.renderer = engine::render::createLegacyOpenGLBackend();
    return graphics;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Arguments arguments = parseArguments(argc, argv);
        arguments.api = engine::render::rendererCandidates(engine::render::defaultRendererPolicy(), arguments.api).front();
        const std::optional<FixtureCase> fixtureCase =
            engine::fixture::parseFixtureCase(arguments.fixtureCaseName);
        if (!fixtureCase) {
            throw std::runtime_error(
                "unknown fixture case: " + arguments.fixtureCaseName);
        }
        const int logicalFrame = arguments.logicalFrame.value_or(
            engine::fixture::fixtureCaseLogicalFrame(*fixtureCase));
        if (logicalFrame < 0) {
            throw std::runtime_error("fixture logical frame must be non-negative.");
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
            engine::fixture::fixtureBackendName(*graphics.renderer);
        if (actualBackend != arguments.api) {
            throw std::runtime_error(
                "requested backend " + arguments.api +
                " resolved to " + actualBackend);
        }

        FixtureSession session(*fixtureCase);
        std::string error;
        if (!session.prepare(&error)) {
            throw std::runtime_error("fixture preparation failed: " + error);
        }

        const int replacementFrame = 2;
        const int totalFrames = arguments.screenshotFrame + 2;
        bool replacementAttempted = false;
        for (int frame = 1; frame <= totalFrames; ++frame) {
            graphics.renderer->beginFrame(0.02f, 0.02f, 0.02f, 1.0f);
            graphics.renderer->beginWorldSceneColorPass(width, height);
            if (!replacementAttempted &&
                engine::fixture::fixtureCaseReplaces(*fixtureCase) &&
                frame >= replacementFrame) {
                replacementAttempted = true;
                std::string replacementError;
                if (!session.attemptReplacement(&replacementError)) {
                    throw std::runtime_error(
                        "fixture replacement failed: " + replacementError);
                }
            }
            std::string renderError;
            engine::fixture::FixtureDrawStats stats;
            if (!engine::fixture::renderFixture(
                    session.activeContent(),
                    *graphics.renderer,
                    logicalFrame,
                    width,
                    height,
                    &stats,
                    &renderError)) {
                throw std::runtime_error("fixture render failed: " + renderError);
            }
            graphics.renderer->endWorldSceneColorPass();
            graphics.renderer->endFrame();
            if (!graphics.renderer->handlesPresentation()) {
                graphics.window->swapBuffers();
            }
        }

        if (engine::fixture::fixtureCaseReplaces(*fixtureCase) &&
            !session.transition().attempted) {
            throw std::runtime_error("fixture replacement case never attempted its replacement.");
        }

        const engine::fixture::FixtureHandshake handshake =
            engine::fixture::buildFixtureHandshake(
                "runtime",
                arguments.api,
                *graphics.renderer,
                session,
                logicalFrame,
                width,
                height);
        if (!arguments.handshakePath.empty() &&
            !engine::fixture::writeFixtureHandshake(
                arguments.handshakePath,
                handshake,
                &error)) {
            throw std::runtime_error("fixture handshake write failed: " + error);
        }
        std::cout
            << "[PublicAssetFixture][Runtime] api=" << handshake.actualBackend
            << " case=" << handshake.caseName
            << " frame=" << handshake.logicalFrame
            << " active=" << handshake.activeRoot
            << " content=" << handshake.activeContentId
            << " hash=" << handshake.activeContentHash
            << " transition=" << handshake.transition.outcome
            << " size=" << width << "x" << height
            << " gpu=\"" << handshake.gpu << "\"\n";
        graphics.renderer->shutdown();
        graphics.renderer.reset();
        graphics.window.reset();
        SDL_Quit();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[PublicAssetFixture][Runtime] FAILED " << exception.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
