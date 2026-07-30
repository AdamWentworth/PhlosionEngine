// Rect.h
#pragma once

namespace ui {

// Minimal SDL-free rectangle used for UI layout.
// Matches SDL_Rect semantics: x/y are top-left; w/h are size in pixels.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

} // namespace ui
