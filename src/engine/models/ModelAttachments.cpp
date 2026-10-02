#include "ModelAttachments.h"
#include "ModelTracks.h"
#include <algorithm>
#include <cmath>
namespace engine::models {
void appendAnimatedBillboards(const MeshData& source, const std::vector<glm::mat4>& nodeGlobals,
    const glm::mat4& modelMatrix, int animationIndex, float animationTime,
    glm::vec3 cameraPosition, glm::vec3 cameraUp, glm::vec3 cameraFacing,
    float alpha, std::vector<batches::WorldIndexedBatch>& out) {
    const auto* mesh = &source;
        for (std::size_t bi = 0; bi < mesh->animatedBillboards.size(); ++bi) {
            const auto& billboard = mesh->animatedBillboards[bi];
            const int clipIndex = animationIndex;
            if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= billboard.clipScales.size() ||
                billboard.clipScales[clipIndex].keys.empty() || billboard.nodeIndex < 0 ||
                static_cast<std::size_t>(billboard.nodeIndex) >= nodeGlobals.size()) continue;
            if (static_cast<std::size_t>(clipIndex) >= mesh->animations.size()) continue;
            const auto& clip = mesh->animations[clipIndex];
            float seconds = std::max(0.0f, animationTime);
            if (static_cast<std::size_t>(clipIndex) < billboard.clipLoops.size() && billboard.clipLoops[clipIndex] && clip.durationSec > 0) seconds = std::fmod(seconds, clip.durationSec);
            const float scale = sampleMaterialCurve(billboard.clipScales[clipIndex], seconds, 0.0f, false, 0.0f, false);
            if (scale <= 0.0003f) continue;
            glm::vec3 offset = billboard.offset;
            if (static_cast<std::size_t>(clipIndex) < billboard.clipOffsets.size())
                for (glm::length_t axis = 0; axis < 3; ++axis)
                    offset[axis] = sampleMaterialCurve(billboard.clipOffsets[clipIndex][axis], seconds,
                        offset[axis], false, 0.0f, false);
            glm::vec3 center = glm::vec3(modelMatrix * nodeGlobals[billboard.nodeIndex] * glm::vec4(offset, 1.0f));
            glm::vec3 forward = cameraFacing;
            if (glm::dot(forward, forward) < 1e-10f) forward = cameraPosition - center;
            if (glm::dot(forward, forward) < 1e-10f) continue;
            forward = glm::normalize(forward);
            // Project the joint up axis
            // onto the camera plane, preserving roll while replacing its normal.
            const glm::vec3 jointUp = glm::vec3(modelMatrix * nodeGlobals[billboard.nodeIndex] * glm::vec4(billboard.up, 0.0f));
            glm::vec3 right = glm::cross(jointUp, forward);
            if (glm::dot(right, right) < 1e-8f) right = glm::cross(cameraUp, forward);
            if (glm::dot(right, right) < 1e-8f) right = glm::cross(glm::vec3(0, 0, 1), forward);
            right = glm::normalize(right);
            const glm::vec3 up = glm::cross(forward, right);
            const float modelScale = glm::length(glm::vec3(modelMatrix[0]));
            center += (right*billboard.quadOffset.x + up*billboard.quadOffset.y +
                forward*billboard.quadOffset.z) * (scale*modelScale);
            const glm::vec2 half = billboard.size * (0.5f * scale * modelScale);
            batches::WorldIndexedBatch batch;
            batch.textureKey = mesh->assetCacheIdentity + "#billboard:" + std::to_string(bi);
            batch.textureCacheKey = batch.textureKey;
            batch.textureRgba = billboard.texture.rgba.data();
            batch.textureWidth = billboard.texture.width;
            batch.textureHeight = billboard.texture.height;
            batch.textureWrapS = batch.textureWrapT = 33071;
            batch.alphaMode = 2u;
            batch.alphaCutoff = 0.0f;
            batch.unlit = true;
            batch.sortDepth = glm::dot(cameraPosition-center, cameraPosition-center);
            for (const glm::vec2 corner : {glm::vec2(-1,-1), glm::vec2(1,-1), glm::vec2(1,1), glm::vec2(-1,1)}) {
                const glm::vec3 position = center + right*(corner.x*half.x) + up*(corner.y*half.y);
                IRenderBackend::WorldMeshVertex vertex;
                vertex.x=position.x; vertex.y=position.y; vertex.z=position.z;
                vertex.u=(corner.x+1)*.5f; vertex.v=(1-corner.y)*.5f;
                vertex.nx=forward.x; vertex.ny=forward.y; vertex.nz=forward.z;
                vertex.a=alpha;
                batch.vertices.push_back(vertex);
            }
            batch.indices = {0,1,2,0,2,3};
            out.push_back(std::move(batch));
        }
}
}
