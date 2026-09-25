#pragma once

#include "engine/render/IRenderBackend.h"
#include <nlohmann/json.hpp>
#include <string>

namespace engine::exploration_fixture {
void drawBattleRulesOverlay(IRenderBackend& renderer, const nlohmann::json& state,
                            int width, int height, const std::string& atlasPath,
                            const std::string& portraitRoot,
                            const std::string& fontAtlasPath);
int battleRulesOptionAt(const nlohmann::json& state, int x, int y,
                        int width, int height);
}
