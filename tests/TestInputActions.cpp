#include "engine/input/InputActions.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace engine::input;
namespace {
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c); } while (false)
using Key = InputEvent::Key;
using Mouse = InputEvent::MouseButton;
constexpr std::size_t ix(GamepadAxis axis) { return static_cast<std::size_t>(axis); }
constexpr std::size_t ix(GamepadButton button) { return static_cast<std::size_t>(button); }
bool near(float a, float b) { return std::abs(a - b) < .00001f; }
ActionState state(const InputActions& input, std::string_view name = "fire") {
    ActionState out; CHECK(input.state(name, out)); return out;
}
void expect(const InputActions& input, std::string_view name, glm::vec2 value, bool down, bool pressed, bool released) {
    const auto s = state(input, name);
    CHECK(near(s.value.x, value.x) && near(s.value.y, value.y));
    CHECK(s.down == down && s.pressed == pressed && s.released == released);
}
ControllerId controller(InputActions& input) { ControllerId id; CHECK(input.addController(id)); return id; }
void digitalFrames() {
    InputActions input;
    CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{Key::Space}, MouseBinding{Mouse::Left}}}));
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space, true)));
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space)));
    expect(input, "fire", {0, 0}, false, false, false);
    input.advanceFrame(); expect(input, "fire", {1, 0}, true, true, false);
    expect(input, "fire", {1, 0}, true, true, false);
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space, true)));
    input.advanceFrame(); expect(input, "fire", {1, 0}, true, false, false);
    CHECK(input.ingest(InputEvent::MouseDownEvent(0, 0, Mouse::Left)));
    CHECK(input.ingest(InputEvent::KeyUpEvent(Key::Space)));
    input.advanceFrame(); expect(input, "fire", {1, 0}, true, false, false);
    CHECK(input.ingest(InputEvent::MouseUpEvent(0, 0, Mouse::Left)));
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, true);
    for (int repeat = 0; repeat < 10; ++repeat) {
        CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space)));
        CHECK(input.ingest(InputEvent::KeyUpEvent(Key::Space)));
        input.advanceFrame(); expect(input, "fire", {0, 0}, false, true, true);
        input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
    }
    CHECK(input.ingest(InputEvent::MouseDownEvent(0, 0, Mouse::Left)));
    CHECK(input.ingest(InputEvent::MouseUpEvent(0, 0, Mouse::Left)));
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, true, true);
    CHECK(input.ingest(InputEvent::QuitEvent()));
    CHECK(input.ingest(InputEvent::MouseWheelEvent(1, 1)));
    CHECK(input.ingest(InputEvent::MouseMoveEvent(1, 1)));
    CHECK(input.ingest(InputEvent::ResizeEvent(1, 1, 1, 1)));
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
}
void combiningAndAnalog() {
    InputActions input;
    CHECK(input.define({"move", ActionKind::Axis2D, {KeyBinding{Key::W, {0, -1}}, KeyBinding{Key::S, {0, 1}},
        KeyBinding{Key::A, {-1, 0}}, KeyBinding{Key::D, {1, 0}}, StickBinding{GamepadStick::Left}}}));
    CHECK(input.define({"axis", ActionKind::Axis1D, {AxisBinding{GamepadAxis::RightX}}}));
    CHECK(input.define({"trigger", ActionKind::Axis1D, {AxisBinding{GamepadAxis::LeftTrigger, {-1, 0}}}}));
    const auto id = controller(input); CHECK(input.selectController(id)); CHECK(input.submit(id, {}));
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::W))); CHECK(input.ingest(InputEvent::KeyDownEvent(Key::D)));
    input.advanceFrame(); expect(input, "move", {std::sqrt(.5f), -std::sqrt(.5f)}, true, true, false);
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::S))); CHECK(input.ingest(InputEvent::KeyDownEvent(Key::A)));
    input.advanceFrame(); expect(input, "move", {0, 0}, false, false, true);
    ControllerSnapshot snap;
    snap.axes[ix(GamepadAxis::LeftX)] = .15f;
    snap.axes[ix(GamepadAxis::RightX)] = -.15f;
    snap.axes[ix(GamepadAxis::LeftTrigger)] = .05f;
    CHECK(input.submit(id, snap)); input.advanceFrame();
    expect(input, "move", {0, 0}, false, false, false); expect(input, "axis", {0, 0}, false, false, false);
    expect(input, "trigger", {0, 0}, false, false, false);
    snap.axes[ix(GamepadAxis::LeftX)] = .3f; snap.axes[ix(GamepadAxis::LeftY)] = .4f;
    CHECK(input.submit(id, snap)); input.advanceFrame();
    expect(input, "move", {.6f * (.35f / .85f), .8f * (.35f / .85f)}, true, true, false);
    snap.axes[ix(GamepadAxis::LeftX)] = .6f; snap.axes[ix(GamepadAxis::LeftY)] = .8f;
    snap.axes[ix(GamepadAxis::RightX)] = -.575f; snap.axes[ix(GamepadAxis::LeftTrigger)] = .525f;
    CHECK(input.submit(id, snap)); input.advanceFrame();
    expect(input, "move", {.6f, .8f}, true, false, false); expect(input, "axis", {-.5f, 0}, true, true, false);
    expect(input, "trigger", {-.5f, 0}, true, true, false);
    snap.axes[ix(GamepadAxis::LeftX)] = 1; snap.axes[ix(GamepadAxis::LeftY)] = 1;
    snap.axes[ix(GamepadAxis::RightX)] = 1; snap.axes[ix(GamepadAxis::LeftTrigger)] = 1;
    CHECK(input.submit(id, snap)); input.advanceFrame();
    expect(input, "move", {std::sqrt(.5f), std::sqrt(.5f)}, true, false, false);
    expect(input, "axis", {1, 0}, true, false, false); expect(input, "trigger", {-1, 0}, true, false, false);
    CHECK(input.rebind("move", {StickBinding{GamepadStick::Left, {1, -1}}}));
    expect(input, "move", {std::sqrt(.5f), -std::sqrt(.5f)}, true, false, false);
    CHECK(input.rebind("axis", {AxisBinding{GamepadAxis::RightX}, KeyBinding{Key::D}}));
    expect(input, "axis", {1, 0}, true, false, false);
    CHECK(input.rebind("move", {AxisBinding{GamepadAxis::RightX, {0, -.5f}, 0}, KeyBinding{Key::D, {.5f, 0}}}));
    expect(input, "move", {.5f, -.5f}, true, false, false);
    snap = {}; snap.axes[ix(GamepadAxis::RightX)] = -.95f;
    CHECK(input.submit(id, snap)); CHECK(input.rebind("axis", {AxisBinding{GamepadAxis::RightX, {1, 0}, .95f}}));
    expect(input, "axis", {0, 0}, false, false, false);
    snap.axes[ix(GamepadAxis::RightX)] = -1;
    CHECK(input.submit(id, snap)); input.advanceFrame(); expect(input, "axis", {-1, 0}, true, true, false);
}
void rebasingAndValidation() {
    InputActions input;
    CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{Key::Space}}}));
    CHECK(input.define({"other", ActionKind::Button, {KeyBinding{Key::Space}}}));
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space)));
    CHECK(input.define({"late", ActionKind::Button, {KeyBinding{Key::Space}}}));
    expect(input, "late", {1, 0}, true, false, false);
    CHECK(input.rebind("fire", {KeyBinding{Key::Space}}));
    expect(input, "fire", {1, 0}, true, false, false);
    input.advanceFrame(); expect(input, "other", {1, 0}, true, true, false);
    CHECK(input.ingest(InputEvent::KeyUpEvent(Key::Space)));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Binding> invalidDigital{
        KeyBinding{Key::Unknown}, KeyBinding{static_cast<Key>(65535)}, MouseBinding{Mouse::Unknown},
        MouseBinding{static_cast<Mouse>(255)}, ButtonBinding{GamepadButton::Count},
        KeyBinding{Key::Space, {-1, 0}}, KeyBinding{Key::Space, {1, 1}}, KeyBinding{Key::Space, {nan, 0}},
        KeyBinding{Key::Space, {inf, 0}}, KeyBinding{Key::Space, {1.01f, 0}},
        AxisBinding{GamepadAxis::LeftX}, StickBinding{GamepadStick::Left}
    };
    for (const auto& b : invalidDigital) { std::string error; CHECK(!input.rebind("fire", {b}, &error)); CHECK(!error.empty()); }
    expect(input, "fire", {1, 0}, true, false, false);
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, true);
    CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space))); input.advanceFrame();
    expect(input, "fire", {1, 0}, true, true, false);
    for (const auto& name : {std::string(), std::string("a b"), std::string("a/b"), std::string(65, 'a'), std::string("\xC3\xA9")})
        CHECK(!input.define({name, ActionKind::Button, {}}));
    CHECK(input.define({std::string(64, 'a'), ActionKind::Button, {}}));
    CHECK(input.define({"A-z_1.2", ActionKind::Button, {}}));
    CHECK(input.define({"a-z_1.2", ActionKind::Button, {}}));
    CHECK(!input.define({"fire", ActionKind::Button, {}}));
    CHECK(!input.define({"bad-kind", static_cast<ActionKind>(99), {}}));
    CHECK(input.define({"axis", ActionKind::Axis1D, {}}));
    CHECK(input.define({"vector", ActionKind::Axis2D, {}}));
    for (float dz : {-1.f, .951f, nan, inf}) {
        CHECK(!input.rebind("axis", {AxisBinding{GamepadAxis::LeftX, {1, 0}, dz}}));
        CHECK(!input.rebind("vector", {StickBinding{GamepadStick::Left, {1, 1}, dz}}));
    }
    CHECK(!input.rebind("axis", {KeyBinding{Key::W, {0, 1}}}));
    CHECK(!input.rebind("axis", {AxisBinding{GamepadAxis::Count}}));
    CHECK(!input.rebind("axis", {StickBinding{GamepadStick::Left, {1, 0}}}));
    CHECK(!input.rebind("vector", {StickBinding{static_cast<GamepadStick>(99)}}));
    CHECK(!input.rebind("vector", {StickBinding{GamepadStick::Left, {1, nan}}}));
    CHECK(!input.ingest(InputEvent::KeyDownEvent(Key::Unknown)));
    CHECK(!input.ingest(InputEvent::KeyUpEvent(static_cast<Key>(999))));
    CHECK(!input.ingest(InputEvent::MouseDownEvent(0, 0, Mouse::Unknown)));
    CHECK(!input.ingest(InputEvent::MouseUpEvent(0, 0, static_cast<Mouse>(99))));
    expect(input, "fire", {1, 0}, true, true, false);
    ActionState unchanged{{.25f, .5f}, true, true, true};
    CHECK(!input.state("missing", unchanged)); CHECK(unchanged.value == glm::vec2(.25f, .5f) && unchanged.released);
    CHECK(!input.rebind("missing", {})); CHECK(!input.remove("missing"));
    CHECK(input.remove("fire")); CHECK(!input.state("fire", unchanged));
    CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{Key::Space}}}));
    expect(input, "fire", {1, 0}, true, false, false);
    CHECK(input.rebind("fire", {})); expect(input, "fire", {0, 0}, false, false, false);
}
void controllerIdentityAndTransactions() {
    InputActions input({2, 2, 1}); InputActions other;
    CHECK(input.define({"fire", ActionKind::Button, {ButtonBinding{GamepadButton::South}}}));
    CHECK(input.define({"x", ActionKind::Axis1D, {AxisBinding{GamepadAxis::LeftX, {1, 0}, 0}}}));
    CHECK(!input.define({"third", ActionKind::Button, {}}));
    CHECK(!input.rebind("fire", {ButtonBinding{GamepadButton::South}, KeyBinding{Key::A}}));
    const auto a = controller(input); const auto b = controller(input); const auto foreign = controller(other);
    CHECK(a != b && a != foreign); CHECK(input.controllers() == std::vector<ControllerId>({a, b}));
    ControllerId unchanged = a; CHECK(!input.addController(unchanged)); CHECK(unchanged == a);
    CHECK(!input.selectController(ControllerId{})); CHECK(!input.selectController(foreign));
    CHECK(!input.removeController(foreign)); CHECK(!input.submit(foreign, {}));
    CHECK(input.selectController(a)); CHECK(input.submit(a, {}));
    ControllerSnapshot held; held.buttons[ix(GamepadButton::South)] = true; held.axes[0] = .5f;
    CHECK(input.submit(b, held)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
    CHECK(input.submit(a, held)); input.advanceFrame(); expect(input, "fire", {1, 0}, true, true, false);
    for (const auto bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.01f, 1.01f}) {
        auto malformed = held; malformed.buttons[0] = false; malformed.axes[5] = bad;
        CHECK(!input.submit(a, malformed)); input.advanceFrame(); expect(input, "fire", {1, 0}, true, false, false);
        expect(input, "x", {.5f, 0}, true, false, false);
    }
    auto malformed = held; malformed.axes[4] = -.01f; CHECK(!input.submit(a, malformed));
    CHECK(input.selectController(b)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, true);
    CHECK(input.submit(b, held)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
    CHECK(input.submit(b, {})); CHECK(input.submit(b, held)); input.advanceFrame(); expect(input, "fire", {1, 0}, true, true, false);
    CHECK(input.removeController(b)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, true);
    CHECK(!input.selectedController()); CHECK(!input.submit(b, {})); CHECK(!input.selectController(b)); CHECK(!input.removeController(b));
    const auto fresh = controller(input); CHECK(fresh != b && fresh != a && fresh != foreign);
    CHECK(input.selectController(a)); CHECK(input.submit(a, {})); CHECK(input.submit(a, held));
    CHECK(input.selectController(std::nullopt)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, true, true);
}
void gatesAndRearm() {
    for (bool focus : {false, true}) {
        InputActions input;
        CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{Key::Space}, MouseBinding{Mouse::Left}, ButtonBinding{GamepadButton::South}}}));
        CHECK(input.define({"x", ActionKind::Axis1D, {AxisBinding{GamepadAxis::LeftX, {1, 0}, 0}}}));
        CHECK(input.define({"y", ActionKind::Axis1D, {AxisBinding{GamepadAxis::LeftY, {1, 0}, 0}}}));
        CHECK(input.define({"trigger", ActionKind::Axis1D, {AxisBinding{GamepadAxis::RightTrigger, {1, 0}, 0}}}));
        const auto id = controller(input); CHECK(input.selectController(id)); CHECK(input.submit(id, {}));
        ControllerSnapshot held; held.buttons[0] = true; held.axes[0] = 1; held.axes[1] = -1; held.axes[5] = 1;
        CHECK(input.submit(id, held)); CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space)));
        auto gate = [&](bool open) { if (focus) input.setFocused(open); else input.setEnabled(open); };
        gate(false); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, true);
        CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space))); CHECK(input.ingest(InputEvent::MouseDownEvent(0, 0, Mouse::Left)));
        CHECK(input.submit(id, {})); CHECK(input.submit(id, held));
        gate(true); CHECK(input.submit(id, held)); input.advanceFrame();
        expect(input, "fire", {0, 0}, false, false, false); expect(input, "x", {0, 0}, false, false, false);
        expect(input, "trigger", {0, 0}, false, false, false);
        CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space, true))); input.advanceFrame(); CHECK(!state(input).down);
        CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space))); input.advanceFrame(); CHECK(state(input).pressed);
        CHECK(input.ingest(InputEvent::KeyUpEvent(Key::Space))); input.advanceFrame(); CHECK(state(input).released);
        held.buttons[0] = false; held.axes[0] = .05f; held.axes[5] = .05f;
        CHECK(input.submit(id, held)); input.advanceFrame();
        expect(input, "x", {0, 0}, false, false, false); expect(input, "y", {0, 0}, false, false, false);
        held.buttons[0] = true; held.axes[0] = 1; held.axes[5] = 1;
        CHECK(input.submit(id, held)); input.advanceFrame(); expect(input, "fire", {1, 0}, true, true, false);
        expect(input, "x", {1, 0}, true, true, false); expect(input, "y", {0, 0}, false, false, false);
        expect(input, "trigger", {1, 0}, true, true, false);
        input.setFocused(false); input.setEnabled(false); input.setFocused(true);
        CHECK(input.submit(id, {})); CHECK(input.ingest(InputEvent::KeyDownEvent(Key::Space)));
        input.setEnabled(true); CHECK(input.submit(id, held)); input.advanceFrame(); CHECK(!state(input).down);
        CHECK(input.submit(id, {})); CHECK(input.submit(id, held)); input.advanceFrame(); CHECK(state(input).down);
        input.setEnabled(true); input.setFocused(true); input.advanceFrame(); CHECK(state(input).down);
    }
}
void capacitiesAndThreads() {
    for (const auto option : {InputOptions{0, 1, 1}, InputOptions{1025, 1, 1}, InputOptions{1, 0, 1},
                             InputOptions{1, 33, 1}, InputOptions{1, 1, 0}, InputOptions{1, 1, 65}}) {
        bool threw = false; try { InputActions invalid(option); } catch (const std::invalid_argument&) { threw = true; } CHECK(threw);
    }
    InputActions max({1024, 32, 64});
    for (int i = 0; i < 32; ++i) (void)controller(max);
    ControllerId unchanged; CHECK(!max.addController(unchanged)); CHECK(unchanged == ControllerId{});
    const std::vector<Binding> bindings(64, KeyBinding{Key::A});
    for (int i = 0; i < 1024; ++i) CHECK(max.define({"a" + std::to_string(i), ActionKind::Button, bindings}));
    CHECK(!max.define({"overflow", ActionKind::Button, {}}));
    CHECK(!max.rebind("a0", std::vector<Binding>(65, KeyBinding{Key::A})));
    InputActions input;
    ControllerId previous;
    { InputActions destroyed; previous = controller(destroyed); }
    CHECK(controller(input) != previous);
    CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{Key::A}}})); const auto id = controller(input);
    std::exception_ptr error;
    std::thread wrong([&] {
        try {
            ActionState output{{.3f, .4f}, true, true, true}; ControllerId out = id;
            const std::vector<std::function<void()>> calls{
                [&] { input.define({"new", ActionKind::Button, {}}); }, [&] { input.rebind("fire", {}); }, [&] { input.remove("fire"); },
                [&] { input.state("fire", output); }, [&] { input.ingest(InputEvent::KeyDownEvent(Key::A)); }, [&] { input.advanceFrame(); },
                [&] { input.addController(out); }, [&] { input.removeController(id); }, [&] { input.submit(id, {}); },
                [&] { (void)input.controllers(); }, [&] { input.selectController(id); }, [&] { (void)input.selectedController(); },
                [&] { input.setFocused(false); }, [&] { input.setEnabled(false); }, [&] { (void)input.focused(); }, [&] { (void)input.enabled(); }
            };
            for (const auto& call : calls) { bool threw = false; try { call(); } catch (const std::logic_error&) { threw = true; } CHECK(threw); }
            CHECK(output.value == glm::vec2(.3f, .4f) && out == id);
            InputActions independent; CHECK(independent.define({"fire", ActionKind::Button, {KeyBinding{Key::A}}}));
            CHECK(independent.ingest(InputEvent::KeyDownEvent(Key::A))); independent.advanceFrame(); CHECK(state(independent).pressed);
            CHECK(controller(independent) != id);
        } catch (...) { error = std::current_exception(); }
    });
    wrong.join(); if (error) std::rethrow_exception(error);
    CHECK(input.focused() && input.enabled() && !input.selectedController()); CHECK(input.controllers().size() == 2);
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false, false);
}
}
int main() {
    static_assert(!std::is_copy_constructible_v<InputActions> && !std::is_move_constructible_v<InputActions>);
    try {
        digitalFrames(); combiningAndAnalog(); rebasingAndValidation(); controllerIdentityAndTransactions(); gatesAndRearm(); capacitiesAndThreads();
        std::cout << "PASS: pure input frames, combinations, analog, validation, transactions, IDs, gates, capacities and threads\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
