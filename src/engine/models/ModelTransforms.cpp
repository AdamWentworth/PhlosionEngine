#include "engine/models/ModelTransforms.h"



#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace {

glm::vec3 safeNormalizeVec3(const glm::vec3& v) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 safeNormalizeVec3(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return fallback;
}

struct NodeTransformCacheEntry {
    glm::mat4 worldM{1.0f};
    glm::mat3 worldNormalM{1.0f};
};

struct TransformScratch {
    std::uint32_t vertexCacheStamp = 1u;

    std::vector<std::vector<glm::mat4>> skinMatricesByNode;
    std::vector<std::uint8_t> skinMatricesReady;
    std::vector<std::vector<float>> gpuSkinMatricesBySkin;
    std::vector<std::uint8_t> gpuSkinMatricesReady;
    std::vector<glm::mat4> nodeGlobalInverseCache;
    std::vector<std::uint8_t> nodeGlobalInverseReady;

    std::vector<NodeTransformCacheEntry> nodeTransformCache;
    std::vector<std::uint8_t> nodeTransformWorldReady;
    std::vector<std::uint8_t> nodeTransformNormalReady;
    std::vector<glm::mat3> nodeModelNormalCache;
    std::vector<std::uint8_t> nodeModelNormalReady;

    std::vector<engine::models::transforms::WorldVertexSample>
        worldVertexCache;
    std::vector<int> worldVertexCacheNode;
    std::vector<std::uint32_t> worldVertexCacheStamp;

    std::vector<glm::vec3> worldVertexPosCache;
    std::vector<int> worldVertexPosCacheNode;
    std::vector<std::uint32_t> worldVertexPosCacheStamp;

    std::vector<glm::vec3> localVertexPosCache;
    std::vector<std::uint32_t> localVertexPosCacheStamp;

    std::vector<glm::vec3> modelVertexNormalCache;
    std::vector<int> modelVertexNormalCacheNode;
    std::vector<std::uint32_t> modelVertexNormalCacheStamp;

    std::vector<glm::vec4> modelVertexTangentCache;
    std::vector<int> modelVertexTangentCacheNode;
    std::vector<std::uint32_t> modelVertexTangentCacheStamp;
};

thread_local TransformScratch g_scratch;
constexpr std::size_t kMaxGpuSkinMatrices = 128u;

} // namespace

namespace engine::models::transforms {

void Resolver::initialize(const TransformOptions& options) {
    mesh_ = options.mesh;
    modelM_ = options.modelMatrix;
    hasClipPose_ = options.hasClipPose;
    usePositionOnlyVertexPath_ = options.positionOnly;
    clipSkinningEnabled_ = options.clipSkinning;
    gpuClipSkinningRequested_ = options.gpuSkinning;
    gpuBindPoseSkinningRequested_ = options.gpuBindPose;
    backendGpuSkinNodeGlobalsEnabled_ = options.pairedSkinMatrices;
    deformLocal_ = options.deformLocal;
    nodeGlobals_ = options.nodeGlobals ? options.nodeGlobals : &mesh_->bindNodeGlobals;
    nodeCount_ = nodeGlobals_->size();

    if (g_scratch.skinMatricesByNode.size() < nodeCount_) g_scratch.skinMatricesByNode.resize(nodeCount_);
    for (std::size_t ni = 0; ni < nodeCount_; ++ni) g_scratch.skinMatricesByNode[ni].clear();

    if (g_scratch.skinMatricesReady.size() < nodeCount_) g_scratch.skinMatricesReady.resize(nodeCount_, 0u);
    std::fill(g_scratch.skinMatricesReady.begin(), g_scratch.skinMatricesReady.begin() + nodeCount_, 0u);

    const std::size_t skinCount = mesh_ ? mesh_->skins.size() : 0u;
    if (g_scratch.gpuSkinMatricesBySkin.size() < skinCount) {
        g_scratch.gpuSkinMatricesBySkin.resize(skinCount);
    }
    if (g_scratch.gpuSkinMatricesReady.size() < skinCount) {
        g_scratch.gpuSkinMatricesReady.resize(skinCount, 0u);
    }
    if (skinCount > 0u) {
        std::fill(
            g_scratch.gpuSkinMatricesReady.begin(),
            g_scratch.gpuSkinMatricesReady.begin() + skinCount,
            0u);
    }

    if (g_scratch.nodeGlobalInverseCache.size() < nodeCount_) {
        g_scratch.nodeGlobalInverseCache.resize(nodeCount_, glm::mat4(1.0f));
    }
    if (g_scratch.nodeGlobalInverseReady.size() < nodeCount_) {
        g_scratch.nodeGlobalInverseReady.resize(nodeCount_, 0u);
    }
    std::fill(
        g_scratch.nodeGlobalInverseReady.begin(),
        g_scratch.nodeGlobalInverseReady.begin() + nodeCount_,
        0u);

    const std::size_t nodeCacheCount = nodeCount_ + 1u;
    if (g_scratch.nodeTransformCache.size() < nodeCacheCount) g_scratch.nodeTransformCache.resize(nodeCacheCount);
    if (g_scratch.nodeTransformWorldReady.size() < nodeCacheCount) {
        g_scratch.nodeTransformWorldReady.resize(nodeCacheCount, 0u);
    }
    if (g_scratch.nodeTransformNormalReady.size() < nodeCacheCount) {
        g_scratch.nodeTransformNormalReady.resize(nodeCacheCount, 0u);
    }
    if (g_scratch.nodeModelNormalCache.size() < nodeCacheCount) {
        g_scratch.nodeModelNormalCache.resize(nodeCacheCount, glm::mat3(1.0f));
    }
    if (g_scratch.nodeModelNormalReady.size() < nodeCacheCount) {
        g_scratch.nodeModelNormalReady.resize(nodeCacheCount, 0u);
    }
    std::fill(
        g_scratch.nodeTransformWorldReady.begin(),
        g_scratch.nodeTransformWorldReady.begin() + nodeCacheCount,
        0u);
    std::fill(
        g_scratch.nodeTransformNormalReady.begin(),
        g_scratch.nodeTransformNormalReady.begin() + nodeCacheCount,
        0u);
    std::fill(
        g_scratch.nodeModelNormalReady.begin(),
        g_scratch.nodeModelNormalReady.begin() + nodeCacheCount,
        0u);

    ++g_scratch.vertexCacheStamp;
    if (g_scratch.vertexCacheStamp == 0u) {
        g_scratch.vertexCacheStamp = 1u;
        std::fill(g_scratch.localVertexPosCacheStamp.begin(), g_scratch.localVertexPosCacheStamp.end(), 0u);
        std::fill(g_scratch.worldVertexCacheStamp.begin(), g_scratch.worldVertexCacheStamp.end(), 0u);
        std::fill(g_scratch.worldVertexPosCacheStamp.begin(), g_scratch.worldVertexPosCacheStamp.end(), 0u);
        std::fill(g_scratch.modelVertexNormalCacheStamp.begin(), g_scratch.modelVertexNormalCacheStamp.end(), 0u);
        std::fill(g_scratch.modelVertexTangentCacheStamp.begin(), g_scratch.modelVertexTangentCacheStamp.end(), 0u);
    }

    const std::size_t meshVertexCount = mesh_ ? mesh_->vertices.size() : 0u;
    if (g_scratch.localVertexPosCache.size() < meshVertexCount) {
        g_scratch.localVertexPosCache.resize(meshVertexCount);
    }
    if (g_scratch.localVertexPosCacheStamp.size() < meshVertexCount) {
        g_scratch.localVertexPosCacheStamp.resize(meshVertexCount, 0u);
    }

    if (!usePositionOnlyVertexPath_) {
        if (g_scratch.worldVertexCache.size() < meshVertexCount) {
            g_scratch.worldVertexCache.resize(meshVertexCount);
        }
        if (g_scratch.worldVertexCacheNode.size() < meshVertexCount) {
            g_scratch.worldVertexCacheNode.resize(meshVertexCount, std::numeric_limits<int>::min());
        }
        if (g_scratch.worldVertexCacheStamp.size() < meshVertexCount) {
            g_scratch.worldVertexCacheStamp.resize(meshVertexCount, 0u);
        }
    }
    if (g_scratch.worldVertexPosCache.size() < meshVertexCount) {
        g_scratch.worldVertexPosCache.resize(meshVertexCount);
    }
    if (g_scratch.worldVertexPosCacheNode.size() < meshVertexCount) {
        g_scratch.worldVertexPosCacheNode.resize(meshVertexCount, std::numeric_limits<int>::min());
    }
    if (g_scratch.worldVertexPosCacheStamp.size() < meshVertexCount) {
        g_scratch.worldVertexPosCacheStamp.resize(meshVertexCount, 0u);
    }

    if (g_scratch.modelVertexNormalCache.size() < meshVertexCount) {
        g_scratch.modelVertexNormalCache.resize(meshVertexCount);
    }
    if (g_scratch.modelVertexNormalCacheNode.size() < meshVertexCount) {
        g_scratch.modelVertexNormalCacheNode.resize(meshVertexCount, std::numeric_limits<int>::min());
    }
    if (g_scratch.modelVertexNormalCacheStamp.size() < meshVertexCount) {
        g_scratch.modelVertexNormalCacheStamp.resize(meshVertexCount, 0u);
    }

    if (g_scratch.modelVertexTangentCache.size() < meshVertexCount) {
        g_scratch.modelVertexTangentCache.resize(meshVertexCount);
    }
    if (g_scratch.modelVertexTangentCacheNode.size() < meshVertexCount) {
        g_scratch.modelVertexTangentCacheNode.resize(meshVertexCount, std::numeric_limits<int>::min());
    }
    if (g_scratch.modelVertexTangentCacheStamp.size() < meshVertexCount) {
        g_scratch.modelVertexTangentCacheStamp.resize(meshVertexCount, 0u);
    }
}

std::size_t Resolver::nodeTransformIndexFor(int triNodeIndex) const {
    std::size_t cacheIndex = 0u;
    if (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeCount_) {
        cacheIndex = static_cast<std::size_t>(triNodeIndex) + 1u;
    }
    return cacheIndex;
}

glm::vec3 Resolver::resolveLocalVertexPos(
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.localVertexPosCache.size() &&
        g_scratch.localVertexPosCacheStamp[vertexIndex] == stamp) {
        return g_scratch.localVertexPosCache[vertexIndex];
    }

    glm::vec3 local = vtx.position;
    if (!hasClipPose_ && deformLocal_) local = deformLocal_(local);

    if (vertexIndex < g_scratch.localVertexPosCache.size()) {
        g_scratch.localVertexPosCache[vertexIndex] = local;
        g_scratch.localVertexPosCacheStamp[vertexIndex] = stamp;
    }
    return local;
}

const std::vector<glm::mat4>* Resolver::ensureSkinMatricesForNode(int nodeIndex) {
    if (!mesh_ || !nodeGlobals_) return nullptr;
    if (nodeIndex < 0 ||
        static_cast<std::size_t>(nodeIndex) >= mesh_->nodeSkin.size() ||
        static_cast<std::size_t>(nodeIndex) >= nodeGlobals_->size()) {
        return nullptr;
    }
    const std::size_t nodeIdx = static_cast<std::size_t>(nodeIndex);
    const int skinIndex = mesh_->nodeSkin[nodeIdx];
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh_->skins.size()) {
        return nullptr;
    }

    if (g_scratch.skinMatricesReady[nodeIdx] == 0u) {
        const auto& skin = mesh_->skins[static_cast<std::size_t>(skinIndex)];
        if (g_scratch.nodeGlobalInverseReady[nodeIdx] == 0u) {
            g_scratch.nodeGlobalInverseCache[nodeIdx] = glm::inverse((*nodeGlobals_)[nodeIdx]);
            g_scratch.nodeGlobalInverseReady[nodeIdx] = 1u;
        }
        const glm::mat4& invMeshGlobal = g_scratch.nodeGlobalInverseCache[nodeIdx];
        auto& mats = g_scratch.skinMatricesByNode[nodeIdx];
        mats.assign(skin.joints.size(), glm::mat4(1.0f));
        for (std::size_t j = 0; j < skin.joints.size(); ++j) {
            const int jointNode = skin.joints[j];
            if (jointNode < 0 || static_cast<std::size_t>(jointNode) >= nodeGlobals_->size()) {
                continue;
            }
            const glm::mat4 invBind =
                (j < skin.inverseBind.size()) ? skin.inverseBind[j] : glm::mat4(1.0f);
            mats[j] =
                invMeshGlobal *
                (*nodeGlobals_)[static_cast<std::size_t>(jointNode)] *
                invBind;
        }
        g_scratch.skinMatricesReady[nodeIdx] = 1u;
    }
    return &g_scratch.skinMatricesByNode[nodeIdx];
}

Resolver::SkinResult Resolver::skinVertexAtNode(
    int nodeIndex,
    const MeshVertex& vtx,
    const glm::vec3& localPos,
    const glm::vec3& localNormal) {
    SkinResult outSkin{localPos, localNormal, false};
    if (hasClipPose_ && !clipSkinningEnabled_) {
        return outSkin;
    }
    const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
    if (!matsPtr) return outSkin;
    const auto& mats = *matsPtr;

    const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
    const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
    const bool rigidSingleJoint =
        (weights[0] >= 0.999f) &&
        (weights[1] <= 0.00001f) &&
        (weights[2] <= 0.00001f) &&
        (weights[3] <= 0.00001f) &&
        (static_cast<std::size_t>(joints[0]) < mats.size());
    if (rigidSingleJoint) {
        const glm::mat4& m = mats[static_cast<std::size_t>(joints[0])];
        outSkin.pos = glm::vec3(m * glm::vec4(localPos, 1.0f));
        outSkin.normal = safeNormalizeVec3(glm::mat3(m) * localNormal);
        outSkin.applied = true;
        return outSkin;
    }

    glm::vec4 blendedPos(0.0f);
    glm::vec3 blendedNormal(0.0f);
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = weights[i];
        if (w <= 0.00001f) continue;
        const std::size_t joint = static_cast<std::size_t>(joints[i]);
        if (joint >= mats.size()) continue;
        blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
        blendedNormal += (glm::mat3(mats[joint]) * localNormal) * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.00001f) return outSkin;
    if (totalWeight < 0.999f) {
        const float remain = 1.0f - totalWeight;
        blendedPos += glm::vec4(localPos, 1.0f) * remain;
        blendedNormal += localNormal * remain;
    }
    outSkin.pos = glm::vec3(blendedPos);
    outSkin.normal = safeNormalizeVec3(blendedNormal);
    outSkin.applied = true;
    return outSkin;
}

glm::vec3 Resolver::skinDirectionAtNode(
    int nodeIndex,
    const MeshVertex& vtx,
    const glm::vec3& localDirection) {
    if (hasClipPose_ && !clipSkinningEnabled_) {
        return localDirection;
    }
    const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
    if (!matsPtr) return localDirection;
    const auto& mats = *matsPtr;

    const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
    const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
    const bool rigidSingleJoint =
        (weights[0] >= 0.999f) &&
        (weights[1] <= 0.00001f) &&
        (weights[2] <= 0.00001f) &&
        (weights[3] <= 0.00001f) &&
        (static_cast<std::size_t>(joints[0]) < mats.size());
    if (rigidSingleJoint) {
        return safeNormalizeVec3(
            glm::mat3(mats[static_cast<std::size_t>(joints[0])]) * localDirection,
            safeNormalizeVec3(localDirection));
    }

    glm::vec3 blendedDirection(0.0f);
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = weights[i];
        if (w <= 0.00001f) continue;
        const std::size_t joint = static_cast<std::size_t>(joints[i]);
        if (joint >= mats.size()) continue;
        blendedDirection += (glm::mat3(mats[joint]) * localDirection) * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.00001f) return localDirection;
    if (totalWeight < 0.999f) {
        const float remain = 1.0f - totalWeight;
        blendedDirection += localDirection * remain;
    }
    return safeNormalizeVec3(blendedDirection, safeNormalizeVec3(localDirection));
}

glm::vec3 Resolver::skinPositionAtNode(int nodeIndex,
                                       const MeshVertex& vtx,
                                       const glm::vec3& localPos) {
    glm::vec3 outPos = localPos;
    if (hasClipPose_ && !clipSkinningEnabled_) {
        return outPos;
    }
    const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
    if (!matsPtr) return outPos;
    const auto& mats = *matsPtr;

    const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
    const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
    const bool rigidSingleJoint =
        (weights[0] >= 0.999f) &&
        (weights[1] <= 0.00001f) &&
        (weights[2] <= 0.00001f) &&
        (weights[3] <= 0.00001f) &&
        (static_cast<std::size_t>(joints[0]) < mats.size());
    if (rigidSingleJoint) {
        outPos = glm::vec3(
            mats[static_cast<std::size_t>(joints[0])] *
            glm::vec4(localPos, 1.0f));
        return outPos;
    }

    glm::vec4 blendedPos(0.0f);
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = weights[i];
        if (w <= 0.00001f) continue;
        const std::size_t joint = static_cast<std::size_t>(joints[i]);
        if (joint >= mats.size()) continue;
        blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.00001f) return outPos;
    if (totalWeight < 0.999f) {
        const float remain = 1.0f - totalWeight;
        blendedPos += glm::vec4(localPos, 1.0f) * remain;
    }
    return glm::vec3(blendedPos);
}

const glm::mat4& Resolver::worldMatrixForNode(int triNodeIndex) {
    const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
    if (g_scratch.nodeTransformWorldReady[cacheIndex] != 0u) {
        return g_scratch.nodeTransformCache[cacheIndex].worldM;
    }

    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    auto& entry = g_scratch.nodeTransformCache[cacheIndex];
    entry.worldM = modelM_ * nodeGlobal;
    g_scratch.nodeTransformWorldReady[cacheIndex] = 1u;
    return entry.worldM;
}

const glm::mat3& Resolver::worldNormalMatrixForNode(int triNodeIndex) {
    const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
    if (g_scratch.nodeTransformNormalReady[cacheIndex] != 0u) {
        return g_scratch.nodeTransformCache[cacheIndex].worldNormalM;
    }
    if (g_scratch.nodeTransformWorldReady[cacheIndex] == 0u) {
        (void)worldMatrixForNode(triNodeIndex);
    }
    auto& entry = g_scratch.nodeTransformCache[cacheIndex];
    entry.worldNormalM = glm::transpose(glm::inverse(glm::mat3(entry.worldM)));
    g_scratch.nodeTransformNormalReady[cacheIndex] = 1u;
    return entry.worldNormalM;
}

const glm::mat3& Resolver::modelNormalMatrixForNode(int triNodeIndex) {
    const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
    if (g_scratch.nodeModelNormalReady[cacheIndex] != 0u) {
        return g_scratch.nodeModelNormalCache[cacheIndex];
    }

    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    g_scratch.nodeModelNormalCache[cacheIndex] = glm::transpose(glm::inverse(glm::mat3(nodeGlobal)));
    g_scratch.nodeModelNormalReady[cacheIndex] = 1u;
    return g_scratch.nodeModelNormalCache[cacheIndex];
}

WorldVertexSample Resolver::resolveWorldVertex(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.worldVertexCache.size() &&
        g_scratch.worldVertexCacheStamp[vertexIndex] == stamp &&
        g_scratch.worldVertexCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.worldVertexCache[vertexIndex];
    }

    const glm::vec3 local = resolveLocalVertexPos(vertexIndex, vtx);
    const auto sk = skinVertexAtNode(triNodeIndex, vtx, local, vtx.normal);
    const glm::mat4& worldM = worldMatrixForNode(triNodeIndex);
    const glm::mat3& worldNormalM = worldNormalMatrixForNode(triNodeIndex);

    WorldVertexSample out;
    out.pos = glm::vec3(worldM * glm::vec4(sk.pos, 1.0f));
    out.normal = safeNormalizeVec3(worldNormalM * sk.normal);

    if (vertexIndex < g_scratch.worldVertexCache.size()) {
        g_scratch.worldVertexCache[vertexIndex] = out;
        g_scratch.worldVertexCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.worldVertexCacheStamp[vertexIndex] = stamp;
    }
    return out;
}

glm::vec3 Resolver::resolveWorldVertexPos(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.worldVertexPosCache.size() &&
        g_scratch.worldVertexPosCacheStamp[vertexIndex] == stamp &&
        g_scratch.worldVertexPosCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.worldVertexPosCache[vertexIndex];
    }

    const glm::vec3 local = resolveLocalVertexPos(vertexIndex, vtx);
    const glm::vec3 skinnedPos = skinPositionAtNode(triNodeIndex, vtx, local);
    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    const glm::vec3 outPos = glm::vec3(nodeGlobal * glm::vec4(skinnedPos, 1.0f));

    if (vertexIndex < g_scratch.worldVertexPosCache.size()) {
        g_scratch.worldVertexPosCache[vertexIndex] = outPos;
        g_scratch.worldVertexPosCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.worldVertexPosCacheStamp[vertexIndex] = stamp;
    }
    return outPos;
}

glm::vec3 Resolver::resolveModelVertexNormal(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.modelVertexNormalCache.size() &&
        g_scratch.modelVertexNormalCacheStamp[vertexIndex] == stamp &&
        g_scratch.modelVertexNormalCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.modelVertexNormalCache[vertexIndex];
    }

    const glm::vec3 skinnedNormal = skinDirectionAtNode(triNodeIndex, vtx, vtx.normal);
    const glm::vec3 outNormal =
        safeNormalizeVec3(modelNormalMatrixForNode(triNodeIndex) * skinnedNormal);

    if (vertexIndex < g_scratch.modelVertexNormalCache.size()) {
        g_scratch.modelVertexNormalCache[vertexIndex] = outNormal;
        g_scratch.modelVertexNormalCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.modelVertexNormalCacheStamp[vertexIndex] = stamp;
    }
    return outNormal;
}

ModelVertexSurfaceSample Resolver::resolveModelVertexSurface(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const MeshVertex& vtx,
    bool needNormal,
    bool needTangent) {
    ModelVertexSurfaceSample out;
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    const bool posCached =
        vertexIndex < g_scratch.worldVertexPosCache.size() &&
        g_scratch.worldVertexPosCacheStamp[vertexIndex] == stamp &&
        g_scratch.worldVertexPosCacheNode[vertexIndex] == triNodeIndex;
    const bool normalCached =
        needNormal &&
        vertexIndex < g_scratch.modelVertexNormalCache.size() &&
        g_scratch.modelVertexNormalCacheStamp[vertexIndex] == stamp &&
        g_scratch.modelVertexNormalCacheNode[vertexIndex] == triNodeIndex;
    const bool tangentCached =
        needTangent &&
        vertexIndex < g_scratch.modelVertexTangentCache.size() &&
        g_scratch.modelVertexTangentCacheStamp[vertexIndex] == stamp &&
        g_scratch.modelVertexTangentCacheNode[vertexIndex] == triNodeIndex;

    if (posCached) {
        out.pos = g_scratch.worldVertexPosCache[vertexIndex];
    }
    if (normalCached) {
        out.normal = g_scratch.modelVertexNormalCache[vertexIndex];
    } else if (!needNormal) {
        out.normal = vtx.normal;
    }
    if (tangentCached) {
        out.tangent = g_scratch.modelVertexTangentCache[vertexIndex];
    } else if (!needTangent) {
        out.tangent = vtx.tangent;
    }
    if (posCached &&
        (!needNormal || normalCached) &&
        (!needTangent || tangentCached)) {
        return out;
    }

    const glm::vec3 local = resolveLocalVertexPos(vertexIndex, vtx);
    const bool needPosComputation = !posCached;
    const bool needNormalComputation = needNormal && !normalCached;
    const bool needTangentComputation = needTangent && !tangentCached;

    glm::mat4 nodeGlobal(1.0f);
    if (needPosComputation) {
        nodeGlobal =
            (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
                ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
                : glm::mat4(1.0f);
    }

    const glm::mat3* modelNormalM = nullptr;
    if (needNormalComputation || needTangentComputation) {
        modelNormalM = &modelNormalMatrixForNode(triNodeIndex);
    }

    if (needPosComputation || needNormalComputation) {
        if (needNormalComputation) {
            const auto skinned = skinVertexAtNode(triNodeIndex, vtx, local, vtx.normal);
            if (needPosComputation) {
                out.pos = glm::vec3(nodeGlobal * glm::vec4(skinned.pos, 1.0f));
            }
            out.normal = safeNormalizeVec3((*modelNormalM) * skinned.normal);
        } else {
            const glm::vec3 skinnedPos = skinPositionAtNode(triNodeIndex, vtx, local);
            out.pos = glm::vec3(nodeGlobal * glm::vec4(skinnedPos, 1.0f));
        }
    }

    if (needTangentComputation) {
        glm::vec3 localTangent(vtx.tangent.x, vtx.tangent.y, vtx.tangent.z);
        const bool authoredTangentFrame = std::fabs(vtx.tangent.w) > 0.5f;
        if (glm::dot(localTangent, localTangent) <= 1e-10f) {
            const glm::vec3 n = safeNormalizeVec3(vtx.normal, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 helper =
                (std::fabs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                          : glm::vec3(1.0f, 0.0f, 0.0f);
            localTangent =
                safeNormalizeVec3(glm::cross(helper, n), glm::vec3(1.0f, 0.0f, 0.0f));
        }

        const glm::vec3 skinnedTangent = skinDirectionAtNode(triNodeIndex, vtx, localTangent);
        const glm::vec3 tangent =
            safeNormalizeVec3((*modelNormalM) * skinnedTangent, glm::vec3(1.0f, 0.0f, 0.0f));
        const float sign =
            authoredTangentFrame ? ((vtx.tangent.w < 0.0f) ? -1.0f : 1.0f) : 0.0f;
        out.tangent = glm::vec4(tangent, sign);
    }

    if (vertexIndex < g_scratch.worldVertexPosCache.size() && needPosComputation) {
        g_scratch.worldVertexPosCache[vertexIndex] = out.pos;
        g_scratch.worldVertexPosCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.worldVertexPosCacheStamp[vertexIndex] = stamp;
    }
    if (vertexIndex < g_scratch.modelVertexNormalCache.size() && needNormalComputation) {
        g_scratch.modelVertexNormalCache[vertexIndex] = out.normal;
        g_scratch.modelVertexNormalCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.modelVertexNormalCacheStamp[vertexIndex] = stamp;
    }
    if (vertexIndex < g_scratch.modelVertexTangentCache.size() && needTangentComputation) {
        g_scratch.modelVertexTangentCache[vertexIndex] = out.tangent;
        g_scratch.modelVertexTangentCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.modelVertexTangentCacheStamp[vertexIndex] = stamp;
    }

    return out;
}

glm::vec4 Resolver::resolveModelVertexTangent(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.modelVertexTangentCache.size() &&
        g_scratch.modelVertexTangentCacheStamp[vertexIndex] == stamp &&
        g_scratch.modelVertexTangentCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.modelVertexTangentCache[vertexIndex];
    }

    glm::vec3 localTangent(vtx.tangent.x, vtx.tangent.y, vtx.tangent.z);
    const bool authoredTangentFrame = std::fabs(vtx.tangent.w) > 0.5f;
    if (glm::dot(localTangent, localTangent) <= 1e-10f) {
        glm::vec3 n = safeNormalizeVec3(vtx.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 helper =
            (std::fabs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        localTangent = safeNormalizeVec3(glm::cross(helper, n), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    const glm::vec3 skinnedTangent = skinDirectionAtNode(triNodeIndex, vtx, localTangent);
    const glm::vec3 tangent = safeNormalizeVec3(
        modelNormalMatrixForNode(triNodeIndex) * skinnedTangent,
        glm::vec3(1.0f, 0.0f, 0.0f));
    const float sign = authoredTangentFrame ? ((vtx.tangent.w < 0.0f) ? -1.0f : 1.0f) : 0.0f;
    const glm::vec4 outTangent(tangent, sign);
    if (vertexIndex < g_scratch.modelVertexTangentCache.size()) {
        g_scratch.modelVertexTangentCache[vertexIndex] = outTangent;
        g_scratch.modelVertexTangentCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.modelVertexTangentCacheStamp[vertexIndex] = stamp;
    }
    return outTangent;
}

glm::vec3 Resolver::resolveGpuSkinningInputPos(
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    const std::uint32_t stamp = g_scratch.vertexCacheStamp;
    if (vertexIndex < g_scratch.worldVertexPosCache.size() &&
        g_scratch.worldVertexPosCacheStamp[vertexIndex] == stamp &&
        g_scratch.worldVertexPosCacheNode[vertexIndex] == std::numeric_limits<int>::min()) {
        return g_scratch.worldVertexPosCache[vertexIndex];
    }

    // GPU skinning path expects local (pre-skin) positions.
    const glm::vec3 outPos = vtx.position;

    if (vertexIndex < g_scratch.worldVertexPosCache.size()) {
        g_scratch.worldVertexPosCache[vertexIndex] = outPos;
        g_scratch.worldVertexPosCacheNode[vertexIndex] = std::numeric_limits<int>::min();
        g_scratch.worldVertexPosCacheStamp[vertexIndex] = stamp;
    }
    return outPos;
}

glm::vec3 Resolver::resolveDeformedLocalVertexPos(
    std::uint32_t vertexIndex,
    const MeshVertex& vtx) {
    return resolveLocalVertexPos(vertexIndex, vtx);
}

int Resolver::gpuSkinningCacheKeyForNode(int triNodeIndex) const {
    if (!mesh_ || triNodeIndex < 0) return -1;
    const std::size_t nodeIdx = static_cast<std::size_t>(triNodeIndex);
    if (nodeIdx >= mesh_->nodeSkin.size()) return -1;
    const int skinIndex = mesh_->nodeSkin[nodeIdx];
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh_->skins.size()) return -1;
    return skinIndex;
}

bool Resolver::configureGpuClipSkinningBatch(
    int triNodeIndex,
    const std::vector<std::uint16_t>* jointPalette,
    std::array<float, 16>& inOutModelMatrix,
    std::uint8_t& outSkinningMode,
    std::vector<float>& outSkinMatrices,
    std::uint32_t& outSkinMatrixCount) {
    outSkinMatrices.clear();
    outSkinningMode = 0u;
    outSkinMatrixCount = 0u;
    if (!gpuClipSkinningRequested_ || !clipSkinningEnabled_ ||
        (!hasClipPose_ && !gpuBindPoseSkinningRequested_) ||
        !usePositionOnlyVertexPath_) {
        return false;
    }

    const int skinIndex = gpuSkinningCacheKeyForNode(triNodeIndex);
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh_->skins.size()) {
        return false;
    }
    const auto& skin = mesh_->skins[static_cast<std::size_t>(skinIndex)];
    if (skin.joints.empty()) return false;
    const bool hasPalette = jointPalette && !jointPalette->empty();
    if (hasPalette) {
        if (jointPalette->size() > kMaxGpuSkinMatrices) return false;
    } else if (skin.joints.size() > kMaxGpuSkinMatrices) {
        return false;
    }

    const std::size_t skinIdx = static_cast<std::size_t>(skinIndex);
    if (g_scratch.gpuSkinMatricesReady[skinIdx] == 0u) {
        auto& packed = g_scratch.gpuSkinMatricesBySkin[skinIdx];
        if (backendGpuSkinNodeGlobalsEnabled_) {
            packed.resize(skin.joints.size() * 32u);
            const std::size_t inverseBindOffset = skin.joints.size() * 16u;
            for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                glm::mat4 jointGlobal(1.0f);
                const int jointNode = skin.joints[j];
                if (jointNode >= 0 && static_cast<std::size_t>(jointNode) < nodeGlobals_->size()) {
                    jointGlobal = (*nodeGlobals_)[static_cast<std::size_t>(jointNode)];
                }
                const glm::mat4 invBind =
                    (j < skin.inverseBind.size()) ? skin.inverseBind[j] : glm::mat4(1.0f);
                const float* jointGlobalSrc = glm::value_ptr(jointGlobal);
                const float* invBindSrc = glm::value_ptr(invBind);
                std::copy(jointGlobalSrc, jointGlobalSrc + 16, packed.data() + (j * 16u));
                std::copy(
                    invBindSrc,
                    invBindSrc + 16,
                    packed.data() + inverseBindOffset + (j * 16u));
            }
        } else {
            packed.resize(skin.joints.size() * 16u);
            for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                const int jointNode = skin.joints[j];
                glm::mat4 jointM(1.0f);
                if (jointNode >= 0 && static_cast<std::size_t>(jointNode) < nodeGlobals_->size()) {
                    const glm::mat4 invBind =
                        (j < skin.inverseBind.size()) ? skin.inverseBind[j] : glm::mat4(1.0f);
                    jointM = (*nodeGlobals_)[static_cast<std::size_t>(jointNode)] * invBind;
                }
                const float* src = glm::value_ptr(jointM);
                std::copy(src, src + 16, packed.data() + (j * 16u));
            }
        }
        g_scratch.gpuSkinMatricesReady[skinIdx] = 1u;
    }

    const float* modelData = glm::value_ptr(modelM_);
    std::copy(modelData, modelData + 16, inOutModelMatrix.begin());

    const auto& packedAll = g_scratch.gpuSkinMatricesBySkin[skinIdx];
    if (backendGpuSkinNodeGlobalsEnabled_) {
        outSkinningMode = 1u;
        if (hasPalette) {
            const std::size_t paletteSize = jointPalette->size();
            const std::size_t srcInverseBindOffset = skin.joints.size() * 16u;
            const std::size_t dstInverseBindOffset = paletteSize * 16u;
            outSkinMatrices.resize(paletteSize * 32u);
            for (std::size_t pi = 0; pi < paletteSize; ++pi) {
                const std::size_t srcJoint = static_cast<std::size_t>((*jointPalette)[pi]);
                if (srcJoint >= skin.joints.size()) return false;
                const float* jointGlobalSrc = packedAll.data() + (srcJoint * 16u);
                const float* invBindSrc = packedAll.data() + srcInverseBindOffset + (srcJoint * 16u);
                std::copy(
                    jointGlobalSrc,
                    jointGlobalSrc + 16u,
                    outSkinMatrices.data() + (pi * 16u));
                std::copy(
                    invBindSrc,
                    invBindSrc + 16u,
                    outSkinMatrices.data() + dstInverseBindOffset + (pi * 16u));
            }
            outSkinMatrixCount = static_cast<std::uint32_t>(paletteSize);
        } else {
            outSkinMatrices = packedAll;
            outSkinMatrixCount = static_cast<std::uint32_t>(skin.joints.size());
        }
    } else if (hasPalette) {
        outSkinMatrices.resize(jointPalette->size() * 16u);
        for (std::size_t pi = 0; pi < jointPalette->size(); ++pi) {
            const std::size_t srcJoint = static_cast<std::size_t>((*jointPalette)[pi]);
            if (srcJoint >= skin.joints.size()) return false;
            const float* src = packedAll.data() + (srcJoint * 16u);
            std::copy(src, src + 16u, outSkinMatrices.data() + (pi * 16u));
        }
        outSkinMatrixCount = static_cast<std::uint32_t>(jointPalette->size());
    } else {
        outSkinMatrices = packedAll;
        outSkinMatrixCount = static_cast<std::uint32_t>(skin.joints.size());
    }
    return true;
}

} // namespace engine::models::transforms
