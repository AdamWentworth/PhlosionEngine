// src/engine/platform/Window.cpp

#include "engine/platform/Window.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <iostream>
#include <stdexcept>
#include <string>

Window::Window(const std::string& title,
               int width,
               int height,
               GraphicsApi graphicsApi_,
               bool vsyncEnabled_)
    : graphicsApi(graphicsApi_)
    , vsyncEnabled(vsyncEnabled_) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        const std::string msg = std::string("SDL_Init failed: ") + SDL_GetError();
        std::cerr << msg << "\n";
        throw std::runtime_error(msg);
    }

    // Allow the click that focuses the window to also propagate as a mouse click.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    if (graphicsApi == GraphicsApi::OpenGL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        flags |= SDL_WINDOW_OPENGL;
    } else if (graphicsApi == GraphicsApi::Vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        flags
    );

    if (!window) {
        const std::string msg = std::string("Window creation failed: ") + SDL_GetError();
        std::cerr << msg << "\n";
        SDL_Quit();
        throw std::runtime_error(msg);
    }

    if (graphicsApi == GraphicsApi::OpenGL) {
        context = SDL_GL_CreateContext(window);
        if (!context) {
            const std::string msg = std::string("OpenGL context creation failed: ") + SDL_GetError();
            std::cerr << msg << "\n";
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            throw std::runtime_error(msg);
        }

        SDL_GL_MakeCurrent(window, context);
        SDL_GL_SetSwapInterval(vsyncEnabled ? 1 : 0);
    }

    // Request foreground/focus on startup so the first user click is less likely
    // to be interpreted as focus-only by the OS/window manager.
    SDL_RaiseWindow(window);
}

Window::~Window() {
    if (context) {
        SDL_GL_DeleteContext(context);
        context = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // IMPORTANT: do NOT call SDL_Quit() here.
    // Caller controls shutdown ordering.
}

void Window::setTitle(const std::string& title) {
    if (window) SDL_SetWindowTitle(window, title.c_str());
}

void Window::getDrawableSize(int& width, int& height) const {
    width = 0;
    height = 0;
    if (!window) return;

    if (graphicsApi == GraphicsApi::OpenGL) {
        SDL_GL_GetDrawableSize(window, &width, &height);
    } else if (graphicsApi == GraphicsApi::Vulkan) {
        SDL_Vulkan_GetDrawableSize(window, &width, &height);
    } else {
        SDL_GetWindowSize(window, &width, &height);
    }
}

bool Window::setVSyncEnabled(bool enabled) {
    vsyncEnabled = enabled;
    if (!window || !context) {
        return false;
    }

    SDL_GL_MakeCurrent(window, context);
    if (SDL_GL_SetSwapInterval(vsyncEnabled ? 1 : 0) != 0) {
        std::cerr << "[Video] SDL_GL_SetSwapInterval failed: " << SDL_GetError() << "\n";
        return false;
    }
    return true;
}

void Window::swapBuffers() {
    if (window && context) SDL_GL_SwapWindow(window);
}
