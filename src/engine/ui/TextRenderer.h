// TextRenderer.h

#pragma once
#include <string>
#include <glm/glm.hpp>
#include <SDL2/SDL_ttf.h>
#include <unordered_map>
#include <memory>
#include <cstdint>

class Shader;
class ShaderCache;

class TextRenderer {
public:
    TextRenderer(const std::string& fontPath, int fontSize, ShaderCache* shaderCache = nullptr);
    ~TextRenderer();

    void renderText(const std::string& text,
                    float x,
                    float y,
                    const glm::vec3& color,
                    float scale,
                    float alpha = 1.0f);

    TTF_Font* getFont() const { return font; }

    float measureTextWidth(const std::string& text, float scale = 1.0f) const;
    float measureTextHeight(float scale = 1.0f) const;
    float measureTextHeight(const std::string& text, float scale = 1.0f) const;

private:
    struct Glyph {
        unsigned int textureID = 0;
        int w = 0;
        int h = 0;
        int advance = 0;
        bool valid = false;
    };

    Glyph& getOrCreateGlyph(unsigned char c);

    unsigned int createTextureFromSurface(SDL_Surface* surface);

    TTF_Font* font = nullptr;

    // Share shader program across instances (no per-instance recompiles)
    std::shared_ptr<Shader> textShader;

    // Cached glyph textures
    std::unordered_map<uint32_t, Glyph> glyphs;

    // Single reusable quad geometry
    unsigned int VAO = 0, VBO = 0, EBO = 0;

    // Cached uniform locations
    int locProjection = -1;
    int locTextColor = -1;
    int locGlobalAlpha = -1;
    int locTexture = -1;
};
