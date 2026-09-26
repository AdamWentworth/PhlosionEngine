#include "BattleRulesOverlay.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
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
    float s, statusScale, width, height;
    explicit Layout(int w, int h)
        : s(std::min({1.0f, static_cast<float>(h) / 400.0f,
                     static_cast<float>(w) / 640.0f})),
          statusScale(std::min({1.0f, static_cast<float>(h) / 480.0f,
                                static_cast<float>(w) / 640.0f})),
          width(static_cast<float>(w)), height(static_cast<float>(h)) {}
    Rect statusOrigin(int index) const {
        const float x = (index == 0 ? 37.0f : index == 1 ? 13.0f :
                         index == 2 ? 378.0f : 402.0f);
        const float y = (index == 0 || index == 2 ? 48.0f : 114.0f);
        return {index < 2 ? x*statusScale : width - (640.0f-x)*statusScale,
                y*statusScale, 0, 0};
    }
    Rect status(int index) const {
        const auto origin = statusOrigin(index);
        return {origin.x + (index < 2 ? 3.0f : 60.0f)*statusScale,
                origin.y + 6.0f*statusScale,
                162.0f*statusScale, 50.0f*statusScale};
    }
    Rect portrait(int index) const {
        const auto origin = statusOrigin(index);
        return {origin.x + (index < 2 ? 165.0f : 2.0f)*statusScale,
                origin.y + 2.0f*statusScale,
                58.0f*statusScale, 58.0f*statusScale};
    }
    Rect title() const { return {38.0f*s, height - 155.0f*s, 236.0f*s, 36.0f*s}; }
    Rect menu() const { return {38.0f*s, height - 121.0f*s, 280.0f*s, 82.0f*s}; }
    Rect commandRect(float x, float y, float w, float h) const {
        return {x*statusScale, height - (480.0f-y)*statusScale,
                w*statusScale, h*statusScale};
    }
    Rect option(const std::string& screen, int index) const {
        if (screen == "command" && index < 4)
            return commandRect(index >= 2 ? 201.0f : 61.0f,
                               index % 2 ? 393.0f : 365.0f, 125.0f, 27.0f);
        if (screen == "move") {
            if (index >= 4) return {0,0,0,0};
            return commandRect(index >= 2 ? 244.0f : 61.0f,
                               index % 2 ? 393.0f : 365.0f,
                               index >= 2 ? 155.0f : 181.0f, 28.0f);
        }
        const auto m = menu();
        if (screen == "command") {
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
    bool retail = false;
    int cell = 24, columns = 21;
    std::array<int, 96> advances{};
    std::vector<unsigned char> alpha;
    explicit FontAtlas(const std::string& png) : path(png) {
        std::ifstream file(std::filesystem::path(png).parent_path() / "manifest.json");
        nlohmann::json manifest;
        if (!(file >> manifest) ||
            std::filesystem::path(manifest.at("atlas").get<std::string>()).filename() !=
                std::filesystem::path(png).filename() ||
            manifest.at("texture_size") != nlohmann::json::array({512, 512}) ||
            manifest.at("first_codepoint") != 32 ||
            manifest.at("advances_ascii").size() != advances.size())
            throw std::runtime_error("Invalid GameCube battle font manifest");
        cell = manifest.at("cell").at(0).get<int>();
        columns = manifest.at("columns").get<int>();
        if (manifest.at("cell") != nlohmann::json::array({cell,cell}) ||
            !((cell == 24 && columns == 21) || (cell == 32 && columns == 16)))
            throw std::runtime_error("Invalid GameCube battle font grid");
        retail = manifest.contains("source_sha1") && manifest.contains("font_id");
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
               float x, float y, Color color, float scale, int maxChars) {
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
            const int glyphWidth = std::max(1, static_cast<int>(std::round(font.cell * glyphScale)));
            const int glyphHeight = std::max(1, static_cast<int>(std::round(
                font.cell * glyphScale)));
            const int atlasX = (index % font.columns) * font.cell;
            const int atlasY = (index / font.columns) * font.cell;
            const auto drawGlyph = [&](Color tint, float offset) {
                for (int row = 0; row < glyphHeight; ++row) {
                    const int sampleY = atlasY + row * font.cell / glyphHeight;
                    for (int col = 0; col < glyphWidth;) {
                        const int sampleX = atlasX + col * font.cell / glyphWidth;
                        const unsigned char opacity = font.alpha[sampleY * 512 + sampleX];
                        if (!opacity) { ++col; continue; }
                        const int start = col;
                        do {
                            ++col;
                        } while (col < glyphWidth &&
                            font.alpha[sampleY * 512 + atlasX + col * font.cell / glyphWidth] == opacity);
                        box(quads, std::round(x + offset) + start,
                            std::round(y + offset) + row,
                            static_cast<float>(col - start), 1.0f,
                            {tint.r, tint.g, tint.b, tint.a * opacity / 255.0f});
                    }
                }
            };
            drawGlyph(font.retail ? Color{0, 0, 0, .75f} : shadow, 1.0f);
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

std::pair<float, float> messageInkRows(const FontAtlas& font,
                                      const std::vector<std::string>& lines,
                                      float scale, float linePitch) {
    const int glyphSize = std::max(1, static_cast<int>(std::round(
        font.cell * scale * (7.0f / 18.0f))));
    float top = std::numeric_limits<float>::infinity();
    float bottom = -std::numeric_limits<float>::infinity();
    for (std::size_t line = 0; line < lines.size(); ++line) {
        for (unsigned char c : lines[line]) {
            if (c <= 32 || c > 127) continue;
            const int index = c - 32;
            const int atlasX = (index % font.columns) * font.cell;
            const int atlasY = (index / font.columns) * font.cell;
            for (int row = 0; row < glyphSize; ++row) {
                const int sampleY = atlasY + row * font.cell / glyphSize;
                for (int col = 0; col < glyphSize; ++col) {
                    if (font.alpha[sampleY * 512 + atlasX +
                                   col * font.cell / glyphSize] < 128) continue;
                    const float y = static_cast<float>(line) * linePitch + row;
                    top = std::min(top, y);
                    bottom = std::max(bottom, y + 1.0f);
                    break;
                }
            }
        }
    }
    return std::isfinite(top) ? std::pair{top, bottom} :
        std::pair{0.0f, static_cast<float>(glyphSize)};
}

void softMessagePanel(std::vector<Quad>& quads, Rect rect, float scale) {
    // fn_8001EC08 draws an inner/outer vertex fan at radii 15/30.
    const float inner = 15.0f * scale, outer = 30.0f * scale;
    const int first = static_cast<int>(std::floor(rect.y - outer));
    const int last = static_cast<int>(std::ceil(rect.y + rect.h + outer));
    for (int row = first; row < last; ++row) {
        const float cy = row + .5f;
        const float dy = std::max({rect.y - cy, cy - rect.y - rect.h, 0.0f});
        if (dy >= outer) continue;
        const auto opacity = [&](float x) {
            const float dx = std::max({rect.x - x, x - rect.x - rect.w, 0.0f});
            const float distance = std::sqrt(dx*dx + dy*dy);
            return .5f * std::clamp((outer - distance) / (outer - inner), 0.0f, 1.0f);
        };
        box(quads, rect.x, static_cast<float>(row), rect.w, 1.0f,
            {0,0,0,opacity(rect.x + rect.w*.5f)});
        for (int edge = 0; edge < static_cast<int>(std::ceil(outer)); ++edge) {
            const float left = rect.x - edge - 1.0f;
            const float right = rect.x + rect.w + edge;
            box(quads, left, static_cast<float>(row), 1.0f, 1.0f,
                {0,0,0,opacity(left + .5f)});
            box(quads, right, static_cast<float>(row), 1.0f, 1.0f,
                {0,0,0,opacity(right + .5f)});
        }
    }
}

IRenderBackend::DebugSprite croppedSprite(const std::string& path, Rect rect,
                                          Rect crop, float atlasWidth,
                                          float atlasHeight) {
    IRenderBackend::DebugSprite sprite;
    sprite.x = std::round(rect.x); sprite.y = std::round(rect.y);
    sprite.w = std::round(rect.x + rect.w) - sprite.x;
    sprite.h = std::round(rect.y + rect.h) - sprite.y;
    sprite.u0 = crop.x / atlasWidth; sprite.v0 = crop.y / atlasHeight;
    sprite.u1 = (crop.x + crop.w) / atlasWidth;
    sprite.v1 = (crop.y + crop.h) / atlasHeight;
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
    return croppedSprite(path, rect, {3,2,58,58}, 256, 128);
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

IRenderBackend::DebugSprite targetCursorSprite(const std::string& atlas,
                                                const Layout& layout, int target) {
    // Menu 0xFF, items 0x1258..0x125B; negative source widths mirror allies.
    constexpr std::array<Rect, 4> positions{{
        {232,61,30,32}, {208,127,30,32},
        {348,61,30,32}, {371,127,30,32}}};
    const auto source = positions[target];
    const float t = layout.statusScale;
    const float x = target < 2 ? source.x*t :
                    layout.width - (640.0f-source.x)*t;
    auto sprite = croppedSprite(atlas,
        {x,source.y*t,source.w*t,source.h*t}, {214,204,30,32}, 256,239);
    if (target < 2) std::swap(sprite.u0, sprite.u1);
    return sprite;
}

std::vector<std::string> battleMessageLines(const nlohmann::json& state) {
    const std::string message = state.at("message").get<std::string>();
    const auto used = message.find(" USED ");
    if (used != std::string::npos) {
        const auto moveStart = used + 6;
        // Move names may contain spaces, so stop only at the result separator.
        const auto result = message.find(" ON ", moveStart);
        const auto missed = message.find(" - MISSED", moveStart);
        const auto end = result != std::string::npos ? result : missed;
        if (end != std::string::npos) {
            if (missed != std::string::npos && state.value("action_delay", 0) >= 12)
                return {upperAscii(message.substr(0,used)) + "'s attack missed!"};
            if (message.find(" - NO EFFECT") != std::string::npos &&
                state.value("action_delay", 0) >= 12)
                return {"It doesn't affect " +
                        upperAscii(message.substr(result + 4,
                            message.find(" - ",result) - result - 4)) + "..."};
            if (message.find(" FAINTED") != std::string::npos &&
                state.value("action_delay", 0) >= 12) {
                const auto fainted = message.find(" FAINTED");
                const auto nameStart = message.rfind(" - ", fainted);
                if (nameStart != std::string::npos)
                    return {upperAscii(message.substr(nameStart + 3, fainted-nameStart-3)) +
                            " fainted!"};
            }
            // Fight message 0x768D inserts msgctrlCR (control 0) after "used".
            return {upperAscii(message.substr(0,used)) + " used",
                    upperAscii(message.substr(moveStart,end-moveStart)) + "!"};
        }
    }
    auto possessive = message.find("'s ");
    if (possessive == std::string::npos) possessive = message.find("'S ");
    const auto finalSpace = message.rfind(' ');
    if (possessive != std::string::npos && finalSpace != std::string::npos &&
        finalSpace > possessive + 3) {
        const auto ending = message.substr(finalSpace + 1);
        if (ending == "fell!" || ending == "rose!")
            return {upperAscii(message.substr(0,possessive)) + "'s",
                    upperAscii(message.substr(possessive + 3,
                                              finalSpace - possessive - 3)),
                    ending};
    }
    std::vector<std::string> lines;
    std::size_t start = 0;
    do {
        const auto end = message.find('\n', start);
        lines.push_back(message.substr(start, end == std::string::npos ? end : end-start));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return lines;
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
                            int width, int height,
                            const std::string& portraitRoot,
                            const std::string& fontAtlasPath,
                            const std::string& portraitFrameAtlasPath,
                            const std::string& statusDetailAtlasPath,
                            const std::string& commandFrameAtlasPath,
                            const std::string& commandFontAtlasPath,
                            const std::string& moveFontAtlasPath,
                            const std::string& messageFontAtlasPath,
                            const std::string& moveTypeAtlasPath) {
    if (width <= 0 || height <= 0) return;
    const Layout layout(width, height);
    const auto s = layout.s;
    static std::unordered_map<std::string, FontAtlas> fonts;
    const auto& font = fonts.try_emplace(fontAtlasPath, fontAtlasPath).first->second;
    const auto& commandFont = fonts.try_emplace(commandFontAtlasPath,
        commandFontAtlasPath).first->second;
    const auto& moveFont = fonts.try_emplace(moveFontAtlasPath,
        moveFontAtlasPath).first->second;
    const auto& messageFont = fonts.try_emplace(messageFontAtlasPath,
        messageFontAtlasPath).first->second;
    std::vector<Quad> underlay;
    underlay.reserve(48);
    std::vector<Quad> messagePanel;
    const auto& party = state.at("party");
    const std::string screen = state.at("screen").get<std::string>();
    const auto narration = screen == "resolving" ? battleMessageLines(state) :
        std::vector<std::string>{};
    float messageScale = 0.0f, messageLinePitch = 0.0f, messageTextY = 0.0f;
    const int highlighted = state.at("actor").get<int>();
    const int target = screen == "target" ?
        targetPartyIndex(state, state.at("cursor").get<int>()) : -1;
    std::vector<IRenderBackend::DebugSprite> sprites;
    sprites.reserve(48);
    for (int i = 0; i < 4; ++i) {
        const auto origin = layout.statusOrigin(i);
        const auto bar = layout.status(i);
        const auto portrait = layout.portrait(i);
        const auto t = layout.statusScale;
        const auto& p = party[i];
        box(underlay, bar.x+3*t, bar.y+3*t, bar.w-6*t, bar.h-6*t,
            {.06f,.11f,.15f,1});
        sprites.push_back(croppedSprite(portraitFrameAtlasPath, bar,
                                        {61,7,162,50}, 256, 128));
        const bool active = i == highlighted && screen != "resolving" && screen != "target";
        const auto path = (std::filesystem::path(portraitRoot) /
                           party[i].at("portrait").get<std::string>()).string();
        const float faceSize = 42.0f*t;
        sprites.push_back(faceSprite(path,
            {origin.x + (i < 2 ? 173.0f : 10.0f)*t,
             origin.y + 10.0f*t,
             faceSize, faceSize}, i < 2));
        sprites.push_back(portraitFrameSprite(portraitFrameAtlasPath, portrait));
        if (active)
            sprites.push_back(croppedSprite(statusDetailAtlasPath,
                {portrait.x-t, portrait.y-t, 60*t, 60*t},
                {64,67,60,60}, 256, 239));
        const float rowX = origin.x + (i < 2 ? 6.0f : 63.0f)*t;
        sprites.push_back(croppedSprite(statusDetailAtlasPath,
            {rowX, origin.y+26*t, 156*t, 11*t},
            {2,22,156,11}, 256, 239));
        sprites.push_back(croppedSprite(statusDetailAtlasPath,
            {origin.x+(i < 2 ? 111.0f : 166.0f)*t,
             origin.y+14*t, 14*t, 9*t},
            {181,26,14,9}, 256, 239));
        const int hp = p.at("hp").get<int>();
        const int maxHp = p.at("max_hp").get<int>();
        if (hp > 0 && maxHp > 0) {
            const Rect gauge = {origin.x+(i < 2 ? 33.0f : 90.0f)*t,
                origin.y+28*t, 126.0f*t*hp/maxHp, 7*t};
            const float gaugeX = hp*4 <= maxHp ? 64.0f : hp*2 <= maxHp ? 32.0f : 0.0f;
            sprites.push_back(croppedSprite(statusDetailAtlasPath, gauge,
                {gaugeX,36,31,7}, 256, 239));
        }
        if (i < 2) {
            sprites.push_back(croppedSprite(statusDetailAtlasPath,
                {origin.x+7*t, origin.y+56*t, 154*t, 8*t},
                {2,12,154,8}, 256, 239));
            const int expPercent = std::clamp(p.value("exp_percent", 0), 0, 100);
            if (expPercent > 0)
                sprites.push_back(croppedSprite(statusDetailAtlasPath,
                    {origin.x+33*t, origin.y+58*t,
                     126.0f*t*expPercent/100.0f, 4*t},
                    {96,34,31,4}, 256, 239));
        }
    }
    const auto title = layout.title();
    const auto menu = layout.menu();
    if (screen == "resolving") {
        const float t = layout.statusScale;
        const float preferred = (18.0f/7.0f)*t;
        messageScale = preferred;
        for (const auto& line : narration)
            messageScale = std::min(messageScale,
                fittedTextScale(messageFont,line,preferred,523.0f*t));
        float messageWidth = 0.0f;
        for (const auto& line : narration)
            messageWidth = std::max(messageWidth,textWidth(messageFont,line,messageScale));
        // Font 0 advances one line by its 22-pixel nominal height plus the
        // 11-pixel default line-space value set by GSmsgGetRect.
        messageLinePitch = 33.0f*t;
        const float panelHeight = 24.0f*t +
            messageLinePitch*static_cast<float>(narration.size()-1);
        const float panelY = height - 42.0f*t - panelHeight;
        const auto [inkTop, inkBottom] = messageInkRows(
            messageFont, narration, messageScale, messageLinePitch);
        messageTextY = panelY + (panelHeight - (inkBottom-inkTop))*0.5f - inkTop;
        softMessagePanel(messagePanel,
            {53.0f*t, panelY,
             std::max(t,messageWidth - t), panelHeight}, t);
    } else if (screen == "target") {
        // Colosseum leaves the field clear while the portrait arrow chooses a target.
    } else if (screen == "command") {
        constexpr std::array<Rect, 5> destinations{{
            {37,319,32,122}, {69,319,203,122}, {272,319,32,122},
            {304,353,8,88}, {312,353,32,88}}};
        constexpr std::array<Rect, 5> sources{{
            {0,0,32,122}, {32,0,8,122}, {40,0,32,122},
            {72,34,8,88}, {112,34,32,88}}};
        for (std::size_t index = 0; index < destinations.size(); ++index) {
            const auto dest = destinations[index];
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(dest.x,dest.y,dest.w,dest.h),
                sources[index], 240, 200));
        }
        const int cursor = state.at("cursor").get<int>();
        if (cursor >= 0 && cursor < 4) {
            const float x = cursor >= 2 ? 206.0f : 66.0f;
            const float y = cursor % 2 ? 402.0f : 374.0f;
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(x,y+3,6,12), {109,127,6,12}, 240, 200));
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(x+6,y,10,18), {110,148,10,18}, 240, 200));
        }
    } else if (screen == "move") {
        // Retail menu 0x4C, items 0xB9..0xC0 in GC6E01 Start.dol.
        constexpr std::array<Rect, 8> destinations{{
            {37,319,32,122}, {69,319,203,122}, {272,319,32,122},
            {304,353,76,88}, {380,353,32,88}, {412,353,159,88},
            {571,353,32,88}, {404,373,95,46}}};
        constexpr std::array<Rect, 8> sources{{
            {0,0,32,122}, {32,0,8,122}, {40,0,32,122},
            {72,34,8,88}, {80,34,32,88}, {72,34,8,88},
            {112,34,32,88}, {2,122,94,46}}};
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const auto dest = destinations[index];
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(dest.x,dest.y,dest.w,dest.h),
                sources[index], 240, 200));
        }
        const int cursor = state.at("cursor").get<int>();
        if (cursor >= 0 && cursor < 4) {
            const float x = cursor >= 2 ? 234.0f : 66.0f;
            const float y = cursor % 2 ? 402.0f : 374.0f;
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(x,y+3,6,12), {109,127,6,12}, 240, 200));
            sprites.push_back(croppedSprite(commandFrameAtlasPath,
                layout.commandRect(x+6,y,10,18), {110,148,10,18}, 240, 200));
            const auto& moves = party[state.at("actor").get<int>()].at("moves");
            if (cursor < static_cast<int>(moves.size())) {
                const std::string type = moves.at(cursor).at("type").get<std::string>();
                constexpr std::array<std::pair<const char*, Rect>, 17> badges{{
                    {"grass", {6,7,79,27}}, {"flying", {90,7,79,27}},
                    {"poison", {173,6,79,27}}, {"water", {6,38,79,27}},
                    {"psychic", {90,38,79,27}}, {"ground", {6,69,79,27}},
                    {"ghost", {90,69,79,27}}, {"fire", {6,100,79,27}},
                    {"rock", {90,100,79,27}}, {"fighting", {6,131,79,27}},
                    {"steel", {90,131,79,27}}, {"dark", {6,162,79,27}},
                    {"electric", {90,162,79,27}}, {"normal", {6,193,79,27}},
                    {"dragon", {90,193,79,27}}, {"bug", {6,224,79,27}},
                    {"ice", {90,224,79,27}}}};
                Rect source{173,38,79,27}; // Retail ??? badge for an unknown type.
                for (const auto& [name, crop] : badges)
                    if (type == name) { source = crop; break; }
                sprites.push_back(croppedSprite(moveTypeAtlasPath,
                    layout.commandRect(502,393,79,28), source, 258, 258));
            }
        }
    } else {
        commandPanel(underlay, title, s);
        commandPanel(underlay, menu, s);
    }
    partyBalls(underlay, layout.statusOrigin(0).x, 34*layout.statusScale,
               layout.statusScale);
    partyBalls(underlay, width - 114*layout.statusScale,
               34*layout.statusScale, layout.statusScale);
    for (std::size_t first = 0; first < underlay.size(); first += 3500)
        renderer.drawDebugQuads(underlay.data() + first,
                                std::min<std::size_t>(3500,underlay.size()-first),
                                width,height);
    renderer.drawDebugSprites(sprites.data(), sprites.size(), width, height);
    for (std::size_t first = 0; first < messagePanel.size(); first += 3500)
        renderer.drawDebugQuads(messagePanel.data() + first,
                                std::min<std::size_t>(3500,messagePanel.size()-first),
                                width,height);

    std::vector<Quad> quads;
    quads.reserve(16000);
    for (int i = 0; i < 4; ++i) {
        const auto origin = layout.statusOrigin(i);
        const float t = layout.statusScale;
        const auto& p = party[i];
        const auto name = upperAscii(p.at("name").get<std::string>());
        const float fontScale = (18.0f/7.0f)*t;
        const auto nameScale = fittedTextScale(font, name, fontScale, 83.0f*t);
        const float nameX = origin.x + (i < 2 ? 12.0f : 69.0f)*t;
        textLabel(quads, font, name, nameX, origin.y + 6*t,
                  ink, nameScale, static_cast<int>(name.size()));
        const auto gender = p.at("gender").get<std::string>();
        if (gender == "male" || gender == "female") {
            // Colosseum's status callback measures the name, then draws a
            // message-font symbol. The atlas stores those source glyphs in
            // two otherwise unused ASCII cells.
            textLabel(quads, font, gender == "male" ? "{" : "}",
                      nameX + textWidth(font, name, nameScale) - 2*t,
                      origin.y + 6*t,
                      gender == "male" ? Color{.06f,.43f,.9f,1} :
                                         Color{.91f,.12f,.28f,1},
                      fontScale, 1);
        }
        const auto level = std::to_string(p.at("level").get<int>());
        textLabel(quads, font, level,
                  origin.x + (i < 2 ? 154.0f : 211.0f)*t -
                      textWidth(font, level, fontScale), origin.y + 7*t,
                  ink, fontScale, 3);
        if (i < 2) {
            const auto hp = std::to_string(p.at("hp").get<int>());
            const auto maxHp = std::to_string(p.at("max_hp").get<int>());
            textLabel(quads, font, hp,
                      origin.x + 58*t - textWidth(font, hp, fontScale),
                      origin.y + 33*t, ink, fontScale, 3);
            textLabel(quads, font, "/", origin.x + 65*t,
                      origin.y + 33*t, ink, fontScale, 1);
            textLabel(quads, font, maxHp,
                      origin.x + 111*t - textWidth(font, maxHp, fontScale),
                      origin.y + 33*t, ink, fontScale, 3);
        }
    }
    const std::string heading = screen == "command" || screen == "move"
        ? party[state.at("actor").get<int>()].at("name").get<std::string>()
        : screen == "resolving" ? "BATTLE" : state.at("title").get<std::string>();
    if (screen == "command" || screen == "move") {
        const auto name = upperAscii(heading);
        const auto origin = layout.commandRect(62, 330, 0, 0); // C1/B4 item at y=332, text offset -2.
        const float preferred = (18.0f/7.0f)*layout.statusScale;
        textLabel(quads, commandFont, name, origin.x, origin.y, ink,
                  fittedTextScale(commandFont, name, preferred,
                                  225*layout.statusScale), 28);
    } else if (screen != "resolving" && screen != "target") {
        textLabel(quads, font, upperAscii(heading), title.x + 14*s, title.y + 11*s,
                  ink, fittedTextScale(font, upperAscii(heading), 2.2f*s,
                                       title.w - 27*s), 28);
    }
    if (screen == "resolving") {
        const float t = layout.statusScale;
        for (std::size_t line = 0; line < narration.size(); ++line)
            textLabel(quads, messageFont, narration[line],
                      53.0f*t, messageTextY + messageLinePitch*static_cast<float>(line),
                      ink, messageScale, static_cast<int>(narration[line].size()));
    }
    const auto& options = state.at("options");
    for (int i = 0; screen != "target" && i < static_cast<int>(options.size()) &&
                    i < (screen == "move" ? 4 : 5); ++i) {
        const auto slot = layout.option(screen, i);
        const bool enabled = options[i].at("enabled").get<bool>();
        const bool active = i == state.at("cursor").get<int>();
        if (active && screen != "command" && screen != "move")
            textLabel(quads, font, ">>", slot.x, slot.y + 5*s,
                      selected, 1.8f*s, 2);
        std::string value = options[i].at("label").get<std::string>();
        if (screen == "command") {
            if (i == 1) value = "PKMN";
            if (i == 2) value = "ITEMS";
        } else if (screen == "move") {
            const auto& move = party[state.at("actor").get<int>()].at("moves").at(i);
            value = move.at("name").get<std::string>();
        }
        const auto display = upperAscii(value);
        if (screen == "command" || screen == "move") {
            const auto origin = layout.commandRect(i >= 2 ?
                                                    (screen == "move" ? 255.0f : 227.0f) : 87.0f,
                                                    i % 2 ? 401.0f : 373.0f,
                                                    0, 0);
            const float preferred = (18.0f/7.0f)*layout.statusScale;
            const auto& menuFont = screen == "move" ? moveFont : commandFont;
            textLabel(quads, menuFont, display, origin.x, origin.y,
                      enabled ? ink : muted,
                      fittedTextScale(menuFont, display, preferred,
                                      (screen == "move" ? (i >= 2 ? 145 : 155) : 99)*
                                      layout.statusScale),
                      static_cast<int>(display.size()));
        } else {
            textLabel(quads, font, display, slot.x + 22*s, slot.y + 5*s,
                      enabled ? ink : muted,
                      fittedTextScale(font, display,
                                      (screen == "move" ? 1.55f : 2.1f)*s,
                                      slot.w - 25*s),
                      static_cast<int>(display.size()));
        }
    }
    if (screen == "move") {
        const int cursor = state.at("cursor").get<int>();
        const auto& moves = party[state.at("actor").get<int>()].at("moves");
        if (cursor >= 0 && cursor < 4 && cursor < static_cast<int>(moves.size())) {
            const auto& move = moves.at(cursor);
            const std::string current = std::to_string(move.at("pp").get<int>());
            const std::string maximum = std::to_string(move.at("max_pp").get<int>());
            const float scale = (18.0f/7.0f)*layout.statusScale;
            // Retail C2 centers a 9-pixel slash in its 78-pixel PP field.
            const auto currentRight = layout.commandRect(537,371,0,0);
            const auto slash = layout.commandRect(537,371,0,0);
            const auto maximumRight = layout.commandRect(581,371,0,0);
            textLabel(quads, commandFont, current,
                      currentRight.x - textWidth(commandFont, current, scale),
                      currentRight.y, ink, scale, static_cast<int>(current.size()));
            textLabel(quads, commandFont, "/", slash.x, slash.y,
                      ink, scale, 1);
            textLabel(quads, commandFont, maximum,
                      maximumRight.x - textWidth(commandFont, maximum, scale),
                      maximumRight.y, ink, scale, static_cast<int>(maximum.size()));
        }
    }
    if (target >= 0) {
        const auto cursor = targetCursorSprite(statusDetailAtlasPath,layout,target);
        renderer.drawDebugSprites(&cursor,1,width,height);
    }
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
    for (int index = 0; index < static_cast<int>(options.size()) &&
                        index < (screen == "move" ? 4 : 5); ++index) {
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
