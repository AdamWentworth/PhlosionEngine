#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

void D3D12RenderBackend::drawDebugQuads(const DebugQuad* quads,
                                        std::size_t quadCount,
                                        int surfaceWidth,
                                        int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !quads || quadCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (quadCount > kMaxDebugQuads) ? kMaxDebugQuads : quadCount;
    const std::size_t vertexCount = safeCount * 6;
    const std::size_t neededBytes = vertexCount * sizeof(DebugVertex);
    const std::size_t debugFrameEnd =
        static_cast<std::size_t>(debugVertexFrameBaseOffset_) +
        static_cast<std::size_t>(debugVertexBufferBytesPerFrame_);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugFrameEnd) return;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(vertexCount);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendQuadAsTriangles(quads[i], verts);
    }
    if (verts.empty()) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
    }
    const std::size_t clampedVertexCount = verts.size();

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
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(clampedVertexCount * sizeof(DebugVertex));
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(clampedVertexCount), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(clampedVertexCount / 3u);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + clampedVertexCount * sizeof(DebugVertex));
#else
    (void)quads;
    (void)quadCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugQuadsCached(const char* cacheKey,
                                              const DebugQuad* quads,
                                              std::size_t quadCount,
                                              int surfaceWidth,
                                              int surfaceHeight) {
#if defined(_WIN32)
    if (!cacheKey || cacheKey[0] == '\0') {
        drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
        return;
    }
    if (!recording_ || !quads || quadCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !commandList_ || !device_) return;

    const std::size_t safeCount = (quadCount > kMaxDebugQuads) ? kMaxDebugQuads : quadCount;
    if (safeCount == 0u) return;

    const std::string key(cacheKey);
    auto cacheIt = cachedDebugQuads_.find(key);
    if (cacheIt == cachedDebugQuads_.end()) {
        static thread_local std::vector<DebugVertex> verts;
        verts.clear();
        verts.reserve(safeCount * 6u);
        for (std::size_t i = 0; i < safeCount; ++i) {
            engine::render::debug::appendQuadAsTriangles(quads[i], verts);
        }
        if (verts.empty()) return;

        const float invW = 1.0f / static_cast<float>(surfaceWidth);
        const float invH = 1.0f / static_cast<float>(surfaceHeight);
        for (DebugVertex& v : verts) {
            v.x = v.x * invW * 2.0f - 1.0f;
            v.y = 1.0f - v.y * invH * 2.0f;
        }

        const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = static_cast<UINT64>(neededBytes);
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        CachedDebugGeometry cached{};
        if (FAILED(device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(cached.vertexBuffer.ReleaseAndGetAddressOf()))) ||
            !cached.vertexBuffer) {
            drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
            return;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(cached.vertexBuffer->Map(0, &readRange, &mapped)) || !mapped) {
            drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
            return;
        }
        std::memcpy(mapped, verts.data(), neededBytes);
        const D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(neededBytes)};
        cached.vertexBuffer->Unmap(0, &writtenRange);

        cached.gpuAddress = cached.vertexBuffer->GetGPUVirtualAddress();
        cached.vertexCount = verts.size();
        cached.vertexBytes = neededBytes;
        cached.valid = true;
        cacheIt = cachedDebugQuads_.emplace(key, std::move(cached)).first;
    }

    const CachedDebugGeometry& cached = cacheIt->second;
    if (!cached.valid || !cached.vertexBuffer || cached.vertexCount == 0u) {
        drawDebugQuads(quads, quadCount, surfaceWidth, surfaceHeight);
        return;
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
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = cached.gpuAddress;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(cached.vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(cached.vertexCount), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(cached.vertexCount / 3u);
#else
    (void)cacheKey;
    (void)quads;
    (void)quadCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugLines(const DebugLine* lines,
                                        std::size_t lineCount,
                                        int surfaceWidth,
                                        int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !lines || lineCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (lineCount > kMaxDebugLines) ? kMaxDebugLines : lineCount;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(safeCount * 6);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendLineAsTriangles(lines[i], verts);
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    const std::size_t debugFrameEnd =
        static_cast<std::size_t>(debugVertexFrameBaseOffset_) +
        static_cast<std::size_t>(debugVertexBufferBytesPerFrame_);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugFrameEnd) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
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
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(verts.size() / 3u);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)lines;
    (void)lineCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugLinesCached(const char* cacheKey,
                                              const DebugLine* lines,
                                              std::size_t lineCount,
                                              int surfaceWidth,
                                              int surfaceHeight) {
#if defined(_WIN32)
    if (!cacheKey || cacheKey[0] == '\0') {
        drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
        return;
    }
    if (!recording_ || !lines || lineCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !commandList_ || !device_) return;

    const std::size_t safeCount = (lineCount > kMaxDebugLines) ? kMaxDebugLines : lineCount;
    if (safeCount == 0u) return;

    const std::string key(cacheKey);
    auto cacheIt = cachedDebugLines_.find(key);
    if (cacheIt == cachedDebugLines_.end()) {
        static thread_local std::vector<DebugVertex> verts;
        verts.clear();
        verts.reserve(safeCount * 6u);
        for (std::size_t i = 0; i < safeCount; ++i) {
            engine::render::debug::appendLineAsTriangles(lines[i], verts);
        }
        if (verts.empty()) return;

        const float invW = 1.0f / static_cast<float>(surfaceWidth);
        const float invH = 1.0f / static_cast<float>(surfaceHeight);
        for (DebugVertex& v : verts) {
            v.x = v.x * invW * 2.0f - 1.0f;
            v.y = 1.0f - v.y * invH * 2.0f;
        }

        const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = static_cast<UINT64>(neededBytes);
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        CachedDebugGeometry cached{};
        if (FAILED(device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(cached.vertexBuffer.ReleaseAndGetAddressOf()))) ||
            !cached.vertexBuffer) {
            drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
            return;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(cached.vertexBuffer->Map(0, &readRange, &mapped)) || !mapped) {
            drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
            return;
        }
        std::memcpy(mapped, verts.data(), neededBytes);
        const D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(neededBytes)};
        cached.vertexBuffer->Unmap(0, &writtenRange);

        cached.gpuAddress = cached.vertexBuffer->GetGPUVirtualAddress();
        cached.vertexCount = verts.size();
        cached.vertexBytes = neededBytes;
        cached.valid = true;
        cacheIt = cachedDebugLines_.emplace(key, std::move(cached)).first;
    }

    const CachedDebugGeometry& cached = cacheIt->second;
    if (!cached.valid || !cached.vertexBuffer || cached.vertexCount == 0u) {
        drawDebugLines(lines, lineCount, surfaceWidth, surfaceHeight);
        return;
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
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = cached.gpuAddress;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(cached.vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(cached.vertexCount), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(cached.vertexCount / 3u);
#else
    (void)cacheKey;
    (void)lines;
    (void)lineCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugTriangles(const DebugTriangle* triangles,
                                            std::size_t triangleCount,
                                            int surfaceWidth,
                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !triangles || triangleCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (triangleCount > kMaxDebugTriangles) ? kMaxDebugTriangles : triangleCount;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(safeCount * 3);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugTriangle& t = triangles[i];
        verts.push_back(DebugVertex{t.x1, t.y1, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x2, t.y2, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    const std::size_t debugFrameEnd =
        static_cast<std::size_t>(debugVertexFrameBaseOffset_) +
        static_cast<std::size_t>(debugVertexBufferBytesPerFrame_);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugFrameEnd) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
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
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(verts.size() / 3u);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)triangles;
    (void)triangleCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugSprites(const DebugSprite* sprites,
                                          std::size_t spriteCount,
                                          int surfaceWidth,
                                          int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !sprites || spriteCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!spritePipelineState_ || !spriteRootSignature_ || !spriteVertexBuffer_ || !commandList_ || !srvHeap_) return;
    if (!spriteVertexMappedData_) return;

    const std::size_t safeCount = (spriteCount > kMaxSpriteQuads) ? kMaxSpriteQuads : spriteCount;

    struct SpriteDrawRun {
        std::uint32_t descriptorIndex = 0;
        std::uint32_t firstInstance = 0;
        std::uint32_t instanceCount = 0;
    };

    static thread_local std::vector<SpriteInstanceData> instances;
    instances.clear();
    instances.reserve(safeCount);
    static thread_local std::vector<SpriteDrawRun> runs;
    runs.clear();
    runs.reserve(safeCount);

    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugSprite& sprite = sprites[i];
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) continue;

        SpriteTexture* texture = ensureSpriteTexture(sprite.texturePath);
        if (!texture || !texture->valid) continue;

        const std::uint32_t instanceIndex = static_cast<std::uint32_t>(instances.size());
        instances.push_back(
            {sprite.x, sprite.y, sprite.w, sprite.h, sprite.u0, sprite.v0, sprite.u1, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a});

        if (!runs.empty() && runs.back().descriptorIndex == texture->descriptorIndex) {
            ++runs.back().instanceCount;
        } else {
            runs.push_back({texture->descriptorIndex, instanceIndex, 1u});
        }
    }
    if (instances.empty() || runs.empty()) return;

    const std::size_t neededBytes = instances.size() * sizeof(SpriteInstanceData);
    const std::size_t spriteFrameEnd =
        static_cast<std::size_t>(spriteVertexFrameBaseOffset_) +
        static_cast<std::size_t>(spriteVertexBufferBytesPerFrame_);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(spriteVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > spriteFrameEnd) return;

    std::memcpy(spriteVertexMappedData_ + writeOffset, instances.data(), neededBytes);

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
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(spriteRootSignature_.Get());
    const float surfaceSize[2] = {
        static_cast<float>(surfaceWidth),
        static_cast<float>(surfaceHeight)
    };
    commandList_->SetGraphicsRoot32BitConstants(0, 2, surfaceSize, 0);
    commandList_->SetPipelineState(spritePipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = spriteVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = spriteVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_GPU_DESCRIPTOR_HANDLE baseSrv = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    for (const SpriteDrawRun& run : runs) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = baseSrv;
        srvHandle.ptr += static_cast<SIZE_T>(run.descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
        commandList_->SetGraphicsRootDescriptorTable(1, srvHandle);
        commandList_->DrawInstanced(6u, run.instanceCount, 0u, run.firstInstance);
    }
    frameDrawCalls_ += static_cast<std::uint32_t>(runs.size());
    frameTriangles_ += static_cast<std::uint64_t>(instances.size() * 2u);
    spriteVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)sprites;
    (void)spriteCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}
