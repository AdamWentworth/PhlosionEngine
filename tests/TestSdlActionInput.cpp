#define SDL_MAIN_HANDLED
#include "engine/input/SdlActionInput.h"
#include <SDL.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace engine::input;
namespace {
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #c + " SDL: " + SDL_GetError()); } while (false)
struct SdlReference {
    SdlReference() { CHECK(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0); }
    ~SdlReference() { SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER); }
};
struct VirtualPad {
    SDL_JoystickID instance = -1;
    SDL_Joystick* joystick = nullptr;
    explicit VirtualPad(const char* name, bool mapped = true) {
        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = static_cast<Uint16>(mapped ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_FLIGHT_STICK);
        desc.naxes = 6; desc.nbuttons = 15;
        desc.axis_mask = (1u << 6) - 1; desc.button_mask = (1u << 15) - 1; desc.name = name;
        const int deviceIndex = SDL_JoystickAttachVirtualEx(&desc);
        CHECK(deviceIndex >= 0);
        instance = SDL_JoystickGetDeviceInstanceID(deviceIndex);
        try {
            CHECK(instance >= 0); CHECK((SDL_IsGameController(deviceIndex) == SDL_TRUE) == mapped);
            joystick = SDL_JoystickOpen(deviceIndex); CHECK(joystick);
            neutral(); SDL_JoystickUpdate();
        } catch (...) { cleanup(); throw; }
    }
    ~VirtualPad() { cleanup(); }
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;
    int deviceIndex() const {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_JoystickGetDeviceInstanceID(i) == instance) return i;
        return -1;
    }
    void cleanup() {
        if (joystick) { SDL_JoystickClose(joystick); joystick = nullptr; }
        const int index = deviceIndex();
        if (index >= 0 && SDL_JoystickIsVirtual(index)) SDL_JoystickDetachVirtual(index);
    }
    void detach() { const int index = deviceIndex(); CHECK(index >= 0); CHECK(SDL_JoystickDetachVirtual(index) == 0); }
    void button(int index, bool down) { CHECK(SDL_JoystickSetVirtualButton(joystick, index, down ? SDL_PRESSED : SDL_RELEASED) == 0); }
    void axis(int index, Sint16 value) { CHECK(SDL_JoystickSetVirtualAxis(joystick, index, value) == 0); }
    void neutral() {
        for (int i = 0; i < 15; ++i) button(i, false);
        for (int i = 0; i < 6; ++i) axis(i, i >= 4 ? static_cast<Sint16>(-32768) : static_cast<Sint16>(0));
    }
};
ActionState state(InputActions& input, std::string_view name) { ActionState out; CHECK(input.state(name, out)); return out; }
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
void expect(InputActions& input, std::string_view name, glm::vec2 value, bool pressed, bool released,
            const std::source_location location = std::source_location::current()) {
    const auto s = state(input, name);
    if (!near(s.value.x, value.x) || !near(s.value.y, value.y) || s.pressed != pressed || s.released != released)
        std::cerr << "Expectation at line " << location.line() << " action " << name << " value " << s.value.x << ',' << s.value.y
                  << " pressed=" << s.pressed << " released=" << s.released << '\n';
    CHECK(near(s.value.x, value.x) && near(s.value.y, value.y));
    CHECK(s.down == (value != glm::vec2(0)) && s.pressed == pressed && s.released == released);
}
ControllerId lookup(SdlActionInput& adapter, const VirtualPad& pad) {
    ControllerId id; CHECK(adapter.controllerForInstance(pad.instance, id)); return id;
}
// This is the test host's pump, intentionally outside the adapter.
void pump(SdlActionInput& adapter) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) { std::string error; const bool ok = adapter.ingest(event, &error); if (!ok) throw std::runtime_error(error); }
}
void key(SdlActionInput& adapter, Uint32 window, Uint32 type, SDL_Keycode keycode, bool repeat = false) {
    SDL_Event event{}; event.type = type; event.key.windowID = window; event.key.keysym.sym = keycode; event.key.repeat = repeat ? 1 : 0;
    CHECK(adapter.ingest(event));
}
void focus(SdlActionInput& adapter, Uint32 window, bool focused) {
    SDL_Event event{}; event.type = SDL_WINDOWEVENT; event.window.windowID = window;
    event.window.event = static_cast<Uint8>(focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST); CHECK(adapter.ingest(event));
}
void mappedDevicesAndEvents() {
    SdlReference ambient;
    VirtualPad first("Phlosion M2-D virtual first");
    VirtualPad unmapped("Phlosion M2-D unmapped", false);
    InputActions input({128, 32, 32});
    CHECK(input.define({"fire", ActionKind::Button, {ButtonBinding{GamepadButton::South}, KeyBinding{InputEvent::Key::Space}}}));
    CHECK(input.define({"move", ActionKind::Axis2D, {StickBinding{GamepadStick::Left}}}));
    CHECK(input.define({"trigger", ActionKind::Axis1D, {AxisBinding{GamepadAxis::LeftTrigger}}}));
    CHECK(input.define({"signed", ActionKind::Axis1D, {AxisBinding{GamepadAxis::RightX, {1, 0}, 0}}}));
    {
        SdlActionInput adapter(input, 42);
        CHECK(adapter.devices().empty()); CHECK(adapter.refresh());
        const auto firstId = lookup(adapter, first);
        ControllerId untouched = firstId; CHECK(!adapter.controllerForInstance(unmapped.instance, untouched)); CHECK(untouched == firstId);
        auto devices = adapter.devices(); CHECK(!devices.empty());
        bool named = false;
        for (const auto& device : devices) if (device.id == firstId) { CHECK(device.name == "Phlosion M2-D virtual first"); named = true; }
        CHECK(named);
        CHECK(adapter.refresh()); CHECK(adapter.devices().size() == devices.size());
        pump(adapter); CHECK(adapter.devices().size() == devices.size());
        CHECK(input.selectController(firstId)); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
        first.button(SDL_CONTROLLER_BUTTON_A, true); first.axis(SDL_CONTROLLER_AXIS_LEFTX, 32767);
        first.axis(SDL_CONTROLLER_AXIS_LEFTY, -32768); first.axis(SDL_CONTROLLER_AXIS_RIGHTX, -32768);
        first.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
        CHECK(adapter.poll()); input.advanceFrame();
        expect(input, "fire", {1, 0}, true, false); expect(input, "move", {std::sqrt(.5f), -std::sqrt(.5f)}, true, false);
        expect(input, "signed", {-1, 0}, true, false); expect(input, "trigger", {1, 0}, true, false);
        // Poll generated queue entries remain host-owned; forwarding them next frame is idempotent.
        pump(adapter); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {1, 0}, false, false);
        first.axis(SDL_CONTROLLER_AXIS_RIGHTX, 32767); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "signed", {1, 0}, false, false);
        pump(adapter); first.neutral(); CHECK(adapter.poll()); pump(adapter); input.advanceFrame();
        expect(input, "fire", {0, 0}, false, true); expect(input, "trigger", {0, 0}, false, true);
        pump(adapter); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);

        // Actual SDL-generated queued edges survive even though polled final state is up.
        first.button(SDL_CONTROLLER_BUTTON_A, true); SDL_GameControllerUpdate();
        first.button(SDL_CONTROLLER_BUTTON_A, false); SDL_GameControllerUpdate();
        pump(adapter); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, true, true);
        input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
        first.axis(SDL_CONTROLLER_AXIS_RIGHTX, 32767); SDL_GameControllerUpdate();
        first.axis(SDL_CONTROLLER_AXIS_RIGHTX, 0); SDL_GameControllerUpdate();
        pump(adapter); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "signed", {0, 0}, true, true);

        VirtualPad second("Phlosion M2-D virtual second");
        pump(adapter); const auto secondId = lookup(adapter, second); CHECK(secondId != firstId);
        CHECK(adapter.devices().size() == devices.size() + 1);
        second.button(SDL_CONTROLLER_BUTTON_A, true); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
        CHECK(input.selectController(secondId)); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
        second.neutral(); CHECK(adapter.poll()); second.button(SDL_CONTROLLER_BUTTON_A, true);
        CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {1, 0}, true, false);
        second.detach(); CHECK(adapter.poll()); input.advanceFrame(); // No removal event forwarded.
        expect(input, "fire", {0, 0}, false, true); CHECK(!input.selectedController());
        CHECK(!adapter.controllerForInstance(second.instance, untouched)); pump(adapter);
        CHECK(adapter.devices().size() == devices.size());

        CHECK(input.selectController(firstId)); first.neutral(); CHECK(adapter.poll());
        first.button(SDL_CONTROLLER_BUTTON_A, true); CHECK(adapter.poll()); input.advanceFrame(); CHECK(state(input, "fire").down);
        focus(adapter, 99, false); CHECK(input.focused()); focus(adapter, 42, false); CHECK(!input.focused());
        input.advanceFrame(); expect(input, "fire", {0, 0}, false, true);
        focus(adapter, 42, true); CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
        first.neutral(); CHECK(adapter.poll()); first.button(SDL_CONTROLLER_BUTTON_A, true);
        CHECK(adapter.poll()); input.advanceFrame(); expect(input, "fire", {1, 0}, true, false);
        pump(adapter); input.advanceFrame(); // Finish queued observations before isolating disconnect.
        first.detach(); pump(adapter); input.advanceFrame(); expect(input, "fire", {0, 0}, false, true);
        CHECK(!input.selectedController()); CHECK(!adapter.controllerForInstance(first.instance, untouched));

        // No event queue consumption: the adapter leaves this test-owned user event alone.
        const Uint32 markerType = SDL_RegisterEvents(1); CHECK(markerType != static_cast<Uint32>(-1));
        SDL_Event marker{}; marker.type = markerType; marker.user.code = 12345; CHECK(SDL_PushEvent(&marker) == 1);
        CHECK(adapter.poll()); CHECK(adapter.refresh());
        SDL_Event retrieved{}; CHECK(SDL_PeepEvents(&retrieved, 1, SDL_GETEVENT, markerType, markerType) == 1); CHECK(retrieved.user.code == 12345);
    }
    CHECK(input.controllers().empty());
    CHECK((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) != 0);
    VirtualPad stillUsable("Phlosion M2-D retained subsystem");
    const std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)> handle(
        SDL_GameControllerOpen(stillUsable.deviceIndex()), &SDL_GameControllerClose);
    CHECK(handle);
    stillUsable.button(SDL_CONTROLLER_BUTTON_A, true); SDL_GameControllerUpdate();
    CHECK(SDL_GameControllerGetButton(handle.get(), SDL_CONTROLLER_BUTTON_A) != 0);
}
void allStandardControls() {
    SdlReference ambient;
    VirtualPad pad("Phlosion M2-D standard controls");
    InputActions input({128, 32, 32});
    for (std::size_t i = 0; i < gamepadButtonCount; ++i)
        CHECK(input.define({"b" + std::to_string(i), ActionKind::Button, {ButtonBinding{static_cast<GamepadButton>(i)}}}));
    for (std::size_t i = 0; i < gamepadAxisCount; ++i)
        CHECK(input.define({"a" + std::to_string(i), ActionKind::Axis1D, {AxisBinding{static_cast<GamepadAxis>(i), {1, 0}, 0}}}));
    {
        SdlActionInput adapter(input); CHECK(adapter.refresh()); CHECK(input.selectController(lookup(adapter, pad)));
        pump(adapter); CHECK(adapter.poll()); input.advanceFrame();
        for (std::size_t i = 0; i < gamepadButtonCount; ++i) {
            pad.button(static_cast<int>(i), true); SDL_GameControllerUpdate(); pump(adapter); CHECK(adapter.poll()); input.advanceFrame();
            for (std::size_t j = 0; j < gamepadButtonCount; ++j)
                expect(input, "b" + std::to_string(j), {i == j ? 1.f : 0.f, 0}, i == j, false);
            pad.button(static_cast<int>(i), false); SDL_GameControllerUpdate(); pump(adapter); CHECK(adapter.poll()); input.advanceFrame();
            expect(input, "b" + std::to_string(i), {0, 0}, false, true);
        }
        for (std::size_t i = 0; i < gamepadAxisCount; ++i) {
            for (const Sint16 raw : {static_cast<Sint16>(-32768), static_cast<Sint16>(0), static_cast<Sint16>(16384), static_cast<Sint16>(32767)}) {
                pad.axis(static_cast<int>(i), raw); SDL_GameControllerUpdate(); pump(adapter); CHECK(adapter.poll()); input.advanceFrame();
                const float expected = i >= 4 ? static_cast<float>((static_cast<int>(raw) + 32768) / 2) / 32767.f :
                    static_cast<float>(raw) / (raw < 0 ? 32768.f : 32767.f);
                CHECK(near(state(input, "a" + std::to_string(i)).value.x, expected));
            }
            pad.neutral(); SDL_GameControllerUpdate(); pump(adapter); CHECK(adapter.poll()); input.advanceFrame();
        }
        pad.button(SDL_CONTROLLER_BUTTON_A, true); CHECK(adapter.poll()); input.advanceFrame(); CHECK(state(input, "b0").down);
    }
    input.advanceFrame(); expect(input, "b0", {0, 0}, false, true);
    CHECK(!input.selectedController() && input.controllers().empty());
    CHECK(SDL_JoystickGetAttached(pad.joystick)); CHECK(SDL_JoystickGetButton(pad.joystick, SDL_CONTROLLER_BUTTON_A) != 0);
}
void windowsThreadsAndCapacity() {
    SdlReference ambient;
    InputActions input;
    CHECK(input.define({"fire", ActionKind::Button, {KeyBinding{InputEvent::Key::Space}, MouseBinding{InputEvent::MouseButton::X2}}}));
    SdlActionInput adapter(input, 42);
    key(adapter, 99, SDL_KEYDOWN, SDLK_SPACE); key(adapter, 42, SDL_KEYDOWN, SDLK_UNKNOWN);
    key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE, true); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
    key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE); key(adapter, 42, SDL_KEYUP, SDLK_SPACE);
    input.advanceFrame(); expect(input, "fire", {0, 0}, true, true);
    SDL_Event mouse{}; mouse.type = SDL_MOUSEBUTTONDOWN; mouse.button.windowID = 99; mouse.button.button = SDL_BUTTON_X2;
    CHECK(adapter.ingest(mouse)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
    mouse.button.windowID = 42; CHECK(adapter.ingest(mouse)); input.advanceFrame(); expect(input, "fire", {1, 0}, true, false);
    mouse.type = SDL_MOUSEBUTTONUP; CHECK(adapter.ingest(mouse)); input.advanceFrame(); expect(input, "fire", {0, 0}, false, true);
    key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE); input.advanceFrame();
    focus(adapter, 42, false); input.advanceFrame(); expect(input, "fire", {0, 0}, false, true);
    key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE); focus(adapter, 42, true); key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE, true);
    input.advanceFrame(); expect(input, "fire", {0, 0}, false, false);
    key(adapter, 42, SDL_KEYDOWN, SDLK_SPACE); input.advanceFrame(); expect(input, "fire", {1, 0}, true, false);
    key(adapter, 42, SDL_KEYUP, SDLK_SPACE); input.advanceFrame();
    {
        SdlActionInput controllerOnly(input);
        key(controllerOnly, 0, SDL_KEYDOWN, SDLK_SPACE); focus(controllerOnly, 0, false);
        input.advanceFrame(); expect(input, "fire", {0, 0}, false, false); CHECK(input.focused());
    }
    std::exception_ptr error;
    std::thread wrong([&] {
        try {
            SDL_Event event{}; ControllerId output;
            const std::vector<std::function<void()>> calls{
                [&] { adapter.refresh(); }, [&] { adapter.poll(); }, [&] { adapter.ingest(event); },
                [&] { (void)adapter.devices(); }, [&] { adapter.controllerForInstance(-1, output); }, [&] { SdlActionInput invalid(input); }
            };
            for (const auto& call : calls) { bool threw = false; try { call(); } catch (const std::logic_error&) { threw = true; } CHECK(threw); }
            CHECK(output == ControllerId{});
        } catch (...) { error = std::current_exception(); }
    });
    wrong.join(); if (error) std::rethrow_exception(error);

    // Occupy the pure capacity ourselves, so this remains deterministic with ambient hardware.
    InputActions limited({1, 1, 1}); ControllerId existing; CHECK(limited.addController(existing));
    VirtualPad device("Phlosion M2-D capacity");
    {
        SdlActionInput capacity(limited); std::string diagnostic;
        SDL_Event added{}; added.type = SDL_CONTROLLERDEVICEADDED; added.cdevice.which = device.deviceIndex();
        CHECK(!capacity.ingest(added, &diagnostic)); CHECK(!diagnostic.empty()); CHECK(capacity.devices().empty());
        CHECK(!capacity.refresh(&diagnostic)); CHECK(!diagnostic.empty()); CHECK(capacity.devices().empty());
        CHECK(limited.controllers() == std::vector<ControllerId>{existing});
        CHECK(limited.removeController(existing)); CHECK(capacity.ingest(added));
        const auto id = lookup(capacity, device); CHECK(id != existing); CHECK(capacity.ingest(added)); CHECK(capacity.devices().size() == 1);
        {
            VirtualPad overflow("Phlosion M2-D overflow");
            CHECK(!capacity.refresh(&diagnostic)); CHECK(!diagnostic.empty());
            CHECK(capacity.devices().size() == 1 && lookup(capacity, device) == id);
            CHECK(capacity.poll()); CHECK(limited.controllers() == std::vector<ControllerId>{id});
        }
        SDL_Event remap{}; remap.type = SDL_CONTROLLERDEVICEREMAPPED; remap.cdevice.which = device.instance; CHECK(capacity.ingest(remap));
        added.cdevice.which = -1; CHECK(!capacity.ingest(added)); CHECK(capacity.devices().size() == 1);
    }
    CHECK(limited.controllers().empty()); CHECK(SDL_JoystickGetAttached(device.joystick));
}
}
int main() {
    static_assert(!std::is_copy_constructible_v<SdlActionInput> && !std::is_move_constructible_v<SdlActionInput>);
    SDL_SetMainReady();
    SDL_version runtime{}; SDL_GetVersion(&runtime);
    std::cout << "SDL runtime " << static_cast<int>(runtime.major) << '.' << static_cast<int>(runtime.minor) << '.' << static_cast<int>(runtime.patch) << '\n';
    try {
        CHECK(SDL_VERSIONNUM(runtime.major, runtime.minor, runtime.patch) >= SDL_VERSIONNUM(2, 24, 0));
        mappedDevicesAndEvents(); allStandardControls(); windowsThreadsAndCapacity();
        std::cout << "PASS: real SDL virtual mapped devices, polling, queued taps, selection, removal, windows, threads and balanced teardown\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
