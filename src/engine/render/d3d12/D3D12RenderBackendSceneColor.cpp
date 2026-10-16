#include "engine/render/D3D12RenderBackend.h"

#include <algorithm>
#include <cstring>
#include "engine/render/ScreenFeedbackShaders.h"
#include "engine/render/SceneBloomShader.h"
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
cbuffer ScenePost : register(b0) { float bloomStrength; };
float2 sceneSize() { uint w,h;gLinearSceneColor.GetDimensions(w,h);return float2(w,h); }
#define SCENE_SIZE sceneSize()
#define SCENE_SAMPLE(uv) gLinearSceneColor.SampleLevel(gSceneSampler,uv,0)
__PHLOSION_SCENE_BLOOM__
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
  scene.rgb=sceneBloom(input.uv,scene.rgb,bloomStrength);
  return float4(
      linearToSrgbChannel(scene.r),
      linearToSrgbChannel(scene.g),
      linearToSrgbChannel(scene.b),
      // The completed view is opaque when embedded in the editor UI.
      1.0f);
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
    const auto pixelSource=engine::render::injectSceneBloom(kPsSource,true);
    if (!compileHlslWithCache(
            pixelSource.data(),
            pixelSource.size(),
            "main",
            "ps_5_0",
            d3dCompileFlags(),
            0,
            psBlob,
            errors) ||
        !psBlob) {
        throw std::runtime_error(
            "D3DCompile failed for world scene-color PS: "+
            (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):std::string("no diagnostics")));
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
    D3D12_ROOT_PARAMETER postParameter{};
    postParameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    postParameter.Constants={0u,0u,1u};
    postParameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_PARAMETER rootParameters[]={rootParameter,postParameter};

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
    rootDesc.NumParameters = 2u;
    rootDesc.pParameters = rootParameters;
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
    ID3D12Resource* sceneColorTarget =
        editorSurfaceActive_
            ? editorSurfaceTarget_.linearColor
            : worldSceneColorTarget_.Get();
    if (!recording_ || !commandList_ || !sceneColorTarget ||
        worldSceneColorPassActive_) {
        return;
    }
    worldBloomStrength_=0;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = sceneColorTarget;
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1u, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (editorSurfaceActive_) {
        rtv = editorSurfaceTarget_.linearRtv;
        dsv = editorSurfaceTarget_.depthDsv;
    } else {
        rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(kFrameCount) *
            static_cast<SIZE_T>(rtvDescriptorSize_);
        if (dsvHeap_) {
            dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
            dsv.ptr += static_cast<SIZE_T>(frameIndex_) *
                static_cast<SIZE_T>(dsvDescriptorSize_);
        }
    }
    commandList_->OMSetRenderTargets(
        1u, &rtv, FALSE, dsv.ptr != 0u ? &dsv : nullptr);
    commandList_->ClearRenderTargetView(rtv, clearColor_, 0u, nullptr);
    if (dsv.ptr != 0u) {
        commandList_->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0u,
            0u,
            nullptr);
    }
    worldSceneColorPassActive_ = true;
    editorSurfaceScenePassActive_ =
        editorSurfaceActive_;
    (void)surfaceWidth;
    (void)surfaceHeight;
#else
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

bool D3D12RenderBackend::captureWorldDepthSnapshot() {
#if defined(_WIN32)
    if (!worldSceneColorPassActive_ || !commandList_) return false;
    ID3D12Resource* source = editorSurfaceActive_ ? editorSurfaceTarget_.depthResource : depthBuffers_[frameIndex_].Get();
    if (!source) return false;
    auto description = source->GetDesc();
    if (description.Format != DXGI_FORMAT_D32_FLOAT || description.SampleDesc.Count != 1) return false;
    // Square power-of-two buckets bound the whole pool to 4/3 of its largest image.
    const auto bucket = [](UINT64 value) { UINT64 result = 256; while (result < value) result *= 2; return result; };
    const auto sourceWidth = description.Width;
    const auto sourceHeight = description.Height;
    if (sourceWidth > 8192 || sourceHeight > 8192) return false;
    description.Width = bucket(std::max<UINT64>(sourceWidth, sourceHeight));
    description.Height = static_cast<UINT>(description.Width);
    const std::string key = "scene-depth/" + std::to_string(description.Width) + "x" + std::to_string(description.Height);
    auto found = worldTextures_.find(key);
    if (found == worldTextures_.end()) {
        if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return false;
        SpriteTexture target;
        description.Format = DXGI_FORMAT_R32_TYPELESS;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(target.resource.GetAddressOf())))) return false;
        target.descriptorIndex = nextSrvDescriptorIndex_++;
        target.srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        target.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        target.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        target.srvDesc.Texture2D.MipLevels = 1;
        target.hasSrvDesc = target.valid = true;
        auto handle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(target.descriptorIndex) * srvDescriptorSize_;
        device_->CreateShaderResourceView(target.resource.Get(), &target.srvDesc, handle);
        found = worldTextures_.emplace(key, std::move(target)).first;
    }
    auto& target = found->second;
    const auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        commandList_->ResourceBarrier(1, &barrier);
    };
    transition(source, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(target.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = target.resource.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commandList_->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    transition(target.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    worldDepthSnapshotDescriptor_ = target.descriptorIndex;
    return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::clearWorldDepth() {
#if defined(_WIN32)
    if (worldShadowActive_ || !worldSceneColorPassActive_ || !commandList_) return false;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (editorSurfaceActive_) dsv = editorSurfaceTarget_.depthDsv;
    else if (dsvHeap_) {
        dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(frameIndex_) * dsvDescriptorSize_;
    }
    if (!dsv.ptr) return false;
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0u, nullptr);
    return true;
#else
    return false;
#endif
}

void D3D12RenderBackend::endWorldSceneColorPass() {
#if defined(_WIN32)
    ID3D12Resource* sceneColorTarget =
        editorSurfaceScenePassActive_
            ? editorSurfaceTarget_.linearColor
            : worldSceneColorTarget_.Get();
    if (!worldSceneColorPassActive_ || !commandList_ ||
        !sceneColorTarget || !worldSceneColorPipelineState_ ||
        !worldSceneColorRootSignature_) {
        worldSceneColorPassActive_ = false;
        editorSurfaceScenePassActive_ = false;
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = sceneColorTarget;
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1u, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE outputRtv{};
    int outputWidth = width_;
    int outputHeight = height_;
    std::uint32_t sceneColorDescriptorIndex =
        worldSceneColorSrvDescriptorIndex_;
    if (editorSurfaceScenePassActive_) {
        outputRtv = editorSurfaceTarget_.displayRtv;
        outputWidth = editorSurfaceTarget_.width;
        outputHeight = editorSurfaceTarget_.height;
        sceneColorDescriptorIndex =
            editorSurfaceTarget_
                .linearSrvDescriptorIndex;
    } else {
        outputRtv =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        outputRtv.ptr +=
            static_cast<SIZE_T>(frameIndex_) *
            static_cast<SIZE_T>(rtvDescriptorSize_);
    }
    commandList_->OMSetRenderTargets(
        1u, &outputRtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(
        (std::max)(1, outputWidth));
    viewport.Height = static_cast<float>(
        (std::max)(1, outputHeight));
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{
        0,
        0,
        static_cast<LONG>((std::max)(1, outputWidth)),
        static_cast<LONG>((std::max)(1, outputHeight))};
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
        static_cast<UINT64>(sceneColorDescriptorIndex) *
        static_cast<UINT64>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(0u, sceneSrv);
    commandList_->SetGraphicsRoot32BitConstants(1u,1u,&worldBloomStrength_,0u);
    commandList_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3u, 1u, 0u, 0u);
    ++frameDrawCalls_;
    ++frameTriangles_;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (editorSurfaceScenePassActive_) {
        dsv = editorSurfaceTarget_.depthDsv;
    } else if (dsvHeap_) {
        dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(frameIndex_) *
            static_cast<SIZE_T>(dsvDescriptorSize_);
    }
    commandList_->OMSetRenderTargets(
        1u,
        &outputRtv,
        FALSE,
        dsv.ptr != 0u ? &dsv : nullptr);
    worldSceneColorPassActive_ = false;
    editorSurfaceScenePassActive_ = false;
#endif
}

void D3D12RenderBackend::createScreenFeedbackPipeline() {
#if defined(_WIN32)
    if (!device_ || !srvHeap_) return;
    if (nextSrvDescriptorIndex_+2 > kMaxSrvDescriptors) {
        throw std::runtime_error(
            "D3D12 SRV heap has no slot for the world scene-color target.");
    }
    for(auto& index : feedbackSrv_) index=nextSrvDescriptorIndex_++;

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

    const char* kPsSource = engine::render::screenFeedbackHlslFs;

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
            std::strlen(kPsSource),
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

    std::array<D3D12_DESCRIPTOR_RANGE,3> ranges{};
    std::array<D3D12_ROOT_PARAMETER,4> parameters{};
    for(UINT i=0;i<3;++i) {
        ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1u,i,0u,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
        parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable={1u,&ranges[i]}; parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants={0u,0u,sizeof(engine::render::ScreenFeedbackUniform)/4};
    parameters[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
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
    rootDesc.NumParameters = 4u;
    rootDesc.pParameters = parameters.data();
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
                feedbackRoot_.ReleaseAndGetAddressOf()))) ||
        !feedbackRoot_) {
        throw std::runtime_error(
            "CreateRootSignature failed for world scene color.");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = feedbackRoot_.Get();
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
                feedbackPipeline_.ReleaseAndGetAddressOf()))) ||
        !feedbackPipeline_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for world scene color.");
    }
#endif
}


void D3D12RenderBackend::endWorldSceneColorPass(const engine::render::ScreenFeedbackFrame& frame) {
    using namespace engine::render;
    validateScreenFeedback(frame);
#if defined(_WIN32)
    if(!worldSceneColorPassActive_) throw std::runtime_error("Screen feedback requires an active world pass");
    const bool editor=editorSurfaceScenePassActive_;
    const UINT sceneIndex=editor ? editorSurfaceTarget_.linearSrvDescriptorIndex : worldSceneColorSrvDescriptorIndex_;
    const int outputWidth=editor?editorSurfaceTarget_.width:width_,outputHeight=editor?editorSurfaceTarget_.height:height_;
    D3D12_CPU_DESCRIPTOR_HANDLE output=editor?editorSurfaceTarget_.displayRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editor)output.ptr+=static_cast<SIZE_T>(frameIndex_)*rtvDescriptorSize_;
    endWorldSceneColorPass();
    if(!feedbackPipeline_)createScreenFeedbackPipeline();
    if(feedbackWidth_!=frame.width || feedbackHeight_!=frame.height) {
        if(feedbackImages_[0])throw std::runtime_error("Screen feedback dimensions must remain fixed for a renderer session");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heapDesc.NumDescriptors=2;
        if(FAILED(device_->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(feedbackRtvHeap_.ReleaseAndGetAddressOf()))))
            throw std::runtime_error("Could not create feedback RTV heap");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width=frame.width;texture.Height=frame.height;texture.DepthOrArraySize=texture.MipLevels=1;
        texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=1;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        for(int i=0;i<2;++i) {
            if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(feedbackImages_[i].ReleaseAndGetAddressOf()))))
                throw std::runtime_error("Could not create screen feedback history");
            auto rtv=feedbackRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=static_cast<SIZE_T>(i)*rtvDescriptorSize_;
            device_->CreateRenderTargetView(feedbackImages_[i].Get(),nullptr,rtv);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=texture.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
            auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=static_cast<SIZE_T>(feedbackSrv_[i])*srvDescriptorSize_;
            device_->CreateShaderResourceView(feedbackImages_[i].Get(),&srv,cpu);
        }
        feedbackWidth_=frame.width;feedbackHeight_=frame.height;
    }
    auto* mask=ensureWorldTextureRaw(frame.maskKey.c_str(),frame.maskRgba,frame.maskWidth,frame.maskHeight,33071,33071,false);
    if(!mask)throw std::runtime_error("Could not upload screen feedback ingredient");
    if(frame.seed) {feedbackSequence_=frame.sequence;feedbackHistory_=0;feedbackDisplayInput_=0;feedbackLast_={};feedbackLast_.count=1;feedbackLast_.passes[0].rectangle={0,0,float(frame.width),float(frame.height)};}
    else if(feedbackSequence_!=frame.sequence)throw std::runtime_error("Screen feedback must be seeded before playback");
    commandList_->SetPipelineState(feedbackPipeline_.Get());commandList_->SetGraphicsRootSignature(feedbackRoot_.Get());
    ID3D12DescriptorHeap* heaps[]={srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    auto bind=[&](UINT slot,UINT index) {auto gpu=srvHeap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=static_cast<UINT64>(index)*srvDescriptorSize_;commandList_->SetGraphicsRootDescriptorTable(slot,gpu);};
    bind(0,sceneIndex);bind(2,mask->descriptorIndex);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto draw=[&](int mode,int input,D3D12_CPU_DESCRIPTOR_HANDLE target,int width,int height,const ScreenFeedbackUpdate& update) {
        bind(1,feedbackSrv_[input]);
        const ScreenFeedbackUniform program{{float(frame.width),float(frame.height),float(mode),float(update.count)},update.passes};
        commandList_->SetGraphicsRoot32BitConstants(3,sizeof(program)/4,&program,0);
        commandList_->OMSetRenderTargets(1,&target,FALSE,nullptr);
        D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT scissor{0,0,width,height};
        commandList_->RSSetViewports(1,&viewport);commandList_->RSSetScissorRects(1,&scissor);
        commandList_->DrawInstanced(3,1,0,0);++frameDrawCalls_;++frameTriangles_;
    };
    auto historyDraw=[&](int mode,int input,int target,const ScreenFeedbackUpdate& update) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=feedbackImages_[target].Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList_->ResourceBarrier(1,&barrier);
        auto rtv=feedbackRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=static_cast<SIZE_T>(target)*rtvDescriptorSize_;
        draw(mode,input,rtv,frame.width,frame.height,update);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commandList_->ResourceBarrier(1,&barrier);
    };
    if(frame.seed)historyDraw(0,1,0,{});
    for(const auto& update : frame.updates) {
        feedbackDisplayInput_=feedbackHistory_;feedbackHistory_=1-feedbackHistory_;feedbackLast_=update;
        historyDraw(1,feedbackDisplayInput_,feedbackHistory_,update);
    }
    if(!frame.seed || !frame.updates.empty())draw(2,feedbackDisplayInput_,output,outputWidth,outputHeight,feedbackLast_);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv=editor?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editor)dsv.ptr+=static_cast<SIZE_T>(frameIndex_)*dsvDescriptorSize_;
    commandList_->OMSetRenderTargets(1,&output,FALSE,&dsv);
    D3D12_VIEWPORT viewport{0,0,float(outputWidth),float(outputHeight),0,1};D3D12_RECT scissor{0,0,outputWidth,outputHeight};
    commandList_->RSSetViewports(1,&viewport);commandList_->RSSetScissorRects(1,&scissor);
#endif
}
