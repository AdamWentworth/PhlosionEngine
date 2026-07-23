#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

#include <stdexcept>

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
using namespace engine::render::d3d12_pipeline_compile;
#endif

void D3D12RenderBackend::createSpritePipeline() {
#if defined(_WIN32)
    if (!device_) return;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = static_cast<UINT>(kMaxSrvDescriptors);
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap_.ReleaseAndGetAddressOf()))) ||
        !srvHeap_) {
        throw std::runtime_error("CreateDescriptorHeap (SRV) failed for D3D12 sprite pipeline.");
    }
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    nextSrvDescriptorIndex_ = 0;
    spriteTextures_.clear();
    worldTextures_.clear();
    worldMaterialDescriptorBlocks_.clear();
    worldSceneMaterialBindingCache_.clear();
    worldSceneMaterialBindingCacheGeneration_ = 0u;
    worldFallbackMaterialDescriptorBlockIndex_ = 0xffffffffu;

    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float2 uSurfaceSize; };"
        "struct VSIn { float4 rect : RECT; float4 uvRect : UVRECT; float4 col : COLOR; uint vertexId : SV_VertexID; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  static const float2 kCorners[6] = {"
        "      float2(0.0f, 0.0f),"
        "      float2(1.0f, 0.0f),"
        "      float2(1.0f, 1.0f),"
        "      float2(0.0f, 0.0f),"
        "      float2(1.0f, 1.0f),"
        "      float2(0.0f, 1.0f)"
        "  };"
        "  float2 corner = kCorners[i.vertexId];"
        "  float2 posPx = i.rect.xy + i.rect.zw * corner;"
        "  float2 ndc;"
        "  ndc.x = (posPx.x / max(uSurfaceSize.x, 1.0f)) * 2.0f - 1.0f;"
        "  ndc.y = 1.0f - (posPx.y / max(uSurfaceSize.y, 1.0f)) * 2.0f;"
        "  o.pos = float4(ndc, 0.0f, 1.0f);"
        "  o.uv = lerp(i.uvRect.xy, i.uvRect.zw, corner);"
        "  o.col = i.col;"
        "  return o;"
        "}";
    static constexpr char kPsSource[] =
        "Texture2D gTex : register(t0);"
        "SamplerState gSamp : register(s0);"
        "struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "float3 linearToSrgb(float3 c) {"
        "  c = max(c, float3(0.0f, 0.0f, 0.0f));"
        "  float3 lo = c * 12.92f;"
        "  float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;"
        "  return lerp(lo, hi, step(float3(0.0031308f, 0.0031308f, 0.0031308f), c));"
        "}"
        "float4 main(PSIn i) : SV_TARGET {"
        "  float4 tex = gTex.Sample(gSamp, i.uv);"
        "  float3 rgb = linearToSrgb(tex.rgb * saturate(i.col.rgb));"
        "  float a = saturate(tex.a * i.col.a);"
        "  return float4(rgb, a);"
        "}";

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
        throw std::runtime_error("D3DCompile failed for sprite VS.");
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
        throw std::runtime_error("D3DCompile failed for sprite PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 2;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = -0.35f;
    sampler.MaxAnisotropy = 16;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for sprite pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(spriteRootSignature_.ReleaseAndGetAddressOf()))) ||
        !spriteRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 sprite pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"RECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"UVRECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = spriteRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
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
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(spritePipelineState_.ReleaseAndGetAddressOf()))) ||
        !spritePipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 sprite pipeline.");
    }

    const std::size_t kSpriteVertexBufferBytesPerFrame =
        kMaxSpriteQuads * sizeof(SpriteInstanceData);
    const std::size_t kBufferBytes =
        kSpriteVertexBufferBytesPerFrame * D3D12RenderBackend::kFrameCount;
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
                                                IID_PPV_ARGS(spriteVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !spriteVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexBufferGpuAddress_ = spriteVertexBuffer_->GetGPUVirtualAddress();
    spriteVertexStride_ = sizeof(SpriteInstanceData);
    spriteVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    spriteVertexBufferBytesPerFrame_ =
        static_cast<UINT>(kSpriteVertexBufferBytesPerFrame);
    spriteVertexMappedData_ = nullptr;
    void* spriteMapped = nullptr;
    D3D12_RANGE spriteReadRange{0, 0};
    if (FAILED(spriteVertexBuffer_->Map(0, &spriteReadRange, &spriteMapped)) || !spriteMapped) {
        throw std::runtime_error("Map failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexMappedData_ = static_cast<std::uint8_t*>(spriteMapped);

    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    SpriteTexture* fallbackBase = ensureWorldTextureRaw(
        "__world_fallback_white_base_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/true);
    SpriteTexture* fallbackNormal = ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/false);
    SpriteTexture* fallbackLinear = ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/false);
    SpriteTexture* fallbackEmissive = ensureWorldTextureRaw(
        "__world_fallback_white_emissive_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/true);
    if (!fallbackBase || !fallbackNormal || !fallbackLinear || !fallbackEmissive) {
        throw std::runtime_error("Failed to create fallback textures for D3D12 world pipeline.");
    }
    worldFallbackTextureDescriptorIndex_ = fallbackBase->descriptorIndex;
    worldFallbackNormalTextureDescriptorIndex_ = fallbackNormal->descriptorIndex;
    worldFallbackMetallicRoughnessTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackOcclusionTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackEmissiveTextureDescriptorIndex_ = fallbackEmissive->descriptorIndex;
    // Keep startup fast: use a cheap 1x1 linear fallback at boot and lazily promote to PMREM.
    worldFallbackEnvTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackEnvTextureReady_ = false;
#endif
}

void D3D12RenderBackend::ensureWorldFallbackEnvTexture() {
#if defined(_WIN32)
    if (worldFallbackEnvTextureReady_) return;

    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    SpriteTexture* fallbackEnv = !neutralPmremAtlas.rgba16f.empty()
        ? ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v2__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            kGlClampToEdge,
            kGlClampToEdge)
        : ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v2__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            kGlClampToEdge,
            kGlClampToEdge,
            /*srgb=*/false);
    if (!fallbackEnv || !fallbackEnv->valid) return;

    worldFallbackEnvTextureDescriptorIndex_ = fallbackEnv->descriptorIndex;
    worldFallbackEnvTextureReady_ = true;
#endif
}

void D3D12RenderBackend::prewarmWorldRenderAssets() {
#if defined(_WIN32)
    std::uint32_t descriptorBlockIndex = 0u;
    float useTexture = 0.0f;
    (void)prepareWorldMaterialDescriptorBlock(
        nullptr,
        /*logPbrBinding=*/false,
        descriptorBlockIndex,
        useTexture);
#endif
}


