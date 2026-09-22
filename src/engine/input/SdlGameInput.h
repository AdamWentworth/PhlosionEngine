#pragma once
#include "engine/editor/EditorSdlGameInput.h"
#include "engine/input/SdlActionInput.h"

namespace engine::input {
// Gate and mouse policy shared by SDL hosts. It never pumps the queue or
// advances actions; pending edges/deltas belong to the next simulation tick.
class SdlGameInput {
public:
    SdlGameInput(InputActions& actions, Uint32 window, bool selectAtStartup);
    void begin(const editor::EditorSdlGameInputContext& context);
    void event(const editor::EditorSdlGameInputContext& context, const SDL_Event& event);
    void end(const editor::EditorSdlGameInputContext& context);
    glm::vec2 takeLookDelta();
    float takeZoomDelta();
    std::vector<SdlInputDevice> devices() const { return adapter_.devices(); }
    bool dragging() const { return dragging_; }
private:
    void gate(const editor::EditorSdlGameInputContext& context);
    InputActions& actions_;
    SdlActionInput adapter_;
    bool dragging_ = false;
    glm::vec2 look_{0};
    float zoom_ = 0;
};
} // namespace engine::input
