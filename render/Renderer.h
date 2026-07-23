// Renderer.h

#pragma once
#include <string>
#include "Camera3D.h"
#include "engine/utils/Shader.h"
#include "engine/utils/GLResource.h"  // New header with RAII wrappers
#include <memory>  // For std::unique_ptr

class Renderer {
public:
    Renderer();
    ~Renderer();

    void shutdown();
    void render();

private:
    VertexArray vao;
    BufferObject vbo; // We'll initialize vbo with GL_ARRAY_BUFFER in the constructor initializer list
    std::unique_ptr<Shader> shader;  // Managed automatically via unique_ptr
    int mvpLocation;
};
