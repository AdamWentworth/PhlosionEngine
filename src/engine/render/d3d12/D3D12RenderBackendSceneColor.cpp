#include "engine/render/D3D12RenderBackend.h"

#include <algorithm>
#include <stdexcept>

#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
using namespace engine::render::d3d12_pipeline_compile;
#endif

void D3D12RenderBackend::createWorldSceneColorPipeline() {
#if defined(_WIN32)
    if (!device_ || !srvHeap_) return;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) {
        throw std::runtime_error(
            "D3D12 SRV heap has no slot for the world scene-color target.");
    }
    worldSceneColorSrvDescriptorIndex_ = nextSrvDescriptorIndex_++;

    static constexpr char kVsSource[] = R"HLSL(
struct VSOut {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};
VSOut main(uint vertexId : SV_VertexID) {
  VSOut output;
  float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
  output.uv = uv;
  output.position = float4(
      uv.x * 2.0f - 1.0f,
      1.0f - uv.y * 2.0f,
      0.0f,
      1.0f);
  return output;
}
)HLSL";

    static constexpr char kPsSource[] = R"HLSL(
Texture2D<float4> gLinearSceneColor : register(t0);
SamplerState gSceneSampler : register(s0);
struct PSIn {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};
float linearToSrgbChannel(float linearColor) {
  float c = saturate(linearColor);
  if (c <= 0.00313080009f) {
    return 12.9200001f * c;
  }
  return 1.05499995f * pow(abs(c), 0.416666657f) - 0.0549999997f;
}
float4 main(PSIn input) : SV_TARGET {
  float4 scene = gLinearSceneColor.Sample(gSceneSampler, input.uv);
  return float4(
      linearToSrgbChannel(scene.r),
      linearToSrgbChannel(scene.g),
      linearToSrgbChannel(scene.b),
      scene.a);
}
)HLSL";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    if (!compileHlslWithCache(
            kVsSource,
            sizeof(kVsSource) - 1u,
            "main",
            "vs_5_0",
            d3dCompileFlags(),
            0,
            vsBlob,
            errors) ||
        !vsBlob) {
        throw std::runtime_error(
            "D3DCompile failed for world scene-color VS.");
    }
    errors.Reset();
    if (!compileHlslWithCache(
            kPsSource,
            sizeof(kPsSource) - 1u,
            "main",
            "ps_5_0",
            d3dCompileFlags(),
            0,
            psBlob,
            errors) ||
        !psBlob) {
        throw std::runtime_error(
            "D3DCompile failed for world scene-color PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1u;
    srvRange.BaseShaderRegister = 0u;
    srvRange.RegisterSpace = 0u;
    srvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameter{};
    rootParameter.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = 1u;
    rootParameter.DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1u;
    rootDesc.pParameters = &rootParameter;
    rootDesc.NumStaticSamplers = 1u;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    Microsoft::WRL::ComPtr<ID3DBlob> rootErrors;
    if (FAILED(D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serializedRoot.ReleaseAndGetAddressOf(),
            rootErrors.ReleaseAndGetAddressOf())) ||
        !serializedRoot) {
        throw std::runtime_error(
            "D3D12SerializeRootSignature failed for world scene color.");
    }
    if (FAILED(device_->CreateRootSignature(
            0u,
            serializedRoot->GetBufferPointer(),
            serializedRoot->GetBufferSize(),
            IID_PPV_ARGS(
                worldSceneColorRootSignature_.ReleaseAndGetAddressOf()))) ||
        !worldSceneColorRootSignature_) {
        throw std::runtime_error(
            "CreateRootSignature failed for world scene color.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = worldSceneColorRootSignature_.Get();
    pipeline.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pipeline.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    pipeline.BlendState.AlphaToCoverageEnable = FALSE;
    pipeline.BlendState.IndependentBlendEnable = FALSE;
    pipeline.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pipeline.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pipeline.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.FrontCounterClockwise = FALSE;
    pipeline.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    pipeline.RasterizerState.DepthBiasClamp =
        D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    pipeline.RasterizerState.SlopeScaledDepthBias =
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = FALSE;
    pipeline.DepthStencilState.DepthWriteMask =
        D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.DepthStencilState.StencilEnable = FALSE;
    pipeline.InputLayout = {nullptr, 0u};
    pipeline.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1u;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1u;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &pipeline,
            IID_PPV_ARGS(
                worldSceneColorPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldSceneColorPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for world scene color.");
    }
#endif
}

void D3D12RenderBackend::createWorldSceneColorResource() {
#if defined(_WIN32)
    releaseWorldSceneColorResource();
    if (!device_ || !rtvHeap_ || !srvHeap_ ||
        worldSceneColorSrvDescriptorIndex_ == 0xffffffffu ||
        width_ <= 0 || height_ <= 0) {
        return;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1u;
    heap.VisibleNodeMask = 1u;

    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = static_cast<UINT64>(width_);
    texture.Height = static_cast<UINT>(height_);
    texture.DepthOrArraySize = 1u;
    texture.MipLevels = 1u;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1u;
    texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::copy(clearColor_, clearColor_ + 4, clear.Color);
    if (FAILED(device_->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear,
            IID_PPV_ARGS(
                worldSceneColorTarget_.ReleaseAndGetAddressOf()))) ||
        !worldSceneColorTarget_) {
        throw std::runtime_error(
            "CreateCommittedResource failed for world scene color.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kFrameCount) *
        static_cast<SIZE_T>(rtvDescriptorSize_);
    device_->CreateRenderTargetView(
        worldSceneColorTarget_.Get(), nullptr, rtv);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1u;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu =
        srvHeap_->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr +=
        static_cast<SIZE_T>(worldSceneColorSrvDescriptorIndex_) *
        static_cast<SIZE_T>(srvDescriptorSize_);
    device_->CreateShaderResourceView(
        worldSceneColorTarget_.Get(), &srv, srvCpu);
#endif
}

void D3D12RenderBackend::releaseWorldSceneColorResource() {
#if defined(_WIN32)
    worldSceneColorPassActive_ = false;
    worldSceneColorTarget_.Reset();
#endif
}

void D3D12RenderBackend::beginWorldSceneColorPass(
    int surfaceWidth,
    int surfaceHeight) {
#if defined(_WIN32)
    (void)surfaceWidth;
    (void)surfaceHeight;
    if (!recording_ || !commandList_ || !worldSceneColorTarget_ ||
        worldSceneColorPassActive_) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldSceneColorTarget_.Get();
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1u, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kFrameCount) *
        static_cast<SIZE_T>(rtvDescriptorSize_);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (dsvHeap_) {
        dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(frameIndex_) *
            static_cast<SIZE_T>(dsvDescriptorSize_);
    }
    commandList_->OMSetRenderTargets(
        1u, &rtv, FALSE, dsvHeap_ ? &dsv : nullptr);
    commandList_->ClearRenderTargetView(rtv, clearColor_, 0u, nullptr);
    if (dsvHeap_) {
        commandList_->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0u,
            0u,
            nullptr);
    }
    worldSceneColorPassActive_ = true;
#else
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::endWorldSceneColorPass() {
#if defined(_WIN32)
    if (!worldSceneColorPassActive_ || !commandList_ ||
        !worldSceneColorTarget_ || !worldSceneColorPipelineState_ ||
        !worldSceneColorRootSignature_) {
        worldSceneColorPassActive_ = false;
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldSceneColorTarget_.Get();
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1u, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    backbufferRtv.ptr += static_cast<SIZE_T>(frameIndex_) *
        static_cast<SIZE_T>(rtvDescriptorSize_);
    commandList_->OMSetRenderTargets(
        1u, &backbufferRtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>((std::max)(1, width_));
    viewport.Height = static_cast<float>((std::max)(1, height_));
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{
        0,
        0,
        static_cast<LONG>((std::max)(1, width_)),
        static_cast<LONG>((std::max)(1, height_))};
    commandList_->RSSetViewports(1u, &viewport);
    commandList_->RSSetScissorRects(1u, &scissor);
    commandList_->SetPipelineState(
        worldSceneColorPipelineState_.Get());
    commandList_->SetGraphicsRootSignature(
        worldSceneColorRootSignature_.Get());
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1u, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSrv =
        srvHeap_->GetGPUDescriptorHandleForHeapStart();
    sceneSrv.ptr +=
        static_cast<UINT64>(worldSceneColorSrvDescriptorIndex_) *
        static_cast<UINT64>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(0u, sceneSrv);
    commandList_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3u, 1u, 0u, 0u);
    ++frameDrawCalls_;
    ++frameTriangles_;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (dsvHeap_) {
        dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(frameIndex_) *
            static_cast<SIZE_T>(dsvDescriptorSize_);
    }
    commandList_->OMSetRenderTargets(
        1u,
        &backbufferRtv,
        FALSE,
        dsvHeap_ ? &dsv : nullptr);
    worldSceneColorPassActive_ = false;
#endif
}
