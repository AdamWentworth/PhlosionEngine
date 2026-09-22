#pragma once

#include "engine/input/InputEvent.h"
#include <glm/vec2.hpp>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine::input {

enum class ActionKind { Button, Axis1D, Axis2D };
enum class GamepadButton {
    South, East, West, North, Back, Guide, Start, LeftStick, RightStick,
    LeftShoulder, RightShoulder, DpadUp, DpadDown, DpadLeft, DpadRight, Count
};
enum class GamepadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };
enum class GamepadStick { Left, Right };
inline constexpr std::size_t gamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
inline constexpr std::size_t gamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);

struct KeyBinding { InputEvent::Key key; glm::vec2 scale{1, 0}; };
struct MouseBinding { InputEvent::MouseButton button; glm::vec2 scale{1, 0}; };
struct ButtonBinding { GamepadButton button; glm::vec2 scale{1, 0}; };
struct AxisBinding {
    GamepadAxis axis;
    glm::vec2 scale{1, 0};
    float deadzone;
    explicit AxisBinding(GamepadAxis source, glm::vec2 contribution = {1, 0});
    AxisBinding(GamepadAxis source, glm::vec2 contribution, float deadzoneValue);
};
struct StickBinding { GamepadStick stick; glm::vec2 scale{1, 1}; float deadzone = .15f; };
using Binding = std::variant<KeyBinding, MouseBinding, ButtonBinding, AxisBinding, StickBinding>;

struct ActionDefinition { std::string name; ActionKind kind = ActionKind::Button; std::vector<Binding> bindings; };
struct ActionState { glm::vec2 value{0}; bool down = false; bool pressed = false; bool released = false; };
struct InputOptions { std::size_t maxActions = 128; std::size_t maxControllers = 8; std::size_t maxBindingsPerAction = 32; };

class ControllerId {
public:
    ControllerId() = default;
    auto operator<=>(const ControllerId&) const = default;
private:
    explicit ControllerId(std::uint64_t value) : value_(value) {}
    std::uint64_t value_ = 0;
    friend class InputActions;
};

struct ControllerSnapshot {
    std::array<bool, gamepadButtonCount> buttons{};
    // Signed axes [-1,1], triggers [0,1]. Y is positive down.
    std::array<float, gamepadAxisCount> axes{};
};

// All operations and destruction belong to the constructing thread. No clock or event pump.
class InputActions {
public:
    explicit InputActions(InputOptions options = {});
    ~InputActions();
    InputActions(const InputActions&) = delete;
    InputActions& operator=(const InputActions&) = delete;
    InputActions(InputActions&&) = delete;
    InputActions& operator=(InputActions&&) = delete;

    bool define(const ActionDefinition& definition, std::string* diagnostic = nullptr);
    bool rebind(std::string_view name, const std::vector<Binding>& bindings, std::string* diagnostic = nullptr);
    bool remove(std::string_view name, std::string* diagnostic = nullptr);
    bool state(std::string_view name, ActionState& output, std::string* diagnostic = nullptr) const;
    bool ingest(const InputEvent& event, std::string* diagnostic = nullptr);
    void advanceFrame();

    bool addController(ControllerId& output, std::string* diagnostic = nullptr);
    bool removeController(ControllerId id, std::string* diagnostic = nullptr);
    bool submit(ControllerId id, const ControllerSnapshot& snapshot, std::string* diagnostic = nullptr);
    std::vector<ControllerId> controllers() const;
    bool selectController(std::optional<ControllerId> id, std::string* diagnostic = nullptr);
    std::optional<ControllerId> selectedController() const;
    void setFocused(bool focused);
    void setEnabled(bool enabled);
    bool focused() const;
    bool enabled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void checkThread() const;
    friend class SdlActionInput;
};
} // namespace engine::input
