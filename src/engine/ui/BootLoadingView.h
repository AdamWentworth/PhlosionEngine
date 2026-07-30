// BootLoadingView.h
#pragma once

#include <memory>
#include <glm/glm.hpp>

class Shader;
class ShaderCache;

class BootLoadingView {
public:
    BootLoadingView() = default;
    ~BootLoadingView();

    void init();
    void init(ShaderCache& shaders);
    void render(float progress01, int screenW, int screenH);

    // Explicit cleanup (idempotent). Safe to call more than once.
    void shutdown();

private:
    std::shared_ptr<Shader> shader;
    unsigned int vao = 0;
    unsigned int vbo = 0;

    void ensureQuad();
    void drawRect(float x, float y, float w, float h, const glm::vec3& rgb,
                  const glm::mat4& projection);
};
