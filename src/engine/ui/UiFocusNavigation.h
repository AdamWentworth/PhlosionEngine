#pragma once

#include "engine/ui/Rect.h"

#include <glm/vec2.hpp>

#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace engine::input { class InputActions; }

namespace engine::ui {

struct UiFocusItem {
    std::string id;
    ::ui::Rect rect;
    bool enabled = true;
};

struct UiActionNames {
    std::string move = "ui.move";
    std::string accept = "ui.accept";
    std::string cancel = "ui.cancel";
};

struct UiControls {
    glm::vec2 direction{0.0f}; // X right, Y down; normally an Axis2D action.
    bool acceptDown = false;
    bool acceptPressed = false;
    bool cancelDown = false;
    bool cancelPressed = false;
};

struct UiRouteResult {
    std::string focusedId;
    std::string activatedId;
    bool focusChanged = false;
    bool cancelRequested = false;
    bool blocksGameplay = false;
};

// Renderer-free focus routing for one menu or dialogue surface. The caller
// provides visible items each frame and gates gameplay when blocksGameplay is
// true. All operations and destruction belong to the constructing thread.
class UiFocusNavigator {
public:
    UiFocusNavigator();
    ~UiFocusNavigator();
    UiFocusNavigator(const UiFocusNavigator&) = delete;
    UiFocusNavigator& operator=(const UiFocusNavigator&) = delete;
    UiFocusNavigator(UiFocusNavigator&&) = delete;
    UiFocusNavigator& operator=(UiFocusNavigator&&) = delete;

    bool route(std::span<const UiFocusItem> items, const UiControls& controls,
               float deltaSeconds, bool active, UiRouteResult& out,
               std::string* error = nullptr);
    bool routeActions(std::span<const UiFocusItem> items,
                      const engine::input::InputActions& actions,
                      const UiActionNames& names, float deltaSeconds,
                      bool active, UiRouteResult& out,
                      std::string* error = nullptr);
    bool setFocus(std::span<const UiFocusItem> items, std::string_view id,
                  std::string* error = nullptr);
    void reset();

private:
    enum class Direction { None, Left, Right, Up, Down };
    void checkOwner() const;
    const std::thread::id owner_;
    std::string focusedId_;
    Direction heldDirection_ = Direction::None;
    float repeatElapsed_ = 0.0f;
    bool repeating_ = false;
    bool needsNeutral_ = true;
};

} // namespace engine::ui
