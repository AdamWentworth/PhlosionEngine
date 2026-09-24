#include "BattleRulesOverlay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace engine::exploration_fixture {
namespace {
using Quad = IRenderBackend::DebugQuad;
using Rows = std::array<unsigned char, 7>;
struct Color { float r, g, b, a; };
constexpr Color ink{.94f, .96f, 1.0f, 1.0f};
constexpr Color selected{.35f, .91f, 1.0f, 1.0f};
constexpr Color muted{.48f, .53f, .60f, 1.0f};
constexpr Color panel{.025f, .045f, .085f, .93f};

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
    quads.push_back({x,y,w,h,c.r,c.g,c.b,c.a});
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
} // namespace

void drawBattleRulesOverlay(IRenderBackend& renderer, const nlohmann::json& state,
                            int width, int height) {
    std::vector<Quad> quads;
    quads.reserve(2800);
    const auto& party = state.at("party");
    for (int i = 0; i < 4; ++i) {
        const float x = i % 2 == 0 ? 25.0f : width - 199.0f;
        const float y = i < 2 ? height - 75.0f : 47.0f;
        const auto& p = party[i];
        label(quads, p.at("name").get<std::string>(), x, y, ink, 2.0f, 14);
        const int hp = p.at("hp").get<int>();
        const int maxHp = p.at("max_hp").get<int>();
        label(quads, "HP " + std::to_string(hp) + "/" + std::to_string(maxHp),
              x, y + 21, ink, 1.6f, 19);
        box(quads, x, y + 39, 150, 9, {.03f,.05f,.07f,1});
        if (hp > 0) box(quads, x + 2, y + 41, 146.0f * hp / maxHp, 5,
                        hp * 4 <= maxHp ? Color{.93f,.30f,.30f,1} :
                        hp * 2 <= maxHp ? Color{.98f,.75f,.19f,1} :
                                            Color{.22f,.79f,.48f,1});
    }
    const float menuWidth = std::min(440.0f, width - 28.0f);
    const float menuX = (width - menuWidth) * .5f;
    const float menuY = height - 237.0f;
    box(quads, menuX, menuY, menuWidth, 169.0f, panel);
    box(quads, menuX, menuY, menuWidth, 3.0f, selected);
    label(quads, state.at("title").get<std::string>(), menuX + 14, menuY + 13,
          selected, 2.0f, static_cast<int>((menuWidth - 28) / 12));
    const auto& options = state.at("options");
    for (int i = 0; i < static_cast<int>(options.size()) && i < 5; ++i) {
        const float rowY = menuY + 42.0f + i * 23.0f;
        const bool enabled = options[i].at("enabled").get<bool>();
        const bool active = i == state.at("cursor").get<int>();
        if (active) box(quads, menuX + 8, rowY - 4, menuWidth - 16, 20,
                        {.14f,.28f,.37f,.90f});
        label(quads, active ? ">" : " ", menuX + 13, rowY,
              selected, 1.75f, 1);
        label(quads, options[i].at("label").get<std::string>(), menuX + 29, rowY,
              enabled ? (active ? selected : ink) : muted, 1.75f,
              static_cast<int>((menuWidth - 48) / 10.5f));
    }
    const std::string message = state.at("message").get<std::string>();
    const float logWidth = std::min(620.0f, std::max(220.0f, width - 424.0f));
    const float logX = (width - logWidth) * .5f;
    const bool showHistory = !state.at("history").empty() &&
                             state.at("screen") != "resolving";
    const std::string last = showHistory ? state.at("history").back().get<std::string>() : "";
    const int historyWidth = static_cast<int>((logWidth - 20) / 8.1f);
    const bool secondLine = static_cast<int>(last.size()) > historyWidth;
    std::size_t split = static_cast<std::size_t>(historyWidth);
    if (secondLine) {
        const auto space = last.rfind(' ', split);
        if (space != std::string::npos && space > split / 2) split = space;
    }
    box(quads, logX, 12, logWidth,
        showHistory ? (secondLine ? 71.0f : 54.0f) : 32.0f, panel);
    const float messageScale = message.size() > 36 ? 1.3f : 1.7f;
    label(quads, message, logX + 10, 21, ink, messageScale,
          static_cast<int>((logWidth - 20) / (6 * messageScale)));
    if (showHistory) {
        label(quads, last.substr(0, split), logX + 10, 49, muted, 1.35f, historyWidth);
        if (secondLine)
            label(quads, last.substr(split + (last[split] == ' ' ? 1 : 0)), logX + 10, 65, muted,
                  1.35f, historyWidth);
    }
    renderer.drawDebugQuads(quads.data(), quads.size(), width, height);
}

} // namespace engine::exploration_fixture
