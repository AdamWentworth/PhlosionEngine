// BoardRenderer.h

#pragma once

#include "Camera3D.h"
#include "engine/utils/Shader.h"
#include <memory>
#include <vector>
#include <string>

class ShaderCache;

class BoardRenderer {
public:
    BoardRenderer(int rows = 8, int cols = 8, float cellSize = 1.0f, ShaderCache* shaderCache = nullptr);
    ~BoardRenderer();

    void draw(const Camera3D& camera);         // Draw the main board
    void drawBench(const Camera3D& camera);    // NEW: Draw bench grid
    void setCellSize(float newCellSize);
    void shutdown();

private:
    void initGrid();
    void initBench(); // NEW
    void rebuildGeometryBuffer();
    std::string loadShaderSource(const char* path);
    unsigned int compileShader(const char* src, unsigned int type);
    unsigned int createShaderProgram(const char* vertPath, const char* fragPath);

    std::shared_ptr<Shader> gridShader;

    ShaderCache* shaderCache = nullptr;

    unsigned int vao, vbo;
    unsigned int shaderProgram;
    int mvpLocation;

    int rows, cols;
    float cellSize;
    std::vector<float> gridVertices;
    std::vector<float> benchVertices; // NEW
};
