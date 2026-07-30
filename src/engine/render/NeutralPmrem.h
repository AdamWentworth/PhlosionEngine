#pragma once

#include <cstdint>
#include <vector>

namespace engine::render::neutral_pmrem {

struct Atlas {
    int width = 0;
    int height = 0;
    int cubeSize = 0;
    int lodMax = 0;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    float maxMip = 0.0f;
    float rgbmRange = 16.0f;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint16_t> rgba16f;
};

// Lazily builds and caches a runtime PMREM atlas matching three.js cubeUV layout.
const Atlas& getNeutralRoomPmremAtlas();

} // namespace engine::render::neutral_pmrem
