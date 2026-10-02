#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/IRenderBackend.h"
#include "engine/vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

namespace vfx::runtime::authored_batches {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    std::uint16_t j0 = 0u;
    std::uint16_t j1 = 0u;
    std::uint16_t j2 = 0u;
    std::uint16_t j3 = 0u;
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct TextureView {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
};

struct WorldIndexedBatch {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    const IRenderBackend::WorldMeshVertex* sharedVertices = nullptr;
    std::size_t sharedVertexCount = 0u;
    const std::uint32_t* sharedIndices = nullptr;
    std::size_t sharedIndexCount = 0u;
    std::string geometryCacheKey;
    std::vector<IRenderBackend::WorldMeshInstance> instances;
    std::string textureKey;
    std::string textureCacheKey;
    const unsigned char* textureRgba = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureWrapS = 10497;
    int textureWrapT = 10497;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t dualSourceBlendEnabled = 0u;
    std::uint8_t depthTestEnabled = 1u;
    float clipSpaceDepthBias = 0.0f;
    float alphaCutoff = 0.5f;
    float alphaWindowMin = 0.0f;
    float alphaWindowMax = 1.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    std::uint8_t characterInkingEnabled = 0u;
    float sortDepth = 0.0f;
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};

    bool hasGeometry() const {
        const bool hasVertices =
            (!vertices.empty()) || (sharedVertices && sharedVertexCount > 0u);
        const bool hasIndices =
            (!indices.empty()) || (sharedIndices && sharedIndexCount > 0u);
        return hasVertices && hasIndices;
    }
};

bool appendPassBatch(std::vector<WorldIndexedBatch>& outBatches,
                     const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                     const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                     const authored::TevState& passTev,
                     const MeshData* passMesh,
                     const TextureView& texture,
                     const glm::vec3& cameraWorldPos);

} // namespace vfx::runtime::authored_batches
