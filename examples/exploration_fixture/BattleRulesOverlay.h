#pragma once

#include "engine/render/IRenderBackend.h"
#include <nlohmann/json.hpp>

namespace engine::exploration_fixture {
void drawBattleRulesOverlay(IRenderBackend& renderer, const nlohmann::json& state,
                            int width, int height);
}
