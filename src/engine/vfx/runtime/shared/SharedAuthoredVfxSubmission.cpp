#include "engine/vfx/runtime/shared/SharedAuthoredVfxSubmission.h"

#include <algorithm>

namespace vfx::runtime::authored_submit {
namespace {

using Batch = authored_batches::WorldIndexedBatch;

IRenderBackend::WorldTextureData toWorldTextureData(const Batch& batch,
                                                    const float* cameraWorldPos3,
                                                    const float* cameraForward3,
                                                    const float* cameraTarget3) {
    IRenderBackend::WorldTextureData tex{};
    tex.key = batch.textureKey.empty() ? nullptr : batch.textureKey.c_str();
    tex.cacheKey = batch.textureCacheKey.empty() ? nullptr : batch.textureCacheKey.c_str();
    tex.rgba = batch.textureRgba;
    tex.width = batch.textureWidth;
    tex.height = batch.textureHeight;
    tex.wrapS = batch.textureWrapS;
    tex.wrapT = batch.textureWrapT;
    tex.alphaMode = batch.alphaMode;
    tex.blendMode = batch.blendMode;
    tex.dualSourceBlendEnabled = batch.dualSourceBlendEnabled;
    tex.depthTestEnabled = batch.depthTestEnabled;
    tex.clipSpaceDepthBias = batch.clipSpaceDepthBias;
    tex.alphaCutoff = batch.alphaCutoff;
    tex.alphaWindowMin = batch.alphaWindowMin;
    tex.alphaWindowMax = batch.alphaWindowMax;
    tex.vertexColorMulR = batch.vertexColorMulR;
    tex.vertexColorMulG = batch.vertexColorMulG;
    tex.vertexColorMulB = batch.vertexColorMulB;
    tex.vertexColorMulA = batch.vertexColorMulA;
    tex.characterInkingEnabled = batch.characterInkingEnabled;
    tex.modelMatrix = batch.modelMatrix;
    if (cameraWorldPos3) {
        tex.cameraPosX = cameraWorldPos3[0];
        tex.cameraPosY = cameraWorldPos3[1];
        tex.cameraPosZ = cameraWorldPos3[2];
    }
    if (cameraForward3) {
        tex.cameraForwardX = cameraForward3[0];
        tex.cameraForwardY = cameraForward3[1];
        tex.cameraForwardZ = cameraForward3[2];
    }
    if (cameraTarget3) {
        tex.cameraTargetX = cameraTarget3[0];
        tex.cameraTargetY = cameraTarget3[1];
        tex.cameraTargetZ = cameraTarget3[2];
    }
    return tex;
}

void drawOneBatch(IRenderBackend& renderer,
                  const Batch& batch,
                  const float* viewProjectionMatrix4x4,
                  int surfaceWidth,
                  int surfaceHeight,
                  const float* cameraWorldPos3,
                  const float* cameraForward3,
                  const float* cameraTarget3) {
    const bool useSharedVertices = batch.sharedVertices && batch.sharedVertexCount > 0u;
    const bool useSharedIndices = batch.sharedIndices && batch.sharedIndexCount > 0u;
    const IRenderBackend::WorldMeshVertex* vertices = useSharedVertices
        ? batch.sharedVertices
        : batch.vertices.data();
    const std::size_t vertexCount = useSharedVertices
        ? batch.sharedVertexCount
        : batch.vertices.size();
    const std::uint32_t* indices = useSharedIndices
        ? batch.sharedIndices
        : batch.indices.data();
    const std::size_t indexCount = useSharedIndices
        ? batch.sharedIndexCount
        : batch.indices.size();
    if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) return;

    IRenderBackend::WorldTextureData tex =
        toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
    const char* geometryKey = batch.geometryCacheKey.empty() ? nullptr : batch.geometryCacheKey.c_str();
    if (!batch.instances.empty()) {
        renderer.drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            batch.instances.data(),
            batch.instances.size(),
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else if (geometryKey) {
        renderer.drawWorldIndexedMeshTexturedCached(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else {
        renderer.drawWorldIndexedMeshTextured(
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    }
}

} // namespace

std::size_t prewarmBatches(IRenderBackend& renderer,
                           const std::vector<authored_batches::WorldIndexedBatch>& batches,
                           const float* cameraWorldPos3,
                           const float* cameraForward3,
                           const float* cameraTarget3) {
    std::size_t warmed = 0u;
    for (const Batch& batch : batches) {
        if (!batch.hasGeometry()) continue;

        const bool useSharedVertices = batch.sharedVertices && batch.sharedVertexCount > 0u;
        const bool useSharedIndices = batch.sharedIndices && batch.sharedIndexCount > 0u;
        const IRenderBackend::WorldMeshVertex* vertices = useSharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = useSharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const std::uint32_t* indices = useSharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const std::size_t indexCount = useSharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) continue;

        IRenderBackend::WorldTextureData tex =
            toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
        renderer.prewarmWorldTextureData(&tex);
        if (!batch.geometryCacheKey.empty()) {
            renderer.prewarmWorldIndexedMeshCached(
                batch.geometryCacheKey.c_str(),
                vertices,
                vertexCount,
                indices,
                indexCount);
        }
        if (!batch.instances.empty()) {
            renderer.prewarmWorldIndexedMeshInstances(batch.instances.size());
        }
        ++warmed;
    }
    return warmed;
}

void submitBatches(IRenderBackend& renderer,
                   const std::vector<authored_batches::WorldIndexedBatch>& batches,
                   const float* viewProjectionMatrix4x4,
                   int surfaceWidth,
                   int surfaceHeight,
                   const float* cameraWorldPos3,
                   const float* cameraForward3,
                   const float* cameraTarget3) {
    if (batches.empty() || !viewProjectionMatrix4x4 || surfaceWidth <= 0 || surfaceHeight <= 0) {
        return;
    }

    static thread_local std::vector<const Batch*> opaqueBatches;
    static thread_local std::vector<const Batch*> blendBatches;
    opaqueBatches.clear();
    blendBatches.clear();
    opaqueBatches.reserve(batches.size());
    blendBatches.reserve(batches.size());

    for (const Batch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (batch.alphaMode == 2u) {
            blendBatches.push_back(&batch);
        } else {
            opaqueBatches.push_back(&batch);
        }
    }

    if (blendBatches.size() > 1u) {
        std::stable_sort(
            blendBatches.begin(),
            blendBatches.end(),
            [](const Batch* lhs, const Batch* rhs) { return lhs->sortDepth > rhs->sortDepth; });
    }

    IRenderBackend::WorldIndexedSubmissionStats stats{};
    renderer.beginWorldIndexedBatchSubmission();
    for (const Batch* batch : opaqueBatches) {
        if (!batch) continue;
        ++stats.opaqueDraws;
        if (!batch->geometryCacheKey.empty()) {
            ++stats.cachedDraws;
        } else {
            ++stats.dynamicDraws;
        }
        if (!batch->instances.empty()) ++stats.instancedDraws;
        drawOneBatch(
            renderer,
            *batch,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight,
            cameraWorldPos3,
            cameraForward3,
            cameraTarget3);
    }
    for (const Batch* batch : blendBatches) {
        if (!batch) continue;
        ++stats.blendDraws;
        if (!batch->geometryCacheKey.empty()) {
            ++stats.cachedDraws;
        } else {
            ++stats.dynamicDraws;
        }
        if (!batch->instances.empty()) ++stats.instancedDraws;
        drawOneBatch(
            renderer,
            *batch,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight,
            cameraWorldPos3,
            cameraForward3,
            cameraTarget3);
    }
    renderer.recordWorldIndexedSubmissionStats(stats);
    renderer.endWorldIndexedBatchSubmission();
}

} // namespace vfx::runtime::authored_submit
