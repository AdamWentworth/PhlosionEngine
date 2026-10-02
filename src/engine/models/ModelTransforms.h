#pragma once

#include "engine/models/ModelData.h"
#include <functional>

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::models::transforms {

struct WorldVertexSample {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

struct ModelVertexSurfaceSample {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};
};

struct TransformOptions {
    const MeshData* mesh = nullptr;
    const std::vector<glm::mat4>* nodeGlobals = nullptr;
    glm::mat4 modelMatrix{1.0f};
    bool hasClipPose = false;
    bool positionOnly = true;
    bool clipSkinning = true;
    bool gpuSkinning = true;
    bool gpuBindPose = true;
    bool pairedSkinMatrices = false;
    std::function<glm::vec3(glm::vec3)> deformLocal;
};

// Scratch is reused on the calling thread. Resolve a complete model before
// initializing another resolver; retain submitted palettes in caller storage.
class Resolver {
  public:
    void initialize(const TransformOptions& options);

    const glm::mat4& worldMatrixForNode(int triNodeIndex);
    const glm::mat3& worldNormalMatrixForNode(int triNodeIndex);

    WorldVertexSample resolveWorldVertex(int triNodeIndex,
                                         std::uint32_t vertexIndex,
                                         const MeshVertex& vtx);
    glm::vec3 resolveWorldVertexPos(int triNodeIndex,
                                    std::uint32_t vertexIndex,
                                    const MeshVertex& vtx);
    glm::vec3 resolveModelVertexNormal(int triNodeIndex,
                                       std::uint32_t vertexIndex,
                                       const MeshVertex& vtx);
    glm::vec4 resolveModelVertexTangent(int triNodeIndex,
                                        std::uint32_t vertexIndex,
                                        const MeshVertex& vtx);
    ModelVertexSurfaceSample resolveModelVertexSurface(
        int triNodeIndex,
        std::uint32_t vertexIndex,
        const MeshVertex& vtx,
        bool needNormal,
        bool needTangent);
    glm::vec3 resolveGpuSkinningInputPos(std::uint32_t vertexIndex,
                                         const MeshVertex& vtx);
    glm::vec3 resolveDeformedLocalVertexPos(std::uint32_t vertexIndex,
                                            const MeshVertex& vtx);
    int gpuSkinningCacheKeyForNode(int triNodeIndex) const;
    bool configureGpuClipSkinningBatch(int triNodeIndex,
                                       const std::vector<std::uint16_t>* jointPalette,
                                       std::array<float, 16>& inOutModelMatrix,
                                       std::uint8_t& outSkinningMode,
                                       std::vector<float>& outSkinMatrices,
                                       std::uint32_t& outSkinMatrixCount);

  private:
    struct SkinResult {
        glm::vec3 pos{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool applied = false;
    };

    const std::vector<glm::mat4>* ensureSkinMatricesForNode(int nodeIndex);
    SkinResult skinVertexAtNode(int nodeIndex,
                                const MeshVertex& vtx,
                                const glm::vec3& localPos,
                                const glm::vec3& localNormal);
    glm::vec3 skinDirectionAtNode(int nodeIndex,
                                  const MeshVertex& vtx,
                                  const glm::vec3& localDirection);
    glm::vec3 skinPositionAtNode(int nodeIndex,
                                 const MeshVertex& vtx,
                                 const glm::vec3& localPos);
    glm::vec3 resolveLocalVertexPos(std::uint32_t vertexIndex,
                                    const MeshVertex& vtx);
    const glm::mat3& modelNormalMatrixForNode(int triNodeIndex);

    std::size_t nodeTransformIndexFor(int triNodeIndex) const;

    std::function<glm::vec3(glm::vec3)> deformLocal_;
    const MeshData* mesh_ = nullptr;
    const std::vector<glm::mat4>* nodeGlobals_ = nullptr;

    glm::mat4 modelM_{1.0f};
    bool hasClipPose_ = false;
    bool usePositionOnlyVertexPath_ = false;
    bool clipSkinningEnabled_ = true;
    bool gpuClipSkinningRequested_ = false;
    bool gpuBindPoseSkinningRequested_ = false;
    bool backendGpuSkinNodeGlobalsEnabled_ = false;
    std::size_t nodeCount_ = 0u;
};

} // namespace engine::models::transforms
