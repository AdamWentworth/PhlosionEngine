// TextRenderer.cpp

#include "TextRenderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/utils/Shader.h"
#include "engine/utils/ShaderCache.h"

TextRenderer::TextRenderer(const std::string& fontPath, int fontSize, ShaderCache* shaderCache) {
    font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!font) {
        std::cerr << "[TextRenderer] Failed to load font: " << TTF_GetError() << "\n";
    }

    // NEW: shared cached shader (prevents multiple program compiles)
    textShader = shaderCache ? shaderCache->get("assets/shaders/ui/text.vert", "assets/shaders/ui/text.frag")
                          : std::make_shared<Shader>("assets/shaders/ui/text.vert", "assets/shaders/ui/text.frag");
if (textShader) {
        locProjection  = glGetUniformLocation(textShader->getID(), "u_Projection");
        locTextColor   = glGetUniformLocation(textShader->getID(), "u_TextColor");
        locGlobalAlpha = glGetUniformLocation(textShader->getID(), "u_GlobalAlpha");
        locTexture     = glGetUniformLocation(textShader->getID(), "u_Texture");
    }

    // NEW: one VAO/VBO/EBO reused for all glyph draws
    float vertices[16] = {
        // x, y, u, v (placeholder)
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    unsigned int indices[6] = { 0, 1, 2, 2, 3, 0 };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

TextRenderer::~TextRenderer() {
    for (auto& kv : glyphs) {
        if (kv.second.textureID) glDeleteTextures(1, &kv.second.textureID);
    }
    glyphs.clear();

    if (EBO) glDeleteBuffers(1, &EBO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (VAO) glDeleteVertexArrays(1, &VAO);

    if (font) {
        TTF_CloseFont(font);
        font = nullptr;
    }

    textShader.reset();
}

unsigned int TextRenderer::createTextureFromSurface(SDL_Surface* surface) {
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!converted) {
        std::cerr << "[TextRenderer] Failed to convert surface: " << SDL_GetError() << "\n";
        return 0;
    }

    unsigned int textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 converted->w,
                 converted->h,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 converted->pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_FreeSurface(converted);

    return textureID;
}

TextRenderer::Glyph& TextRenderer::getOrCreateGlyph(unsigned char c) {
    uint32_t key = (uint32_t)c;
    auto it = glyphs.find(key);
    if (it != glyphs.end()) return it->second;

    Glyph g;

    if (!font || !TTF_GlyphIsProvided(font, c)) {
        glyphs[key] = g;
        return glyphs[key];
    }

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* glyphSurface = TTF_RenderGlyph_Blended(font, c, white);
    if (!glyphSurface) {
        glyphs[key] = g;
        return glyphs[key];
    }

    g.textureID = createTextureFromSurface(glyphSurface);
    g.w = glyphSurface->w;
    g.h = glyphSurface->h;

    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &advance) == 0) {
        g.advance = advance;
    } else {
        g.advance = g.w;
    }

    g.valid = (g.textureID != 0);
    SDL_FreeSurface(glyphSurface);

    glyphs[key] = g;
    return glyphs[key];
}

void TextRenderer::renderText(const std::string& text,
                              float x,
                              float y,
                              const glm::vec3& color,
                              float scale,
                              float alpha)
{
    if (!font || !textShader || text.empty()) return;

    int vp[4] = {0,0,0,0};
    glGetIntegerv(GL_VIEWPORT, vp);
    const float screenW = static_cast<float>(vp[2]);
    const float screenH = static_cast<float>(vp[3]);

    glm::mat4 projection = glm::ortho(0.0f, screenW, screenH, 0.0f);

    textShader->use();
    if (locProjection >= 0)  glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(projection));
    if (locTextColor >= 0)   glUniform3f(locTextColor, color.x, color.y, color.z);
    if (locGlobalAlpha >= 0) glUniform1f(locGlobalAlpha, alpha);
    if (locTexture >= 0)     glUniform1i(locTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(VAO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float posX = x;

    for (unsigned char c : text) {
        Glyph& g = getOrCreateGlyph(c);
        if (!g.valid || g.w <= 0 || g.h <= 0) continue;

        const float quadW = (float)g.w * scale;
        const float quadH = (float)g.h * scale;

        const float xpos = posX;
        const float ypos = y;

        float vertices[16] = {
            // pos                 // tex
            xpos,         ypos + quadH, 0.0f, 1.0f,
            xpos,         ypos,         0.0f, 0.0f,
            xpos + quadW, ypos,         1.0f, 0.0f,
            xpos + quadW, ypos + quadH, 1.0f, 1.0f
        };

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glBindTexture(GL_TEXTURE_2D, g.textureID);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        posX += (float)g.advance * scale;
    }

    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float TextRenderer::measureTextWidth(const std::string& text, float scale) const {
    if (!font) return 0.0f;
    float width = 0.0f;

    for (unsigned char c : text) {
        if (!TTF_GlyphIsProvided(font, c)) continue;

        int minx, maxx, miny, maxy, advance;
        if (TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &advance) == 0) {
            width += (float)advance * scale;
        }
    }
    return width;
}

float TextRenderer::measureTextHeight(float scale) const {
    if (!font) return 0.0f;
    return static_cast<float>(TTF_FontHeight(font)) * scale;
}

float TextRenderer::measureTextHeight(const std::string& text, float scale) const {
    if (!font) return 0.0f;
    if (text.empty()) return measureTextHeight(scale);

    float maxH = 0.0f;
    for (unsigned char c : text) {
        if (!TTF_GlyphIsProvided(font, c)) continue;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
        if (TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &advance) == 0) {
            const float h = static_cast<float>(maxy - miny) * scale;
            if (h > maxH) maxH = h;
        }
    }
    if (maxH <= 0.0f) return measureTextHeight(scale);
    return maxH;
}
