// Standalone material fixture host: creates a hidden native window, selects the
// requested backend without fallback, loads the authored glTF through the
// production loader and the existing CPU decode helpers, and presents the
// eight-patch material layout for the requested case.

#define SDL_MAIN_HANDLED

#include "MaterialFixtureAssets.h"
#include "MaterialFixturePresentation.h"

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

using engine::material_fixture::FixtureCase;
using engine::material_fixture::FixtureTextureSet;

struct Arguments {
    std::string api = "opengl";
    std::string caseName = "authored-maps";
    std::string handshakePath;
    int width = 640;
    int height = 360;
    std::optional<int> logicalFrame;
    int screenshotFrame = 4;
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
        throw std::runtime_error(std::string("invalid integer for ") + name + ": " +
                                 text);
    }
}

Arguments parseArguments(int argc, char** argv) {
    Arguments arguments;
    if (const std::string envApi = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_API");
        !envApi.empty()) {
        arguments.api = envApi;
    }
    if (const std::string envCase = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_CASE");
        !envCase.empty()) {
        arguments.caseName = envCase;
    }
    if (const std::string envHandshake =
            envOrEmpty("PHLOSION_MATERIAL_FIXTURE_HANDSHAKE");
        !envHandshake.empty()) {
        arguments.handshakePath = envHandshake;
    }
    if (const std::string envWidth = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_WIDTH");
        !envWidth.empty()) {
        arguments.width = parseInt(envWidth, "PHLOSION_MATERIAL_FIXTURE_WIDTH");
    }
    if (const std::string envHeight =
            envOrEmpty("PHLOSION_MATERIAL_FIXTURE_HEIGHT");
        !envHeight.empty()) {
        arguments.height = parseInt(envHeight, "PHLOSION_MATERIAL_FIXTURE_HEIGHT");
    }
    if (const std::string envFrame =
            envOrEmpty("PHLOSION_MATERIAL_FIXTURE_LOGICAL_FRAME");
        !envFrame.empty()) {
        arguments.logicalFrame =
            parseInt(envFrame, "PHLOSION_MATERIAL_FIXTURE_LOGICAL_FRAME");
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
                "unknown material fixture runtime argument: " + argument);
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
        graphics.window = std::make_unique<Window>("Phlosion Material Fixture",
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
        graphics.window = std::make_unique<Window>("Phlosion Material Fixture",
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
        throw std::runtime_error("unknown material fixture backend: " +
                                 arguments.api);
    }
    graphics.window = std::make_unique<Window>("Phlosion Material Fixture",
                                               arguments.width,
                                               arguments.height,
                                               Window::GraphicsApi::OpenGL,
                                               false,
                                               !arguments.hidden);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error(
            "Failed to initialize GLAD for the material fixture runtime.");
    }
    graphics.renderer = std::make_unique<OpenGLRenderBackend>();
    return graphics;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parseArguments(argc, argv);
        const std::optional<FixtureCase> parsedCase =
            engine::material_fixture::parseFixtureCase(arguments.caseName);
        if (!parsedCase) {
            throw std::runtime_error("unknown material fixture case: " +
                                     arguments.caseName);
        }
        // A requested logical frame must be the case's authored frame; silently
        // ignoring it would let a mislabelled capture look valid.
        const int catalogFrame =
            engine::material_fixture::fixtureCaseLogicalFrame(*parsedCase);
        if (arguments.logicalFrame && *arguments.logicalFrame != catalogFrame) {
            throw std::runtime_error(
                "material fixture logical frame " +
                std::to_string(*arguments.logicalFrame) +
                " does not match the authored frame " +
                std::to_string(catalogFrame) + " for case " +
                arguments.caseName);
        }

        FixtureTextureSet textures;
        std::string error;
        if (!engine::material_fixture::decodeFixtureTextures(
                *parsedCase, textures, &error)) {
            throw std::runtime_error("material fixture inputs failed: " + error);
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
            engine::material_fixture::fixtureBackendName(*graphics.renderer);
        if (actualBackend != arguments.api) {
            throw std::runtime_error("requested backend " + arguments.api +
                                     " resolved to " + actualBackend);
        }

        engine::material_fixture::FixtureDrawStats stats;
        const int totalFrames = arguments.screenshotFrame + 2;
        for (int frame = 1; frame <= totalFrames; ++frame) {
            graphics.renderer->beginFrame(0.02f, 0.02f, 0.02f, 1.0f);
            graphics.renderer->beginWorldSceneColorPass(width, height);
            std::string renderError;
            if (!engine::material_fixture::renderMaterialFixture(
                    textures,
                    *graphics.renderer,
                    width,
                    height,
                    &stats,
                    &renderError)) {
                throw std::runtime_error("material fixture render failed: " +
                                         renderError);
            }
            graphics.renderer->endWorldSceneColorPass();
            graphics.renderer->endFrame();
            if (!graphics.renderer->handlesPresentation()) {
                graphics.window->swapBuffers();
            }
        }

        const engine::material_fixture::FixtureHandshake handshake =
            engine::material_fixture::buildFixtureHandshake("runtime",
                                                            arguments.api,
                                                            textures,
                                                            *graphics.renderer,
                                                            width,
                                                            height,
                                                            stats);
        if (!arguments.handshakePath.empty() &&
            !engine::material_fixture::writeFixtureHandshake(
                arguments.handshakePath, handshake, &error)) {
            throw std::runtime_error("material fixture handshake write failed: " +
                                     error);
        }
        std::cout << "[MaterialFixture][Runtime] api=" << handshake.actualBackend
                  << " case=" << handshake.caseName
                  << " frame=" << handshake.logicalFrame
                  << " size=" << width << "x" << height << " gpu=\""
                  << handshake.gpu << "\"\n";
        graphics.renderer->shutdown();
        graphics.renderer.reset();
        graphics.window.reset();
        SDL_Quit();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[MaterialFixture][Runtime] FAILED " << exception.what()
                  << '\n';
        SDL_Quit();
        return 1;
    }
}
