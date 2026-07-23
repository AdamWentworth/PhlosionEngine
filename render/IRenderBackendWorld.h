#pragma once

#include "engine/render/RenderBackendTypes.h"

class IRenderBackendWorld {
public:
    using WorldIndexedSubmissionStats = engine::render::backend::WorldIndexedSubmissionStats;
    using WorldMeshVertex = engine::render::backend::WorldMeshVertex;
    using WorldTextureData = engine::render::backend::WorldTextureData;
    using WorldMeshInstance = engine::render::backend::WorldMeshInstance;
    template <typename Tag>
    using WorldSceneHandle = engine::render::backend::WorldSceneHandle<Tag>;
    using WorldSceneGeometryHandle = engine::render::backend::WorldSceneGeometryHandle;
    using WorldSceneMaterialHandle = engine::render::backend::WorldSceneMaterialHandle;
    using WorldSceneSkeletonLayoutHandle =
        engine::render::backend::WorldSceneSkeletonLayoutHandle;
    using WorldSceneAnimationClipHandle =
        engine::render::backend::WorldSceneAnimationClipHandle;
    using WorldSceneRenderObjectHandle =
        engine::render::backend::WorldSceneRenderObjectHandle;
    using WorldSceneRenderInstanceHandle =
        engine::render::backend::WorldSceneRenderInstanceHandle;
    using WorldSceneFastPathCaps = engine::render::backend::WorldSceneFastPathCaps;
    using WorldSceneGeometry = engine::render::backend::WorldSceneGeometry;
    using WorldSceneMaterial = engine::render::backend::WorldSceneMaterial;
    using WorldSceneSkeletonLayout = engine::render::backend::WorldSceneSkeletonLayout;
    using WorldSceneAnimationClip = engine::render::backend::WorldSceneAnimationClip;
    using WorldSceneRenderObject = engine::render::backend::WorldSceneRenderObject;
    using WorldSceneInstance = engine::render::backend::WorldSceneInstance;
    using WorldSceneDrawClass = engine::render::backend::WorldSceneDrawClass;
    using WorldSceneFrame = engine::render::backend::WorldSceneFrame;
    using WorldSceneView = engine::render::backend::WorldSceneView;
    using WorldTriangle = engine::render::backend::WorldTriangle;

    virtual ~IRenderBackendWorld() = default;

    virtual bool supportsWorldTriangles3D() const { return false; }
    virtual bool supportsWorldIndexedMeshes() const { return false; }
    virtual bool supportsWorldIndexedMeshInstancing() const { return false; }
    virtual bool supportsWorldSceneFastPath() const { return false; }
    virtual bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps& outCaps) const {
        outCaps = WorldSceneFastPathCaps{};
        return false;
    }
    virtual void beginWorldIndexedBatchSubmission() {}
    virtual void endWorldIndexedBatchSubmission() {}
    virtual void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats& stats) {
        (void)stats;
    }
    virtual void submitWorldScene(const WorldSceneFrame& frame,
                                  const WorldSceneView& view) {
        (void)frame;
        (void)view;
    }
    virtual void drawWorldTriangles(const WorldTriangle* triangles,
                                    std::size_t triangleCount,
                                    const float* viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) {
        (void)triangles;
        (void)triangleCount;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight) {
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawWorldIndexedMeshCached(const char* geometryKey,
                                            const WorldMeshVertex* vertices,
                                            std::size_t vertexCount,
                                            const std::uint32_t* indices,
                                            std::size_t indexCount,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) {
        (void)geometryKey;
        drawWorldIndexedMesh(
            vertices,
            vertexCount,
            indices,
            indexCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    }
    virtual void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                               const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount) {
        (void)geometryKey;
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
    }
    virtual void prewarmWorldIndexedMeshInstances(std::size_t instanceCount) {
        (void)instanceCount;
    }
    virtual void prewarmWorldTextureData(const WorldTextureData* texture) {
        (void)texture;
    }
    virtual void prewarmWorldRenderAssets() {}
    virtual void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                              std::size_t vertexCount,
                                              const std::uint32_t* indices,
                                              std::size_t indexCount,
                                              const WorldTextureData* texture,
                                              const float* viewProjectionMatrix4x4,
                                              int surfaceWidth,
                                              int surfaceHeight) {
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
        (void)texture;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                                    const WorldMeshVertex* vertices,
                                                    std::size_t vertexCount,
                                                    const std::uint32_t* indices,
                                                    std::size_t indexCount,
                                                    const WorldTextureData* texture,
                                                    const float* viewProjectionMatrix4x4,
                                                    int surfaceWidth,
                                                    int surfaceHeight) {
        (void)geometryKey;
        drawWorldIndexedMeshTextured(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    }
    virtual void drawWorldIndexedMeshTexturedCachedInstanced(
        const char* geometryKey,
        const WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        const WorldTextureData* texture,
        const WorldMeshInstance* instances,
        std::size_t instanceCount,
        const float* viewProjectionMatrix4x4,
        int surfaceWidth,
        int surfaceHeight) {
        if (!instances || instanceCount == 0u) {
            drawWorldIndexedMeshTexturedCached(
                geometryKey,
                vertices,
                vertexCount,
                indices,
                indexCount,
                texture,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
            return;
        }

        const auto mulColumnMajor = [](const std::array<float, 16>& lhs,
                                       const std::array<float, 16>& rhs) {
            std::array<float, 16> out{};
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        sum += lhs[static_cast<std::size_t>(k) * 4u +
                                   static_cast<std::size_t>(row)] *
                               rhs[static_cast<std::size_t>(col) * 4u +
                                   static_cast<std::size_t>(k)];
                    }
                    out[static_cast<std::size_t>(col) * 4u +
                        static_cast<std::size_t>(row)] = sum;
                }
            }
            return out;
        };

        static const std::array<float, 16> kIdentity{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};

        for (std::size_t i = 0; i < instanceCount; ++i) {
            WorldTextureData instancedTexture = texture ? *texture : WorldTextureData{};
            const WorldMeshInstance& instance = instances[i];
            instancedTexture.modelMatrix = texture
                ? mulColumnMajor(texture->modelMatrix, instance.modelMatrix)
                : instance.modelMatrix;
            instancedTexture.vertexColorMulR =
                (texture ? texture->vertexColorMulR : 1.0f) * instance.vertexColorMulR;
            instancedTexture.vertexColorMulG =
                (texture ? texture->vertexColorMulG : 1.0f) * instance.vertexColorMulG;
            instancedTexture.vertexColorMulB =
                (texture ? texture->vertexColorMulB : 1.0f) * instance.vertexColorMulB;
            instancedTexture.vertexColorMulA =
                (texture ? texture->vertexColorMulA : 1.0f) * instance.vertexColorMulA;
            if (instance.gpuSkinning != 0u &&
                instance.skinMatrices != nullptr &&
                instance.skinMatrixCount > 0u) {
                instancedTexture.gpuSkinning = 1u;
                instancedTexture.gpuSkinningMode = instance.gpuSkinningMode;
                instancedTexture.skinMatrixCount = instance.skinMatrixCount;
                instancedTexture.skinMatrices = instance.skinMatrices;
            }
            if (!texture) {
                instancedTexture.modelMatrix = mulColumnMajor(kIdentity, instance.modelMatrix);
            }
            drawWorldIndexedMeshTexturedCached(
                geometryKey,
                vertices,
                vertexCount,
                indices,
                indexCount,
                texture ? &instancedTexture : nullptr,
                viewProjectionMatrix4x4,
                surfaceWidth,
                surfaceHeight);
        }
    }
};
