// BattleFeed.cpp

#include "BattleFeed.h"
#include "TextRenderer.h"
#include <algorithm>
#include <glad/glad.h>
#include <SDL2/SDL_ttf.h>   // NEW: for TTF_FontHeight()

BattleFeed::BattleFeed(const std::string& fontPath, int fontSize) {
    text = std::make_unique<TextRenderer>(fontPath, fontSize);
}

void BattleFeed::push(const std::string& msg, const glm::vec3& color, float lifetime) {
    if (msg.empty()) return;
    lines.push_back({msg, color, 0.f, lifetime});
    while ((int)lines.size() > maxLines) lines.pop_front();
}

void BattleFeed::clear() { lines.clear(); }

void BattleFeed::update(float dt) {
    for (auto& l : lines) l.age += dt;
    while (!lines.empty() && lines.front().age >= lines.front().lifetime) {
        lines.pop_front();
    }
}

void BattleFeed::render(int screenW, int screenH) {
    if (!text || lines.empty()) return;

    // 2D overlay: disable depth; enable alpha blending while drawing
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    if (!blendWasEnabled) glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float resScale = std::clamp(
        std::min(static_cast<float>(screenW) / 1280.0f,
                 static_cast<float>(screenH) / 720.0f),
        0.80f, 1.10f);
    const float scale = baseScale * resScale;
    const float gap = lineGap * resScale;

    // Use font height for consistent line spacing
    int fh = 24;
    if (TTF_Font* f = text->getFont()) {
        fh = TTF_FontHeight(f);
        if (fh <= 0) fh = 24;
    }
    const float lineH = fh * scale;  // scaled pixel height per line

    float x = alignRight ? (screenW - padX) : padX;
    // Start either from explicit baseline or one line-height above bottom padding.
    float y = hasBaselineYOverride ? baselineYOverride : (screenH - padY - lineH);

    for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i) {
        const auto& ln = lines[i];

        // Compute fade alpha (last 25% of lifetime)
        float t = std::clamp(ln.age / ln.lifetime, 0.f, 1.f);
        float alpha = (t < 0.75f) ? 1.f : std::max(0.f, 1.f - (t - 0.75f) / 0.25f);

        // Word wrap to fixed width (in pixels)
        auto wrapped = wrap(ln.text, wrapWidth, scale);

        // Draw the wrapped lines bottom-up so the last line sits on 'y'
        for (int w = static_cast<int>(wrapped.size()) - 1; w >= 0; --w) {
            float drawX = x;
            if (alignRight) {
                const float lineW = text->measureTextWidth(wrapped[w], scale);
                drawX = x - lineW;
            }
            // Render and then move up by one line
            text->renderText(
                wrapped[w],
                drawX, y,
                ln.color,
                scale,
                alpha /* NEW: true alpha fade */
            );
            y -= lineH;
            if (y < -lineH) break; // off-screen; stop
        }

        // Add small gap between entries
        y -= gap;
        if (y < -lineH) break;
    }

    // Restore GL state
    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
}

std::vector<std::string> BattleFeed::wrap(const std::string& s, float maxWidth, float scale) {
    std::vector<std::string> out;
    std::string cur, word;

    auto flush = [&](){
        if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    };

    for (size_t i = 0; i <= s.size(); ++i) {
        char c = (i < s.size() ? s[i] : ' ');
        if (c == ' ' || i == s.size()) {
            std::string candidate = cur.empty() ? word : (cur + " " + word);
            float w = text->measureTextWidth(candidate, scale);
            if (w > maxWidth && !cur.empty()) {
                flush();
                cur = word;
            } else {
                cur = candidate;
            }
            word.clear();
            if (i == s.size()) flush();
        } else {
            word.push_back(c);
        }
    }
    if (out.empty()) out.push_back(s);
    return out;
}
