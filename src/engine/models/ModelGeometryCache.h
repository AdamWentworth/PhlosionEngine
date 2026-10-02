#pragma once
#include "engine/models/ModelData.h"
#include "engine/render/IRenderBackend.h"
#include <string>
#include <vector>
#include <span>
namespace engine::models::geometry {
struct FastTexturedBatchTemplate {
    std::size_t baseSubmeshIndex = 0u;
    int triNodeIndex = -1;
    bool skinnedBatch = false;
    std::string geometryCacheKey;
    std::vector<std::uint32_t> sourceVertexIndices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> gpuJointPalette;
    std::vector<IRenderBackend::WorldMeshVertex> gpuTemplateVertices;
};
struct FastTexturedMeshTemplateCache {
    const engine::models::MeshData* mesh = nullptr;
    std::string assetCacheIdentitySnapshot;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    std::size_t baseBatchCount = 0u;
    bool preferFullGpuSkinning = false;
    int defaultSkinNodeIndex = -1;
    std::vector<int> submeshNodeFallbackSnapshot;
    std::vector<std::uint8_t> preserveVertexColorSnapshot;
    std::vector<FastTexturedBatchTemplate> batches;
};
std::vector<int> buildSubmeshNodeFallback(const engine::models::MeshData& mesh);
std::string makeIndexedGeometryCacheKey(const std::string& keyPrefix,
    std::size_t baseSubmeshIndex, std::size_t batchIndex, std::size_t baseBatchCount);
int resolveDefaultSkinNodeIndex(const engine::models::MeshData* mesh);
const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const engine::models::MeshData* mesh, const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount, bool preferFullGpuSkinning,
    std::span<const std::uint8_t> preserveVertexColor = {});
const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    FastTexturedMeshTemplateCache& cache, const MeshData* mesh,
    const std::vector<int>& submeshNodeFallback, std::size_t baseBatchCount,
    bool preferFullGpuSkinning, std::span<const std::uint8_t> preserveVertexColor = {});
inline constexpr std::size_t kMaxGpuSkinMatrices = 128u;
}
