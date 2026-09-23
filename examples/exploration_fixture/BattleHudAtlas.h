#pragma once

#include "engine/render/IRenderBackend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::exploration_fixture {

class BattleHudAtlas {
public:
    explicit BattleHudAtlas(const std::string& path);
    bool available() const noexcept { return !rgba_.empty(); }
    void draw(IRenderBackend& renderer, int width, int height) const;

private:
    std::string path_;
    std::vector<std::uint8_t> rgba_;
};

} // namespace engine::exploration_fixture
