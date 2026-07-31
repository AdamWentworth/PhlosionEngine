#include "engine/render/D3D12RenderBackend.h"

#include <algorithm>

#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#if defined(_WIN32)

std::uint32_t
D3D12RenderBackend::reserveEditorSceneColorDescriptor() {
    if (!device_ || !srvHeap_ ||
        nextSrvDescriptorIndex_ >=
            engine::render::d3d12_internal::
                kMaxSrvDescriptors) {
        return 0xffffffffu;
    }
    return nextSrvDescriptorIndex_++;
}

bool D3D12RenderBackend::writeEditorSceneColorDescriptor(
    std::uint32_t descriptorIndex,
    ID3D12Resource* resource) {
    if (!device_ || !srvHeap_ || !resource ||
        descriptorIndex >=
            engine::render::d3d12_internal::
                kMaxSrvDescriptors) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension =
        D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1u;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        srvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr +=
        static_cast<SIZE_T>(descriptorIndex) *
        static_cast<SIZE_T>(srvDescriptorSize_);
    device_->CreateShaderResourceView(
        resource,
        &srv,
        cpu);
    return true;
}

bool D3D12RenderBackend::beginEditorSurface(
    const EditorSurfaceTarget& target) {
    if (!recording_ || !commandList_ ||
        !target.linearColor ||
        !target.displayColor ||
        target.linearRtv.ptr == 0u ||
        target.displayRtv.ptr == 0u ||
        target.depthDsv.ptr == 0u ||
        target.linearSrvDescriptorIndex ==
            0xffffffffu ||
        target.width <= 0 ||
        target.height <= 0) {
        return false;
    }
    if (editorSurfaceActive_) {
        endEditorSurface();
    }
    editorSurfaceTarget_ = target;
    editorSurfaceActive_ = true;
    editorSurfaceScenePassActive_ = false;

    D3D12_RESOURCE_BARRIER displayToRtv{};
    displayToRtv.Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    displayToRtv.Transition.pResource =
        target.displayColor;
    displayToRtv.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    displayToRtv.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    displayToRtv.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1u, &displayToRtv);

    commandList_->OMSetRenderTargets(
        1u,
        &editorSurfaceTarget_.displayRtv,
        FALSE,
        &editorSurfaceTarget_.depthDsv);
    commandList_->ClearRenderTargetView(
        editorSurfaceTarget_.displayRtv,
        clearColor_,
        0u,
        nullptr);
    commandList_->ClearDepthStencilView(
        editorSurfaceTarget_.depthDsv,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0u,
        0u,
        nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width =
        static_cast<float>(target.width);
    viewport.Height =
        static_cast<float>(target.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissor{
        0,
        0,
        static_cast<LONG>(target.width),
        static_cast<LONG>(target.height)};
    commandList_->RSSetViewports(1u, &viewport);
    commandList_->RSSetScissorRects(1u, &scissor);
    return true;
}

void D3D12RenderBackend::endEditorSurface() {
    if (!editorSurfaceActive_ || !commandList_) {
        return;
    }
    if (worldSceneColorPassActive_) {
        endWorldSceneColorPass();
    }
    D3D12_RESOURCE_BARRIER displayToSrv{};
    displayToSrv.Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    displayToSrv.Transition.pResource =
        editorSurfaceTarget_.displayColor;
    displayToSrv.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    displayToSrv.Transition.StateBefore =
        D3D12_RESOURCE_STATE_RENDER_TARGET;
    displayToSrv.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1u, &displayToSrv);

    editorSurfaceTarget_ = {};
    editorSurfaceActive_ = false;
    editorSurfaceScenePassActive_ = false;
    bindBackbufferForEditorUi();
}

void D3D12RenderBackend::bindBackbufferForEditorUi() {
    if (!recording_ || !commandList_ || !rtvHeap_) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr +=
        static_cast<SIZE_T>(frameIndex_) *
        static_cast<SIZE_T>(rtvDescriptorSize_);
    commandList_->OMSetRenderTargets(
        1u,
        &rtv,
        FALSE,
        nullptr);
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(
        (std::max)(1, width_));
    viewport.Height = static_cast<float>(
        (std::max)(1, height_));
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissor{
        0,
        0,
        static_cast<LONG>((std::max)(1, width_)),
        static_cast<LONG>((std::max)(1, height_))};
    commandList_->RSSetViewports(1u, &viewport);
    commandList_->RSSetScissorRects(1u, &scissor);
}

void D3D12RenderBackend::waitUntilIdle() {
    waitForGpu();
}

#endif
