// HealthBarRenderer.h

#pragma once
#include <vector>
#include <memory>
#include <string>
#include "HealthBarData.h"
#include "engine/utils/Shader.h"
#include "engine/ui/TextRenderer.h"

class ShaderCache;

class HealthBarRenderer {
public:
    HealthBarRenderer() = default;  // No shader initialization here
    void init();
    void init(ShaderCache& shaders);
    void setFont(const std::string& fontPath, int fontSize, ShaderCache* shaders = nullptr);
    void render(const std::vector<HealthBarData>& healthBars);

private:
    std::shared_ptr<Shader> shader; 
    void renderQuad();
    void ensureRingGeometry();
    void renderRingArc(const glm::vec2& center, float innerR, float outerR,
                       float startRad, float endRad, const glm::vec3& color);

    std::unique_ptr<TextRenderer> levelText;
    int levelFontSize = 16;
    unsigned int ringVAO = 0;
    unsigned int ringVBO = 0;
};
