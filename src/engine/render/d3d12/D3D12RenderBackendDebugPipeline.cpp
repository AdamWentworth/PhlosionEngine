#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <stdexcept>

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
using namespace engine::render::d3d12_pipeline_compile;
#endif

void D3D12RenderBackend::createDebugPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "struct VSIn { float2 pos : POSITION; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }";
    static constexpr char kPsSource[] =
        "struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "float4 main(PSIn i) : SV_TARGET { return i.col; }";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (!compileHlslWithCache(kVsSource,
                              sizeof(kVsSource) - 1,
                              "main",
                              "vs_5_0",
                              d3dCompileFlags(),
                              0,
                              vsBlob,
                              errBlob) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for debug VS.");
    }
    errBlob.Reset();
    if (!compileHlslWithCache(kPsSource,
                              sizeof(kPsSource) - 1,
                              "main",
                              "ps_5_0",
                              d3dCompileFlags(),
                              0,
                              psBlob,
                              errBlob) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for debug PS.");
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(debugRootSignature_.ReleaseAndGetAddressOf()))) ||
        !debugRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 debug pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = debugRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = engine::render::parity_contract::kDebugBlendEnabled ? TRUE : FALSE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode =
        engine::render::parity_contract::kWorldCullEnabled ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise =
        engine::render::parity_contract::kWorldFrontFaceClockwise ? FALSE : TRUE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.BackFace = depthStencil.FrontFace;
    pso.DepthStencilState = depthStencil;

    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(debugPipelineState_.ReleaseAndGetAddressOf()))) ||
        !debugPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 debug pipeline.");
    }

    const std::size_t kDebugVertexBufferBytesPerFrame =
        kMaxDebugVertices * sizeof(DebugVertex);
    const std::size_t kBufferBytes =
        kDebugVertexBufferBytesPerFrame * D3D12RenderBackend::kFrameCount;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(debugVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !debugVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 debug vertex buffer.");
    }

    debugVertexBufferGpuAddress_ = debugVertexBuffer_->GetGPUVirtualAddress();
    debugVertexStride_ = sizeof(DebugVertex);
    debugVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    debugVertexBufferBytesPerFrame_ =
        static_cast<UINT>(kDebugVertexBufferBytesPerFrame);
    debugVertexMappedData_ = nullptr;
    void* debugMapped = nullptr;
    D3D12_RANGE debugReadRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &debugReadRange, &debugMapped)) || !debugMapped) {
        throw std::runtime_error("Map failed for D3D12 debug vertex buffer.");
    }
    debugVertexMappedData_ = static_cast<std::uint8_t*>(debugMapped);
#endif
}
