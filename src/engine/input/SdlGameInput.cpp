#include "SdlGameInput.h"
#include <stdexcept>
#include <utility>
#include <glm/common.hpp>

namespace engine::input {
namespace { void require(bool ok, const std::string& e) { if (!ok) throw std::runtime_error(e); } }
SdlGameInput::SdlGameInput(InputActions& actions, Uint32 window, bool selectAtStartup)
    : actions_(actions), adapter_(actions, window) {
    actions_.setEnabled(false);
    std::string error;
    require(adapter_.refresh(&error), error);
    const auto devices = adapter_.devices();
    if (selectAtStartup && !devices.empty()) require(actions_.selectController(devices.front().id, &error), error);
}
void SdlGameInput::gate(const editor::EditorSdlGameInputContext& c) {
    actions_.setFocused(c.windowFocused);
    actions_.setEnabled(editor::gameInputEnabled(c));
    if (!editor::gameInputEnabled(c)) { dragging_ = false; look_ = {}; zoom_ = 0; }
}
void SdlGameInput::begin(const editor::EditorSdlGameInputContext& c) { gate(c); }
void SdlGameInput::event(const editor::EditorSdlGameInputContext& c, const SDL_Event& e) {
    gate(c);
    std::string error;
    // Controller discovery and releases continue when gameplay is gated.
    require(adapter_.ingest(e, &error), error);
    if (!editor::gameInputEnabled(c)) return;
    if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        ControllerId id;
        if (adapter_.controllerForInstance(e.cbutton.which, id, &error)) require(actions_.selectController(id, &error), error);
    }
    if (e.type == SDL_KEYDOWN && e.key.windowID == c.windowId && !e.key.repeat && e.key.keysym.sym == SDLK_TAB) {
        const auto devices = adapter_.devices();
        if (!devices.empty()) {
            std::size_t next = 0;
            for (std::size_t i = 0; i < devices.size(); ++i)
                if (actions_.selectedController() == devices[i].id) next = (i + 1) % devices.size();
            require(actions_.selectController(devices[next].id, &error), error);
        }
    }
    if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) &&
        e.button.windowID == c.windowId && e.button.button == SDL_BUTTON_RIGHT) {
        dragging_ = e.type == SDL_MOUSEBUTTONDOWN && c.viewportHovered && editor::gamePointerInside(c, e.button.x, e.button.y);
    }
    if (e.type == SDL_MOUSEMOTION && e.motion.windowID == c.windowId && dragging_ &&
        editor::gamePointerInside(c, e.motion.x, e.motion.y)) {
        look_ += glm::vec2(-static_cast<float>(e.motion.xrel), static_cast<float>(e.motion.yrel)) * .006f;
        look_ = glm::clamp(look_, glm::vec2(-20), glm::vec2(20));
    }
    if (e.type == SDL_MOUSEWHEEL && e.wheel.windowID == c.windowId && c.viewportHovered) {
        zoom_ = std::clamp(zoom_ - static_cast<float>(e.wheel.y) * .5f, -100.0f, 100.0f);
    }
}
void SdlGameInput::end(const editor::EditorSdlGameInputContext& c) {
    gate(c); std::string error; require(adapter_.poll(&error), error);
}
glm::vec2 SdlGameInput::takeLookDelta() { return std::exchange(look_, glm::vec2(0)); }
float SdlGameInput::takeZoomDelta() { return std::exchange(zoom_, 0.0f); }
} // namespace engine::input
