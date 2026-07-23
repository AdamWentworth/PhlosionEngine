// src/engine/platform/Window.h

#ifndef WINDOW_H
#define WINDOW_H

#include <SDL2/SDL.h>
#include <string>

class Window {
public:
    enum class GraphicsApi {
        OpenGL,
        Vulkan,
        Native
    };

    Window(const std::string& title,
           int width,
           int height,
           GraphicsApi graphicsApi = GraphicsApi::OpenGL,
           bool vsyncEnabled = true);
    ~Window();

    SDL_Window* getSDLWindow() const { return window; }
    SDL_GLContext getContext() const { return context; }
    bool hasOpenGLContext() const { return context != nullptr; }
    GraphicsApi getGraphicsApi() const { return graphicsApi; }
    void getDrawableSize(int& width, int& height) const;

    void setTitle(const std::string& title);
    bool setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const { return vsyncEnabled; }
    void swapBuffers();

private:
    GraphicsApi graphicsApi = GraphicsApi::OpenGL;
    bool vsyncEnabled = true;
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
};

#endif // WINDOW_H
