// src/engine/runtime/Application.h
#pragma once

#include <memory>
#include <string>

#include "engine/render/Camera3D.h"
#include "engine/utils/ShaderCache.h"
#include "engine/utils/ResourceManager.h"
#include "engine/core/EngineServices.h"
#include "engine/events/EventBus.h"

class Window;
class IRenderBackend;
class BootLoadingView;

class GameLoop;
struct GameContext;

class Application {
public:
    Application();
    ~Application();

    void run(GameLoop& game);

    Window*   getWindow()   const { return window.get(); }
    IRenderBackend* getRenderer() const { return renderer.get(); }
    Camera3D* getCamera()   const { return camera.get();  }

    int getDrawableW() const { return drawableW; }
    int getDrawableH() const { return drawableH; }

    float getMouseScaleX() const { return mouseScaleX; }
    float getMouseScaleY() const { return mouseScaleY; }

    void setTitle(const std::string& title);
    void swapBuffers();
    bool pumpPreloadEvents();
    void renderBootLoading(float progress01);

private:
    bool initApplication();
    void shutdownApplication();

    void updateDrawableSizeAndViewport();
    void updateMouseScale();

private:
    std::unique_ptr<Window>   window;
    std::unique_ptr<IRenderBackend> renderer;
    std::unique_ptr<Camera3D> camera;

    std::unique_ptr<BootLoadingView> bootLoadingView;

    bool initialized = false;

    ResourceManager resourceManager;
    ShaderCache shaderCache;

    // Engine-owned event bus and service bundle (wired in Application::run).
    EventBus eventBus;
    EngineServices services;

    int drawableW = 1280;
    int drawableH = 720;

    int windowW = 1280;
    int windowH = 720;

    float mouseScaleX = 1.0f;
    float mouseScaleY = 1.0f;
};
