// src/engine/input/InputEvent.h
#pragma once

#include <cstdint>

/*
    InputEvent:
    - Engine-owned input/event type used at the engine/game boundary.
    - Keeps SDL types out of the public interface.
    - Only contains the data your game actually needs.
*/
struct InputEvent {
    enum class Type : std::uint8_t {
        Unknown = 0,

        Quit,
        Resize,

        KeyDown,
        KeyUp,

        MouseMove,
        MouseDown,
        MouseUp,

        MouseWheel
    };

    // Engine-defined key identifiers (SDL-agnostic).
    // Populate these in the platform layer (e.g., SDL translation) and use them in gameplay.
    enum class Key : std::uint16_t {
        Unknown = 0,

        // Digits
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Common controls
        Escape,
        Enter,
        Space,
        Tab,
        Backspace,

        // Arrows
        Left,
        Right,
        Up,
        Down,

        // Function keys
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        // Modifiers
        LShift,
        RShift,
        LCtrl,
        RCtrl,
        LAlt,
        RAlt
    };

    // Engine-defined mouse buttons (SDL-agnostic).
    enum class MouseButton : std::uint8_t {
        Unknown = 0,
        Left,
        Middle,
        Right,
        X1,
        X2
    };

    Type type = Type::Unknown;

    // --- Keyboard ---
    Key keyId = Key::Unknown;
    bool repeat = false;

    // --- Mouse ---
    int mouseX = 0;
    int mouseY = 0;
    MouseButton mouseButtonId = MouseButton::Unknown;

    // --- Wheel ---
    int wheelX = 0;
    int wheelY = 0;

    // --- Resize ---
    int windowW = 0;
    int windowH = 0;
    int drawableW = 0;
    int drawableH = 0;

    static InputEvent QuitEvent() {
        InputEvent e; e.type = Type::Quit; return e;
    }

    static InputEvent ResizeEvent(int wW, int wH, int dW, int dH) {
        InputEvent e;
        e.type = Type::Resize;
        e.windowW = wW; e.windowH = wH;
        e.drawableW = dW; e.drawableH = dH;
        return e;
    }

    static InputEvent KeyDownEvent(Key key, bool isRepeat = false) {
        InputEvent e;
        e.type = Type::KeyDown;
        e.keyId = key;
        e.repeat = isRepeat;
        return e;
    }

    static InputEvent KeyUpEvent(Key key) {
        InputEvent e;
        e.type = Type::KeyUp;
        e.keyId = key;
        return e;
    }

    static InputEvent MouseMoveEvent(int x, int y) {
        InputEvent e;
        e.type = Type::MouseMove;
        e.mouseX = x; e.mouseY = y;
        return e;
    }

    static InputEvent MouseDownEvent(int x, int y, MouseButton btn) {
        InputEvent e;
        e.type = Type::MouseDown;
        e.mouseX = x; e.mouseY = y;
        e.mouseButtonId = btn;
        return e;
    }

    static InputEvent MouseUpEvent(int x, int y, MouseButton btn) {
        InputEvent e;
        e.type = Type::MouseUp;
        e.mouseX = x; e.mouseY = y;
        e.mouseButtonId = btn;
        return e;
    }

    static InputEvent MouseWheelEvent(int x, int y) {
        InputEvent e;
        e.type = Type::MouseWheel;
        e.wheelX = x; e.wheelY = y;
        return e;
    }
};
