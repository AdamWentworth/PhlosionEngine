#pragma once
#include "ModelData.h"
#include "WorldIndexedBatches.h"
namespace engine::models {
// Texture data is borrowed from source, which must outlive the returned templates.
std::vector<batches::WorldIndexedBatch> buildMaterialTemplates(const MeshData& source,
    const std::string& keyPrefix, std::size_t batchCount);
bool applyEmissionAnimation(const batches::WorldIndexedBatch& source,
    batches::WorldIndexedBatch& destination, const MeshData& mesh,
    std::size_t submeshIndex, int animationIndex, float animationTime);
}
