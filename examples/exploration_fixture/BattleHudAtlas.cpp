#include "BattleHudAtlas.h"

#include <stb_image.h>

#include <array>
#include <stdexcept>

namespace engine::exploration_fixture {
namespace {

constexpr float atlasWidth = 520.0f;
constexpr float atlasHeight = 190.0f;

IRenderBackend::DebugSprite sprite(const std::string& path,
                                   float x, float y, float w, float h,
                                   float u, float v, float uw, float vh) {
    IRenderBackend::DebugSprite result;
    result.x = x;
    result.y = y;
    result.w = w;
    result.h = h;
    result.u0 = u / atlasWidth;
    result.v0 = v / atlasHeight;
    result.u1 = (u + uw) / atlasWidth;
    result.v1 = (v + vh) / atlasHeight;
    result.texturePath = path;
    return result;
}

} // namespace

BattleHudAtlas::BattleHudAtlas(const std::string& path) : path_(path) {
    if (path.empty()) return;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width != static_cast<int>(atlasWidth) ||
        height != static_cast<int>(atlasHeight)) {
        stbi_image_free(pixels);
        throw std::runtime_error("Battle HUD atlas must be the recovered 520x190 RGBA sheet: " + path);
    }
    rgba_.assign(pixels, pixels + width * height * 4);
    stbi_image_free(pixels);
}

void BattleHudAtlas::draw(IRenderBackend& renderer, int width, int height) const {
    if (rgba_.empty()) return;
    constexpr float panelWidth = 200.0f;
    constexpr float panelHeight = 78.0f;
    const float right = static_cast<float>(width) - panelWidth - 12.0f;
    const float bottom = static_cast<float>(height) - panelHeight - 12.0f;
    const std::array sprites{
        sprite(path_, 12, 32, panelWidth, panelHeight, 111, 31, 151, 58),
        sprite(path_, right, 32, panelWidth, panelHeight, 111, 31, 151, 58),
        sprite(path_, 12, bottom, panelWidth, panelHeight, 111, 31, 151, 58),
        sprite(path_, right, bottom, panelWidth, panelHeight, 111, 31, 151, 58),
    };
    renderer.drawDebugSprites(sprites.data(), sprites.size(), width, height);
}

} // namespace engine::exploration_fixture
