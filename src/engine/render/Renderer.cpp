// src/engine/core/Renderer.cpp

#include "Renderer.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <string>
#include <glm/gtc/type_ptr.hpp>

// Helper function to check for OpenGL errors.
static void checkGLError(const std::string& context) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        engine::log::error(std::string("[OpenGL Error] ") + context + ": 0x" + engine::log::to_hex((unsigned)err));
    }
}

Renderer::Renderer() 
    : vbo(GL_ARRAY_BUFFER)  // Initialize vbo with GL_ARRAY_BUFFER
{
    engine::log::info("[Renderer] Constructing Renderer...");
    float vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };

    engine::log::info("[Renderer] Creating shader program using our Shader class...");
    // Create the shader using std::make_unique
    shader = std::make_unique<Shader>(engine::paths::asset("shaders/engine/default.vert"), engine::paths::asset("shaders/engine/default.frag"));
    engine::log::info(std::string("[Renderer] Shader program created with ID: ") + std::to_string(shader->getID()));
    checkGLError("After shader program creation");

    glBindVertexArray(vao.getID());
    glBindBuffer(vbo.getTarget(), vbo.getID());
    glBufferData(vbo.getTarget(), sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    checkGLError("After setting up vertex attributes");

    // Get the uniform location for u_MVP from our shader.
    mvpLocation = glGetUniformLocation(shader->getID(), "u_MVP");
    if (mvpLocation == -1) {
        engine::log::error("[Renderer] ERROR: u_MVP uniform not found in shader.");
    }
    engine::log::info(std::string("[Renderer] u_MVP location = ") + std::to_string(mvpLocation));
}

Renderer::~Renderer() {
    // RAII objects (vao and vbo) automatically clean up when Renderer is destroyed.
}

void Renderer::render() {
    shader->use();
    glBindVertexArray(vao.getID());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::shutdown() {
    // No need to explicitly delete vao and vbo; their destructors will handle it.
}
