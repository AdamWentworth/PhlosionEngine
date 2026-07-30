// BoardRenderer.cpp

#include "BoardRenderer.h"
#include "engine/utils/ShaderCache.h"
#include "engine/utils/Shader.h"
#include "engine/utils/ShaderCache.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

BoardRenderer::BoardRenderer(int rows, int cols, float cellSize, ShaderCache* shaderCache)
    : rows(rows), cols(cols), cellSize(cellSize)
{
    this->shaderCache = shaderCache;
    initGrid();
    initBench(); // NEW
    gridShader = shaderCache ? shaderCache->get(
                   "assets/shaders/engine/grid.vert",
                   "assets/shaders/engine/grid.frag")
                : std::make_shared<Shader>("assets/shaders/engine/grid.vert", "assets/shaders/engine/grid.frag");
mvpLocation = glGetUniformLocation(gridShader->getID(), "u_MVP");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    rebuildGeometryBuffer();
}

BoardRenderer::~BoardRenderer() {
    shutdown();
}

void BoardRenderer::draw(const Camera3D& camera) {
    gridShader->use();
    glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::mat4(1.0f);
    glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, &mvp[0][0]);
    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, (GLsizei)(gridVertices.size() / 3));
}

void BoardRenderer::drawBench(const Camera3D& camera) {
    gridShader->use();
    glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::mat4(1.0f);
    glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, &mvp[0][0]);
    glBindVertexArray(vao);

    size_t benchOffset = gridVertices.size() / 3;
    size_t benchCount = benchVertices.size() / 3;
    glDrawArrays(GL_LINES, (GLsizei)benchOffset, (GLsizei)benchCount);
}

void BoardRenderer::setCellSize(float newCellSize) {
    const float clamped = std::max(0.05f, newCellSize);
    if (std::abs(clamped - cellSize) < 0.0001f) return;
    cellSize = clamped;
    initGrid();
    initBench();
    rebuildGeometryBuffer();
}

void BoardRenderer::shutdown() {
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    gridShader.reset();
}

void BoardRenderer::rebuildGeometryBuffer() {
    std::vector<float> allVertices = gridVertices;
    allVertices.insert(allVertices.end(), benchVertices.begin(), benchVertices.end());

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(float), allVertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void BoardRenderer::initGrid() {
    gridVertices.clear();
    float halfW = (cols * cellSize) / 2.0f;
    float halfH = (rows * cellSize) / 2.0f;

    for (int i = 0; i <= rows; ++i) {
        float y = 0.0f;
        float z = -halfH + i * cellSize;
        gridVertices.insert(gridVertices.end(), {-halfW, y, z, halfW, y, z});
    }

    for (int j = 0; j <= cols; ++j) {
        float x = -halfW + j * cellSize;
        float y = 0.0f;
        gridVertices.insert(gridVertices.end(), {x, y, -halfH, x, y, halfH});
    }

    gridVertices.insert(gridVertices.end(), {-halfW, 0.01f, 0.0f, halfW, 0.01f, 0.0f});
}

void BoardRenderer::initBench() {
    benchVertices.clear();
    float benchY = 0.01f;  // Slightly above ground
    float slotSize = cellSize; // Make bench use same cell size as board
    float totalWidth = 8 * slotSize;
    float startX = -totalWidth / 2.0f; // Centered
    float startZ = (rows * cellSize) / 2.0f + 0.5f; // Just in front of the grid

    for (int i = 0; i <= 8; ++i) {
        float x = startX + i * slotSize;
        benchVertices.insert(benchVertices.end(), {x, benchY, startZ, x, benchY, startZ + slotSize});
    }

    for (int j = 0; j <= 1; ++j) {
        float z = startZ + j * slotSize;
        benchVertices.insert(benchVertices.end(), {startX, benchY, z, startX + 8 * slotSize, benchY, z});
    }
}

std::string BoardRenderer::loadShaderSource(const char* path) {
    std::ifstream file(path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int BoardRenderer::compileShader(const char* src, unsigned int type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    return shader;
}

unsigned int BoardRenderer::createShaderProgram(const char* vertPath, const char* fragPath) {
    std::string vertSrc = loadShaderSource(vertPath);
    std::string fragSrc = loadShaderSource(fragPath);
    unsigned int vert = compileShader(vertSrc.c_str(), GL_VERTEX_SHADER);
    unsigned int frag = compileShader(fragSrc.c_str(), GL_FRAGMENT_SHADER);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    return program;
}
