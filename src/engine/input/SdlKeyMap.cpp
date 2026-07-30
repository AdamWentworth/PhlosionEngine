// src/engine/input/SdlKeyMap.cpp
#include "engine/input/SdlKeyMap.h"

#include <SDL2/SDL_keycode.h>

namespace engine::input {

InputEvent::Key mapSdlKeyToEngineKey(int sdlKeycode) {
    switch (sdlKeycode) {

            case SDLK_0: return InputEvent::Key::Num0;
            case SDLK_1: return InputEvent::Key::Num1;
            case SDLK_2: return InputEvent::Key::Num2;
            case SDLK_3: return InputEvent::Key::Num3;
            case SDLK_4: return InputEvent::Key::Num4;
            case SDLK_5: return InputEvent::Key::Num5;
            case SDLK_6: return InputEvent::Key::Num6;
            case SDLK_7: return InputEvent::Key::Num7;
            case SDLK_8: return InputEvent::Key::Num8;
            case SDLK_9: return InputEvent::Key::Num9;
            case SDLK_KP_0: return InputEvent::Key::Num0;
            case SDLK_KP_1: return InputEvent::Key::Num1;
            case SDLK_KP_2: return InputEvent::Key::Num2;
            case SDLK_KP_3: return InputEvent::Key::Num3;
            case SDLK_KP_4: return InputEvent::Key::Num4;
            case SDLK_KP_5: return InputEvent::Key::Num5;
            case SDLK_KP_6: return InputEvent::Key::Num6;
            case SDLK_KP_7: return InputEvent::Key::Num7;
            case SDLK_KP_8: return InputEvent::Key::Num8;
            case SDLK_KP_9: return InputEvent::Key::Num9;

            case SDLK_a: return InputEvent::Key::A;
            case SDLK_b: return InputEvent::Key::B;
            case SDLK_c: return InputEvent::Key::C;
            case SDLK_d: return InputEvent::Key::D;
            case SDLK_e: return InputEvent::Key::E;
            case SDLK_f: return InputEvent::Key::F;
            case SDLK_g: return InputEvent::Key::G;
            case SDLK_h: return InputEvent::Key::H;
            case SDLK_i: return InputEvent::Key::I;
            case SDLK_j: return InputEvent::Key::J;
            case SDLK_k: return InputEvent::Key::K;
            case SDLK_l: return InputEvent::Key::L;
            case SDLK_m: return InputEvent::Key::M;
            case SDLK_n: return InputEvent::Key::N;
            case SDLK_o: return InputEvent::Key::O;
            case SDLK_p: return InputEvent::Key::P;
            case SDLK_q: return InputEvent::Key::Q;
            case SDLK_r: return InputEvent::Key::R;
            case SDLK_s: return InputEvent::Key::S;
            case SDLK_t: return InputEvent::Key::T;
            case SDLK_u: return InputEvent::Key::U;
            case SDLK_v: return InputEvent::Key::V;
            case SDLK_w: return InputEvent::Key::W;
            case SDLK_x: return InputEvent::Key::X;
            case SDLK_y: return InputEvent::Key::Y;
            case SDLK_z: return InputEvent::Key::Z;

            case SDLK_ESCAPE: return InputEvent::Key::Escape;
            case SDLK_RETURN: return InputEvent::Key::Enter;
            case SDLK_KP_ENTER: return InputEvent::Key::Enter;
            case SDLK_SPACE:  return InputEvent::Key::Space;
            case SDLK_TAB:    return InputEvent::Key::Tab;
            case SDLK_BACKSPACE: return InputEvent::Key::Backspace;

            case SDLK_LEFT:  return InputEvent::Key::Left;
            case SDLK_RIGHT: return InputEvent::Key::Right;
            case SDLK_UP:    return InputEvent::Key::Up;
            case SDLK_DOWN:  return InputEvent::Key::Down;

            case SDLK_F1:  return InputEvent::Key::F1;
            case SDLK_F2:  return InputEvent::Key::F2;
            case SDLK_F3:  return InputEvent::Key::F3;
            case SDLK_F4:  return InputEvent::Key::F4;
            case SDLK_F5:  return InputEvent::Key::F5;
            case SDLK_F6:  return InputEvent::Key::F6;
            case SDLK_F7:  return InputEvent::Key::F7;
            case SDLK_F8:  return InputEvent::Key::F8;
            case SDLK_F9:  return InputEvent::Key::F9;
            case SDLK_F10: return InputEvent::Key::F10;
            case SDLK_F11: return InputEvent::Key::F11;
            case SDLK_F12: return InputEvent::Key::F12;

            case SDLK_LSHIFT: return InputEvent::Key::LShift;
            case SDLK_RSHIFT: return InputEvent::Key::RShift;
            case SDLK_LCTRL:  return InputEvent::Key::LCtrl;
            case SDLK_RCTRL:  return InputEvent::Key::RCtrl;
            case SDLK_LALT:   return InputEvent::Key::LAlt;
            case SDLK_RALT:   return InputEvent::Key::RAlt;
        default: return InputEvent::Key::Unknown;
    }
}

} // namespace engine::input
