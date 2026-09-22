#include "engine/input/SdlActionInput.h"
#include "engine/input/SdlKeyMap.h"

#include <SDL.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <thread>

namespace engine::input {
namespace {
constexpr std::array<SDL_GameControllerButton, gamepadButtonCount> buttons{
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT
};
constexpr std::array<SDL_GameControllerAxis, gamepadAxisCount> axes{
    SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT
};
bool reject(std::string* diagnostic, const std::string& message) { if (diagnostic) *diagnostic = message; return false; }
float normalize(Sint16 value, std::size_t axis) {
    if (axis >= 4) return static_cast<float>(std::max<Sint16>(0, value)) / 32767.f;
    return static_cast<float>(value) / (value < 0 ? 32768.f : 32767.f);
}
InputEvent::MouseButton mouseButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return InputEvent::MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return InputEvent::MouseButton::Middle;
    case SDL_BUTTON_RIGHT: return InputEvent::MouseButton::Right;
    case SDL_BUTTON_X1: return InputEvent::MouseButton::X1;
    case SDL_BUTTON_X2: return InputEvent::MouseButton::X2;
    default: return InputEvent::MouseButton::Unknown;
    }
}
struct CloseController { void operator()(SDL_GameController* handle) const { SDL_GameControllerClose(handle); } };
using Handle = std::unique_ptr<SDL_GameController, CloseController>;
}

struct SdlActionInput::Impl {
    struct Device { Handle handle; SdlInputDevice info; ControllerSnapshot snapshot; };
    InputActions& actions;
    Uint32 windowId;
    std::thread::id owner = std::this_thread::get_id();
    std::map<SDL_JoystickID, Device> devices;
    explicit Impl(InputActions& input, Uint32 window) : actions(input), windowId(window) {}

    bool open(int deviceIndex, std::string* diagnostic) {
        const int count = SDL_NumJoysticks();
        if (deviceIndex < 0 || deviceIndex >= count) return reject(diagnostic, "Invalid SDL controller device index");
        if (!SDL_IsGameController(deviceIndex)) return true;
        const auto instance = SDL_JoystickGetDeviceInstanceID(deviceIndex);
        if (instance < 0) return reject(diagnostic, std::string("SDL instance lookup failed: ") + SDL_GetError());
        if (devices.contains(instance)) return true;
        Handle handle(SDL_GameControllerOpen(deviceIndex));
        if (!handle) return reject(diagnostic, std::string("SDL controller open failed: ") + SDL_GetError());
        const char* name = SDL_GameControllerName(handle.get());
        Device prepared{std::move(handle), {{}, instance, name ? name : ""}, {}};
        auto [it, inserted] = devices.emplace(instance, std::move(prepared));
        (void)inserted;
        if (!actions.addController(it->second.info.id, diagnostic)) { devices.erase(it); return false; }
        return sample(it->second, diagnostic);
    }
    bool close(SDL_JoystickID instance, std::string* diagnostic) {
        const auto it = devices.find(instance);
        if (it == devices.end()) return true;
        const bool result = actions.removeController(it->second.info.id, diagnostic);
        devices.erase(it);
        return result;
    }
    bool sample(Device& device, std::string* diagnostic) {
        ControllerSnapshot next;
        for (std::size_t i = 0; i < buttons.size(); ++i) next.buttons[i] = SDL_GameControllerGetButton(device.handle.get(), buttons[i]) != 0;
        for (std::size_t i = 0; i < axes.size(); ++i) next.axes[i] = normalize(SDL_GameControllerGetAxis(device.handle.get(), axes[i]), i);
        if (!actions.submit(device.info.id, next, diagnostic)) return false;
        device.snapshot = next; return true;
    }
};

SdlActionInput::SdlActionInput(InputActions& actions, Uint32 windowId) {
    actions.checkThread();
    auto prepared = std::make_unique<Impl>(actions, windowId);
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        throw std::runtime_error(std::string("SDL game-controller initialization failed: ") + SDL_GetError());
    impl_ = std::move(prepared);
}
SdlActionInput::~SdlActionInput() {
    if (impl_->owner != std::this_thread::get_id()) std::terminate();
    for (const auto& [instance, device] : impl_->devices) { (void)instance; impl_->actions.removeController(device.info.id); }
    impl_->devices.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}
void SdlActionInput::checkThread() const {
    if (impl_->owner != std::this_thread::get_id()) throw std::logic_error("SdlActionInput used from non-owner thread");
    impl_->actions.checkThread();
}
bool SdlActionInput::refresh(std::string* diagnostic) {
    checkThread();
    bool result = true;
    std::string errors;
    const auto collect = [&](bool success, const std::string& error) {
        if (!success) { result = false; if (!errors.empty()) errors += "; "; errors += error; }
    };
    for (auto it = impl_->devices.begin(); it != impl_->devices.end();) {
        const auto instance = it->first;
        const bool detached = !SDL_GameControllerGetAttached(it->second.handle.get());
        ++it;
        if (detached) { std::string error; const bool ok = impl_->close(instance, &error); collect(ok, error); }
    }
    const int count = SDL_NumJoysticks();
    if (count < 0) return reject(diagnostic, std::string("SDL device enumeration failed: ") + SDL_GetError());
    for (int i = 0; i < count; ++i) { std::string error; const bool ok = impl_->open(i, &error); collect(ok, error); }
    if (!result && diagnostic) *diagnostic = errors;
    return result;
}
bool SdlActionInput::poll(std::string* diagnostic) {
    checkThread(); SDL_GameControllerUpdate();
    bool result = true; std::string errors;
    for (auto it = impl_->devices.begin(); it != impl_->devices.end();) {
        auto& device = it->second;
        const auto instance = it->first;
        ++it;
        std::string error;
        const bool ok = SDL_GameControllerGetAttached(device.handle.get()) ? impl_->sample(device, &error) : impl_->close(instance, &error);
        if (!ok) { result = false; if (!errors.empty()) errors += "; "; errors += error; }
    }
    if (!result && diagnostic) *diagnostic = errors;
    return result;
}
bool SdlActionInput::ingest(const SDL_Event& event, std::string* diagnostic) {
    checkThread();
    if (event.type == SDL_CONTROLLERDEVICEADDED) return impl_->open(event.cdevice.which, diagnostic);
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) return impl_->close(event.cdevice.which, diagnostic);
    if (event.type == SDL_CONTROLLERDEVICEREMAPPED) {
        const auto it = impl_->devices.find(event.cdevice.which);
        if (it == impl_->devices.end()) return true;
        return impl_->sample(it->second, diagnostic);
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP || event.type == SDL_CONTROLLERAXISMOTION) {
        const auto instance = event.type == SDL_CONTROLLERAXISMOTION ? event.caxis.which : event.cbutton.which;
        const auto it = impl_->devices.find(instance);
        if (it == impl_->devices.end()) return true;
        auto next = it->second.snapshot;
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            const auto axis = std::find(axes.begin(), axes.end(), static_cast<SDL_GameControllerAxis>(event.caxis.axis));
            if (axis == axes.end()) return true;
            const auto i = static_cast<std::size_t>(axis - axes.begin());
            next.axes[i] = normalize(event.caxis.value, i);
        } else {
            const auto button = std::find(buttons.begin(), buttons.end(), static_cast<SDL_GameControllerButton>(event.cbutton.button));
            if (button == buttons.end()) return true;
            next.buttons[static_cast<std::size_t>(button - buttons.begin())] = event.type == SDL_CONTROLLERBUTTONDOWN;
        }
        if (!impl_->actions.submit(it->second.info.id, next, diagnostic)) return false;
        it->second.snapshot = next; return true;
    }
    if (impl_->windowId == 0) return true;
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.key.windowID != impl_->windowId) return true;
        const auto key = mapSdlKeyToEngineKey(event.key.keysym.sym);
        if (key == InputEvent::Key::Unknown) return true;
        return impl_->actions.ingest(event.type == SDL_KEYDOWN ? InputEvent::KeyDownEvent(key, event.key.repeat != 0) : InputEvent::KeyUpEvent(key), diagnostic);
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.windowID != impl_->windowId) return true;
        const auto button = mouseButton(event.button.button);
        if (button == InputEvent::MouseButton::Unknown) return true;
        return impl_->actions.ingest(event.type == SDL_MOUSEBUTTONDOWN ? InputEvent::MouseDownEvent(event.button.x, event.button.y, button) :
                                     InputEvent::MouseUpEvent(event.button.x, event.button.y, button), diagnostic);
    }
    if (event.type == SDL_WINDOWEVENT && event.window.windowID == impl_->windowId) {
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) impl_->actions.setFocused(false);
        else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) impl_->actions.setFocused(true);
    }
    return true;
}
std::vector<SdlInputDevice> SdlActionInput::devices() const {
    checkThread(); std::vector<SdlInputDevice> result; result.reserve(impl_->devices.size());
    for (const auto& [instance, device] : impl_->devices) { (void)instance; result.push_back(device.info); }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
bool SdlActionInput::controllerForInstance(SDL_JoystickID instance, ControllerId& output, std::string* diagnostic) const {
    checkThread(); const auto it = impl_->devices.find(instance);
    if (it == impl_->devices.end()) return reject(diagnostic, "Unknown SDL controller instance");
    output = it->second.info.id; return true;
}
} // namespace engine::input
