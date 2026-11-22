#pragma once
#include "engine/render/PreparationProgress.h"
#include <SDL2/SDL.h>
#include <stdexcept>
namespace engine::platform {
inline auto preparationWindowProgress(SDL_Window* window) {
    return [window,title=std::string(SDL_GetWindowTitle(window)),last=std::string{}](std::string_view label) mutable {
        const std::string value=label.empty()?title:title+" | "+std::string(label);
        if(value!=last){SDL_SetWindowTitle(window,value.c_str());last=value;}
        SDL_PumpEvents();
        SDL_Event event{};
        if(SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0)
            throw engine::render::PreparationCancelled{};
    };
}
}
