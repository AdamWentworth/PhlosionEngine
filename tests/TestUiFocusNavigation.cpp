#include "engine/ui/UiFocusNavigation.h"

#include "engine/input/InputActions.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace engine::ui;
using namespace engine::input;

namespace {

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

constexpr std::size_t ix(GamepadButton button) {
    return static_cast<std::size_t>(button);
}

std::array<UiFocusItem, 4> row() {
    return {{{"a", {0, 0, 40, 40}},
             {"b", {100, 0, 40, 40}},
             {"c", {200, 0, 40, 40}},
             {"d", {300, 0, 40, 40}}}};
}

void same(const UiRouteResult& a, const UiRouteResult& b) {
    CHECK(a.focusedId == b.focusedId);
    CHECK(a.activatedId == b.activatedId);
    CHECK(a.focusChanged == b.focusChanged);
    CHECK(a.cancelRequested == b.cancelRequested);
    CHECK(a.blocksGameplay == b.blocksGameplay);
}

void checkSpatialFocusAndActivation() {
    UiFocusNavigator nav;
    std::array<UiFocusItem, 4> items{{
        {"fight", {0, 0, 80, 40}},
        {"disabled", {100, 0, 80, 40}, false},
        {"bag", {200, 0, 80, 40}},
        {"run", {0, 100, 80, 40}}}};
    UiRouteResult result;
    std::string error;
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(error.empty() && result.focusedId == "fight");
    CHECK(result.focusChanged && result.blocksGameplay);
    UiControls right;
    right.direction.x = 1.0f;
    CHECK(nav.route(items, right, 0.016f, true, result, &error));
    CHECK(result.focusedId == "bag" && result.focusChanged);
    UiControls down;
    down.direction.y = 1.0f;
    CHECK(nav.route(items, down, 0.016f, true, result, &error));
    CHECK(result.focusedId == "run");
    UiControls both;
    both.acceptPressed = true;
    both.cancelPressed = true;
    CHECK(nav.route(items, both, 0.016f, true, result, &error));
    CHECK(result.cancelRequested && result.activatedId.empty());
    UiControls accept;
    accept.acceptPressed = true;
    CHECK(nav.route(items, accept, 0.016f, true, result, &error));
    CHECK(result.activatedId == "run" && !result.cancelRequested);
    CHECK(nav.setFocus(items, "bag", &error));
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(result.focusedId == "bag");
    CHECK(!nav.setFocus(items, "disabled", &error));
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(result.focusedId == "bag");
    items[2].enabled = false;
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(result.focusedId == "fight" && result.focusChanged);
    nav.reset();
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(result.focusedId == "fight" && result.focusChanged);
}

void checkRepeatAndModalRearm() {
    UiFocusNavigator nav;
    const auto items = row();
    UiRouteResult result;
    CHECK(nav.route(items, {}, 0.0f, true, result));
    UiControls right;
    right.direction.x = 1.0f;
    CHECK(nav.route(items, right, 0.0f, true, result));
    CHECK(result.focusedId == "b");
    CHECK(nav.route(items, right, 0.20f, true, result));
    CHECK(result.focusedId == "b");
    CHECK(nav.route(items, right, 0.16f, true, result));
    CHECK(result.focusedId == "c");
    CHECK(nav.route(items, right, 0.12f, true, result));
    CHECK(result.focusedId == "d");
    UiControls left;
    left.direction.x = -1.0f;
    left.acceptPressed = true;
    CHECK(nav.route(items, left, 0.016f, false, result));
    CHECK(result.focusedId == "d" && result.activatedId.empty());
    CHECK(!result.blocksGameplay);
    CHECK(nav.route(items, left, 0.016f, true, result));
    CHECK(result.focusedId == "d" && result.activatedId.empty());
    CHECK(result.blocksGameplay);
    CHECK(nav.route(items, {}, 0.016f, true, result));
    CHECK(result.focusedId == "d");
    left.acceptPressed = false;
    CHECK(nav.route(items, left, 0.016f, true, result));
    CHECK(result.focusedId == "c");
    nav.reset();
    CHECK(nav.route(items, right, 0.016f, true, result));
    CHECK(result.focusedId == "a");
    CHECK(nav.route(items, {}, 0.016f, true, result));
    CHECK(nav.route(items, right, 0.016f, true, result));
    CHECK(result.focusedId == "b");
}

void checkNamedKeyboardAndControllerActions() {
    InputActions actions;
    CHECK(actions.define({"ui.move", ActionKind::Axis2D,
        {KeyBinding{InputEvent::Key::Left, {-1, 0}},
         KeyBinding{InputEvent::Key::Right, {1, 0}},
         ButtonBinding{GamepadButton::DpadRight, {1, 0}}}}));
    CHECK(actions.define({"ui.accept", ActionKind::Button,
        {KeyBinding{InputEvent::Key::Space},
         ButtonBinding{GamepadButton::South}}}));
    CHECK(actions.define({"ui.cancel", ActionKind::Button,
        {KeyBinding{InputEvent::Key::Escape},
         ButtonBinding{GamepadButton::East}}}));
    UiFocusNavigator nav;
    const auto items = row();
    UiRouteResult result;
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.0f, true, result));
    CHECK(result.focusedId == "a");
    CHECK(actions.ingest(InputEvent::KeyDownEvent(InputEvent::Key::Right)));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    CHECK(result.focusedId == "b");
    CHECK(actions.ingest(InputEvent::KeyDownEvent(InputEvent::Key::Space)));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    CHECK(result.activatedId == "b");
    CHECK(actions.ingest(InputEvent::KeyUpEvent(InputEvent::Key::Right)));
    CHECK(actions.ingest(InputEvent::KeyUpEvent(InputEvent::Key::Space)));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    ControllerId id;
    CHECK(actions.addController(id));
    CHECK(actions.selectController(id));
    CHECK(actions.submit(id, {}));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    ControllerSnapshot pad;
    pad.buttons[ix(GamepadButton::DpadRight)] = true;
    CHECK(actions.submit(id, pad));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    CHECK(result.focusedId == "c");
    pad.buttons[ix(GamepadButton::South)] = true;
    CHECK(actions.submit(id, pad));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    CHECK(result.activatedId == "c");
    pad.buttons[ix(GamepadButton::East)] = true;
    CHECK(actions.submit(id, pad));
    actions.advanceFrame();
    CHECK(nav.routeActions(items, actions, {}, 0.016f, true, result));
    CHECK(result.cancelRequested && result.activatedId.empty());
}

void checkInvalidFramesAndOwnership() {
    UiFocusNavigator nav;
    const auto items = row();
    UiRouteResult result;
    std::string error;
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    const UiRouteResult prior = result;
    auto duplicate = items;
    duplicate[1].id = "a";
    CHECK(!nav.route(duplicate, {}, 0.0f, true, result, &error));
    CHECK(!error.empty());
    same(result, prior);
    UiControls invalid;
    invalid.direction.x = (std::numeric_limits<float>::quiet_NaN)();
    CHECK(!nav.route(items, invalid, 0.0f, true, result, &error));
    same(result, prior);
    CHECK(!nav.route(items, {}, -0.1f, true, result, &error));
    same(result, prior);
    InputActions actions;
    CHECK(!nav.routeActions(items, actions, {}, 0.0f, true, result, &error));
    same(result, prior);
    CHECK(!nav.setFocus(items, "missing", &error));
    CHECK(nav.route(items, {}, 0.0f, true, result, &error));
    CHECK(result.focusedId == "a" && !result.focusChanged);
    bool wrongThreadRejected = false;
    std::thread worker([&] {
        try {
            UiRouteResult unused;
            (void)nav.route(items, {}, 0.0f, true, unused);
        } catch (const std::logic_error&) {
            wrongThreadRejected = true;
        }
    });
    worker.join();
    CHECK(wrongThreadRejected);
}

} // namespace

int main() {
    try {
        checkSpatialFocusAndActivation();
        checkRepeatAndModalRearm();
        checkNamedKeyboardAndControllerActions();
        checkInvalidFramesAndOwnership();
        std::cout << "[PASS] UI focus contracts\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] UI focus contracts: " << exception.what() << '\n';
        return 1;
    }
}
