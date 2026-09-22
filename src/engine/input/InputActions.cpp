#include "engine/input/InputActions.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace engine::input {
namespace {
template<class E> constexpr std::size_t index(E value) { return static_cast<std::size_t>(value); }
bool trigger(GamepadAxis axis) { return axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger; }
bool reject(std::string* diagnostic, const char* message) { if (diagnostic) *diagnostic = message; return false; }
bool validKey(InputEvent::Key key) { return key > InputEvent::Key::Unknown && key <= InputEvent::Key::RAlt; }
bool validMouse(InputEvent::MouseButton button) { return button > InputEvent::MouseButton::Unknown && button <= InputEvent::MouseButton::X2; }
bool validName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    });
}
bool validScale(glm::vec2 scale) {
    return std::isfinite(scale.x) && std::isfinite(scale.y) && std::abs(scale.x) <= 1 && std::abs(scale.y) <= 1;
}
bool validDeadzone(float value) { return std::isfinite(value) && value >= 0 && value <= .95f; }
float axial(float value, float deadzone) {
    return std::abs(value) <= deadzone ? 0.f : std::copysign((std::abs(value) - deadzone) / (1 - deadzone), value);
}
float magnitude(glm::vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
// A single linked module owns this sequence; wraparound is permanently exhausted.
std::atomic<std::uint64_t> nextController{1};
std::uint64_t allocateControllerId() {
    auto next = nextController.load(std::memory_order_relaxed);
    while (next != 0) {
        const auto successor = next == std::numeric_limits<std::uint64_t>::max() ? 0 : next + 1;
        if (nextController.compare_exchange_weak(next, successor, std::memory_order_relaxed)) return next;
    }
    return 0;
}
}

AxisBinding::AxisBinding(GamepadAxis source, glm::vec2 contribution)
    : axis(source), scale(contribution), deadzone(trigger(source) ? .05f : .15f) {}
AxisBinding::AxisBinding(GamepadAxis source, glm::vec2 contribution, float deadzoneValue)
    : axis(source), scale(contribution), deadzone(deadzoneValue) {}

struct InputActions::Impl {
    struct Action {
        ActionKind kind;
        std::vector<Binding> bindings;
        ActionState current;
        ActionState published;
    };
    struct Controller {
        ControllerSnapshot snapshot;
        std::array<bool, gamepadButtonCount> blockedButtons{};
        std::array<bool, gamepadAxisCount> blockedAxes{};
        void suppress() { blockedButtons.fill(true); blockedAxes.fill(true); }
    };
    InputOptions options;
    std::thread::id owner = std::this_thread::get_id();
    std::map<std::string, Action, std::less<>> actions;
    std::map<ControllerId, Controller> pads;
    std::optional<ControllerId> selected;
    std::array<bool, index(InputEvent::Key::RAlt) + 1> keys{};
    std::array<bool, index(InputEvent::MouseButton::X2) + 1> mouse{};
    bool focused = true;
    bool enabled = true;

    bool validate(ActionKind kind, const std::vector<Binding>& bindings, std::string* diagnostic) const {
        if (kind != ActionKind::Button && kind != ActionKind::Axis1D && kind != ActionKind::Axis2D)
            return reject(diagnostic, "Unknown action kind");
        if (bindings.size() > options.maxBindingsPerAction) return reject(diagnostic, "Binding capacity exceeded");
        for (const auto& binding : bindings) {
            const bool valid = std::visit([kind](const auto& b) {
                using T = std::decay_t<decltype(b)>;
                if (!validScale(b.scale)) return false;
                if (kind == ActionKind::Axis1D && b.scale.y != 0) return false;
                if constexpr (std::is_same_v<T, AxisBinding> || std::is_same_v<T, StickBinding>) {
                    if (kind == ActionKind::Button || !validDeadzone(b.deadzone)) return false;
                    if constexpr (std::is_same_v<T, AxisBinding>) return index(b.axis) < gamepadAxisCount;
                    else return kind == ActionKind::Axis2D && (b.stick == GamepadStick::Left || b.stick == GamepadStick::Right);
                } else {
                    if (kind == ActionKind::Button && b.scale != glm::vec2(1, 0)) return false;
                    if constexpr (std::is_same_v<T, KeyBinding>) return validKey(b.key);
                    else if constexpr (std::is_same_v<T, MouseBinding>) return validMouse(b.button);
                    else return index(b.button) < gamepadButtonCount;
                }
            }, binding);
            if (!valid) return reject(diagnostic, "Invalid or incompatible binding");
        }
        return true;
    }
    glm::vec2 value(const Action& action) const {
        if (!focused || !enabled) return glm::vec2(0);
        const Controller* pad = selected ? &pads.at(*selected) : nullptr;
        const auto axisValue = [pad](std::size_t i) { return pad && !pad->blockedAxes[i] ? pad->snapshot.axes[i] : 0.f; };
        glm::vec2 sum(0);
        for (const auto& binding : action.bindings) {
            sum += std::visit([&](const auto& b) -> glm::vec2 {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, KeyBinding>) return keys[index(b.key)] ? b.scale : glm::vec2(0);
                else if constexpr (std::is_same_v<T, MouseBinding>) return mouse[index(b.button)] ? b.scale : glm::vec2(0);
                else if constexpr (std::is_same_v<T, ButtonBinding>)
                    return pad && !pad->blockedButtons[index(b.button)] && pad->snapshot.buttons[index(b.button)] ? b.scale : glm::vec2(0);
                else if constexpr (std::is_same_v<T, AxisBinding>) return b.scale * axial(axisValue(index(b.axis)), b.deadzone);
                else {
                    const std::size_t first = b.stick == GamepadStick::Left ? 0 : 2;
                    const glm::vec2 raw(axisValue(first), axisValue(first + 1));
                    const float length = magnitude(raw);
                    if (length <= b.deadzone) return glm::vec2(0);
                    return b.scale * (raw / length) * ((std::min(length, 1.f) - b.deadzone) / (1 - b.deadzone));
                }
            }, binding);
        }
        if (action.kind == ActionKind::Button) return {sum.x > 0 ? 1.f : 0.f, 0};
        if (action.kind == ActionKind::Axis1D) return {std::clamp(sum.x, -1.f, 1.f), 0};
        const float length = magnitude(sum);
        return length > 1 ? sum / length : sum;
    }
    void update(Action& action, bool rebase = false) {
        const auto next = value(action);
        const bool down = magnitude(next) > 1e-6f;
        action.current.pressed |= down && !action.current.down;
        action.current.released |= !down && action.current.down;
        action.current.value = next;
        action.current.down = down;
        if (rebase) {
            action.current.pressed = action.current.released = false;
            action.published = action.current;
        }
    }
    void updateAll() { for (auto& [name, action] : actions) { (void)name; update(action); } }
    void gate(bool& gateValue, bool next) {
        if (gateValue == next) return;
        gateValue = next;
        keys.fill(false); mouse.fill(false);
        for (auto& [id, pad] : pads) { (void)id; pad.suppress(); }
        updateAll();
        if (!focused || !enabled) {
            for (auto& [name, action] : actions) { (void)name; action.current.pressed = false; }
        }
    }
};

InputActions::InputActions(InputOptions options) : impl_(std::make_unique<Impl>()) {
    if (options.maxActions < 1 || options.maxActions > 1024 || options.maxControllers < 1 || options.maxControllers > 32 ||
        options.maxBindingsPerAction < 1 || options.maxBindingsPerAction > 64) throw std::invalid_argument("Invalid input capacity options");
    impl_->options = options;
}
InputActions::~InputActions() { if (impl_->owner != std::this_thread::get_id()) std::terminate(); }
void InputActions::checkThread() const {
    if (impl_->owner != std::this_thread::get_id()) throw std::logic_error("InputActions used from non-owner thread");
}
bool InputActions::define(const ActionDefinition& definition, std::string* diagnostic) {
    checkThread();
    if (!validName(definition.name)) return reject(diagnostic, "Invalid action name");
    if (impl_->actions.contains(definition.name)) return reject(diagnostic, "Action already exists");
    if (impl_->actions.size() >= impl_->options.maxActions) return reject(diagnostic, "Action capacity exceeded");
    if (!impl_->validate(definition.kind, definition.bindings, diagnostic)) return false;
    Impl::Action action{definition.kind, definition.bindings, {}, {}};
    impl_->update(action, true);
    impl_->actions.emplace(definition.name, std::move(action));
    return true;
}
bool InputActions::rebind(std::string_view name, const std::vector<Binding>& bindings, std::string* diagnostic) {
    checkThread();
    auto it = impl_->actions.find(name);
    if (it == impl_->actions.end()) return reject(diagnostic, "Unknown action");
    if (!impl_->validate(it->second.kind, bindings, diagnostic)) return false;
    auto prepared = bindings;
    it->second.bindings.swap(prepared);
    impl_->update(it->second, true);
    return true;
}
bool InputActions::remove(std::string_view name, std::string* diagnostic) {
    checkThread();
    const auto it = impl_->actions.find(name);
    if (it == impl_->actions.end()) return reject(diagnostic, "Unknown action");
    impl_->actions.erase(it); return true;
}
bool InputActions::state(std::string_view name, ActionState& output, std::string* diagnostic) const {
    checkThread();
    const auto it = impl_->actions.find(name);
    if (it == impl_->actions.end()) return reject(diagnostic, "Unknown action");
    output = it->second.published; return true;
}
bool InputActions::ingest(const InputEvent& event, std::string* diagnostic) {
    checkThread();
    if (event.type == InputEvent::Type::KeyDown || event.type == InputEvent::Type::KeyUp) {
        if (!validKey(event.keyId)) return reject(diagnostic, "Invalid key");
        if (!impl_->focused || !impl_->enabled || (event.type == InputEvent::Type::KeyDown && event.repeat)) return true;
        impl_->keys[index(event.keyId)] = event.type == InputEvent::Type::KeyDown;
    } else if (event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::MouseUp) {
        if (!validMouse(event.mouseButtonId)) return reject(diagnostic, "Invalid mouse button");
        if (!impl_->focused || !impl_->enabled) return true;
        impl_->mouse[index(event.mouseButtonId)] = event.type == InputEvent::Type::MouseDown;
    } else return true;
    impl_->updateAll(); return true;
}
void InputActions::advanceFrame() {
    checkThread();
    for (auto& [name, action] : impl_->actions) {
        (void)name; action.published = action.current; action.current.pressed = action.current.released = false;
    }
}
bool InputActions::addController(ControllerId& output, std::string* diagnostic) {
    checkThread();
    if (impl_->pads.size() >= impl_->options.maxControllers) return reject(diagnostic, "Controller capacity exceeded");
    const auto raw = allocateControllerId();
    if (!raw) return reject(diagnostic, "Controller ID space exhausted");
    ControllerId id(raw);
    impl_->pads.emplace(id, Impl::Controller{}); output = id; return true;
}
bool InputActions::removeController(ControllerId id, std::string* diagnostic) {
    checkThread();
    const auto it = impl_->pads.find(id);
    if (it == impl_->pads.end()) return reject(diagnostic, "Unknown controller");
    if (impl_->selected == id) impl_->selected.reset();
    impl_->pads.erase(it); impl_->updateAll(); return true;
}
bool InputActions::submit(ControllerId id, const ControllerSnapshot& snapshot, std::string* diagnostic) {
    checkThread();
    const auto it = impl_->pads.find(id);
    if (it == impl_->pads.end()) return reject(diagnostic, "Unknown controller");
    for (std::size_t i = 0; i < gamepadAxisCount; ++i) {
        const float v = snapshot.axes[i];
        if (!std::isfinite(v) || v > 1 || v < (i >= index(GamepadAxis::LeftTrigger) ? 0.f : -1.f))
            return reject(diagnostic, "Invalid controller snapshot axis");
    }
    auto& pad = it->second;
    pad.snapshot = snapshot;
    if (impl_->focused && impl_->enabled) {
        for (std::size_t i = 0; i < gamepadButtonCount; ++i) if (!snapshot.buttons[i]) pad.blockedButtons[i] = false;
        for (std::size_t i = 0; i < gamepadAxisCount; ++i) {
            if (pad.blockedAxes[i] && std::abs(snapshot.axes[i]) <= .05f) {
                pad.blockedAxes[i] = false;
                // The observation that rearms a control is neutral even with a zero binding deadzone.
                pad.snapshot.axes[i] = 0;
            }
        }
    }
    impl_->updateAll(); return true;
}
std::vector<ControllerId> InputActions::controllers() const {
    checkThread(); std::vector<ControllerId> result; result.reserve(impl_->pads.size());
    for (const auto& [id, pad] : impl_->pads) { (void)pad; result.push_back(id); } return result;
}
bool InputActions::selectController(std::optional<ControllerId> id, std::string* diagnostic) {
    checkThread();
    if (id && !impl_->pads.contains(*id)) return reject(diagnostic, "Unknown controller");
    if (impl_->selected == id) return true;
    impl_->selected = id;
    if (id) impl_->pads.at(*id).suppress();
    impl_->updateAll(); return true;
}
std::optional<ControllerId> InputActions::selectedController() const { checkThread(); return impl_->selected; }
void InputActions::setFocused(bool value) { checkThread(); impl_->gate(impl_->focused, value); }
void InputActions::setEnabled(bool value) { checkThread(); impl_->gate(impl_->enabled, value); }
bool InputActions::focused() const { checkThread(); return impl_->focused; }
bool InputActions::enabled() const { checkThread(); return impl_->enabled; }
} // namespace engine::input
