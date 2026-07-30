// HealthBarRenderer.cpp

#include "HealthBarRenderer.h"
#include "engine/utils/ShaderCache.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void HealthBarRenderer::init() {
    // Fallback: compile directly (no cache). Prefer init(shaders).
    shader = std::make_shared<Shader>(
        "assets/shaders/ui/healthbar.vert",
        "assets/shaders/ui/healthbar.frag"
    );
}

void HealthBarRenderer::init(ShaderCache& shaders) {
    shader = shaders.get(
        "assets/shaders/ui/healthbar.vert",
        "assets/shaders/ui/healthbar.frag"
    );
}

void HealthBarRenderer::setFont(const std::string& fontPath, int fontSize, ShaderCache* shaders) {
    levelFontSize = std::max(8, fontSize);
    levelText = std::make_unique<TextRenderer>(fontPath, levelFontSize, shaders);
}

void HealthBarRenderer::ensureRingGeometry() {
    if (ringVAO != 0) return;

    glGenVertexArrays(1, &ringVAO);
    glGenBuffers(1, &ringVBO);

    glBindVertexArray(ringVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ringVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void HealthBarRenderer::renderRingArc(const glm::vec2& center,
                                      float innerR,
                                      float outerR,
                                      float startRad,
                                      float endRad,
                                      const glm::vec3& color) {
    if (!shader) return;
    ensureRingGeometry();

    const float arc = endRad - startRad;
    if (std::abs(arc) < 0.0001f) return;

    const float full = 6.2831853f;
    int segments = std::max(6, static_cast<int>(std::ceil(std::abs(arc) / full * 32.0f)));

    std::vector<glm::vec2> verts;
    verts.reserve((segments + 1) * 2);

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float a = startRad + arc * t;
        float cs = std::cos(a);
        float sn = std::sin(a);
        verts.emplace_back(center.x + cs * outerR, center.y + sn * outerR);
        verts.emplace_back(center.x + cs * innerR, center.y + sn * innerR);
    }

    shader->setUniform("u_Model", glm::mat4(1.0f));
    shader->setUniform("u_Color", color);

    glBindVertexArray(ringVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ringVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec2), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

void HealthBarRenderer::render(const std::vector<HealthBarData>& healthBars) {
    if (!shader) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->use();

    // Get viewport dimensions
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float screenWidth = static_cast<float>(viewport[2]);
    float screenHeight = static_cast<float>(viewport[3]);
    glm::mat4 projection = glm::ortho(0.0f, screenWidth, screenHeight, 0.0f);
    shader->setUniform("u_Projection", projection);

    for (const auto& hb : healthBars) {
        const float cellPx = (hb.cellPx > 0.0f) ? hb.cellPx : 50.0f;
        const float width = cellPx * 0.45f;
        const float hpH   = cellPx * 0.07f;
        const float enH   = cellPx * 0.06f;
        const float yOffset = cellPx * 0.35f;      // top of HP bar
        const float gap     = cellPx * 0.03f;      // space between HP and Energy bar

        const float ringOuter = cellPx * 0.155f;
        const float ringInner = cellPx * 0.135f;
        const float ringGap = cellPx * 0.035f;
        const float ringPad = ringOuter + ringGap;
        const float leftExtent = ringOuter + ringPad;

        glm::vec2 pos = hb.screenPosition;
        pos.x = pos.x - (width - leftExtent) * 0.5f;
        pos.y = pos.y - yOffset;

        // Background
        glm::mat4 modelBg = glm::translate(glm::mat4(1.0f), glm::vec3(pos, 0.0f));
        modelBg = glm::scale(modelBg, glm::vec3(width, hpH, 1.0f));
        shader->setUniform("u_Model", modelBg);
        shader->setUniform("u_Color", glm::vec3(0.3f, 0.3f, 0.3f));
        renderQuad();

        // Foreground
        float percent = static_cast<float>(hb.currentHP)/hb.maxHP;
        glm::vec3 color = hb.isEnemy ? glm::vec3(1.0f, 0.0f, 0.0f)
                                     : glm::vec3(0.0f, 1.0f, 0.0f);

        glm::mat4 modelFg = glm::translate(glm::mat4(1.0f), glm::vec3(pos, 0.0f));
        modelFg = glm::scale(modelFg, glm::vec3(width*percent, hpH, 1.0f));
        shader->setUniform("u_Model", modelFg);
        shader->setUniform("u_Color", color);
        renderQuad();

        // ----- Energy bar (blue) just below HP -----
        float eFrac = (hb.maxEnergy > 0) ? (static_cast<float>(hb.currentEnergy) / hb.maxEnergy) : 0.0f;
        glm::vec2 ePos = pos + glm::vec2(0.0f, hpH + gap);
    
        // Background (energy)
        glm::mat4 eBg = glm::translate(glm::mat4(1.0f), glm::vec3(ePos, 0.0f));
        eBg = glm::scale(eBg, glm::vec3(width, enH, 1.0f));
        shader->setUniform("u_Model", eBg);
        shader->setUniform("u_Color", glm::vec3(0.25f, 0.25f, 0.25f));
        renderQuad();
    
        // Foreground (energy)
        glm::mat4 eFg = glm::translate(glm::mat4(1.0f), glm::vec3(ePos, 0.0f));
        eFg = glm::scale(eFg, glm::vec3(width*eFrac, enH, 1.0f));
        shader->setUniform("u_Model", eFg);
        shader->setUniform("u_Color", glm::vec3(0.95f, 0.65f, 0.20f)); // yellow/orange
        renderQuad();

        // ----- Level text + XP ring (player only), placed left of bars -----
        const float barH = hpH + gap + enH;
        glm::vec2 levelCenter = glm::vec2(pos.x - ringPad, pos.y + barH * 0.5f - cellPx * 0.02f);

        if (hb.showXP && hb.maxXP > 0) {
            float xFrac = std::clamp(static_cast<float>(hb.currentXP) / hb.maxXP, 0.0f, 1.0f);
            const float start = -1.5707963f; // 12 o'clock
            const float end = start + xFrac * 6.2831853f;
            renderRingArc(levelCenter, ringInner, ringOuter, start, start + 6.2831853f, glm::vec3(0.20f, 0.20f, 0.20f));
            renderRingArc(levelCenter, ringInner, ringOuter, start, end, glm::vec3(0.20f, 0.55f, 1.0f));
        }

        if (levelText) {
            const std::string lvl = std::to_string(std::max(1, hb.level));
            const float textScale = std::clamp(
                (ringInner * 1.55f) / std::max(1.0f, static_cast<float>(levelFontSize)),
                0.72f, 1.05f);
            const float w = levelText->measureTextWidth(lvl, textScale);
            const float h = static_cast<float>(levelFontSize) * textScale;
            const float tx = levelCenter.x - w * 0.5f;
            const float ty = levelCenter.y - h * 0.5f + cellPx * 0.03f;
            levelText->renderText(lvl, tx, ty, glm::vec3(1.0f, 1.0f, 1.0f), textScale);
            // TextRenderer switches shader/blend state; restore for subsequent bars.
            shader->use();
            shader->setUniform("u_Projection", projection);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void HealthBarRenderer::renderQuad() {
    static unsigned int VAO = 0, VBO;
    if (VAO == 0) {
        float vertices[] = {0.0f,0.0f, 1.0f,0.0f, 1.0f,1.0f, 0.0f,1.0f};
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}
