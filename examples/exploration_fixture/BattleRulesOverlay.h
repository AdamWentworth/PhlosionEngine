#pragma once

#include "engine/render/IRenderBackend.h"
#include <nlohmann/json.hpp>
#include <string>

namespace engine::exploration_fixture {
void drawBattleRulesOverlay(IRenderBackend& renderer, const nlohmann::json& state,
                            int width, int height,
                            const std::string& portraitRoot,
                            const std::string& fontAtlasPath,
                            const std::string& statusPlateAtlasPath,
                            const std::string& statusDetailAtlasPath,
                            const std::string& commandFrameAtlasPath,
                            const std::string& commandFontAtlasPath,
                            const std::string& moveFontAtlasPath,
                            const std::string& messageFontAtlasPath,
                            const std::string& moveTypeAtlasPath);
int battleRulesOptionAt(const nlohmann::json& state, int x, int y,
                        int width, int height);
}
