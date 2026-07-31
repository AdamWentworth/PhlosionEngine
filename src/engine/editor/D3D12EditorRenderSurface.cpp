#include "engine/editor/D3D12EditorRenderSurface.h"

#include <algorithm>

#include "engine/render/D3D12RenderBackend.h"

namespace engine::editor {

D3D12EditorRenderSurface::D3D12EditorRenderSurface(
    D3D12RenderBackend& renderer,
    EditorTextureDescriptor textureDescriptor)
    : renderer_(&renderer),
      textureDescriptor_(textureDescriptor) {}

D3D12EditorRenderSurface::~D3D12EditorRenderSurface() {
    shutdown();
}

bool D3D12EditorRenderSurface::ensure(
    int width,
    int height) {
#if defined(_WIN32)
    width = std::max(1, width);
    height = std::max(1, height);
    if (linearColor_ && displayColor_ && depth_ &&
        width_ == width && height_ == height) {
        return true;
    }
    if (!renderer_ || !textureDescriptor_.valid()) {
        return false;
    }
    ID3D12Device* device = renderer_->nativeDevice();
    if (!device) {
        return false;
    }
    renderer_->waitUntilIdle();
    linearColor_.Reset();
    displayColor_.Reset();
    depth_.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type =
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 2u;
    if (FAILED(device->CreateDescriptorHeap(
            &rtvHeapDesc,
            IID_PPV_ARGS(
                rtvHeap_.ReleaseAndGetAddressOf()))) ||
        !rtvHeap_) {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type =
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1u;
    if (FAILED(device->CreateDescriptorHeap(
            &dsvHeapDesc,
            IID_PPV_ARGS(
                dsvHeap_.ReleaseAndGetAddressOf()))) ||
        !dsvHeap_) {
        return false;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1u;
    heap.VisibleNodeMask = 1u;
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension =
        D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = static_cast<UINT64>(width);
    colorDesc.Height = static_cast<UINT>(height);
    colorDesc.DepthOrArraySize = 1u;
    colorDesc.MipLevels = 1u;
    colorDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1u;
    colorDesc.Layout =
        D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    colorClear.Color[0] = 0.025f;
    colorClear.Color[1] = 0.031f;
    colorClear.Color[2] = 0.037f;
    colorClear.Color[3] = 1.0f;
    const auto createColor =
        [&](Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
            return SUCCEEDED(
                device->CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &colorDesc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    &colorClear,
                    IID_PPV_ARGS(
                        out.ReleaseAndGetAddressOf()))) &&
                out;
        };
    if (!createColor(linearColor_) ||
        !createColor(displayColor_)) {
        return false;
    }

    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClear,
            IID_PPV_ARGS(
                depth_.ReleaseAndGetAddressOf()))) ||
        !depth_) {
        return false;
    }

    const UINT rtvSize =
        device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE linearRtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE displayRtv =
        linearRtv;
    displayRtv.ptr += rtvSize;
    device->CreateRenderTargetView(
        linearColor_.Get(),
        nullptr,
        linearRtv);
    device->CreateRenderTargetView(
        displayColor_.Get(),
        nullptr,
        displayRtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension =
        D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(
        depth_.Get(),
        &dsvDesc,
        dsvHeap_->
            GetCPUDescriptorHandleForHeapStart());

    if (linearSrvDescriptorIndex_ ==
        0xffffffffu) {
        linearSrvDescriptorIndex_ =
            renderer_->
                reserveEditorSceneColorDescriptor();
    }
    if (linearSrvDescriptorIndex_ ==
            0xffffffffu ||
        !renderer_->writeEditorSceneColorDescriptor(
            linearSrvDescriptorIndex_,
            linearColor_.Get())) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC displaySrv{};
    displaySrv.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    displaySrv.ViewDimension =
        D3D12_SRV_DIMENSION_TEXTURE2D;
    displaySrv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    displaySrv.Texture2D.MipLevels = 1u;
    D3D12_CPU_DESCRIPTOR_HANDLE displaySrvCpu{
        static_cast<SIZE_T>(
            textureDescriptor_.cpuHandle)};
    device->CreateShaderResourceView(
        displayColor_.Get(),
        &displaySrv,
        displaySrvCpu);

    width_ = width;
    height_ = height;
    return true;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

bool D3D12EditorRenderSurface::begin(
    int width,
    int height) {
#if defined(_WIN32)
    if (active_ || !ensure(width, height) ||
        !renderer_) {
        return false;
    }
    const UINT rtvSize =
        renderer_->nativeDevice()->
            GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE linearRtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE displayRtv =
        linearRtv;
    displayRtv.ptr += rtvSize;
    active_ = renderer_->beginEditorSurface(
        D3D12RenderBackend::EditorSurfaceTarget{
            .linearColor = linearColor_.Get(),
            .displayColor = displayColor_.Get(),
            .linearRtv = linearRtv,
            .displayRtv = displayRtv,
            .depthDsv =
                dsvHeap_->
                    GetCPUDescriptorHandleForHeapStart(),
            .linearSrvDescriptorIndex =
                linearSrvDescriptorIndex_,
            .width = width_,
            .height = height_});
    return active_;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

void D3D12EditorRenderSurface::end() {
    if (!active_ || !renderer_) {
        return;
    }
    renderer_->endEditorSurface();
    active_ = false;
}

void D3D12EditorRenderSurface::shutdown() {
    if (active_) {
        end();
    }
#if defined(_WIN32)
    if (renderer_ &&
        (linearColor_ || displayColor_ || depth_)) {
        renderer_->waitUntilIdle();
    }
    linearColor_.Reset();
    displayColor_.Reset();
    depth_.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();
#endif
    width_ = 0;
    height_ = 0;
}

std::uint64_t
D3D12EditorRenderSurface::textureId() const noexcept {
#if defined(_WIN32)
    return displayColor_
        ? textureDescriptor_.gpuHandle
        : 0u;
#else
    return 0u;
#endif
}

} // namespace engine::editor
