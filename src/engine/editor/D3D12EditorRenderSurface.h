#pragma once

#include "engine/editor/EditorRenderSurface.h"
#include "engine/editor/EditorShell.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#endif

class D3D12RenderBackend;

namespace engine::editor {

class D3D12EditorRenderSurface final
    : public EditorRenderSurface {
public:
    D3D12EditorRenderSurface(
        D3D12RenderBackend& renderer,
        EditorTextureDescriptor textureDescriptor);
    ~D3D12EditorRenderSurface() override;

    D3D12EditorRenderSurface(
        const D3D12EditorRenderSurface&) = delete;
    D3D12EditorRenderSurface& operator=(
        const D3D12EditorRenderSurface&) = delete;

    bool begin(int width, int height) override;
    void end() override;
    void shutdown() override;
    std::uint64_t textureId() const noexcept override;

private:
    bool ensure(int width, int height);

    D3D12RenderBackend* renderer_ = nullptr;
    EditorTextureDescriptor textureDescriptor_{};
    std::uint32_t linearSrvDescriptorIndex_ =
        0xffffffffu;
    int width_ = 0;
    int height_ = 0;
    bool active_ = false;

#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
        rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
        dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>
        linearColor_;
    Microsoft::WRL::ComPtr<ID3D12Resource>
        displayColor_;
    Microsoft::WRL::ComPtr<ID3D12Resource>
        depth_;
#endif
};

} // namespace engine::editor
