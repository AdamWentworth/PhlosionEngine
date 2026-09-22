#pragma once

#include "engine/input/InputActions.h"
#include <SDL_events.h>

namespace engine::input {
struct SdlInputDevice {
    ControllerId id;
    SDL_JoystickID instanceId = -1;
    std::string name;
};

// Owns one SDL game-controller subsystem reference, never the event queue/window.
// Destroy on the owner thread before InputActions and before external SDL_Quit.
class SdlActionInput {
public:
    explicit SdlActionInput(InputActions& actions, Uint32 windowId = 0);
    ~SdlActionInput();
    SdlActionInput(const SdlActionInput&) = delete;
    SdlActionInput& operator=(const SdlActionInput&) = delete;
    SdlActionInput(SdlActionInput&&) = delete;
    SdlActionInput& operator=(SdlActionInput&&) = delete;

    // Explicit startup discovery; partial progress is retained when a device fails.
    bool refresh(std::string* diagnostic = nullptr);
    bool ingest(const SDL_Event& event, std::string* diagnostic = nullptr);
    bool poll(std::string* diagnostic = nullptr);
    std::vector<SdlInputDevice> devices() const;
    bool controllerForInstance(SDL_JoystickID instance, ControllerId& output,
                               std::string* diagnostic = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void checkThread() const;
};
} // namespace engine::input
