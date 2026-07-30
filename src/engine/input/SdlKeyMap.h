// src/engine/input/SdlKeyMap.h
#pragma once

#include "engine/input/InputEvent.h"

namespace engine::input {

// Maps SDL keycodes (SDLK_*) to engine InputEvent::Key.
// This is intentionally isolated to keep SDL details out of core application logic.
InputEvent::Key mapSdlKeyToEngineKey(int sdlKeycode);

} // namespace engine::input
