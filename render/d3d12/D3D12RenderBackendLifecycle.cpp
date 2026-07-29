#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/utils/LogSink.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <SDL2/SDL.h>
#include <stb_image_write.h>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <SDL2/SDL_syswm.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

namespace {

engine::log::Sink& d3d12LifecycleLog() {
    static engine::log::Sink log("D3D12Lifecycle", &std::cout, &std::cerr);
    return log;
}

}

void D3D12RenderBackend::beginFrame(float r, float g, float b, float a) {
    ++frameCounter_;
    worldSceneColorPassActive_ = false;
    clearColor_[0] = r;
    clearColor_[1] = g;
    clearColor_[2] = b;
    clearColor_[3] = a;
    frameDrawCalls_ = 0u;
    frameTriangles_ = 0u;
    frameIndexedOpaqueDraws_ = 0u;
    frameIndexedBlendDraws_ = 0u;
    frameIndexedCachedDraws_ = 0u;
    frameIndexedDynamicDraws_ = 0u;
    frameIndexedInstancedDraws_ = 0u;
    frameIndexedOutlineBatches_ = 0u;
    frameIndexedGeometrySwitches_ = 0u;
    frameIndexedMaterialSwitches_ = 0u;
    frameIndexedTextureSwitches_ = 0u;
    frameIndexedD3d12PsoSets_ = 0u;
    frameIndexedD3d12DescriptorTableSets_ = 0u;
    frameFastSceneInstances_ = 0u;
    frameFastSceneDrawClasses_ = 0u;
    frameFastSceneVisibleSkeletons_ = 0u;
    frameFastScenePaletteUploadBytes_ = 0u;
    frameFastSceneMaterialTableBinds_ = 0u;
    frameFastSceneIndirectCommands_ = 0u;

#if defined(_WIN32)
    if (!initialized_ || !device_ || !swapChain_ || !commandList_) return;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    if (frameFenceValues_[frameIndex_] != 0u) {
        if (!waitForFenceValue(frameFenceValues_[frameIndex_])) return;
    }

    if (timestampReadbackBuffer_ && timestampFrequency_ > 0 && frameFenceValues_[frameIndex_] != 0u) {
        const std::uint32_t queryBase = frameIndex_ * kTimestampQueriesPerFrame;
        D3D12_RANGE readRange{};
        readRange.Begin = static_cast<SIZE_T>(queryBase) * sizeof(std::uint64_t);
        readRange.End = static_cast<SIZE_T>(queryBase + kTimestampQueriesPerFrame) * sizeof(std::uint64_t);

        std::uint64_t* queryData = nullptr;
        if (SUCCEEDED(timestampReadbackBuffer_->Map(
                0,
                &readRange,
                reinterpret_cast<void**>(&queryData))) &&
            queryData) {
            const std::uint64_t tsBegin = queryData[queryBase];
            const std::uint64_t tsEnd = queryData[queryBase + 1];
            const D3D12_RANGE writtenRange{0, 0};
            timestampReadbackBuffer_->Unmap(0, &writtenRange);

            if (tsEnd > tsBegin) {
                const double ticks = static_cast<double>(tsEnd - tsBegin);
                const double freq = static_cast<double>(timestampFrequency_);
                lastGpuFrameMs_ = static_cast<float>((ticks * 1000.0) / freq);
                lastGpuFrameValid_ = true;
            }
        }
    }

    auto& allocator = commandAllocators_[frameIndex_];
    if (!allocator) return;

    if (FAILED(allocator->Reset())) return;
    if (FAILED(commandList_->Reset(allocator.Get(), nullptr))) return;

    debugVertexFrameBaseOffset_ =
        static_cast<std::uint32_t>(frameSliceBase(frameIndex_, debugVertexBufferBytesPerFrame_));
    debugVertexFrameOffset_ = debugVertexFrameBaseOffset_;
    worldVertexFrameBaseOffset_ = frameIndex_ * worldVertexBufferBytesPerFrame_;
    worldVertexFrameOffset_ = worldVertexFrameBaseOffset_;
    worldIndexFrameBaseOffset_ = frameIndex_ * worldIndexBufferBytesPerFrame_;
    worldIndexFrameOffset_ = worldIndexFrameBaseOffset_;
    worldVsConstantFrameBaseOffset_ = frameIndex_ * worldVsConstantBufferBytesPerFrame_;
    worldVsConstantFrameOffset_ = worldVsConstantFrameBaseOffset_;
    worldSkinMatrixFrameBaseOffset_ = frameIndex_ * worldSkinMatrixBufferBytesPerFrame_;
    worldSkinMatrixFrameOffset_ = worldSkinMatrixFrameBaseOffset_ + 256u;
    lastWorldSkinMatrices_ = nullptr;
    lastWorldSkinningMode_ = 0u;
    lastWorldSkinMatrixCount_ = 0u;
    lastWorldSkinMatrixGpuAddress_ =
        worldSkinMatrixBufferGpuAddress_ +
        static_cast<std::uint64_t>(worldSkinMatrixFrameBaseOffset_);
    worldInstanceFrameBaseOffset_ = frameIndex_ * worldInstanceBufferBytesPerFrame_;
    worldInstanceFrameOffset_ =
        worldInstanceFrameBaseOffset_ + static_cast<UINT>(sizeof(WorldInstanceVertexData));
    spriteVertexFrameBaseOffset_ =
        static_cast<std::uint32_t>(frameSliceBase(frameIndex_, spriteVertexBufferBytesPerFrame_));
    spriteVertexFrameOffset_ = spriteVertexFrameBaseOffset_;

    if (timestampQueryHeap_) {
        const std::uint32_t queryBase = frameIndex_ * kTimestampQueriesPerFrame;
        commandList_->EndQuery(
            timestampQueryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryBase);
    }

    D3D12_RESOURCE_BARRIER toRtv{};
    toRtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRtv.Transition.pResource = renderTargets_[frameIndex_].Get();
    toRtv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRtv.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRtv.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &toRtv);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * static_cast<SIZE_T>(rtvDescriptorSize_);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    if (dsvHeap_) {
        dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * static_cast<SIZE_T>(dsvDescriptorSize_);
    }

    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHeap_ ? &dsvHandle : nullptr);
    commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
    if (dsvHeap_) {
        commandList_->ClearDepthStencilView(
            dsvHandle,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);
    }

    recording_ = true;
#else
    (void)r;
    (void)g;
    (void)b;
    (void)a;
#endif
}

void D3D12RenderBackend::endFrame() {
#if defined(_WIN32)
    if (!initialized_ || !recording_ || !commandList_ || !swapChain_ || !commandQueue_) return;
    if (worldSceneColorPassActive_) {
        endWorldSceneColorPass();
    }

    lastPresentWaitMs_ = 0.0f;

    const bool captureThisFrame =
        screenshotCaptureConfigured_ &&
        !screenshotCaptured_ &&
        frameCounter_ >= screenshotFrameTarget_;
    Microsoft::WRL::ComPtr<ID3D12Resource> screenshotReadbackBuffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT screenshotFootprint{};
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    UINT64 screenshotTotalBytes = 0;

    if (captureThisFrame && renderTargets_[frameIndex_]) {
        const D3D12_RESOURCE_DESC backbufferDesc = renderTargets_[frameIndex_]->GetDesc();
        screenshotWidth = static_cast<int>(backbufferDesc.Width);
        screenshotHeight = static_cast<int>(backbufferDesc.Height);

        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        device_->GetCopyableFootprints(
            &backbufferDesc,
            0,
            1,
            0,
            &screenshotFootprint,
            &numRows,
            &rowSizeInBytes,
            &screenshotTotalBytes);

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        readbackHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        readbackHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        readbackHeap.CreationNodeMask = 1;
        readbackHeap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Alignment = 0;
        readbackDesc.Width = screenshotTotalBytes;
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.SampleDesc.Quality = 0;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (SUCCEEDED(device_->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(screenshotReadbackBuffer.ReleaseAndGetAddressOf()))) &&
            screenshotReadbackBuffer) {
            D3D12_RESOURCE_BARRIER toCopySrc{};
            toCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopySrc.Transition.pResource = renderTargets_[frameIndex_].Get();
            toCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            toCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            toCopySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            commandList_->ResourceBarrier(1, &toCopySrc);

            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource = renderTargets_[frameIndex_].Get();
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLoc.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource = screenshotReadbackBuffer.Get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dstLoc.PlacedFootprint = screenshotFootprint;

            commandList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

            D3D12_RESOURCE_BARRIER backToRtv{};
            backToRtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            backToRtv.Transition.pResource = renderTargets_[frameIndex_].Get();
            backToRtv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            backToRtv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            backToRtv.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList_->ResourceBarrier(1, &backToRtv);
        } else {
            screenshotReadbackBuffer.Reset();
        }
    }

    D3D12_RESOURCE_BARRIER toPresent{};
    toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toPresent.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toPresent.Transition.pResource = renderTargets_[frameIndex_].Get();
    toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &toPresent);

    if (timestampQueryHeap_ && timestampReadbackBuffer_) {
        const std::uint32_t queryBase = frameIndex_ * kTimestampQueriesPerFrame;
        commandList_->EndQuery(
            timestampQueryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryBase + 1);
        commandList_->ResolveQueryData(
            timestampQueryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryBase,
            kTimestampQueriesPerFrame,
            timestampReadbackBuffer_.Get(),
            static_cast<UINT64>(queryBase) * sizeof(std::uint64_t));
    }

    if (FAILED(commandList_->Close())) {
        recording_ = false;
        return;
    }

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    const auto presentStart = std::chrono::high_resolution_clock::now();
    UINT presentFlags = 0;
    if (!vsyncEnabled_ && allowTearingSupported_) {
        BOOL dxgiFullscreen = FALSE;
        if (SUCCEEDED(swapChain_->GetFullscreenState(&dxgiFullscreen, nullptr)) && !dxgiFullscreen) {
            presentFlags = DXGI_PRESENT_ALLOW_TEARING;
        }
    }
    swapChain_->Present(vsyncEnabled_ ? 1 : 0, presentFlags);
    const auto presentEnd = std::chrono::high_resolution_clock::now();
    lastPresentWaitMs_ = static_cast<float>(
        std::chrono::duration<double, std::milli>(presentEnd - presentStart).count());

    if (fence_) {
        const std::uint64_t signal = ++fenceValue_;
        if (SUCCEEDED(commandQueue_->Signal(fence_.Get(), signal))) {
            frameFenceValues_[frameIndex_] = signal;
            if (captureThisFrame && screenshotReadbackBuffer && screenshotWidth > 0 && screenshotHeight > 0) {
                if (waitForFenceValue(signal)) {
                    const std::size_t rowBytes = static_cast<std::size_t>(screenshotWidth) * 4u;
                    std::vector<unsigned char> rgba(
                        static_cast<std::size_t>(screenshotWidth) *
                            static_cast<std::size_t>(screenshotHeight) *
                            4u,
                        0u);
                    D3D12_RANGE readRange{};
                    readRange.Begin = 0;
                    readRange.End = static_cast<SIZE_T>(screenshotTotalBytes);
                    unsigned char* mapped = nullptr;
                    if (SUCCEEDED(screenshotReadbackBuffer->Map(
                            0,
                            &readRange,
                            reinterpret_cast<void**>(&mapped))) &&
                        mapped) {
                        const std::size_t srcStride =
                            static_cast<std::size_t>(screenshotFootprint.Footprint.RowPitch);
                        for (int y = 0; y < screenshotHeight; ++y) {
                            const std::size_t srcOff = static_cast<std::size_t>(y) * srcStride;
                            const std::size_t dstOff = static_cast<std::size_t>(y) * rowBytes;
                            std::memcpy(rgba.data() + dstOff, mapped + srcOff, rowBytes);
                        }
                        const D3D12_RANGE writtenRange{0, 0};
                        screenshotReadbackBuffer->Unmap(0, &writtenRange);

                        try {
                            const std::filesystem::path outPath(screenshotPath_);
                            if (!outPath.parent_path().empty()) {
                                std::filesystem::create_directories(outPath.parent_path());
                            }
                            const int wrote = stbi_write_png(
                                outPath.string().c_str(),
                                screenshotWidth,
                                screenshotHeight,
                                4,
                                rgba.data(),
                                screenshotWidth * 4);
                            std::ostringstream msg;
                            msg << "[Screenshot][D3D12] "
                                << (wrote != 0 ? "WROTE " : "FAILED ")
                                << outPath.string()
                                << " size=" << screenshotWidth << "x" << screenshotHeight
                                << " frame=" << frameCounter_;
                            d3d12LifecycleLog().info(msg.str());
                        } catch (const std::exception& ex) {
                            d3d12LifecycleLog().error(
                                std::string("[Screenshot][D3D12] FAILED exception=") + ex.what());
                        }

                        screenshotCaptured_ = true;
                    }
                }
            }
        }
    }

    lastFrameDrawCalls_ = frameDrawCalls_;
    lastFrameTriangles_ = frameTriangles_;
    lastFrameIndexedOpaqueDraws_ = frameIndexedOpaqueDraws_;
    lastFrameIndexedBlendDraws_ = frameIndexedBlendDraws_;
    lastFrameIndexedCachedDraws_ = frameIndexedCachedDraws_;
    lastFrameIndexedDynamicDraws_ = frameIndexedDynamicDraws_;
    lastFrameIndexedInstancedDraws_ = frameIndexedInstancedDraws_;
    lastFrameIndexedOutlineBatches_ = frameIndexedOutlineBatches_;
    lastFrameIndexedGeometrySwitches_ = frameIndexedGeometrySwitches_;
    lastFrameIndexedMaterialSwitches_ = frameIndexedMaterialSwitches_;
    lastFrameIndexedTextureSwitches_ = frameIndexedTextureSwitches_;
    lastFrameIndexedD3d12PsoSets_ = frameIndexedD3d12PsoSets_;
    lastFrameIndexedD3d12DescriptorTableSets_ = frameIndexedD3d12DescriptorTableSets_;
    lastFrameFastSceneInstances_ = frameFastSceneInstances_;
    lastFrameFastSceneDrawClasses_ = frameFastSceneDrawClasses_;
    lastFrameFastSceneVisibleSkeletons_ = frameFastSceneVisibleSkeletons_;
    lastFrameFastScenePaletteUploadBytes_ = frameFastScenePaletteUploadBytes_;
    lastFrameFastSceneMaterialTableBinds_ = frameFastSceneMaterialTableBinds_;
    lastFrameFastSceneIndirectCommands_ = frameFastSceneIndirectCommands_;

    recording_ = false;
#endif
}

void D3D12RenderBackend::onResize(int width, int height) {
#if defined(_WIN32)
    const int newW = (width > 1) ? width : 1;
    const int newH = (height > 1) ? height : 1;
    if (!initialized_ || ((newW == width_) && (newH == height_))) return;

    width_ = newW;
    height_ = newH;
    if (!swapChain_ || !device_) return;

    waitForGpu();
    recording_ = false;
    releaseRenderTargets();
    releaseDepthResources();

    if (SUCCEEDED(swapChain_->ResizeBuffers(
            kFrameCount,
            static_cast<UINT>(width_),
            static_cast<UINT>(height_),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            allowTearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0))) {
        frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
        createRenderTargets();
        createDepthResources();
    }
#else
    (void)width;
    (void)height;
#endif
}

void D3D12RenderBackend::shutdown() {
#if defined(_WIN32)
    if (!initialized_) return;

    waitForGpu();
    releaseRenderTargets();
    releaseDepthResources();
    if (debugVertexBuffer_ && debugVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        debugVertexBuffer_->Unmap(0, &readRange);
    }
    debugVertexMappedData_ = nullptr;
    debugVertexBuffer_.Reset();
    debugVertexBufferGpuAddress_ = 0;
    debugVertexStride_ = 0;
    debugVertexBufferSize_ = 0;
    debugVertexBufferBytesPerFrame_ = 0;
    debugVertexFrameBaseOffset_ = 0;
    debugVertexFrameOffset_ = 0;
    cachedDebugQuads_.clear();
    cachedDebugLines_.clear();
    if (worldVertexBuffer_ && worldVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldVertexBuffer_->Unmap(0, &readRange);
    }
    worldVertexMappedData_ = nullptr;
    worldVertexBuffer_.Reset();
    worldVertexBufferGpuAddress_ = 0;
    worldVertexStride_ = 0;
    worldVertexBufferSize_ = 0;
    worldVertexBufferBytesPerFrame_ = 0;
    worldVertexFrameBaseOffset_ = 0;
    worldVertexFrameOffset_ = 0;
    if (worldIndexBuffer_ && worldIndexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldIndexBuffer_->Unmap(0, &readRange);
    }
    worldIndexMappedData_ = nullptr;
    worldIndexBuffer_.Reset();
    worldIndexBufferGpuAddress_ = 0;
    worldIndexBufferSize_ = 0;
    worldIndexBufferBytesPerFrame_ = 0;
    worldIndexFrameBaseOffset_ = 0;
    worldIndexFrameOffset_ = 0;
    if (worldVsConstantBuffer_ && worldVsConstantMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldVsConstantBuffer_->Unmap(0, &readRange);
    }
    worldVsConstantMappedData_ = nullptr;
    worldVsConstantBuffer_.Reset();
    worldVsConstantBufferGpuAddress_ = 0;
    worldVsConstantBufferSize_ = 0;
    worldVsConstantBufferBytesPerFrame_ = 0;
    worldVsConstantFrameBaseOffset_ = 0;
    worldVsConstantFrameOffset_ = 0;
    if (worldSkinMatrixBuffer_ && worldSkinMatrixMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldSkinMatrixBuffer_->Unmap(0, &readRange);
    }
    worldSkinMatrixMappedData_ = nullptr;
    worldSkinMatrixBuffer_.Reset();
    worldSkinMatrixBufferGpuAddress_ = 0;
    worldSkinMatrixBufferSize_ = 0;
    worldSkinMatrixBufferBytesPerFrame_ = 0;
    worldSkinMatrixFrameBaseOffset_ = 0;
    worldSkinMatrixFrameOffset_ = 0;
    if (worldInstanceBuffer_ && worldInstanceMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldInstanceBuffer_->Unmap(0, &readRange);
    }
    worldInstanceMappedData_ = nullptr;
    worldInstanceBuffer_.Reset();
    worldInstanceBufferGpuAddress_ = 0;
    worldInstanceBufferSize_ = 0;
    worldInstanceBufferBytesPerFrame_ = 0;
    worldInstanceFrameBaseOffset_ = 0;
    worldInstanceFrameOffset_ = 0;
    worldFallbackTextureDescriptorIndex_ = 0;
    worldFallbackNormalTextureDescriptorIndex_ = 0;
    worldFallbackMetallicRoughnessTextureDescriptorIndex_ = 0;
    worldFallbackOcclusionTextureDescriptorIndex_ = 0;
    worldFallbackEmissiveTextureDescriptorIndex_ = 0;
    worldFallbackEnvTextureDescriptorIndex_ = 0;
    worldFallbackMaterialDescriptorBlockIndex_ = 0xffffffffu;
    worldFallbackEnvTextureReady_ = false;
    worldPremultipliedBlendPipelineState_.Reset();
    worldAdditiveBlendPipelineState_.Reset();
    worldBlendPipelineState_.Reset();
    worldNoDepthPremultipliedBlendPipelineState_.Reset();
    worldNoDepthAdditiveBlendPipelineState_.Reset();
    worldNoDepthBlendPipelineState_.Reset();
    worldNoDepthDualSourceAdditiveBlendPipelineState_.Reset();
    worldNoDepthDualSourceBlendPipelineState_.Reset();
    worldDualSourceAdditiveBlendPipelineState_.Reset();
    worldDualSourceBlendPipelineState_.Reset();
    worldPipelineState_.Reset();
    worldRootSignature_.Reset();
    spriteTextures_.clear();
    worldTextures_.clear();
    worldMaterialDescriptorBlocks_.clear();
    worldSceneMaterialBindingCache_.clear();
    worldSceneMaterialBindingCacheGeneration_ = 0u;
    worldSceneColorTarget_.Reset();
    worldSceneColorPipelineState_.Reset();
    worldSceneColorRootSignature_.Reset();
    worldSceneColorSrvDescriptorIndex_ = 0xffffffffu;
    worldSceneColorPassActive_ = false;
    if (spriteVertexBuffer_ && spriteVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        spriteVertexBuffer_->Unmap(0, &readRange);
    }
    spriteVertexMappedData_ = nullptr;
    spriteVertexBuffer_.Reset();
    spriteVertexBufferGpuAddress_ = 0;
    spriteVertexStride_ = 0;
    spriteVertexBufferSize_ = 0;
    spriteVertexBufferBytesPerFrame_ = 0;
    spriteVertexFrameBaseOffset_ = 0;
    spriteVertexFrameOffset_ = 0;
    spritePipelineState_.Reset();
    spriteRootSignature_.Reset();
    srvHeap_.Reset();
    srvDescriptorSize_ = 0;
    nextSrvDescriptorIndex_ = 0;
    debugPipelineState_.Reset();
    debugRootSignature_.Reset();

    commandList_.Reset();
    for (auto& allocator : commandAllocators_) allocator.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();
    dsvDescriptorSize_ = 0;
    swapChain_.Reset();
    commandQueue_.Reset();
    fence_.Reset();
    timestampReadbackBuffer_.Reset();
    timestampQueryHeap_.Reset();
    timestampFrequency_ = 0;
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();

    if (fenceEvent_) {
        CloseHandle(static_cast<HANDLE>(fenceEvent_));
        fenceEvent_ = nullptr;
    }
#endif
    frameFenceValues_.fill(0u);
    lastPresentWaitMs_ = 0.0f;
    lastGpuFrameMs_ = 0.0f;
    lastGpuFrameValid_ = false;
    frameDrawCalls_ = 0u;
    frameTriangles_ = 0u;
    lastFrameDrawCalls_ = 0u;
    lastFrameTriangles_ = 0u;
    initialized_ = false;
}

void D3D12RenderBackend::initDeviceAndSwapchain(const std::string& preferredAdapterName) {
#if !defined(_WIN32)
    (void)preferredAdapterName;
    throw std::runtime_error("D3D12RenderBackend is only available on Windows.");
#else
    ensureWindowHandle();

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()))) || !factory_) {
        throw std::runtime_error("CreateDXGIFactory1 failed for D3D12 backend.");
    }

    allowTearingSupported_ = false;
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5)) && factory5) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing,
                sizeof(allowTearing)))) {
            allowTearingSupported_ = (allowTearing == TRUE);
        }
    }

    const auto selection = engine::render::dxgi::selectHardwareAdapter(factory_.Get(), preferredAdapterName);
    if (!selection.adapter) {
        throw std::runtime_error("No suitable DXGI adapter found for D3D12 backend.");
    }
    adapter_ = selection.adapter;
    adapterName_ = selection.name.empty() ? "<unnamed dxgi adapter>" : selection.name;
    discreteAdapter_ = selection.discrete;

    if (FAILED(D3D12CreateDevice(adapter_.Get(),
                                 D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(device_.ReleaseAndGetAddressOf()))) ||
        !device_) {
        throw std::runtime_error("D3D12CreateDevice failed.");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue_.ReleaseAndGetAddressOf()))) ||
        !commandQueue_) {
        throw std::runtime_error("CreateCommandQueue failed.");
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount = kFrameCount;
    swapDesc.Width = static_cast<UINT>(width_);
    swapDesc.Height = static_cast<UINT>(height_);
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.Flags = allowTearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory_->CreateSwapChainForHwnd(commandQueue_.Get(),
                                                static_cast<HWND>(hwnd_),
                                                &swapDesc,
                                                nullptr,
                                                nullptr,
                                                swapChain1.ReleaseAndGetAddressOf())) ||
        !swapChain1) {
        throw std::runtime_error("CreateSwapChainForHwnd failed.");
    }
    if (FAILED(factory_->MakeWindowAssociation(static_cast<HWND>(hwnd_), DXGI_MWA_NO_ALT_ENTER))) {
        // Non-fatal.
    }
    if (FAILED(swapChain1.As(&swapChain_)) || !swapChain_) {
        throw std::runtime_error("Query IDXGISwapChain3 failed.");
    }
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kFrameCount + 1u;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.ReleaseAndGetAddressOf()))) ||
        !rtvHeap_) {
        throw std::runtime_error("CreateDescriptorHeap (RTV) failed.");
    }
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(commandAllocators_[i].ReleaseAndGetAddressOf()))) ||
            !commandAllocators_[i]) {
            throw std::runtime_error("CreateCommandAllocator failed.");
        }
    }

    if (FAILED(device_->CreateCommandList(0,
                                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          commandAllocators_[0].Get(),
                                          nullptr,
                                          IID_PPV_ARGS(commandList_.ReleaseAndGetAddressOf()))) ||
        !commandList_) {
        throw std::runtime_error("CreateCommandList failed.");
    }
    commandList_->Close();

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf()))) ||
        !fence_) {
        throw std::runtime_error("CreateFence failed.");
    }
    fenceValue_ = 0;
    frameFenceValues_.fill(0u);

    if (FAILED(commandQueue_->GetTimestampFrequency(&timestampFrequency_))) {
        timestampFrequency_ = 0;
    }
    if (timestampFrequency_ > 0) {
        D3D12_QUERY_HEAP_DESC queryHeapDesc{};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = kTimestampQueryCount;
        queryHeapDesc.NodeMask = 0;
        if (FAILED(device_->CreateQueryHeap(
                &queryHeapDesc,
                IID_PPV_ARGS(timestampQueryHeap_.ReleaseAndGetAddressOf()))) ||
            !timestampQueryHeap_) {
            timestampFrequency_ = 0;
        } else {
            D3D12_HEAP_PROPERTIES readbackHeapProps{};
            readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
            readbackHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            readbackHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            readbackHeapProps.CreationNodeMask = 1;
            readbackHeapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC readbackDesc{};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Alignment = 0;
            readbackDesc.Width = static_cast<UINT64>(kTimestampQueryCount) * sizeof(std::uint64_t);
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.SampleDesc.Quality = 0;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            if (FAILED(device_->CreateCommittedResource(
                    &readbackHeapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &readbackDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(timestampReadbackBuffer_.ReleaseAndGetAddressOf()))) ||
                !timestampReadbackBuffer_) {
                timestampQueryHeap_.Reset();
                timestampFrequency_ = 0;
            }
        }
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        throw std::runtime_error("CreateEvent failed for D3D12 fence.");
    }

    const auto startupInitStart = std::chrono::steady_clock::now();

    auto stageStart = std::chrono::steady_clock::now();
    createDebugPipeline();
    auto stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createDebugPipeline=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    stageStart = std::chrono::steady_clock::now();
    createWorldPipeline();
    stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createWorldPipeline=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    stageStart = std::chrono::steady_clock::now();
    createSpritePipeline();
    stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createSpritePipeline=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    stageStart = std::chrono::steady_clock::now();
    createWorldSceneColorPipeline();
    stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createWorldSceneColorPipeline=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    stageStart = std::chrono::steady_clock::now();
    createRenderTargets();
    stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createRenderTargets=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    stageStart = std::chrono::steady_clock::now();
    createDepthResources();
    stageEnd = std::chrono::steady_clock::now();
    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] createDepthResources=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - stageStart).count()) +
        "ms");

    d3d12LifecycleLog().info(
        "[Renderer][D3D12][Startup] totalInit=" +
        std::to_string(std::chrono::duration<double, std::milli>(stageEnd - startupInitStart).count()) +
        "ms");
    d3d12LifecycleLog().info(
        std::string("[Renderer][D3D12][Startup] allowTearing=") +
        (allowTearingSupported_ ? "1" : "0"));
    initialized_ = true;
#endif
}

void D3D12RenderBackend::createRenderTargets() {
#if defined(_WIN32)
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        renderTargets_[i].Reset();
        if (SUCCEEDED(swapChain_->GetBuffer(i, IID_PPV_ARGS(renderTargets_[i].ReleaseAndGetAddressOf()))) &&
            renderTargets_[i]) {
            device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, handle);
        }
        handle.ptr += static_cast<SIZE_T>(rtvDescriptorSize_);
    }
    createWorldSceneColorResource();
#endif
}

void D3D12RenderBackend::createDepthResources() {
#if defined(_WIN32)
    if (!device_ || width_ <= 0 || height_ <= 0) return;

    if (!dsvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = kFrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(dsvHeap_.ReleaseAndGetAddressOf()))) ||
            !dsvHeap_) {
            throw std::runtime_error("CreateDescriptorHeap (DSV) failed.");
        }
        dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = static_cast<UINT64>(width_);
    depthDesc.Height = static_cast<UINT>(height_);
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        depthBuffers_[i].Reset();
        if (FAILED(device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(depthBuffers_[i].ReleaseAndGetAddressOf()))) ||
            !depthBuffers_[i]) {
            throw std::runtime_error("CreateCommittedResource failed for D3D12 depth buffer.");
        }
        device_->CreateDepthStencilView(depthBuffers_[i].Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += static_cast<SIZE_T>(dsvDescriptorSize_);
    }
#endif
}

void D3D12RenderBackend::releaseDepthResources() {
#if defined(_WIN32)
    for (auto& depth : depthBuffers_) {
        depth.Reset();
    }
#endif
}

void D3D12RenderBackend::releaseRenderTargets() {
#if defined(_WIN32)
    releaseWorldSceneColorResource();
    for (auto& target : renderTargets_) {
        target.Reset();
    }
#endif
}

void D3D12RenderBackend::waitForGpu() {
#if defined(_WIN32)
    if (!initialized_ || !commandQueue_ || !fence_ || !fenceEvent_) return;

    const std::uint64_t signal = ++fenceValue_;
    if (FAILED(commandQueue_->Signal(fence_.Get(), signal))) return;
    (void)waitForFenceValue(signal);
#endif
}

bool D3D12RenderBackend::waitForFenceValue(std::uint64_t value) {
#if defined(_WIN32)
    if (!initialized_ || !fence_ || !fenceEvent_) return false;
    if (value == 0u) return true;
    if (fence_->GetCompletedValue() >= value) return true;
    if (FAILED(fence_->SetEventOnCompletion(value, static_cast<HANDLE>(fenceEvent_)))) return false;
    return WaitForSingleObject(static_cast<HANDLE>(fenceEvent_), INFINITE) == WAIT_OBJECT_0;
#else
    (void)value;
    return false;
#endif
}

void D3D12RenderBackend::ensureWindowHandle() {
#if defined(_WIN32)
    if (hwnd_) return;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window_, &wmInfo)) {
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());
    }
    hwnd_ = wmInfo.info.win.window;
    if (!hwnd_) {
        throw std::runtime_error("Failed to resolve HWND from SDL window.");
    }
#endif
}
