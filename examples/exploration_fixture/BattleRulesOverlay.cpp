#include "BattleRulesOverlay.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace engine::exploration_fixture {
namespace {
using Quad = IRenderBackend::DebugQuad;
using Rows = std::array<unsigned char, 7>;
struct Color { float r, g, b, a; };
constexpr Color ink{.94f, .96f, 1.0f, 1.0f};
constexpr Color selected{.86f, .11f, .08f, 1.0f};
constexpr Color muted{.48f, .53f, .60f, 1.0f};
constexpr Color shadow{.015f, .025f, .035f, 1.0f};

std::string upperAscii(std::string value) {
    for (char& c : value)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

Rows glyph(char c) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,14};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {31,4,4,4,4,4,31};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,30,1,1,17,14};
    case '6': return {6,8,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,2,12};
    case '/': return {1,1,2,4,8,16,16};
    case '-': return {0,0,0,31,0,0,0};
    case ':': return {0,4,4,0,4,4,0};
    case '.': return {0,0,0,0,0,12,12};
    case '!': return {4,4,4,4,4,0,4};
    case 39: return {4,4,2,0,0,0,0};
    case '>': return {16,8,4,2,4,8,16};
    default: return {};
    }
}

void box(std::vector<Quad>& quads, float x, float y, float w, float h, Color c) {
    const float left = std::round(x), top = std::round(y);
    const float right = std::round(x + w), bottom = std::round(y + h);
    if (right > left && bottom > top)
        quads.push_back({left,top,right-left,bottom-top,c.r,c.g,c.b,c.a});
}

void label(std::vector<Quad>& quads, std::string value, float x, float y,
           Color color, float scale, int maxChars) {
    if (static_cast<int>(value.size()) > maxChars) value.resize(maxChars);
    for (char c : value) {
        const auto rows = glyph(c);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5;) {
                if (!(rows[row] & (1u << (4 - col)))) { ++col; continue; }
                const int start = col;
                while (col < 5 && (rows[row] & (1u << (4 - col)))) ++col;
                box(quads, x + start*scale, y + row*scale,
                    (col - start)*scale, scale, color);
            }
        x += 6*scale;
    }
}

struct Rect { float x, y, w, h; };

struct Layout {
    float s, width, height;
    explicit Layout(int w, int h)
        : s(std::min(static_cast<float>(h) / 338.0f,
                     static_cast<float>(w) / 600.0f)),
          width(static_cast<float>(w)), height(static_cast<float>(h)) {}
    Rect status(int index) const {
        const float x = (index == 0 || index == 2 ? 38.0f : 15.0f) * s;
        const float y = (index == 0 || index == 2 ? 53.0f : 121.0f) * s;
        return {index < 2 ? x : width - x - 150.0f*s, y, 150.0f*s, 52.0f*s};
    }
    Rect portrait(int index) const {
        const auto bar = status(index);
        return {index < 2 ? bar.x + bar.w : bar.x - 56.0f*s,
                bar.y - 4.0f*s, 56.0f*s, 56.0f*s};
    }
    Rect title() const { return {38.0f*s, height - 155.0f*s, 236.0f*s, 36.0f*s}; }
    Rect menu() const { return {38.0f*s, height - 121.0f*s, 280.0f*s, 82.0f*s}; }
    Rect option(const std::string& screen, int index) const {
        const auto m = menu();
        if (screen == "command" || screen == "move") {
            if (index >= 4) return {title().x + title().w - 58.0f*s,
                                    title().y + 9.0f*s, 52.0f*s, 24.0f*s};
            const int column = index / 2;
            const int row = index % 2;
            return {m.x + (column ? 144.0f : 17.0f)*s,
                    m.y + (row ? 45.0f : 14.0f)*s, 116.0f*s, 29.0f*s};
        }
        return {m.x + 17.0f*s, m.y + (8.0f + index*20.0f)*s,
                280.0f*s, 21.0f*s};
    }
};

void fallbackTextLabel(std::vector<Quad>& quads, const std::string& value,
               float x, float y, Color color, float scale, int maxChars) {
    label(quads, value, x + scale*.5f, y + scale*.5f, shadow, scale, maxChars);
    label(quads, value, x, y, color, scale, maxChars);
}

struct FontAtlas {
    std::string path;
    std::array<int, 96> advances{};
    std::vector<unsigned char> alpha;
    explicit FontAtlas(const std::string& png) : path(png) {
        std::ifstream file(std::filesystem::path(png).parent_path() / "manifest.json");
        nlohmann::json manifest;
        if (!(file >> manifest) ||
            std::filesystem::path(manifest.at("atlas").get<std::string>()).filename() !=
                std::filesystem::path(png).filename() ||
            manifest.at("cell") != nlohmann::json::array({24, 24}) ||
            manifest.at("columns") != 21 ||
            manifest.at("texture_size") != nlohmann::json::array({512, 512}) ||
            manifest.at("first_codepoint") != 32 ||
            manifest.at("advances_ascii").size() != advances.size())
            throw std::runtime_error("Invalid GameCube battle font manifest");
        for (std::size_t i = 0; i < advances.size(); ++i)
            advances[i] = manifest.at("advances_ascii").at(i).get<int>();
        int width = 0, height = 0, channels = 0;
        unsigned char* rgba = stbi_load(png.c_str(), &width, &height, &channels, 4);
        if (!rgba || width != 512 || height != 512) {
            stbi_image_free(rgba);
            throw std::runtime_error("Invalid GameCube battle font atlas: " + png);
        }
        alpha.resize(512 * 512);
        for (std::size_t i = 0; i < alpha.size(); ++i) alpha[i] = rgba[i * 4 + 3];
        stbi_image_free(rgba);
    }
};

void textLabel(std::vector<Quad>& quads, const FontAtlas& font,
               const std::string& value,
               float x, float y, Color color, float scale, int maxChars,
               float heightScale = 1.0f, bool bold = false) {
    if (font.path.empty()) {
        fallbackTextLabel(quads, value, x, y, color, scale, maxChars);
        return;
    }
    const float glyphScale = scale * (7.0f / 18.0f);
    int count = 0;
    for (unsigned char c : value) {
        if (count++ >= maxChars) break;
        if (c < 32 || c > 127) continue;
        const int index = c - 32;
        if (c != ' ') {
            const int glyphWidth = std::max(1, static_cast<int>(std::round(24.0f * glyphScale)));
            const int glyphHeight = std::max(1, static_cast<int>(std::round(
                24.0f * glyphScale * heightScale)));
            const int atlasX = (index % 21) * 24;
            const int atlasY = (index / 21) * 24;
            const auto drawGlyph = [&](Color tint, float offset) {
                for (int row = 0; row < glyphHeight; ++row) {
                    const int sampleY = atlasY + row * 24 / glyphHeight;
                    for (int col = 0; col < glyphWidth;) {
                        const int sampleX = atlasX + col * 24 / glyphWidth;
                        const unsigned char opacity = font.alpha[sampleY * 512 + sampleX];
                        if (!opacity) { ++col; continue; }
                        const int start = col;
                        do {
                            ++col;
                        } while (col < glyphWidth &&
                            font.alpha[sampleY * 512 + atlasX + col * 24 / glyphWidth] == opacity);
                        box(quads, std::round(x + offset) + start,
                            std::round(y + offset) + row,
                            static_cast<float>(col - start), 1.0f,
                            {tint.r, tint.g, tint.b, tint.a * opacity / 255.0f});
                    }
                }
            };
            drawGlyph(shadow, 1.0f);
            if (bold) drawGlyph(color, 1.0f);
            drawGlyph(color, 0.0f);
        }
        x += (font.advances[index] + 1.0f) * glyphScale;
    }
}

float textWidth(const FontAtlas& font, const std::string& value, float scale) {
    if (font.path.empty()) return value.size() * 6.0f * scale;
    float width = 0;
    for (unsigned char c : value)
        if (c >= 32 && c <= 127)
            width += (font.advances[c - 32] + 1.0f) * scale * (7.0f / 18.0f);
    return width;
}

float fittedTextScale(const FontAtlas& font, const std::string& value,
                      float preferred, float availableWidth) {
    const float width = textWidth(font, value, preferred);
    return width > availableWidth ? preferred * availableWidth / width : preferred;
}

void genderSymbol(std::vector<Quad>& quads, const std::string& gender,
                  float x, float y, float s) {
    const Color color = gender == "female" ? Color{.91f,.18f,.35f,1} :
                                              Color{.17f,.57f,.96f,1};
    constexpr std::array<int, 7> widths{3,5,7,7,7,5,3};
    for (int row = 0; row < 7; ++row) {
        const int inset = (7 - widths[row]) / 2;
        box(quads, x + inset*s, y + (row + 2)*s, s, s, color);
        box(quads, x + (6 - inset)*s, y + (row + 2)*s, s, s, color);
    }
    box(quads, x + 2*s, y + 2*s, 3*s, s, color);
    box(quads, x + 2*s, y + 8*s, 3*s, s, color);
    if (gender == "female") {
        box(quads, x + 3*s, y + 9*s, s, 4*s, color);
        box(quads, x + s, y + 11*s, 5*s, s, color);
    } else {
        box(quads, x + 6*s, y, 4*s, s, color);
        box(quads, x + 9*s, y, s, 4*s, color);
        box(quads, x + 6*s, y + 2*s, 2*s, s, color);
    }
}

void softMessagePanel(std::vector<Quad>& quads, Rect rect, float scale) {
    const int height = static_cast<int>(std::round(rect.h));
    const float fade = 9.0f * scale;
    const float radius = 13.0f * scale;
    for (int row = 0; row < height; ++row) {
        const float dy = std::min(row + .5f, height - row - .5f);
        const float vertical = std::min(1.0f, dy / fade);
        const float corner = dy < radius ?
            radius - std::sqrt(std::max(0.0f, radius * radius -
                                       (radius - dy) * (radius - dy))) : 0.0f;
        for (int edge = 0; edge < 9; ++edge) {
            const float opacity = .72f * vertical * (edge + .5f) / 9.0f;
            box(quads, rect.x + corner + edge * scale, rect.y + row,
                scale, 1.0f, {0, 0, 0, opacity});
            box(quads, rect.x + rect.w - corner - (edge + 1) * scale,
                rect.y + row, scale, 1.0f, {0, 0, 0, opacity});
        }
        box(quads, rect.x + corner + fade, rect.y + row,
            rect.w - 2.0f * (corner + fade), 1.0f,
            {0, 0, 0, .72f * vertical});
    }
}

IRenderBackend::DebugSprite atlasSprite(const std::string& path, Rect rect) {
    IRenderBackend::DebugSprite sprite;
    sprite.x = std::round(rect.x); sprite.y = std::round(rect.y);
    sprite.w = std::round(rect.x + rect.w) - sprite.x;
    sprite.h = std::round(rect.y + rect.h) - sprite.y;
    sprite.u0 = 111.0f / 520.0f; sprite.v0 = 31.0f / 190.0f;
    sprite.u1 = 262.0f / 520.0f; sprite.v1 = 89.0f / 190.0f;
    sprite.texturePath = path;
    return sprite;
}

IRenderBackend::DebugSprite faceSprite(const std::string& path, Rect rect,
                                       bool flipHorizontal) {
    IRenderBackend::DebugSprite sprite;
    sprite.x = std::round(rect.x); sprite.y = std::round(rect.y);
    sprite.w = std::round(rect.w);
    sprite.h = std::round(rect.h);
    sprite.u0 = flipHorizontal ? 1.0f : 0.0f;
    sprite.v0 = 0;
    sprite.u1 = flipHorizontal ? 0.0f : 1.0f;
    sprite.v1 = 1;
    sprite.texturePath = path;
    return sprite;
}

IRenderBackend::DebugSprite portraitFrameSprite(const std::string& path, Rect rect) {
    IRenderBackend::DebugSprite sprite;
    sprite.x = std::round(rect.x); sprite.y = std::round(rect.y);
    sprite.w = std::round(rect.x + rect.w) - sprite.x;
    sprite.h = std::round(rect.y + rect.h) - sprite.y;
    sprite.u0 = 3.0f / 256.0f; sprite.v0 = 2.0f / 128.0f;
    sprite.u1 = 62.0f / 256.0f; sprite.v1 = 60.0f / 128.0f;
    sprite.texturePath = path;
    return sprite;
}

void commandPanel(std::vector<Quad>& quads, Rect rect, float s) {
    box(quads, rect.x + 2*s, rect.y + 3*s, rect.w, rect.h,
        {.01f,.02f,.025f,.55f});
    box(quads, rect.x + 4*s, rect.y, rect.w - 8*s, rect.h,
        {.42f,.49f,.50f,1});
    box(quads, rect.x, rect.y + 4*s, rect.w, rect.h - 8*s,
        {.42f,.49f,.50f,1});
    box(quads, rect.x + 3*s, rect.y + 3*s, rect.w - 6*s, rect.h - 6*s,
        {.68f,.73f,.71f,1});
    box(quads, rect.x + 5*s, rect.y + 5*s, rect.w - 10*s, rect.h - 10*s,
        {.12f,.19f,.20f,.82f});
    box(quads, rect.x + 8*s, rect.y + 7*s, rect.w - 16*s, s,
        {.82f,.86f,.83f,.50f});
    box(quads, rect.x + 8*s, rect.y + rect.h - 8*s, rect.w - 16*s, s,
        {.03f,.06f,.07f,.85f});
}

void portraitFrame(std::vector<Quad>& quads, Rect rect, bool active, float s) {
    box(quads, rect.x + 2*s, rect.y + 3*s, rect.w, rect.h,
        {.02f,.025f,.03f,.72f});
    box(quads, rect.x, rect.y, rect.w, rect.h,
        active ? Color{.67f,.20f,.19f,1} : Color{.61f,.67f,.67f,1});
    box(quads, rect.x + 3*s, rect.y + 3*s, rect.w - 6*s, rect.h - 6*s,
        active ? Color{.30f,.04f,.05f,1} : Color{.17f,.22f,.24f,1});
    box(quads, rect.x + 6*s, rect.y + 6*s, rect.w - 12*s, rect.h - 12*s,
        {.02f,.035f,.04f,1});
}

int targetPartyIndex(const nlohmann::json& state, int optionIndex) {
    const auto& options = state.at("options");
    if (optionIndex < 0 || optionIndex >= static_cast<int>(options.size())) return -1;
    const auto choice = options[optionIndex].at("label").get<std::string>();
    const auto& party = state.at("party");
    for (int i = 0; i < static_cast<int>(party.size()); ++i) {
        const auto name = party[i].at("name").get<std::string>();
        if (choice.compare(0, name.size(), name) == 0 &&
            (choice.size() == name.size() || choice[name.size()] == ' ')) return i;
    }
    return -1;
}

void targetArrow(std::vector<Quad>& quads, Rect portrait, bool ally, float s) {
    const float x = ally ? portrait.x + portrait.w + 3*s : portrait.x - 17*s;
    const float y = portrait.y + (portrait.h - 15*s)*.5f;
    for (int row = 0; row < 15; ++row) {
        const int extent = 7 - std::abs(row - 7);
        box(quads, x + (ally ? 11 - extent : 0)*s, y + row*s,
            (4 + extent)*s, s, selected);
    }
}

void partyBalls(std::vector<Quad>& quads, float x, float y, float s) {
    constexpr std::array<int, 13> widths{5,9,11,13,13,13,13,13,13,13,11,9,5};
    for (int i = 0; i < 6; ++i) {
        const float bx = x + i*17.0f*s;
        for (int row = 0; row < 13; ++row) {
            const int inset = (13 - widths[row]) / 2;
            box(quads, bx + inset*s, y + row*s, widths[row]*s, s,
                {.02f,.025f,.03f,1});
            if (row > 0 && row < 12 && row != 6) {
                box(quads, bx + (inset + 1)*s, y + row*s,
                    (widths[row] - 2)*s, s,
                    i < 4 ? Color{.19f,.25f,.28f,1} :
                    row < 6 ? Color{.86f,.12f,.10f,1} :
                              Color{.82f,.84f,.84f,1});
            }
        }
        box(quads, bx + 5*s, y + 5*s, 3*s, 3*s,
            {.91f,.92f,.91f,1});
    }
}
} // namespace

void drawBattleRulesOverlay(IRenderBackend& renderer, const nlohmann::json& state,
                            int width, int height, const std::string& atlasPath,
                            const std::string& portraitRoot,
                            const std::string& fontAtlasPath,
                            const std::string& portraitFrameAtlasPath) {
    if (width <= 0 || height <= 0) return;
    const Layout layout(width, height);
    const auto s = layout.s;
    static std::unordered_map<std::string, FontAtlas> fonts;
    const auto& font = fonts.try_emplace(fontAtlasPath, fontAtlasPath).first->second;
    std::vector<Quad> underlay;
    underlay.reserve(48);
    const auto& party = state.at("party");
    const std::string screen = state.at("screen").get<std::string>();
    const int highlighted = state.at("actor").get<int>();
    const int target = screen == "target" ?
        targetPartyIndex(state, state.at("cursor").get<int>()) : -1;
    std::vector<IRenderBackend::DebugSprite> sprites;
    sprites.reserve(14);
    for (int i = 0; i < 4; ++i) {
        const auto bar = layout.status(i);
        const auto portrait = layout.portrait(i);
        if (!atlasPath.empty()) sprites.push_back(atlasSprite(atlasPath, bar));
        else box(underlay, bar.x, bar.y, bar.w, bar.h,
                 {.06f,.11f,.13f,.95f});
        const bool active = i == highlighted && screen != "resolving" && screen != "target";
        const auto path = (std::filesystem::path(portraitRoot) /
                           party[i].at("portrait").get<std::string>()).string();
        const float faceSize = 42.0f*s;
        sprites.push_back(faceSprite(path,
            {portrait.x + (portrait.w - faceSize)*.5f,
             portrait.y + (portrait.h - faceSize)*.5f,
             faceSize, faceSize}, i < 2));
        if (!portraitFrameAtlasPath.empty())
            sprites.push_back(portraitFrameSprite(portraitFrameAtlasPath, portrait));
        else portraitFrame(underlay, portrait, active, s);
    }
    const auto title = layout.title();
    const auto menu = layout.menu();
    if (screen == "resolving") {
        softMessagePanel(underlay,
            {25.0f*s, height - 112.0f*s, 242.0f*s, 102.0f*s}, s);
    } else if (screen == "target") {
        // Colosseum leaves the field clear while the portrait arrow chooses a target.
    } else {
        commandPanel(underlay, title, s);
        commandPanel(underlay, menu, s);
    }
    partyBalls(underlay, layout.status(0).x, layout.status(0).y - 17*s, s);
    partyBalls(underlay, width - 122*s, layout.status(2).y - 17*s, s);
    renderer.drawDebugQuads(underlay.data(), underlay.size(), width, height);
    renderer.drawDebugSprites(sprites.data(), sprites.size(), width, height);

    std::vector<Quad> quads;
    quads.reserve(16000);
    for (int i = 0; i < 4; ++i) {
        const auto bar = layout.status(i);
        const auto& p = party[i];
        box(quads, bar.x + 4*s, bar.y + 22*s, 142*s, 12*s,
            {.01f,.025f,.035f,1});
        const auto name = upperAscii(p.at("name").get<std::string>());
        const auto nameScale = fittedTextScale(font, name, 1.55f*s, 86.0f*s);
        textLabel(quads, font, name, bar.x + 9*s, bar.y + 3*s,
                  ink, nameScale, static_cast<int>(name.size()), 1.55f);
        genderSymbol(quads, p.at("gender").get<std::string>(),
                     bar.x + 9*s + textWidth(font, name, nameScale), bar.y + 7*s, s);
        textLabel(quads, font, "Lv", bar.x + 102*s, bar.y + 4*s,
                  {.94f,.82f,.37f,1}, 1.6f*s, 2, 1.45f);
        const auto level = std::to_string(p.at("level").get<int>());
        const float levelScale = 1.55f*s;
        textLabel(quads, font, level,
                  bar.x + 143*s - textWidth(font, level, levelScale), bar.y + 7*s,
                  ink, levelScale, 3, 1.4f, true);
        const int hp = p.at("hp").get<int>();
        const int maxHp = p.at("max_hp").get<int>();
        textLabel(quads, font, "HP", bar.x + 7*s, bar.y + 16*s,
                  {.82f,.35f,.27f,1}, 1.5f*s, 2, 1.3f, true);
        box(quads, bar.x + 27*s, bar.y + 22*s, 118*s, 11*s,
            {.01f,.02f,.03f,1});
        box(quads, bar.x + 29*s, bar.y + 24*s, 114*s, 7*s,
            {.34f,.42f,.42f,1});
        if (hp > 0) box(quads, bar.x + 29*s, bar.y + 24*s,
                        114.0f*s*hp/maxHp, 7*s,
                        hp * 4 <= maxHp ? Color{.93f,.30f,.30f,1} :
                        hp * 2 <= maxHp ? Color{.98f,.75f,.19f,1} :
                                            Color{.02f,.78f,.20f,1});
        if (i < 2) {
            const auto hpCount = std::to_string(hp) + " / " + std::to_string(maxHp);
            const float hpScale = fittedTextScale(font, hpCount, 2.45f*s, 106*s);
            textLabel(quads, font, hpCount,
                      bar.x + 40*s, bar.y + 29*s,
                      ink, hpScale, 10, .9f, true);
            box(quads, bar.x + 27*s, bar.y + 50*s, 119*s, 7*s,
                {.51f,.58f,.59f,1});
            box(quads, bar.x + 29*s, bar.y + 51*s, 115*s, 5*s,
                {.01f,.02f,.025f,1});
            const int expPercent = p.value("exp_percent", 0);
            if (expPercent > 0)
                box(quads, bar.x + 30*s, bar.y + 52*s,
                    113.0f*s*expPercent/100, 3*s,
                    {.16f,.28f,1.0f,1});
            textLabel(quads, font, "EXP", bar.x + 3*s, bar.y + 45*s,
                      {.96f,.77f,.28f,1}, 1.3f*s, 3, 1.1f, true);
        }
    }
    if (screen != "resolving" && screen != "target" &&
        highlighted >= 0 && highlighted < 4) {
        const auto portrait = layout.portrait(highlighted);
        box(quads, portrait.x + 4*s, portrait.y - 2*s,
            portrait.w - 8*s, 2*s, selected);
        box(quads, portrait.x + 4*s, portrait.y + portrait.h,
            portrait.w - 8*s, 2*s, selected);
        box(quads, portrait.x - 2*s, portrait.y + 4*s,
            2*s, portrait.h - 8*s, selected);
        box(quads, portrait.x + portrait.w, portrait.y + 4*s,
            2*s, portrait.h - 8*s, selected);
    }
    const std::string heading = screen == "command" || screen == "move"
        ? party[state.at("actor").get<int>()].at("name").get<std::string>()
        : screen == "resolving" ? "BATTLE" : state.at("title").get<std::string>();
    if (screen != "resolving" && screen != "target")
        textLabel(quads, font, upperAscii(heading), title.x + 14*s, title.y + 11*s,
                  ink, fittedTextScale(font, upperAscii(heading), 2.2f*s,
                                       title.w - 27*s), 28);
    if (screen == "resolving") {
        std::string message = upperAscii(state.at("message").get<std::string>());
        std::vector<std::string> lines;
        const auto used = message.find(" USED ");
        const auto on = message.find(" ON ", used == std::string::npos ? 0 : used + 6);
        const auto missed = message.find(" - MISSED", used == std::string::npos ? 0 : used + 6);
        if (used != std::string::npos && (on != std::string::npos || missed != std::string::npos)) {
            const auto moveEnd = on != std::string::npos ? on : missed;
            lines.push_back(message.substr(0, used) + " used");
            lines.push_back(message.substr(used + 6, moveEnd - used - 6) + "!");
            if (missed != std::string::npos) lines.push_back("But it missed!");
            else if (message.find(" - NO EFFECT") != std::string::npos)
                lines.push_back("It had no effect...");
            else if (message.find(" FAINTED") != std::string::npos)
                lines.push_back(message.substr(on + 4, message.find(" - ", on) - on - 4) +
                                " fainted!");
        } else {
            for (int line = 0; line < 3 && !message.empty(); ++line) {
                std::size_t split = message.size() > 23 ? message.rfind(' ', 23) :
                                    std::string::npos;
                if (split == std::string::npos || split < 8) split = message.size();
                lines.push_back(message.substr(0, split));
                message.erase(0, std::min(message.size(), split + 1));
            }
        }
        for (std::size_t line = 0; line < lines.size() && line < 3; ++line) {
            textLabel(quads, font, lines[line],
                      43.0f*s, height - (96.0f - 27.0f*line)*s,
                      ink, 2.35f*s, 25);
        }
    }
    const auto& options = state.at("options");
    for (int i = 0; screen != "target" && i < static_cast<int>(options.size()) && i < 5; ++i) {
        const auto slot = layout.option(screen, i);
        const bool enabled = options[i].at("enabled").get<bool>();
        const bool active = i == state.at("cursor").get<int>();
        if (active) textLabel(quads, font, ">>", slot.x, slot.y + 5*s,
                              selected, 1.8f*s, 2);
        std::string value = options[i].at("label").get<std::string>();
        if (screen == "command") {
            if (i == 1) value = "PKMN";
            if (i == 2) value = "ITEMS";
        } else if (screen == "move" && i < 4) {
            const auto& move = party[state.at("actor").get<int>()].at("moves").at(i);
            value = move.at("name").get<std::string>();
            textLabel(quads, font, "PP " + std::to_string(move.at("pp").get<int>()) +
                      "/" + std::to_string(move.at("max_pp").get<int>()),
                      slot.x + 22*s, slot.y + 19*s,
                      {.85f,.89f,.89f,1}, 1.0f*s, 14);
        }
        const auto display = upperAscii(value);
        textLabel(quads, font, display, slot.x + 22*s, slot.y + 5*s,
                  enabled ? ink : muted,
                  fittedTextScale(font, display,
                                  (screen == "move" ? 1.55f : 2.1f)*s,
                                  slot.w - 25*s),
                  static_cast<int>(display.size()));
    }
    if (target >= 0) targetArrow(quads, layout.portrait(target), target < 2, s);
    for (std::size_t first = 0; first < quads.size(); first += 3500)
        renderer.drawDebugQuads(quads.data() + first,
                                std::min<std::size_t>(3500, quads.size() - first),
                                width, height);
}

int battleRulesOptionAt(const nlohmann::json& state, int x, int y,
                        int width, int height) {
    if (width <= 0 || height <= 0) return -1;
    const Layout layout(width, height);
    const std::string screen = state.at("screen").get<std::string>();
    const auto& options = state.at("options");
    for (int index = 0; index < static_cast<int>(options.size()) && index < 5; ++index) {
        const int target = screen == "target" ? targetPartyIndex(state, index) : -1;
        if (screen == "target" && target < 0) continue;
        const auto rect = screen == "target" ? layout.portrait(target) :
                          layout.option(screen, index);
        if (x >= rect.x && x < rect.x + rect.w &&
            y >= rect.y && y < rect.y + rect.h) return index;
    }
    return -1;
}

} // namespace engine::exploration_fixture
