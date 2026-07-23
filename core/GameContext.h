// src/engine/core/GameContext.h
#pragma once

#include <functional>
#include <string>

// Forward-declare engine services
class IRenderBackend;
class Camera3D;
struct EngineServices;

/*
    GameContext:
    - The *stable* engine-facing API exposed to the game layer.
    - Keeps the game from depending on a specific host loop directly.
    - Add fields here deliberately (treat as public engine/game contract).
*/
struct GameContext {
    struct VideoMode {
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
    };

    // Core render services (owned by engine)
    IRenderBackend* renderer = nullptr;
    Camera3D* camera   = nullptr;


    // Optional engine service bundle (owned by engine; may be null)
    EngineServices* services = nullptr;
    // Current drawable size (framebuffer size)
    int drawableW = 1280;
    int drawableH = 720;

    // Minimal engine callbacks (kept intentionally small)
    std::function<void(const std::string&)> setTitle;
    std::function<void()>                  swapBuffers;

    // Request application shutdown (safe to call during init/preload)
    std::function<void()>                  requestQuit;

    // Loading / long task helpers
    std::function<bool()>                  pumpPreloadEvents;
    std::function<void(float /*progress01*/)> renderBootLoading;

    // Runtime video controls (optional)
    std::function<bool(int /*width*/, int /*height*/, bool /*fullscreen*/)> applyVideoMode;
    std::function<VideoMode()> queryVideoMode;
};
