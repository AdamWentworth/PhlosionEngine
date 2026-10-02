#include "engine/models/ModelGeometryCache.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string makeIndexedBatchKeyPrefix(
    const engine::models::MeshData& mesh) {
    if (!mesh.assetCacheIdentity.empty()) {
        return "__runtime_mesh_id__:" +
               mesh.assetCacheIdentity;
    }
    return "__runtime_mesh__:" +
           std::to_string(static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(&mesh)));
}

auto& fastTexturedMeshTemplateCaches() {
    using Cache = engine::models::geometry::
        FastTexturedMeshTemplateCache;
    static thread_local std::unordered_map<
        const engine::models::MeshData*,
        Cache> caches;
    return caches;
}

int resolveDefaultSkinNodeIndexImpl(const engine::models::MeshData* mesh) {
    if (!mesh) return -1;
    int selectedSkin = -1;
    int selectedNode = -1;
    for (std::size_t ni = 0; ni < mesh->nodeSkin.size(); ++ni) {
        const int skinIndex = mesh->nodeSkin[ni];
        if (skinIndex < 0) continue;
        if (selectedSkin < 0) {
            selectedSkin = skinIndex;
            selectedNode = static_cast<int>(ni);
            continue;
        }
        if (selectedSkin != skinIndex) {
            return -1;
        }
    }
    return selectedNode;
}

bool nodeUsesGpuFullSkinning(const engine::models::MeshData* mesh,
                             int triNodeIndex,
                             bool preferFullGpuSkinning,
                             std::size_t maxGpuSkinMatrices) {
    if (!preferFullGpuSkinning) return false;
    if (!mesh || triNodeIndex < 0) return false;
    const std::size_t nodeIndex = static_cast<std::size_t>(triNodeIndex);
    if (nodeIndex >= mesh->nodeSkin.size()) return false;
    const int skinIndex = mesh->nodeSkin[nodeIndex];
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh->skins.size()) return false;
    const auto& skin = mesh->skins[static_cast<std::size_t>(skinIndex)];
    return !skin.joints.empty() && skin.joints.size() <= maxGpuSkinMatrices;
}

} // namespace

namespace engine::models::geometry {
std::vector<int> buildSubmeshNodeFallback(
    const engine::models::MeshData& mesh) {
    std::vector<int> submeshNodeFallback;
    if (mesh.submeshMeshIndex.empty()) return submeshNodeFallback;
    submeshNodeFallback.assign(mesh.submeshMeshIndex.size(), -1);
    for (std::size_t si = 0; si < mesh.submeshMeshIndex.size(); ++si) {
        const int meshIndex = mesh.submeshMeshIndex[si];
        if (meshIndex >= 0 &&
            static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
            submeshNodeFallback[si] =
                mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
        }
    }
    return submeshNodeFallback;
}

std::string makeIndexedGeometryCacheKey(const std::string& keyPrefix,
                                        std::size_t baseSubmeshIndex,
                                        std::size_t batchIndex,
                                        std::size_t baseBatchCount) {
    std::string key = keyPrefix + "#submesh_geom:" + std::to_string(baseSubmeshIndex);
    if (batchIndex >= baseBatchCount) {
        key += "#split:" + std::to_string(batchIndex);
    }
    return key;
}

int resolveDefaultSkinNodeIndex(const engine::models::MeshData* mesh) {
    return resolveDefaultSkinNodeIndexImpl(mesh);
}

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const engine::models::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount,
    bool preferFullGpuSkinning,
    std::span<const std::uint8_t> preserveVertexColor) {
    if (!mesh) return nullptr;
    return ensureFastTexturedMeshTemplateCache(fastTexturedMeshTemplateCaches()[mesh],
        mesh, submeshNodeFallback, baseBatchCount, preferFullGpuSkinning, preserveVertexColor);
}

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    FastTexturedMeshTemplateCache& cache, const MeshData* mesh,
    const std::vector<int>& submeshNodeFallback, std::size_t baseBatchCount,
    bool preferFullGpuSkinning, std::span<const std::uint8_t> preserveVertexColor) {
    if (!mesh || baseBatchCount == 0u) {
        return nullptr;
    }
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.assetCacheIdentitySnapshot ==
            mesh->assetCacheIdentity &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.preferFullGpuSkinning == preferFullGpuSkinning &&
        cache.submeshNodeFallbackSnapshot == submeshNodeFallback &&
        std::equal(cache.preserveVertexColorSnapshot.begin(), cache.preserveVertexColorSnapshot.end(),
                   preserveVertexColor.begin(), preserveVertexColor.end()) &&
        !cache.batches.empty();
    if (cacheValid) {
        return &cache;
    }

    cache = {};
    cache.mesh = mesh;
    cache.assetCacheIdentitySnapshot =
        mesh->assetCacheIdentity;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.preferFullGpuSkinning = preferFullGpuSkinning;
    cache.defaultSkinNodeIndex = resolveDefaultSkinNodeIndex(mesh);
    cache.submeshNodeFallbackSnapshot = submeshNodeFallback;
    cache.preserveVertexColorSnapshot.assign(preserveVertexColor.begin(), preserveVertexColor.end());
    const std::string keyPrefix = makeIndexedBatchKeyPrefix(*mesh);

    const std::size_t triangleCount = mesh->indices.size() / 3u;
    if (triangleCount == 0u || mesh->vertices.empty()) {
        return nullptr;
    }

    cache.batches.assign(baseBatchCount, FastTexturedBatchTemplate{});
    for (std::size_t si = 0; si < baseBatchCount; ++si) {
        cache.batches[si].baseSubmeshIndex = si;
        cache.batches[si].geometryCacheKey =
            makeIndexedGeometryCacheKey(keyPrefix, si, si, baseBatchCount);
    }

    std::vector<std::unordered_map<int, std::size_t>> nodeToBatch(baseBatchCount);
    std::vector<std::size_t> batchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> triangleCountByBatch(baseBatchCount, 0u);

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t submeshIndex = 0u;
        if (triIdx < mesh->triangleSubmesh.size()) {
            submeshIndex = static_cast<std::size_t>(mesh->triangleSubmesh[triIdx]);
            if (submeshIndex >= baseBatchCount) submeshIndex = 0u;
        }

        int triNodeIndex =
            (triIdx < mesh->triangleNodeIndex.size()) ? mesh->triangleNodeIndex[triIdx] : -1;
        if (triNodeIndex < 0 &&
            triIdx < mesh->triangleSubmesh.size() &&
            !submeshNodeFallback.empty()) {
            const std::uint16_t fallbackSubmeshIndex = mesh->triangleSubmesh[triIdx];
            if (fallbackSubmeshIndex < submeshNodeFallback.size()) {
                triNodeIndex = submeshNodeFallback[fallbackSubmeshIndex];
            }
        }

        std::size_t batchIndex = submeshIndex;
        if (triNodeIndex >= 0) {
            auto& mapForSubmesh = nodeToBatch[submeshIndex];
            const auto found = mapForSubmesh.find(triNodeIndex);
            if (found != mapForSubmesh.end()) {
                batchIndex = found->second;
            } else if (mapForSubmesh.empty()) {
                mapForSubmesh.emplace(triNodeIndex, submeshIndex);
                cache.batches[submeshIndex].triNodeIndex = triNodeIndex;
                batchIndex = submeshIndex;
            } else {
                batchIndex = cache.batches.size();
                mapForSubmesh.emplace(triNodeIndex, batchIndex);
                FastTexturedBatchTemplate newBatch{};
                newBatch.baseSubmeshIndex = submeshIndex;
                newBatch.triNodeIndex = triNodeIndex;
                newBatch.skinnedBatch =
                    nodeUsesGpuFullSkinning(mesh, triNodeIndex, preferFullGpuSkinning, kMaxGpuSkinMatrices);
                newBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
                    keyPrefix, submeshIndex, batchIndex, baseBatchCount);
                cache.batches.push_back(std::move(newBatch));
                triangleCountByBatch.push_back(0u);
            }
        }

        if (batchIndex >= triangleCountByBatch.size()) {
            triangleCountByBatch.resize(batchIndex + 1u, 0u);
        }
        ++triangleCountByBatch[batchIndex];
        batchIndexByTriangle[triIdx] = batchIndex;
    }

    std::vector<std::vector<std::size_t>> splitBatchCandidatesBySource(cache.batches.size());
    std::vector<std::vector<std::uint16_t>> splitJointPaletteByBatch(cache.batches.size());
    std::vector<std::uint8_t> fullSkinBatchBySource(cache.batches.size(), 0u);
    std::vector<std::size_t> rigidNoWeightBatchBySource(
        cache.batches.size(),
        std::numeric_limits<std::size_t>::max());
    std::vector<std::size_t> splitBatchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> splitTriangleCountByBatch(cache.batches.size(), 0u);
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        auto& batch = cache.batches[bi];
        if (batch.triNodeIndex >= 0) {
            batch.skinnedBatch =
                nodeUsesGpuFullSkinning(mesh, batch.triNodeIndex, preferFullGpuSkinning, kMaxGpuSkinMatrices);
        } else {
            batch.skinnedBatch = false;
        }
        fullSkinBatchBySource[bi] = batch.skinnedBatch ? 1u : 0u;
        splitBatchCandidatesBySource[bi].push_back(bi);
    }

    constexpr float kJointWeightEpsilon = 0.00001f;
    const auto ensureRigidNoWeightBatch = [&](std::size_t sourceBatchIndex) -> std::size_t {
        if (sourceBatchIndex >= rigidNoWeightBatchBySource.size()) {
            rigidNoWeightBatchBySource.resize(
                sourceBatchIndex + 1u,
                std::numeric_limits<std::size_t>::max());
        }
        std::size_t& cachedBatchIndex = rigidNoWeightBatchBySource[sourceBatchIndex];
        if (cachedBatchIndex != std::numeric_limits<std::size_t>::max()) {
            return cachedBatchIndex;
        }

        const auto& sourceBatch = cache.batches[sourceBatchIndex];
        FastTexturedBatchTemplate rigidBatch{};
        rigidBatch.baseSubmeshIndex = sourceBatch.baseSubmeshIndex;
        rigidBatch.triNodeIndex = sourceBatch.triNodeIndex;
        rigidBatch.skinnedBatch = false;
        rigidBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
            keyPrefix, sourceBatch.baseSubmeshIndex, cache.batches.size(), baseBatchCount);
        cache.batches.push_back(std::move(rigidBatch));
        splitBatchCandidatesBySource.emplace_back();
        splitJointPaletteByBatch.emplace_back();
        splitTriangleCountByBatch.push_back(0u);
        fullSkinBatchBySource.push_back(0u);
        rigidNoWeightBatchBySource.push_back(std::numeric_limits<std::size_t>::max());
        cachedBatchIndex = cache.batches.size() - 1u;
        return cachedBatchIndex;
    };
    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t sourceBatchIndex = batchIndexByTriangle[triIdx];
        if (sourceBatchIndex >= splitBatchCandidatesBySource.size()) sourceBatchIndex = 0u;

        std::array<std::uint16_t, 12> triJoints{};
        std::size_t triJointCount = 0u;
        const std::size_t i = triIdx * 3u;
        if (i + 2u < mesh->indices.size()) {
            const std::uint32_t i0 = mesh->indices[i + 0u];
            const std::uint32_t i1 = mesh->indices[i + 1u];
            const std::uint32_t i2 = mesh->indices[i + 2u];
            const auto appendWeightedJoints = [&](const engine::models::MeshVertex& v) {
                const std::uint16_t joints[4] = {v.j0, v.j1, v.j2, v.j3};
                const float weights[4] = {v.w0, v.w1, v.w2, v.w3};
                for (int ji = 0; ji < 4; ++ji) {
                    if (weights[ji] <= kJointWeightEpsilon) continue;
                    bool seen = false;
                    for (std::size_t existing = 0; existing < triJointCount; ++existing) {
                        if (triJoints[existing] == joints[ji]) {
                            seen = true;
                            break;
                        }
                    }
                    if (seen || triJointCount >= triJoints.size()) continue;
                    triJoints[triJointCount++] = joints[ji];
                }
            };
            if (i0 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i0]);
            if (i1 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i1]);
            if (i2 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i2]);
        }

        if (fullSkinBatchBySource[sourceBatchIndex] != 0u) {
            const std::size_t chosenBatchIndex =
                triJointCount > 0u ? sourceBatchIndex : ensureRigidNoWeightBatch(sourceBatchIndex);
            if (chosenBatchIndex >= splitTriangleCountByBatch.size()) {
                splitTriangleCountByBatch.resize(chosenBatchIndex + 1u, 0u);
            }
            ++splitTriangleCountByBatch[chosenBatchIndex];
            splitBatchIndexByTriangle[triIdx] = chosenBatchIndex;
            continue;
        }

        std::size_t chosenBatchIndex = splitBatchCandidatesBySource[sourceBatchIndex][0];
        bool foundCandidate = false;
        for (const std::size_t candidateBatchIndex :
             splitBatchCandidatesBySource[sourceBatchIndex]) {
            if (candidateBatchIndex >= splitJointPaletteByBatch.size()) continue;
            const auto& candidatePalette = splitJointPaletteByBatch[candidateBatchIndex];
            std::size_t addedJointCount = 0u;
            for (std::size_t ji = 0; ji < triJointCount; ++ji) {
                const bool exists =
                    std::find(candidatePalette.begin(), candidatePalette.end(), triJoints[ji]) !=
                    candidatePalette.end();
                if (!exists) ++addedJointCount;
            }
            if (candidatePalette.size() + addedJointCount <= kMaxGpuSkinMatrices) {
                chosenBatchIndex = candidateBatchIndex;
                foundCandidate = true;
                break;
            }
        }

        if (!foundCandidate) {
            const auto& sourceBatch = cache.batches[sourceBatchIndex];
            FastTexturedBatchTemplate splitBatch{};
            splitBatch.baseSubmeshIndex = sourceBatch.baseSubmeshIndex;
            splitBatch.triNodeIndex = sourceBatch.triNodeIndex;
            splitBatch.skinnedBatch = false;
            splitBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
                keyPrefix,
                sourceBatch.baseSubmeshIndex,
                cache.batches.size(),
                baseBatchCount);
            cache.batches.push_back(std::move(splitBatch));
            splitJointPaletteByBatch.emplace_back();
            splitTriangleCountByBatch.push_back(0u);
            fullSkinBatchBySource.push_back(0u);
            chosenBatchIndex = cache.batches.size() - 1u;
            splitBatchCandidatesBySource[sourceBatchIndex].push_back(chosenBatchIndex);
        }

        auto& chosenPalette = splitJointPaletteByBatch[chosenBatchIndex];
        for (std::size_t ji = 0; ji < triJointCount; ++ji) {
            const bool exists =
                std::find(chosenPalette.begin(), chosenPalette.end(), triJoints[ji]) !=
                chosenPalette.end();
            if (!exists) chosenPalette.push_back(triJoints[ji]);
        }

        ++splitTriangleCountByBatch[chosenBatchIndex];
        splitBatchIndexByTriangle[triIdx] = chosenBatchIndex;
    }

    batchIndexByTriangle.swap(splitBatchIndexByTriangle);
    triangleCountByBatch.swap(splitTriangleCountByBatch);

    std::vector<std::vector<int>> remapByBatch(cache.batches.size());
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        auto& batch = cache.batches[bi];
        const std::size_t triCountForBatch =
            (bi < triangleCountByBatch.size()) ? triangleCountByBatch[bi] : 0u;
        const std::size_t indexReserve = triCountForBatch * 3u;
        batch.indices.reserve(indexReserve);
        batch.sourceVertexIndices.reserve(std::min(mesh->vertices.size(), indexReserve));
        remapByBatch[bi].assign(mesh->vertices.size(), -1);
    }

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        const std::size_t batchIndex = batchIndexByTriangle[triIdx];
        if (batchIndex >= cache.batches.size()) continue;
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = mesh->indices[i + 0];
        const std::uint32_t i1 = mesh->indices[i + 1];
        const std::uint32_t i2 = mesh->indices[i + 2];
        if (i0 >= mesh->vertices.size() ||
            i1 >= mesh->vertices.size() ||
            i2 >= mesh->vertices.size()) {
            continue;
        }

        auto& batch = cache.batches[batchIndex];
        auto& remap = remapByBatch[batchIndex];
        const auto appendVertex = [&](std::uint32_t src) -> std::uint32_t {
            int& mapped = remap[src];
            if (mapped >= 0) return static_cast<std::uint32_t>(mapped);
            mapped = static_cast<int>(batch.sourceVertexIndices.size());
            batch.sourceVertexIndices.push_back(src);
            return static_cast<std::uint32_t>(mapped);
        };

        batch.indices.push_back(appendVertex(i0));
        batch.indices.push_back(appendVertex(i1));
        batch.indices.push_back(appendVertex(i2));
    }

    for (auto& batch : cache.batches) {
        batch.gpuJointPalette.clear();
        std::unordered_map<std::uint16_t, std::uint16_t> jointRemap;
        jointRemap.reserve(16u);
        const bool useFullSkinning = batch.skinnedBatch;
        if (!useFullSkinning) {
            bool jointPaletteOverflow = false;
            for (const std::uint32_t srcIndex : batch.sourceVertexIndices) {
                if (srcIndex >= mesh->vertices.size()) continue;
                const auto& src = mesh->vertices[srcIndex];
                const std::uint16_t joints[4] = {src.j0, src.j1, src.j2, src.j3};
                const float weights[4] = {src.w0, src.w1, src.w2, src.w3};
                for (int ji = 0; ji < 4; ++ji) {
                    if (weights[ji] <= 0.00001f) continue;
                    if (jointRemap.find(joints[ji]) != jointRemap.end()) continue;
                    if (jointRemap.size() >= kMaxGpuSkinMatrices) {
                        jointPaletteOverflow = true;
                        break;
                    }
                    const std::uint16_t next = static_cast<std::uint16_t>(jointRemap.size());
                    jointRemap.emplace(joints[ji], next);
                    batch.gpuJointPalette.push_back(joints[ji]);
                }
                if (jointPaletteOverflow) break;
            }
            if (jointPaletteOverflow) {
                jointRemap.clear();
                batch.gpuJointPalette.clear();
            }
        }

        batch.gpuTemplateVertices.resize(batch.sourceVertexIndices.size());
        for (std::size_t vi = 0; vi < batch.sourceVertexIndices.size(); ++vi) {
            const std::uint32_t srcIndex = batch.sourceVertexIndices[vi];
            const auto& src = mesh->vertices[srcIndex];
            IRenderBackend::WorldMeshVertex outVertex{};
            outVertex.x = src.position.x;
            outVertex.y = src.position.y;
            outVertex.z = src.position.z;
            outVertex.u = src.uv.x;
            outVertex.v = src.uv.y;
            const bool preserveAuxiliaryVertexColor =
                batch.baseSubmeshIndex < preserveVertexColor.size() &&
                preserveVertexColor[batch.baseSubmeshIndex] != 0;
            const glm::vec3 authoredColor =
                (mesh->hasVertexColor || preserveAuxiliaryVertexColor)
                ? glm::clamp(glm::vec3(src.color.r, src.color.g, src.color.b), 0.0f, 1.0f)
                : glm::vec3(1.0f);
            const float authoredAlpha =
                (mesh->hasVertexColor || preserveAuxiliaryVertexColor)
                ? std::clamp(src.color.a, 0.0f, 1.0f)
                : 1.0f;
            outVertex.r = authoredColor.r;
            outVertex.g = authoredColor.g;
            outVertex.b = authoredColor.b;
            outVertex.a = authoredAlpha;
            outVertex.nx = src.normal.x;
            outVertex.ny = src.normal.y;
            outVertex.nz = src.normal.z;
            outVertex.tx = src.tangent.x;
            outVertex.ty = src.tangent.y;
            outVertex.tz = src.tangent.z;
            outVertex.tw = src.tangent.w;
            std::uint16_t mappedJ0 = src.j0;
            std::uint16_t mappedJ1 = src.j1;
            std::uint16_t mappedJ2 = src.j2;
            std::uint16_t mappedJ3 = src.j3;
            if (!useFullSkinning && !jointRemap.empty()) {
                if (src.w0 > 0.00001f) {
                    const auto it = jointRemap.find(src.j0);
                    if (it != jointRemap.end()) mappedJ0 = it->second;
                }
                if (src.w1 > 0.00001f) {
                    const auto it = jointRemap.find(src.j1);
                    if (it != jointRemap.end()) mappedJ1 = it->second;
                }
                if (src.w2 > 0.00001f) {
                    const auto it = jointRemap.find(src.j2);
                    if (it != jointRemap.end()) mappedJ2 = it->second;
                }
                if (src.w3 > 0.00001f) {
                    const auto it = jointRemap.find(src.j3);
                    if (it != jointRemap.end()) mappedJ3 = it->second;
                }
            }
            outVertex.joint0 = static_cast<float>(mappedJ0);
            outVertex.joint1 = static_cast<float>(mappedJ1);
            outVertex.joint2 = static_cast<float>(mappedJ2);
            outVertex.joint3 = static_cast<float>(mappedJ3);
            outVertex.weight0 = src.w0;
            outVertex.weight1 = src.w1;
            outVertex.weight2 = src.w2;
            outVertex.weight3 = src.w3;
            batch.gpuTemplateVertices[vi] = outVertex;
        }
    }

    cache.batches.erase(
        std::remove_if(
            cache.batches.begin(),
            cache.batches.end(),
            [](const FastTexturedBatchTemplate& batch) {
                return batch.indices.empty() || batch.gpuTemplateVertices.empty();
            }),
        cache.batches.end());

    return cache.batches.empty() ? nullptr : &cache;
}

} // namespace engine::models::geometry
