// BootLoadingView.cpp

#include "BootLoadingView.h"

#include "engine/utils/Shader.h"
#include "engine/utils/ShaderCache.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

BootLoadingView::~BootLoadingView() {
    shutdown();
}

void BootLoadingView::shutdown() {
    // Requires active GL context; call while context still exists.
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }

    shader.reset();
}

void BootLoadingView::init() {
    // Fallback: compile directly (no cache). Prefer init(shaders).
    shader = std::make_shared<Shader>(
        "assets/shaders/ui/healthbar.vert",
        "assets/shaders/ui/healthbar.frag"
    );
    ensureQuad();
}

void BootLoadingView::init(ShaderCache& shaders) {
    shader = shaders.get(
        "assets/shaders/ui/healthbar.vert",
        "assets/shaders/ui/healthbar.frag"
    );
    ensureQuad();
}

void BootLoadingView::ensureQuad() {
    if (vao != 0) return;

    // 2D unit quad (0..1) in XY, drawn as TRIANGLE_FAN
    float verts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void BootLoadingView::drawRect(float x, float y, float w, float h, const glm::vec3& rgb,
                              const glm::mat4& projection) {
    if (!shader) return;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));

    shader->setUniform("u_Projection", projection);
    shader->setUniform("u_Model", model);
    shader->setUniform("u_Color", rgb);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void BootLoadingView::render(float progress01, int screenW, int screenH) {
    if (!shader) return;

    progress01 = std::clamp(progress01, 0.0f, 1.0f);

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    if (!blendWasEnabled) glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, screenW, screenH);

    shader->use();

    glm::mat4 projection = glm::ortho(0.0f, (float)screenW, (float)screenH, 0.0f);

    // Background fill (dark)
    drawRect(0.0f, 0.0f, (float)screenW, (float)screenH, glm::vec3(0.05f, 0.05f, 0.07f), projection);

    // Progress bar
    const float barW = (float)screenW * 0.60f;
    const float barH = 18.0f;
    const float barX = ((float)screenW - barW) * 0.5f;
    const float barY = (float)screenH * 0.65f;

    // Bar background
    drawRect(barX, barY, barW, barH, glm::vec3(0.20f, 0.20f, 0.22f), projection);

    // Fill
    drawRect(barX, barY, barW * progress01, barH, glm::vec3(0.75f, 0.75f, 0.78f), projection);

    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
}
