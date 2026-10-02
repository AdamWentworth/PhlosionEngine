#pragma once
#include "ModelData.h"
#include "WorldIndexedBatches.h"
namespace engine::models {
void appendAnimatedBillboards(const MeshData& mesh, const std::vector<glm::mat4>& nodeGlobals,
    const glm::mat4& modelMatrix, int animationIndex, float animationTime,
    glm::vec3 cameraPosition, glm::vec3 cameraUp, glm::vec3 cameraFacing,
    float alpha, std::vector<batches::WorldIndexedBatch>& out);
}
