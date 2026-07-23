#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

namespace {

void packWorldVsConstants(const float* viewProjectionMatrix4x4,
                          const float* modelMatrix4x4,
                          bool skinningEnabled,
                          std::uint32_t skinMatrixCount,
                          std::uint32_t skinningMode,
                          float clipSpaceDepthBias,
                          float* out40) {
    if (!out40) return;
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* vp = viewProjectionMatrix4x4 ? viewProjectionMatrix4x4 : kIdentity;
    const float* model = modelMatrix4x4 ? modelMatrix4x4 : kIdentity;
    std::memcpy(out40, vp, sizeof(float) * 16u);
    std::memcpy(out40 + 16u, model, sizeof(float) * 16u);
    out40[32] = skinningEnabled ? 1.0f : 0.0f;
    out40[33] = static_cast<float>(skinMatrixCount);
    out40[34] = static_cast<float>(skinningMode);
    out40[35] = 0.0f;
    out40[36] = (clipSpaceDepthBias > 0.0f) ? clipSpaceDepthBias : 0.0f;
    out40[37] = 0.0f;
    out40[38] = 0.0f;
    out40[39] = 0.0f;
}

} // namespace

void D3D12RenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                            std::size_t triangleCount,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ || !worldRootSignature_ || !worldVertexBuffer_ || !commandList_ || !srvHeap_) return;
    if (!worldVertexMappedData_ || !worldVsConstantMappedData_ || !worldSkinMatrixMappedData_) return;
    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            nullptr,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }

    const std::size_t safeCount = (triangleCount > kMaxWorldTriangles) ? kMaxWorldTriangles : triangleCount;
    if (safeCount == 0) return;
    const std::size_t vertexCount = safeCount * 3;
    const std::size_t neededBytes = vertexCount * sizeof(WorldVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    const std::size_t vertexFrameEnd =
        static_cast<std::size_t>(worldVertexFrameBaseOffset_) +
        static_cast<std::size_t>(worldVertexBufferBytesPerFrame_);
    const std::size_t vsConstantsFrameEnd =
        static_cast<std::size_t>(worldVsConstantFrameBaseOffset_) +
        static_cast<std::size_t>(worldVsConstantBufferBytesPerFrame_);
    if (neededBytes == 0 || writeOffset + neededBytes > vertexFrameEnd) return;
    if (vsConstantsWriteOffset + 256u > vsConstantsFrameEnd) return;

    WorldVertex* out = reinterpret_cast<WorldVertex*>(worldVertexMappedData_ + writeOffset);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& t = triangles[i];
        const auto resolve = [](float vtx, float fallback) {
            return (vtx >= 0.0f) ? vtx : fallback;
        };
        const std::size_t base = i * 3;
        out[base + 0] = WorldVertex{
            t.x1, t.y1, t.z1,
            0.0f, 0.0f,
            resolve(t.r1, t.r),
            resolve(t.g1, t.g),
            resolve(t.b1, t.b),
            resolve(t.a1, t.a)};
        out[base + 1] = WorldVertex{
            t.x2, t.y2, t.z2,
            0.0f, 0.0f,
            resolve(t.r2, t.r),
            resolve(t.g2, t.g),
            resolve(t.b2, t.b),
            resolve(t.a2, t.a)};
        out[base + 2] = WorldVertex{
            t.x3, t.y3, t.z3,
            0.0f, 0.0f,
            resolve(t.r3, t.r),
            resolve(t.g3, t.g),
            resolve(t.b3, t.b),
            resolve(t.a3, t.a)};
    }
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    if (srvHeap_) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
        commandList_->SetDescriptorHeaps(1, heaps);
    }
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    float vsConstants[40] = {};
    packWorldVsConstants(viewProjectionMatrix4x4, nullptr, false, 0u, 0u, 0.0f, vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootShaderResourceView(
        2,
        worldSkinMatrixBufferGpuAddress_ +
            static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_));
    commandList_->SetGraphicsRootShaderResourceView(
        4,
        worldInstanceBufferGpuAddress_ +
            static_cast<std::uint64_t>(worldInstanceFrameBaseOffset_));
    const WorldPsConstants worldPs = makeWorldPsConstants(nullptr, useTexture);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    materialHandle.ptr += static_cast<SIZE_T>(materialDescriptorBlockIndex) *
                          static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, materialHandle);
    frameIndexedD3d12DescriptorTableSets_ += 1u;
    commandList_->SetPipelineState(worldPipelineState_.Get());
    ++frameIndexedD3d12PsoSets_;
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(writeOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(vertexCount / 3u);
    worldVertexFrameOffset_ = static_cast<UINT>(writeOffset + neededBytes);
    worldVsConstantFrameOffset_ = static_cast<UINT>(vsConstantsWriteOffset + 256u);
#else
    (void)triangles;
    (void)triangleCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                              std::size_t vertexCount,
                                              const std::uint32_t* indices,
                                              std::size_t indexCount,
                                              const float* viewProjectionMatrix4x4,
                                              int surfaceWidth,
                                              int surfaceHeight) {
#if defined(_WIN32)
    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            nullptr,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }
    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        nullptr,
        useTexture,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                      std::size_t vertexCount,
                                                      const std::uint32_t* indices,
                                                      std::size_t indexCount,
                                                      const WorldTextureData* texture,
                                                      const float* viewProjectionMatrix4x4,
                                                      int surfaceWidth,
                                                      int surfaceHeight) {
#if defined(_WIN32)
    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            texture,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }

    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        texture,
        useTexture,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                                            const WorldMeshVertex* vertices,
                                                            std::size_t vertexCount,
                                                            const std::uint32_t* indices,
                                                            std::size_t indexCount,
                                                            const WorldTextureData* texture,
                                                            const float* viewProjectionMatrix4x4,
                                                            int surfaceWidth,
                                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!geometryKey || geometryKey[0] == '\0') {
        drawWorldIndexedMeshTextured(
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

    CachedWorldMesh* cached =
        ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (!cached || !cached->valid) {
        drawWorldIndexedMeshTextured(
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

    std::uint32_t materialDescriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    if (!prepareWorldMaterialDescriptorBlock(
            texture,
            /*logPbrBinding=*/false,
            materialDescriptorBlockIndex,
            useTexture)) {
        return;
    }

    drawWorldIndexedMeshTexturedCachedInternal(
        *cached,
        vertices,
        vertexCount,
        indices,
        indexCount,
        materialDescriptorBlockIndex,
        texture,
        useTexture,
        viewProjectionMatrix4x4,
        worldInstanceBufferGpuAddress_ +
            static_cast<std::uint64_t>(worldInstanceFrameBaseOffset_),
        1u,
        surfaceWidth,
        surfaceHeight);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}
