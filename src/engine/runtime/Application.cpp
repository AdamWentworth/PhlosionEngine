// src/engine/runtime/Application.cpp

#include "engine/runtime/Application.h"
#include "engine/runtime/FixedStep.h"
#include "engine/platform/Window.h"

#include "engine/core/GameLoop.h"
#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"

#include "engine/input/InputEvent.h"
#include "engine/input/SdlKeyMap.h"

#include "engine/render/IRenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/ui/BootLoadingView.h"

#include "engine/utils/LogSink.h"
#include "engine/utils/ResourceManager.h"

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <iostream>
#include <exception>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;

    engine::log::Sink& applicationLog() {
        static engine::log::Sink log("Application", &std::cout, &std::cerr);
        return log;
    }

    static int scaledMouseX(int x, float s) { return (int)std::lround((float)x * s); }
    static int scaledMouseY(int y, float s) { return (int)std::lround((float)y * s); }

    static InputEvent::MouseButton mapSdlMouseButtonToEngineButton(int sdlButton) {
        switch (sdlButton) {
            case SDL_BUTTON_LEFT:   return InputEvent::MouseButton::Left;
            case SDL_BUTTON_MIDDLE: return InputEvent::MouseButton::Middle;
            case SDL_BUTTON_RIGHT:  return InputEvent::MouseButton::Right;
            case SDL_BUTTON_X1:     return InputEvent::MouseButton::X1;
            case SDL_BUTTON_X2:     return InputEvent::MouseButton::X2;
            default:
                return InputEvent::MouseButton::Unknown;
        }
    }

    static bool translateSdlToInputEvent(
        const SDL_Event& sdl,
        float mouseScaleX, float mouseScaleY,
        int windowW, int windowH,
        int drawableW, int drawableH,
        InputEvent& out
    ) {
        switch (sdl.type) {
            case SDL_QUIT:
                out = InputEvent::QuitEvent();
                return true;

            case SDL_WINDOWEVENT:
                if (sdl.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdl.window.event == SDL_WINDOWEVENT_RESIZED) {
                    out = InputEvent::ResizeEvent(windowW, windowH, drawableW, drawableH);
                    return true;
                }
                return false;

            case SDL_KEYDOWN:
                out = InputEvent::KeyDownEvent(engine::input::mapSdlKeyToEngineKey((int)sdl.key.keysym.sym), sdl.key.repeat != 0);
                return true;

            case SDL_KEYUP:
                out = InputEvent::KeyUpEvent(engine::input::mapSdlKeyToEngineKey((int)sdl.key.keysym.sym));
                return true;

            case SDL_MOUSEMOTION: {
                int mx = scaledMouseX(sdl.motion.x, mouseScaleX);
                int my = scaledMouseY(sdl.motion.y, mouseScaleY);
                out = InputEvent::MouseMoveEvent(mx, my);
                return true;
            }

            case SDL_MOUSEBUTTONDOWN: {
                int mx = scaledMouseX(sdl.button.x, mouseScaleX);
                int my = scaledMouseY(sdl.button.y, mouseScaleY);
                out = InputEvent::MouseDownEvent(mx, my, mapSdlMouseButtonToEngineButton((int)sdl.button.button));
                return true;
            }

            case SDL_MOUSEBUTTONUP: {
                int mx = scaledMouseX(sdl.button.x, mouseScaleX);
                int my = scaledMouseY(sdl.button.y, mouseScaleY);
                out = InputEvent::MouseUpEvent(mx, my, mapSdlMouseButtonToEngineButton((int)sdl.button.button));
                return true;
            }

            case SDL_MOUSEWHEEL:
                out = InputEvent::MouseWheelEvent((int)sdl.wheel.x, (int)sdl.wheel.y);
                return true;

            default:
                return false;
        }
    }
}

Application::Application() {
    initialized = initApplication();
}

Application::~Application() {
    shutdownApplication();
}

bool Application::initApplication() {
    // Window ctor performs SDL_Init + creates GL context.
    try {
        window = std::make_unique<Window>("Pokemon Autochess", (int)START_W, (int)START_H);
    } catch (const std::exception& ex) {
        applicationLog().error(std::string("[Application] Window init failed: ") + ex.what());
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        applicationLog().error("[Application] Failed to initialize GLAD");
        return false;
    }

    // Wire engine-owned shader cache BEFORE anything calls shaderCache.get()
    // TTF depends on SDL being initialized (now true because Window ctor did SDL_Init).
    if (TTF_Init() == -1) {
        applicationLog().warn(std::string("[Application] TTF_Init error: ") + TTF_GetError());
    }

    updateDrawableSizeAndViewport();
    updateMouseScale();

    bootLoadingView = std::make_unique<BootLoadingView>();
    bootLoadingView->init(shaderCache);

    // show a first frame so the window looks alive
    setTitle("PokemonAutochess - Loading...");
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    swapBuffers();
    pumpPreloadEvents();

    renderer = std::make_unique<OpenGLRenderBackend>();
    renderer->onResize(drawableW, drawableH);
    camera   = std::make_unique<Camera3D>(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);

    applicationLog().info("[Init] Application initialized.");
    return true;
}

void Application::shutdownApplication() {
    applicationLog().info("[Shutdown] Application...");

    // 1) Destroy anything that might issue GL calls while the context is still alive.
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }

    // 2) Force-release GL-backed caches BEFORE destroying the window/context.
        shaderCache.clear();

    resourceManager.clear();
    bootLoadingView.reset();
    camera.reset();

    // 3) Destroy the GL context + window
    window.reset();

    // 4) Tear down SDL subsystems last
    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0) {
        TTF_Quit();
        SDL_Quit();
    }

    applicationLog().info("[Shutdown] Application done.");
}

void Application::updateDrawableSizeAndViewport() {
    if (!window || !window->getSDLWindow()) return;

    SDL_GetWindowSize(window->getSDLWindow(), &windowW, &windowH);
    SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);

    if (drawableW <= 0) drawableW = windowW;
    if (drawableH <= 0) drawableH = windowH;

    glViewport(0, 0, drawableW, drawableH);
}

void Application::updateMouseScale() {
    if (windowW > 0 && windowH > 0) {
        mouseScaleX = (float)drawableW / (float)windowW;
        mouseScaleY = (float)drawableH / (float)windowH;
    } else {
        mouseScaleX = 1.0f;
        mouseScaleY = 1.0f;
    }
}

void Application::setTitle(const std::string& title) {
    if (window) window->setTitle(title);
}

void Application::swapBuffers() {
    if (window) window->swapBuffers();
}

bool Application::pumpPreloadEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return false;

        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                e.window.event == SDL_WINDOWEVENT_RESIZED) {
                updateDrawableSizeAndViewport();
                updateMouseScale();
                if (renderer) {
                    renderer->onResize(drawableW, drawableH);
                }

                if (camera && drawableW > 0 && drawableH > 0) {
                    *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                }
            }
        }
    }
    return true;
}

void Application::renderBootLoading(float progress01) {
    if (!bootLoadingView) return;
    updateDrawableSizeAndViewport();
    bootLoadingView->render(progress01, drawableW, drawableH);
    swapBuffers();
}

void Application::run(GameLoop& game) {
    applicationLog().info(
        "[Run] Main loop @ " +
        std::to_string(static_cast<int>(engine::runtime::fixed_step::kHz)) +
        " Hz...");

    bool running = true;
    // Wire engine-owned services bundle (lifetime: Application)
    services.resources = &resourceManager;
    services.shaders = &shaderCache;
    services.events = &eventBus;

    GameContext ctx;
    ctx.renderer = renderer.get();
    ctx.camera   = camera.get();
    ctx.services = &services;
    ctx.drawableW = drawableW;
    ctx.drawableH = drawableH;

    ctx.setTitle = [this](const std::string& t) { this->setTitle(t); };
    ctx.swapBuffers = [this]() { this->swapBuffers(); };
    ctx.requestQuit = [&running]() { running = false; };
    ctx.pumpPreloadEvents = [this]() { return this->pumpPreloadEvents(); };
    ctx.renderBootLoading = [this](float p) { this->renderBootLoading(p); };

    game.init(ctx);

    if (!running) {
        game.shutdown();
        return;
    }

    using clock = std::chrono::high_resolution_clock;
    auto previous = clock::now();
    double accumulator = 0.0;

    int frameCount = 0;
    static double fpsTimer = 0.0;

    while (running) {
        SDL_Event sdlEvent;

        while (SDL_PollEvent(&sdlEvent)) {
            if (sdlEvent.type == SDL_QUIT ||
                (sdlEvent.type == SDL_KEYDOWN && sdlEvent.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            if (sdlEvent.type == SDL_WINDOWEVENT) {
                if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED) {
                    updateDrawableSizeAndViewport();
                    updateMouseScale();
                    if (renderer) {
                        renderer->onResize(drawableW, drawableH);
                    }

                    if (camera && drawableW > 0 && drawableH > 0) {
                        *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                    }

                    ctx.drawableW = drawableW;
                    ctx.drawableH = drawableH;
                }
            }

            InputEvent e;
            if (translateSdlToInputEvent(
                    sdlEvent,
                    mouseScaleX, mouseScaleY,
                    windowW, windowH,
                    drawableW, drawableH,
                    e
                )) {
                game.handleEvent(e);
            }
        }

        auto now = clock::now();
        double frameDt = std::chrono::duration<double>(now - previous).count();
        frameDt = std::min(frameDt, 0.25);
        previous = now;

        accumulator += frameDt;

        while (accumulator >= engine::runtime::fixed_step::kSeconds) {
            game.fixedUpdate(engine::runtime::fixed_step::kSeconds);
            accumulator -= engine::runtime::fixed_step::kSeconds;
        }

        if (renderer) {
            renderer->beginFrame(0.1f, 0.1f, 0.1f, 1.0f);
        }

        game.render(drawableW, drawableH);

        if (renderer) {
            renderer->endFrame();
            if (!renderer->handlesPresentation()) {
                swapBuffers();
            }
        } else {
            swapBuffers();
        }

        frameCount++;
        fpsTimer += frameDt;
        if (fpsTimer >= 1.0) {
            applicationLog().info("[FPS] " + std::to_string(frameCount));
            frameCount = 0;
            fpsTimer = 0.0;
        }
    }

    game.shutdown();
}

