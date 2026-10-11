#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include <stdexcept>

#if defined(_WIN32)
void D3D12RenderBackend::writeWorldShadowSrv(std::uint32_t index) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = 1;
    auto handle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(index) * srvDescriptorSize_;
    device_->CreateShaderResourceView(worldShadowDepth_.Get(), &desc, handle);
}
#endif

void D3D12RenderBackend::prewarmWorldDirectionalShadow(std::uint32_t resolution) {
#if defined(_WIN32)
    if (resolution < 128 || resolution > 4096 || (resolution & (resolution - 1)))
        throw std::invalid_argument("Invalid directional-shadow resolution.");
    if (worldShadowDepth_ && worldShadowResolution_ == resolution && !worldShadowActive_) return;
    if (!device_ || !srvHeap_ || worldShadowActive_ || recording_)
        throw std::runtime_error("Directional shadows must be prewarmed outside a frame.");
    waitForGpu();
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap.NumDescriptors = 1;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(worldShadowDsvHeap_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("Cannot allocate directional-shadow DSV.");
    D3D12_HEAP_PROPERTIES properties{}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = texture.Height = resolution; texture.DepthOrArraySize = 1;
    texture.MipLevels = 1; texture.Format = DXGI_FORMAT_R32_TYPELESS;
    texture.SampleDesc.Count = 1; texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{}; clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 1;
    if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(worldShadowDepth_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("Cannot allocate directional-shadow depth target.");
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(worldShadowDepth_.Get(), &dsv, worldShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());
    worldShadowResolution_ = resolution; worldShadowReady_ = false;
    for (const auto& [key, base] : worldMaterialDescriptorBlocks_) writeWorldShadowSrv(base + 8u);
#else
    (void)resolution;
#endif
}

bool D3D12RenderBackend::beginWorldDirectionalShadow(const engine::render::WorldDirectionalShadowView& view) {
#if defined(_WIN32)
    if (worldShadowActive_) return false;
    worldShadowReady_ = false;
    if (!recording_ || !engine::render::validWorldDirectionalShadowView(view) ||
        !worldShadowDepth_ || view.resolution != worldShadowResolution_) return false;
    worldShadowView_ = view;
    worldShadowSavedViewport_ = worldLastViewport_; worldShadowSavedScissor_ = worldLastScissor_;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldShadowDepth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList_->ResourceBarrier(1, &barrier);
    const auto dsv = worldShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    worldShadowActive_ = true;
    return true;
#else
    (void)view; return false;
#endif
}

bool D3D12RenderBackend::endWorldDirectionalShadow() {
#if defined(_WIN32)
    if (!worldShadowActive_) return false;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldShadowDepth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1, &barrier);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if (editorSurfaceActive_) {
        rtv = worldSceneColorPassActive_ ? editorSurfaceTarget_.linearRtv : editorSurfaceTarget_.displayRtv;
        dsv = editorSurfaceTarget_.depthDsv;
    } else {
        rtv.ptr += SIZE_T(worldSceneColorPassActive_ ? kFrameCount : frameIndex_) * rtvDescriptorSize_;
        dsv.ptr += SIZE_T(frameIndex_) * dsvDescriptorSize_;
    }
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    commandList_->RSSetViewports(1, &worldShadowSavedViewport_);
    commandList_->RSSetScissorRects(1, &worldShadowSavedScissor_);
    worldShadowActive_ = false; worldShadowReady_ = true;
    return true;
#else
    return false;
#endif
}

void D3D12RenderBackend::clearWorldDirectionalShadow() {
#if defined(_WIN32)
    if (worldShadowActive_) endWorldDirectionalShadow();
    worldShadowReady_ = false; worldShadowView_ = {};
#endif
}
