#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/render/WorldPlanarCapture.h"
#include "engine/core/Environment.h"
#include "engine/render/WorldLocalReflectionFilter.h"
#include "engine/render/WorldScreenReflectionTiles.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldSurfaceReflection.h"
#include "engine/render/WorldSurfaceComposition.h"

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
cbuffer ScenePost : register(b0) { float bloomStrength; float sceneExposure; };
#define SCENE_EXPOSURE sceneExposure
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
  scene.rgb=sceneDisplayMap(sceneBloom(input.uv,scene.rgb,bloomStrength));
  return float4(sceneDitherDisplay(float3(
      linearToSrgbChannel(scene.r),
      linearToSrgbChannel(scene.g),
      linearToSrgbChannel(scene.b)),input.uv),
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
    postParameter.Constants={0u,0u,2u};
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
    if (FAILED(engine::render::prepareRootSignature(device_.Get(),
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
            &pipeline,
            IID_PPV_ARGS(
                worldSceneColorPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldSceneColorPipelineState_) {
        throw std::runtime_error(
            "CreateGraphicsPipelineState failed for world scene color.");
    }
    // Depth copies from a padded snapshot cannot target a smaller DSV in D3D12.
    // Exact integer texel loads restore only depth, without another allocation.
    static constexpr char depthSource[]=R"HLSL(
Texture2D<float> depthImage : register(t0);
float main(float4 position : SV_POSITION) : SV_DEPTH {
    return depthImage.Load(int3(int2(position.xy),0));
}
)HLSL";
    errors.Reset();
    if(!compileHlslWithCache(depthSource,sizeof(depthSource)-1,"main","ps_5_0",
        d3dCompileFlags(),0,psBlob,errors) || !psBlob)
        throw std::runtime_error("Depth restore shader compilation failed");
    pipeline.PS={psBlob->GetBufferPointer(),psBlob->GetBufferSize()};
    pipeline.DepthStencilState.DepthEnable=TRUE;
    pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    pipeline.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask=0;
    if(FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &pipeline,IID_PPV_ARGS(worldDepthRestorePipeline_.ReleaseAndGetAddressOf()))))
        throw std::runtime_error("Depth restore pipeline creation failed");

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
    texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texture.SampleDesc.Count = 1u;
    texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    worldProbeScratch_.clear();worldProbeSurfaces_.clear();placedProbes.clear();probeResidency.clear();placedProbeDescriptor=0xffffffffu;worldProbeSurfaceKey_.clear();
    worldSurfaceTarget_=nullptr;worldSurfaceActive_=worldSurfaceReady_=worldSurfaceContact_=false;
    worldSurfaceFoliageComposing_=worldSurfaceComposing_=worldSurfaceComposed_=worldSurfaceCompositionFinished_=worldSurfaceCompositionCapture_=false;
    worldHazeRtvHeap_.Reset();worldHazeTarget_=nullptr;worldHazeActive_=false;worldHazeDescriptor_=0xffffffffu;
    for(auto& frame:worldHazeStencil_)frame.clear();
    worldHazeStencilSlot_=0;worldHazeDsvHeap_.Reset();worldHazeStencilActive_=false;
    worldSceneColorPassActive_ = false;
    worldSceneColorTarget_.Reset();
#endif
}

void D3D12RenderBackend::beginWorldSceneColorPass(
    int surfaceWidth,
    int surfaceHeight) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    ID3D12Resource* sceneColorTarget =
        editorSurfaceActive_
            ? editorSurfaceTarget_.linearColor
            : worldSceneColorTarget_.Get();
    if (!recording_ || !commandList_ || !sceneColorTarget ||
        worldSceneColorPassActive_) {
        return;
    }
    clearWorldSurfaceBuffer();
    clearWorldTemporalScene();
    worldBloomStrength_=0;
    worldSceneRadianceExposure_=0;
    worldDiffuseProbes_={};
    worldContact_={};worldContactFilterDescriptors_.fill(0xffffffffu);
    clearWorldClouds();worldWet_={};worldWetDescriptor_=0xffffffffu;worldWetOwner_=0;worldFog_={};worldFogDescriptors_.fill(0xffffffffu);
    clearWorldScreenReflection();
    clearWorldLocalReflection();
    worldPlanarReflection_={};
    worldPlanarDescriptor_=0xffffffffu;

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

bool D3D12RenderBackend::beginWorldContactVisibility(const engine::render::WorldContactUniforms& value) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldShadowActive_ || worldContact_[8][3]!=0)return false;
    worldContact_=value;
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldContactVisibility() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    if(worldContact_[8][3]>=0)return false;
    if(worldSurfaceContact_ && processWorldContactVisibility())return true;
#if defined(_WIN32)
    if(worldContact_[9][3]>.5f && !engine::env::flagEnabled("PHLOSION_LEGACY_CONTACT_VISIBILITY")) {
        if(!worldSurfaceContact_ && !captureWorldDepthSnapshotFor("contact-depth"))return false;
        const auto depth=worldDepthSnapshotDescriptor_;
        const auto surface=std::uint64_t(editorSurfaceActive_);
        const bool history=!engine::env::flagEnabled("PHLOSION_DISABLE_CONTACT_HISTORY") && worldContactHistory_.accepts(worldContact_,frameCounter_,surface,worldScopes_.active());
        worldContact_[9][2]=history?1.f:0.f;
        worldContact_[9][1]=float(frameCounter_%8)*.125f;
        for(int row=0;row<8;++row)worldContact_[14+row]=worldContactHistory_.view[row];
        worldContact_[22]=worldContactHistory_.view[10];
        std::uint32_t normals=0xffffffffu,input=0xffffffffu,prepared=0xffffffffu;
        std::array<float,4> sampling{};
        if(worldSurfaceContact_) normals=worldSurfaceTarget_->planes[1].descriptorIndex;
        else if(!captureWorldRadianceSnapshot("contact-normal",int(worldContact_[8][0]),int(worldContact_[8][1]),normals,sampling))return false;
        const bool result=engine::render::filterWorldContact(*this,worldContact_,[&](int stage){
            worldContactFilterDescriptors_={input,(stage==2 || stage==3)?prepared:(history?worldContactPreviousVisibility_:normals),
                stage==4 && history?worldContactPreviousDepth_:normals};
        },[&](int stage,int width,int height){
            const char* purpose=stage==5?"contact-prepared-depth":stage==2?"contact-raw":stage==3?"contact-spatial":"contact-history";
            if(!captureWorldRadianceSnapshot(purpose,width,height,stage==5?prepared:input,sampling))return false;
            worldContact_[23]=sampling;return true;
        });
        if(!result){clearWorldContactVisibility();return false;}
        worldContactPreviousVisibility_=input;worldContactPreviousDepth_=depth;
        worldContactHistory_={worldContact_,frameCounter_,surface,worldScopes_.active(),true};
        return true;
    }
#endif
    worldContact_[9][3]=0;
    if(!captureWorldDepthSnapshot())return false;
    worldContact_[8][3]=1;
    return clearWorldDepth();
}
void D3D12RenderBackend::clearWorldContactVisibility() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

    worldContact_={};
#if defined(_WIN32)
    worldContactHistory_={};worldContactFilterDescriptors_.fill(0xffffffffu);worldContactPreviousVisibility_=worldContactPreviousDepth_=0xffffffffu;
#endif
}

bool D3D12RenderBackend::beginWorldLocalReflection(const engine::render::WorldLocalReflectionProbe& probe) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldShadowActive_ || worldSceneRadianceExposure_<=0 ||
       worldPlanarReflection_[4][3]!=0 || worldScreenReflection_[8][3]!=0 || worldContact_[8][3]!=0 || worldLocalReflection_[0][3]!=0)return false;
    worldLocalReflection_=engine::render::packWorldLocalReflection(probe);
    worldLocalReflectionPrepared_=probe.sampling==engine::render::WorldLocalReflectionSampling::PreparedRadiance;
    worldLocalReflectionFace_=0;return true;
#else
    (void)probe;return false;
#endif
}
bool D3D12RenderBackend::endWorldLocalReflectionFace() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldLocalReflection_[0][3]>=0)return false;
    const int size=int(worldLocalReflection_[2][3]);
    if(!captureWorldRadianceSnapshot("local-reflection",size,size,worldLocalReflectionDescriptor_,worldLocalReflection_[3],worldLocalReflectionFace_))return false;
    if(++worldLocalReflectionFace_==6 && !worldLocalReflectionPrepared_)worldLocalReflection_[0][3]=-3;
    if(worldLocalReflectionFace_==(worldLocalReflectionPrepared_?6:engine::render::worldLocalReflectionViewCount))return engine::render::filterWorldLocalReflection(*this,worldLocalReflection_,worldLocalReflectionPrepared_,[&](int extent,int face,int level,int base,bool clearScene,bool batchFaces){
        std::array<float,4> sampling{};
        return captureWorldRadianceSnapshot("local-reflection",extent,extent,worldLocalReflectionDescriptor_,sampling,face,level,base,0,0,0,clearScene,batchFaces);
    },editorSurfaceActive_?editorSurfaceTarget_.width:width_,editorSurfaceActive_?editorSurfaceTarget_.height:height_);
    return true;
#else
    return false;
#endif
}
void D3D12RenderBackend::clearWorldLocalReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

    worldLocalReflection_={};worldLocalReflectionFace_=0;
#if defined(_WIN32)
    worldLocalReflectionDescriptor_=0xffffffffu;
#endif
}

bool D3D12RenderBackend::beginWorldScreenReflection(const engine::render::WorldScreenReflectionUniforms& value) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldShadowActive_ || worldSceneRadianceExposure_<=0 ||
        worldPlanarReflection_[4][3]<0 || worldScreenReflection_[8][3]!=0 || value[8][3]!=-1)return false;
    const int width=editorSurfaceActive_?editorSurfaceTarget_.width:width_;
    const int height=editorSurfaceActive_?editorSurfaceTarget_.height:height_;
    if(value[9][3]>.5f && (value[8][0]!=width || value[8][1]!=height))return false;
    worldScreenReflection_=value;
    if(engine::render::worldScreenReflectionCanReuseContactDepth(value,worldContact_) &&
       worldContactHistory_.frame==frameCounter_ &&
       worldDepthSnapshotDescriptor_==worldContactPreviousDepth_ &&
       !restoreWorldDepthSnapshot())return false;
    return true;
#else
    (void)value;return false;
#endif
}
bool D3D12RenderBackend::endWorldScreenReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldScreenReflection_[8][3]>=0 || !captureWorldDepthSnapshot())return false;
    auto& screen=worldScreenReflection_;
    const auto layout=engine::render::worldScreenReflectionTileLayout(int(screen[8][0]),int(screen[8][1]));
    const auto quality=screen[10];
    const bool tiles=(quality[2]<=0 || quality[2]>=4096) && layout[2]<=8192 &&
        !engine::env::flagEnabled("PHLOSION_DISABLE_SCREEN_DEPTH_TILES");
    if(!captureWorldRadianceSnapshot("screen-reflection",int(screen[8][0]),int(screen[8][1]),
        worldScreenReflectionDescriptor_,screen[10],-1,0,tiles?layout[2]:0))return false;
    screen[10][2]=quality[2];screen[10][3]=quality[3];
    if(tiles && !engine::render::buildWorldScreenReflectionTiles(*this,screen,[&](int width,int height,int reserve,int offset){
        std::array<float,4> sampling{};
        return captureWorldRadianceSnapshot("screen-reflection",width,height,worldScreenReflectionDescriptor_,sampling,-1,0,reserve,offset);
    }))return false;
    worldScreenReflection_[8][3]=engine::env::flagEnabled("PHLOSION_DIAG_SKIP_SCREEN_TRACE")?.25f:1.f;
    if(screen[9][3]>.5f && !restoreWorldDepthSnapshot())return false;
    return true;
#else
    return false;
#endif
}
void D3D12RenderBackend::clearWorldScreenReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

    worldScreenReflection_={};
#if defined(_WIN32)
    worldScreenReflectionDescriptor_=0xffffffffu;
#endif
}

bool D3D12RenderBackend::captureWorldDepthSnapshot() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    return captureWorldDepthSnapshotFor("scene-depth");
}
bool D3D12RenderBackend::captureWorldDepthSnapshotFor(const char* purpose) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if (!worldSceneColorPassActive_ || !commandList_) return false;
    ID3D12Resource* source = editorSurfaceActive_ ? editorSurfaceTarget_.depthResource : depthBuffers_[frameIndex_].Get();
    if (!source) return false;
    auto description = source->GetDesc();
    if (description.Format != DXGI_FORMAT_D32_FLOAT || description.SampleDesc.Count != 1) return false;
    // Keep in-flight views independent, like the HDR snapshots. Power-of-two
    // buckets bound each frame's pool to 4/3 of its largest image.
    const auto bucket = [](UINT64 value) { UINT64 result = 256; while (result < value) result *= 2; return result; };
    const auto sourceWidth = description.Width;
    const auto sourceHeight = description.Height;
    if (sourceWidth > 8192 || sourceHeight > 8192) return false;
    description.Width = bucket(std::max<UINT64>(sourceWidth, sourceHeight));
    description.Height = static_cast<UINT>(description.Width);
    const std::string key = std::string(purpose)+"/" + std::to_string(frameIndex_) + "/" + std::to_string(description.Width) + "x" + std::to_string(description.Height);
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

bool D3D12RenderBackend::beginWorldLocalHaze(int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldHazeActive_ || width<=0 || height<=0 || width>2048 || height>2048)return false;
    UINT size=64;while(size<static_cast<UINT>(std::max(width,height)))size*=2;
    const std::string key="local-haze/"+std::to_string(frameIndex_)+"/"+std::to_string(size);
    auto found=worldTextures_.find(key);
    if(found==worldTextures_.end()) {
        if(nextSrvDescriptorIndex_>=kMaxSrvDescriptors)throw std::runtime_error("Haze descriptor budget exhausted");
        SpriteTexture target;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=desc.Height=size;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear,IID_PPV_ARGS(target.resource.GetAddressOf()))))throw std::runtime_error("Haze image allocation failed");
        target.descriptorIndex=nextSrvDescriptorIndex_++;
        target.srvDesc.Format=desc.Format;target.srvDesc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        target.srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        target.srvDesc.Texture2D.MipLevels=1;target.hasSrvDesc=target.valid=true;
        auto handle=srvHeap_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=SIZE_T(target.descriptorIndex)*srvDescriptorSize_;
        device_->CreateShaderResourceView(target.resource.Get(),&target.srvDesc,handle);
        found=worldTextures_.emplace(key,std::move(target)).first;
    }
    if(!worldHazeRtvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=1;
        if(FAILED(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(worldHazeRtvHeap_.GetAddressOf()))))
            throw std::runtime_error("Haze RTV allocation failed");
    }
    worldHazeTarget_=found->second.resource.Get();worldHazeDescriptor_=found->second.descriptorIndex;
    const auto rtv=worldHazeRtvHeap_->GetCPUDescriptorHandleForHeapStart();device_->CreateRenderTargetView(worldHazeTarget_,nullptr,rtv);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={worldHazeTarget_,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
    commandList_->ResourceBarrier(1,&barrier);commandList_->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    const float black[4]{};commandList_->ClearRenderTargetView(rtv,black,0,nullptr);
    worldHazeActive_=true;return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldLocalHaze() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldHazeActive_)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={worldHazeTarget_,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    commandList_->ResourceBarrier(1,&barrier);
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_) {rtv.ptr+=SIZE_T(kFrameCount)*rtvDescriptorSize_;dsv.ptr+=SIZE_T(frameIndex_)*dsvDescriptorSize_;}
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);worldHazeActive_=false;return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::beginWorldPlanarReflection(const engine::render::WorldPlanarReflectionView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if (!worldSceneColorPassActive_ || worldShadowActive_ || worldPlanarReflection_[4][3]<0 ||
        view.width<=0 || view.heightPixels<=0) return false;
    worldPlanarReflectionView_=view;
    worldPlanarReflection_=engine::render::packWorldPlanarReflection(view);
    worldPlanarScissor_=engine::render::worldPlanarCaptureScissor(view);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv=editorSurfaceActive_ ? editorSurfaceTarget_.linearRtv : rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    if (!editorSurfaceActive_) rtv.ptr+=static_cast<SIZE_T>(kFrameCount)*rtvDescriptorSize_;
    const float black[]{0,0,0,1};
    commandList_->ClearRenderTargetView(rtv,black,0,nullptr);
    clearWorldDepth();
    return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::endWorldPlanarReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldPlanarReflection_[4][3]>=0)return false;
    if(!captureWorldRadianceSnapshot("planar",worldPlanarReflectionView_.width,worldPlanarReflectionView_.heightPixels,worldPlanarDescriptor_,worldPlanarReflection_[5]))return false;
    if(worldSurfacePlanar_)clearWorldSurfaceBuffer();
    worldPlanarReflection_[4][3]=1;
    return true;
#else
    return false;
#endif
}

#if defined(_WIN32)
bool D3D12RenderBackend::captureWorldRadianceSnapshot(const char* purpose,int width,int height,
    std::uint32_t& descriptor,std::array<float,4>& sampling,int atlasFace,int atlasLevel,int atlasResolution,int offsetY,int offsetX,int residentExtent,bool clearScene,bool batchFaces) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    if (!worldSceneColorPassActive_) return false;
    auto* source=editorSurfaceActive_ ? editorSurfaceTarget_.linearColor : worldSceneColorTarget_.Get();
    auto desc=source->GetDesc();
    if ((batchFaces?3*width:width)>static_cast<int>(desc.Width) || (batchFaces?2*height:height)>static_cast<int>(desc.Height))
        throw std::runtime_error("Reflection capture exceeds scene target");
    UINT size=256; while (size<static_cast<UINT>(std::max({width,height,atlasResolution})*(atlasFace>=0?3:1))) size*=2;
    while(size<static_cast<unsigned>(residentExtent))size*=2;
    if (size>8192) throw std::runtime_error("Reflection target exceeds size limit");
    if(offsetY<0 || offsetY+height>static_cast<int>(size))throw std::runtime_error("Reflection copy offset exceeds target");
    const bool compact=std::string_view(purpose)=="placed-reflection";
    const std::string key=std::string(purpose)+"/"+(residentExtent?std::string("resident"):std::to_string(frameIndex_))+"/"+std::to_string(size);
    auto found=worldTextures_.find(key);
    if (found==worldTextures_.end()) {
        if (nextSrvDescriptorIndex_>=kMaxSrvDescriptors) throw std::runtime_error("Reflection descriptor budget exhausted");
        SpriteTexture target;
        desc.Width=desc.Height=size; desc.Flags=compact?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        if(compact)desc.Format=DXGI_FORMAT_R11G11B10_FLOAT;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask=heap.VisibleNodeMask=1;
        if (FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(target.resource.GetAddressOf()))))
            throw std::runtime_error("Reflection image allocation failed");
        target.descriptorIndex=nextSrvDescriptorIndex_++;
        target.srvDesc.Format=desc.Format; target.srvDesc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        target.srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        target.srvDesc.Texture2D.MipLevels=1; target.hasSrvDesc=target.valid=true;
        auto handle=srvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr+=static_cast<SIZE_T>(target.descriptorIndex)*srvDescriptorSize_;
        device_->CreateShaderResourceView(target.resource.Get(),&target.srvDesc,handle);
        found=worldTextures_.emplace(key,std::move(target)).first;
    }
    auto& target=found->second;
    const auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};
        commandList_->ResourceBarrier(1,&barrier);
    };
    if(compact) {
        if(!worldProbeCopyRoot_) {
            std::array<D3D12_DESCRIPTOR_RANGE,2> ranges{};
            std::array<D3D12_ROOT_PARAMETER,3> params{};
            for(unsigned i=0;i<2;++i) {
                ranges[i].RangeType=i?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[i].NumDescriptors=1;ranges[i].BaseShaderRegister=0;
                params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};
            }
            params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,6};
            D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=3;root.pParameters=params.data();
            Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
            if(FAILED(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,blob.GetAddressOf(),error.GetAddressOf())) ||
               FAILED(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(worldProbeCopyRoot_.GetAddressOf()))))
                throw std::runtime_error("Probe radiance copy root");
            const char* shader=R"SHADER(
Texture2D<float4> sourceColor : register(t0);
RWTexture2D<float3> radiance : register(u0);
cbuffer CopyRegion : register(b0) {uint2 sourceOffset;uint2 destinationOffset;uint2 extent;}
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=extent))return;
    float3 color=sourceColor.Load(int3(id.xy+sourceOffset,0)).rgb;
    color=float3(isfinite(color.x)?color.x:0,isfinite(color.y)?color.y:0,isfinite(color.z)?color.z:0);
    radiance[id.xy+destinationOffset]=clamp(color,0.0,64512.0);
}
)SHADER";
            Microsoft::WRL::ComPtr<ID3DBlob> code;
            if(!compileHlslWithCache(shader,std::strlen(shader),"main","cs_5_0",d3dCompileFlags(),0,code,error))
                throw std::runtime_error("Probe radiance copy shader: "+d3dCompileErrorMessage(error.Get()));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=worldProbeCopyRoot_.Get();
            pipeline.CS={code->GetBufferPointer(),code->GetBufferSize()};
            if(FAILED(engine::render::prepareComputePipeline(device_.Get(), &pipeline,IID_PPV_ARGS(worldProbeCopyPipeline_.GetAddressOf()))))
                throw std::runtime_error("Probe radiance copy pipeline");
        }
        if(worldProbeCopyUav_==0xffffffffu) {
            if(nextSrvDescriptorIndex_>=kMaxSrvDescriptors)throw std::runtime_error("Probe radiance UAV budget");
            worldProbeCopyUav_=nextSrvDescriptorIndex_++;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R11G11B10_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(worldProbeCopyUav_)*srvDescriptorSize_;
            device_->CreateUnorderedAccessView(target.resource.Get(),nullptr,&uav,cpu);
        }
        transition(source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(target.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList_->SetComputeRootSignature(worldProbeCopyRoot_.Get());commandList_->SetPipelineState(worldProbeCopyPipeline_.Get());
        ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
        auto input=srvHeap_->GetGPUDescriptorHandleForHeapStart(),output=input;
        input.ptr+=UINT64(editorSurfaceActive_?editorSurfaceTarget_.linearSrvDescriptorIndex:worldSceneColorSrvDescriptorIndex_)*srvDescriptorSize_;
        output.ptr+=UINT64(worldProbeCopyUav_)*srvDescriptorSize_;
        commandList_->SetComputeRootDescriptorTable(0,input);commandList_->SetComputeRootDescriptorTable(1,output);
        for(int face=0;face<(batchFaces?6:1);++face) {
            const auto offset=engine::render::worldPlacedReflectionAtlasOffset(atlasFace+face,atlasLevel,atlasResolution);
            const UINT data[]{UINT(batchFaces?(face%3)*width:0),UINT(batchFaces?(face/3)*height:0),
                UINT(offset[0]+offsetX),UINT(offset[1]+offsetY),UINT(width),UINT(height)};
            commandList_->SetComputeRoot32BitConstants(2,6,data,0);commandList_->Dispatch((width+7)/8,(height+7)/8,1);
        }
        transition(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(target.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    } else {
    transition(source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(target.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=source; from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=target.resource.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    for(int face=0;face<(batchFaces?6:1);++face) {
        const UINT x=batchFaces?(face%3)*width:0,y=batchFaces?(face/3)*height:0;
        const D3D12_BOX box{x,y,0,x+static_cast<UINT>(width),y+static_cast<UINT>(height),1};
        const auto atlasOffset=engine::render::worldLocalReflectionAtlasOffset(atlasFace+face,width,atlasLevel,atlasResolution);
        commandList_->CopyTextureRegion(&to,atlasOffset[0]+offsetX,atlasOffset[1]+offsetY,0,&from,&box);
    }
    transition(source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    auto rtv=editorSurfaceActive_ ? editorSurfaceTarget_.linearRtv : rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    if (!editorSurfaceActive_) rtv.ptr+=static_cast<SIZE_T>(kFrameCount)*rtvDescriptorSize_;
    if(clearScene) {
        const float black[]{0,0,0,1};commandList_->ClearRenderTargetView(rtv,black,0,nullptr);
        clearWorldDepth();
    }
    descriptor=target.descriptorIndex;
    sampling={static_cast<float>(width)/size,static_cast<float>(height)/size,1,.5f/static_cast<float>(std::min(width,height))};
    return true;
}
#endif

bool D3D12RenderBackend::restoreWorldDepthSnapshot() {
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || !commandList_ || !worldDepthRestorePipeline_ ||
       worldDepthSnapshotDescriptor_==0xffffffffu)return false;
    const int width=worldPlanarReflection_[4][3]<0?worldPlanarReflectionView_.width:editorSurfaceActive_?editorSurfaceTarget_.width:width_;
    const int height=worldPlanarReflection_[4][3]<0?worldPlanarReflectionView_.heightPixels:editorSurfaceActive_?editorSurfaceTarget_.height:height_;
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_) {
        rtv.ptr+=static_cast<SIZE_T>(kFrameCount)*rtvDescriptorSize_;
        dsv.ptr+=static_cast<SIZE_T>(frameIndex_)*dsvDescriptorSize_;
    }
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    const D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};
    const D3D12_RECT scissor{0,0,width,height};
    commandList_->RSSetViewports(1,&viewport);commandList_->RSSetScissorRects(1,&scissor);
    commandList_->SetPipelineState(worldDepthRestorePipeline_.Get());
    commandList_->SetGraphicsRootSignature(worldSceneColorRootSignature_.Get());
    ID3D12DescriptorHeap* heaps[]={srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    auto srv=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    srv.ptr+=static_cast<UINT64>(worldDepthSnapshotDescriptor_)*srvDescriptorSize_;
    commandList_->SetGraphicsRootDescriptorTable(0,srv);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3,1,0,0);
    ++frameDrawCalls_;++frameTriangles_;
    return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::clearWorldDepth() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if (worldShadowActive_ || !worldSceneColorPassActive_ || !commandList_) return false;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    if (editorSurfaceActive_) dsv = editorSurfaceTarget_.depthDsv;
    else if (dsvHeap_) {
        dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(frameIndex_) * dsvDescriptorSize_;
    }
    if (!dsv.ptr) return false;
    worldSurfaceComposed_=false;
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0u, nullptr);
    return true;
#else
    return false;
#endif
}

void D3D12RenderBackend::endWorldSceneColorPass() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    resolveWorldTemporalScene();
    clearWorldSurfaceBuffer();
    engine::render::RenderPhaseScope phase(*this,"post-compose");
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
    if(worldTemporalOutput_!=0xffffffffu)sceneColorDescriptorIndex=worldTemporalOutput_;
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
    const float post[]{worldDisplayReady_?0.f:worldBloomStrength_,worldDisplayReady_?-1.f:worldSceneRadianceExposure_};
    commandList_->SetGraphicsRoot32BitConstants(1u,2u,post,0u);
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

    const auto pixelSource = engine::render::injectSceneBloom(engine::render::screenFeedbackHlslFs,true);
    const char* kPsSource = pixelSource.c_str();

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
    if (FAILED(engine::render::prepareRootSignature(device_.Get(),
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
    if (FAILED(engine::render::prepareGraphicsPipeline(device_.Get(),
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
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    using namespace engine::render;
    validateScreenFeedback(frame);
#if defined(_WIN32)
    if(!worldSceneColorPassActive_) throw std::runtime_error("Screen feedback requires an active world pass");
    const bool editor=editorSurfaceScenePassActive_;
    resolveWorldTemporalScene();
    const UINT sceneIndex=worldTemporalOutput_!=0xffffffffu?worldTemporalOutput_:
        editor ? editorSurfaceTarget_.linearSrvDescriptorIndex : worldSceneColorSrvDescriptorIndex_;
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
        const ScreenFeedbackUniform program{{float(frame.width),float(frame.height),float(mode),float(update.count)},update.passes,{worldDisplayReady_?0.f:worldBloomStrength_,worldDisplayReady_?-1.f:worldSceneRadianceExposure_,0,0}};
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

bool D3D12RenderBackend::beginWorldSurfaceBuffer(const float* matrix,int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(width<=0 || height<=0)return false;
    const auto view=engine::render::packWorldContactVisibility(matrix,width,height,.8f,.015f);
    const int targetWidth=editorSurfaceActive_?editorSurfaceTarget_.width:width_;
    const int targetHeight=editorSurfaceActive_?editorSurfaceTarget_.height:height_;
    if(!recording_ || !worldSceneColorPassActive_ || worldSurfaceActive_ || worldHazeActive_ || worldShadowActive_ ||
       worldContact_[8][3]!=0 || worldScreenReflection_[8][3]!=0 ||
       worldLocalReflection_[0][3]<0 || width>targetWidth || height>targetHeight ||
       (worldProbeSurfaceKey_.empty() && !(worldPlanarReflection_[4][3]<0) && (width!=targetWidth || height!=targetHeight)))return false;
    const bool planar=worldPlanarReflection_[4][3]<0;
    if(planar && (width!=worldPlanarReflectionView_.width || height!=worldPlanarReflectionView_.heightPixels ||
       !std::equal(worldPlanarReflectionView_.viewProjection.begin(),worldPlanarReflectionView_.viewProjection.end(),matrix)))return false;
    worldSurfacePlanar_=planar;
    worldClusteredView_={};
    worldSurfaceComposed_=worldSurfaceCompositionFinished_=worldSurfaceCompositionCapture_=false;
    worldSurfaceReady_=false;worldMotionWords_={{}};worldMotionGpu_=0;
    if(worldSurfaceFrame_!=frameCounter_){worldSurfaceFrame_=frameCounter_;worldSurfaceSlot_=0;worldSurfaceAllocationSlots_={};}
    // Admit two scene views plus their cold cube faces during a handoff/prewarm.
    if(worldSurfaceSlot_>=32)throw std::runtime_error("Surface captures per frame exceeded");
    const std::string key=worldProbeSurfaceKey_.empty()
        ?(planar?std::string("planar/"):std::string())+std::to_string(frameIndex_)+"/"+std::to_string(worldSurfaceAllocationSlots_[planar?1:0]++):worldProbeSurfaceKey_;
    ++worldSurfaceSlot_;
    auto found=worldSurfaceTargets_.find(key);
    const bool fresh=found==worldSurfaceTargets_.end();
    if(fresh || found->second.planes[0].resource->GetDesc().Width!=static_cast<UINT64>(width) || found->second.planes[0].resource->GetDesc().Height!=static_cast<UINT>(height)) {
        if(!fresh)waitForGpu();
        if(fresh && nextSrvDescriptorIndex_+7>kMaxSrvDescriptors)
            throw std::runtime_error("Surface buffer budget exhausted");
        SurfaceTarget target;target.probeScratch=!worldProbeSurfaceKey_.empty();
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeap{};rtvHeap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtvHeap.NumDescriptors=6;
        if(FAILED(device_->CreateDescriptorHeap(&rtvHeap,IID_PPV_ARGS(target.rtvs.GetAddressOf()))))
            throw std::runtime_error("Surface RTV allocation failed");
        for(unsigned i=0;i<6;++i) {
            auto& plane=target.planes[i];
            const auto scratchKey=std::to_string(width)+"/"+std::to_string(i);
            const bool shared=!worldProbeSurfaceKey_.empty() && i<3;
            const auto scratch=worldProbeScratch_.find(scratchKey);
            if(shared && scratch!=worldProbeScratch_.end()) {
                plane=scratch->second;
                auto rtv=target.rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(i)*rtvDescriptorSize_;
                device_->CreateRenderTargetView(plane.resource.Get(),nullptr,rtv);
                continue;
            }
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
            desc.Format=engine::render::d3d12_internal::kWorldSurfaceFormats[i];desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
            if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clear,IID_PPV_ARGS(plane.resource.GetAddressOf()))))throw std::runtime_error("Surface image allocation failed");
            plane.descriptorIndex=fresh?nextSrvDescriptorIndex_++:found->second.planes[i].descriptorIndex;
            plane.srvDesc.Format=desc.Format;plane.srvDesc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            plane.srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            plane.srvDesc.Texture2D.MipLevels=1;plane.hasSrvDesc=plane.valid=true;
            auto handle=srvHeap_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=SIZE_T(plane.descriptorIndex)*srvDescriptorSize_;
            device_->CreateShaderResourceView(plane.resource.Get(),&plane.srvDesc,handle);
            auto rtv=target.rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(i)*rtvDescriptorSize_;
            device_->CreateRenderTargetView(plane.resource.Get(),nullptr,rtv);
            if(shared)worldProbeScratch_.emplace(scratchKey,plane);
        }
        if(!worldProbeSurfaceKey_.empty() || planar) {
            auto& depth=target.probeDepth;
            const auto depthKey=std::to_string(width)+"/depth";
            const bool shared=!worldProbeSurfaceKey_.empty();
            const auto scratch=worldProbeScratch_.find(depthKey);
            if(shared && scratch!=worldProbeScratch_.end())depth=scratch->second;
            else {
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
            desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
            if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(depth.resource.GetAddressOf()))))
                throw std::runtime_error("Probe depth allocation failed");
            depth.descriptorIndex=fresh?nextSrvDescriptorIndex_++:found->second.probeDepth.descriptorIndex;
            depth.srvDesc.Format=DXGI_FORMAT_R32_FLOAT;depth.srvDesc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            depth.srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depth.srvDesc.Texture2D.MipLevels=1;depth.hasSrvDesc=depth.valid=true;
            auto srv=srvHeap_->GetCPUDescriptorHandleForHeapStart();srv.ptr+=SIZE_T(depth.descriptorIndex)*srvDescriptorSize_;
            device_->CreateShaderResourceView(depth.resource.Get(),&depth.srvDesc,srv);
            if(shared)worldProbeScratch_.emplace(depthKey,depth);
            }
            D3D12_DESCRIPTOR_HEAP_DESC dsv{};dsv.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;dsv.NumDescriptors=1;
            if(FAILED(device_->CreateDescriptorHeap(&dsv,IID_PPV_ARGS(target.probeDsv.GetAddressOf()))))
                throw std::runtime_error("Probe DSV allocation failed");
            D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=DXGI_FORMAT_D32_FLOAT;depthView.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depth.resource.Get(),&depthView,target.probeDsv->GetCPUDescriptorHandleForHeapStart());
        }
        if(fresh)found=worldSurfaceTargets_.emplace(key,std::move(target)).first;
        else found->second=std::move(target);
    }
    worldSurfaceTarget_=&found->second;worldSurfaceView_=view;worldSurfaceTarget_->view=view;
    std::array<D3D12_RESOURCE_BARRIER,6> barriers{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,6> rtvs{};
    for(unsigned i=0;i<6;++i) {
        barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition={worldSurfaceTarget_->planes[i].resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
        rtvs[i]=worldSurfaceTarget_->rtvs->GetCPUDescriptorHandleForHeapStart();rtvs[i].ptr+=SIZE_T(i)*rtvDescriptorSize_;
    }
    commandList_->ResourceBarrier(6,barriers.data());
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_)dsv.ptr+=SIZE_T(frameIndex_)*dsvDescriptorSize_;
    if(worldSurfaceTarget_->probeDepth.resource) {
        dsv=worldSurfaceTarget_->probeDsv->GetCPUDescriptorHandleForHeapStart();
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={worldSurfaceTarget_->probeDepth.resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE};
        commandList_->ResourceBarrier(1,&barrier);
    }
    commandList_->OMSetRenderTargets(6,rtvs.data(),FALSE,&dsv);
    const float black[4]{};
    for(auto rtv:rtvs)commandList_->ClearRenderTargetView(rtv,black,0,nullptr);
    commandList_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    worldSurfaceActive_=true;return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSurfaceActive_)return false;
    std::array<D3D12_RESOURCE_BARRIER,6> barriers{};
    for(unsigned i=0;i<6;++i) {
        barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition={worldSurfaceTarget_->planes[i].resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    }
    commandList_->ResourceBarrier(6,barriers.data());
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_) {rtv.ptr+=SIZE_T(kFrameCount)*rtvDescriptorSize_;dsv.ptr+=SIZE_T(frameIndex_)*dsvDescriptorSize_;}
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    worldSurfaceActive_=false;
    if(worldSurfaceTarget_->probeDepth.resource) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={worldSurfaceTarget_->probeDepth.resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        commandList_->ResourceBarrier(1,&barrier);
        worldSurfaceDepth_=worldSurfaceTarget_->probeDepth.descriptorIndex;
    } else {
        if(!captureWorldDepthSnapshotFor("surface-depth"))return false;
        worldSurfaceDepth_=worldDepthSnapshotDescriptor_;
    }
    worldSurfaceReady_=true;
    return clearWorldDepth();
#else
    return false;
#endif
}
void D3D12RenderBackend::clearWorldSurfaceBuffer() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    worldSurfaceFoliageComposing_=worldSurfaceComposing_=worldSurfaceComposed_=worldSurfaceCompositionFinished_=worldSurfaceCompositionCapture_=false;
#endif
#if defined(_WIN32)
    worldClusteredDescriptors_.fill(0xffffffffu);worldStoredSurfaceDescriptors_.fill(0xffffffffu);
    if(worldSurfaceActive_)endWorldSurfaceBuffer();
    worldClusteredView_={};
    worldSurfaceComposed_=worldSurfaceCompositionFinished_=worldSurfaceCompositionCapture_=false;
    worldProbeSurfaces_.cancel();worldProbeSurfaceKey_.clear();
    if(worldScreenReflection_[10][3]==-8)worldScreenReflection_[10][3]=0;
    worldSurfacePlanar_=false;
    worldSurfaceReady_=false;worldSurfaceContact_=false;worldSurfaceTarget_=nullptr;
    worldSurfaceDepth_=0xffffffffu;worldSurfaceView_={};
#endif
}
bool D3D12RenderBackend::resolveWorldSurfaceContact(const engine::render::WorldContactUniforms& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!worldSceneColorPassActive_ || !worldSurfaceReady_ || worldSurfaceActive_ ||
       !engine::render::surfaceContactViewMatches(worldSurfaceView_,view) ||
       engine::env::flagEnabled("PHLOSION_LEGACY_CONTACT_VISIBILITY") || !beginWorldContactVisibility(view))return false;
    worldContact_[13][3]=1; // Prevent history reuse across geometric/mapped-normal paths.
    worldDepthSnapshotDescriptor_=worldSurfaceDepth_;worldSurfaceContact_=true;
    try {const bool ok=endWorldContactVisibility();worldSurfaceContact_=false;return ok;}
    catch(...) {worldSurfaceContact_=false;clearWorldContactVisibility();throw;}
#else
    return false;
#endif
}
bool D3D12RenderBackend::restoreWorldSurfaceDepth(const engine::render::WorldContactUniforms& view) {
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    const bool planar=worldPlanarReflection_[4][3]<0;
    const int width=planar?worldPlanarReflectionView_.width:editorSurfaceActive_?editorSurfaceTarget_.width:width_;
    const int height=planar?worldPlanarReflectionView_.heightPixels:editorSurfaceActive_?editorSurfaceTarget_.height:height_;
    if(worldSurfacePlanar_!=planar || view[8][0]!=width || view[8][1]!=height)return false;
    if(!recording_ || !worldSceneColorPassActive_ || !worldSurfaceReady_ || worldSurfaceActive_ ||
       worldShadowActive_ || worldHazeActive_ || !worldProbeSurfaceKey_.empty() ||
       worldLocalReflection_[0][3]<0 ||
       !engine::render::surfaceContactViewMatches(worldSurfaceView_,view))return false;
    engine::render::RenderPhaseScope phase(*this,"surface-depth-reuse");
    const auto previous=worldDepthSnapshotDescriptor_;
    worldDepthSnapshotDescriptor_=worldSurfaceDepth_;
    try {
        const bool result=restoreWorldDepthSnapshot();
        worldDepthSnapshotDescriptor_=previous;return result;
    } catch(...) {worldDepthSnapshotDescriptor_=previous;throw;}
#else
    return false;
#endif
}
bool D3D12RenderBackend::beginWorldSurfaceCompositionCapture(const float* matrix,int width,int height,
        const engine::render::WorldClusteredLightingView& lighting) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!beginWorldSurfaceBuffer(matrix,width,height))return false;
    worldCompositionLighting=lighting;worldSurfaceCompositionCapture_=true;return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::beginWorldSurfaceComposition(const engine::render::WorldContactUniforms& view,
        const engine::render::WorldClusteredLightingView& lighting) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!worldSurfaceCompositionCapture_ || worldSurfaceCompositionFinished_ || worldSceneRadianceExposure_<=0 || worldSurfaceComposing_ || worldSurfaceComposed_ || worldClusteredView_[4][3]<.5f ||
       (worldDiffuseProbes_[0][3]>1.5f && worldIndirectReceiver_==0xffffffffu) ||
       !engine::render::worldSurfaceCompositionLightingMatches(worldCompositionLighting,lighting) ||
       !restoreWorldSurfaceDepth(view))return false;
    engine::render::RenderPhaseScope phase(*this,"surface-composition");
    const auto contact=worldContact_;
    std::copy_n(view.begin(),8,worldContact_.begin());
    worldSurfaceComposing_=true;
    try {
        engine::render::drawWorldSurfaceReflectionResolve(*this,int(view[8][0]),int(view[8][1]),&lighting,true);
        if(std::any_of(worldMaterialProfile_.modes.begin(),worldMaterialProfile_.modes.end(),
                [](const auto& mode){return mode.foliageComposition;})) {
            worldSurfaceFoliageComposing_=true;
            engine::render::drawWorldSurfaceReflectionResolve(*this,int(view[8][0]),int(view[8][1]),&lighting,true);
        }
        worldSurfaceFoliageComposing_=worldSurfaceComposing_=false;
        worldSurfaceComposed_=worldSurfaceCompositionFinished_=true;worldContact_=contact;
        return true;
    } catch(...) {worldSurfaceFoliageComposing_=worldSurfaceComposing_=false;worldContact_=contact;throw;}
#else
    return false;
#endif
}
void D3D12RenderBackend::endWorldSurfaceComposition() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    worldSurfaceComposed_=false;
#endif
}
bool D3D12RenderBackend::showWorldSurfaceBuffer(unsigned plane) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!worldSceneColorPassActive_ || !worldSurfaceReady_ || worldSurfaceActive_ || plane>=4)return false;
    const auto descriptors=worldContactFilterDescriptors_;
    const auto original=worldScreenReflection_;
    const int width=int(worldSurfaceView_[8][0]),height=int(worldSurfaceView_[8][1]);
    worldContactFilterDescriptors_={worldSurfaceTarget_->planes[plane].descriptorIndex,worldSurfaceDepth_,worldSurfaceTarget_->planes[2].descriptorIndex};
    worldScreenReflection_[8]={float(width),float(height),0,2};
    worldScreenReflection_[10][3]=-float(plane+1);
    try {engine::render::drawWorldSurfaceReflectionResolve(*this,width,height);}
    catch(...) {worldContactFilterDescriptors_=descriptors;worldScreenReflection_=original;throw;}
    worldContactFilterDescriptors_=descriptors;worldScreenReflection_=original;return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::resolveWorldSurfaceScreenReflection() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!recording_ || !worldSceneColorPassActive_ || !worldSurfaceReady_ || worldSurfaceActive_ ||
       !engine::render::surfaceReflectionViewMatches(worldSurfaceView_,worldScreenReflection_))return false;
    const auto descriptors=worldContactFilterDescriptors_;
    const auto original=worldScreenReflection_;
    const int width=int(original[8][0]),height=int(original[8][1]);
    worldContactFilterDescriptors_={worldSurfaceTarget_->planes[1].descriptorIndex,worldSurfaceDepth_,worldSurfaceTarget_->planes[2].descriptorIndex};
    worldScreenReflection_[8][3]=2;
    try {
        engine::render::RenderPhaseScope phase(*this,"surface-reflection-resolve");
        engine::render::drawWorldSurfaceReflectionResolve(*this,width,height);
        std::uint32_t resolved=0xffffffffu;std::array<float,4> sampling{};
        const auto purpose=std::string("surface-reflection/")+(worldSurfacePlanar_?"planar/":"primary/")+
            std::to_string(worldSurfaceAllocationSlots_[worldSurfacePlanar_?1:0]);
        if(!captureWorldRadianceSnapshot(purpose.c_str(),width,height,resolved,sampling))
            throw std::runtime_error("Surface reflection capture failed");
        worldContactFilterDescriptors_=descriptors;
        worldContactFilterDescriptors_[1]=resolved;
        worldScreenReflection_=original;worldScreenReflection_[8][3]=3;
        if(original[9][3]>.5f && !restoreWorldDepthSnapshot())
            throw std::runtime_error("Surface reflection depth restore failed");
        return true;
    } catch(...) {
        worldContactFilterDescriptors_=descriptors;worldScreenReflection_=original;throw;
    }
#else
    return false;
#endif
}

bool D3D12RenderBackend::setWorldSurfaceMotion(const engine::render::WorldMotionMatrix& view,
    std::span<const engine::render::WorldMotionSample> samples) {
#if defined(_WIN32)
    if(!worldSurfaceActive_)return false;
    engine::render::validateWorldMotionSamples(samples);
    std::vector<std::uint32_t> offsets;offsets.reserve(samples.size());
    const auto base=worldSkinMatrixBufferGpuAddress_+worldSkinMatrixFrameBaseOffset_;
    for(const auto& sample:samples) {
        std::uint64_t address=base;
        const auto& palette=sample.previous->deformation;
        if(!palette.empty() && !uploadWorldSkinPalette(palette.data(),palette.size(),address,sample.previous->deformationVersion))
            throw std::runtime_error("Previous deformation upload budget exhausted");
        offsets.push_back(static_cast<std::uint32_t>((address-base)/16u));
    }
    engine::render::packWorldMotionDraw(view,samples,worldMotionWords_,offsets);worldMotionGpu_=0;
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::captureWorldSurfaceMotion() {
#if defined(_WIN32)
    if(!recording_ || !worldSceneColorPassActive_ || !worldSurfaceReady_ || worldSurfaceActive_ || worldMotionReadback_)return false;
    auto desc=worldSurfaceTarget_->planes[4].resource->GetDesc();
    worldMotionWidth_=int(desc.Width);worldMotionHeight_=int(desc.Height);
    worldMotionSurfaceBytes_=0;
    for(const auto& plane:worldSurfaceTarget_->planes) {
        const auto d=plane.resource->GetDesc();
        worldMotionSurfaceBytes_+=UINT64(d.Width)*d.Height*((d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R32G32_UINT)?8:4);
    }
    constexpr unsigned planes[]{4,5,1,2,0};
    UINT64 size=0;
    for(unsigned i=0;i<5;++i) {
        desc=worldSurfaceTarget_->planes[planes[i]].resource->GetDesc();
        const auto offset=(size+511)&~UINT64(511);
        UINT64 bytes=0;
        device_->GetCopyableFootprints(&desc,0,1,offset,&worldMotionFootprints_[i],nullptr,nullptr,&bytes);
        size=offset+bytes;
    }
    worldMotionReadbackBytes_=size;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width=size;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;
    buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(worldMotionReadback_.GetAddressOf()))))
        throw std::runtime_error("Motion readback allocation failed");
    for(unsigned i=0;i<5;++i) {
        auto* source=worldSurfaceTarget_->planes[planes[i]].resource.Get();
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        commandList_->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=worldMotionReadback_.Get();
        to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=worldMotionFootprints_[i];
        commandList_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        commandList_->ResourceBarrier(1,&barrier);
    }
    return true;
#else
    return false;
#endif
}
engine::render::WorldMotionImage D3D12RenderBackend::readWorldSurfaceMotion() {
    engine::render::WorldMotionImage result;
#if defined(_WIN32)
    if(recording_ || !worldMotionReadback_)return result;
    waitForGpu();
    result.width=worldMotionWidth_;result.height=worldMotionHeight_;
    const auto count=std::size_t(result.width)*result.height;
    result.velocity.resize(count);result.identity.resize(count);result.identityMetadata.resize(count);result.temporalMaterial.resize(count);
    result.albedo.resize(count);result.normal.resize(count);result.materialClass.resize(count);result.material.resize(count);result.surfaceTexelBytes=worldMotionSurfaceBytes_;
    const unsigned char* bytes=nullptr;D3D12_RANGE range{0,SIZE_T(worldMotionReadbackBytes_)};
    if(FAILED(worldMotionReadback_->Map(0,&range,reinterpret_cast<void**>(const_cast<unsigned char**>(&bytes)))))
        throw std::runtime_error("Motion readback map failed");
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
        const auto index=std::size_t(y)*result.width+x;
        const auto* v=reinterpret_cast<const std::uint16_t*>(bytes+worldMotionFootprints_[0].Offset+
            std::size_t(y)*worldMotionFootprints_[0].Footprint.RowPitch+x*8);
        for(unsigned c=0;c<4;++c)result.velocity[index][c]=engine::render::decodeMotionHalf(v[c]);
        std::memcpy(&result.identity[index],bytes+worldMotionFootprints_[1].Offset+
            std::size_t(y)*worldMotionFootprints_[1].Footprint.RowPitch+x*8,4);
        std::uint32_t temporal=0;
        std::memcpy(&temporal,bytes+worldMotionFootprints_[1].Offset+
            std::size_t(y)*worldMotionFootprints_[1].Footprint.RowPitch+x*8+4,4);
        result.identityMetadata[index]=temporal;result.temporalMaterial[index]=std::uint8_t(temporal>>8);
        std::uint32_t packedNormal=0;
        std::memcpy(&packedNormal,bytes+worldMotionFootprints_[2].Offset+
            std::size_t(y)*worldMotionFootprints_[2].Footprint.RowPitch+x*4,4);
        std::memcpy(&result.albedo[index],bytes+worldMotionFootprints_[4].Offset+
            std::size_t(y)*worldMotionFootprints_[4].Footprint.RowPitch+x*4,4);
        result.normal[index]=packedNormal;
        result.materialClass[index]=std::uint8_t(packedNormal>>30);
        std::memcpy(&result.material[index],bytes+worldMotionFootprints_[3].Offset+
            std::size_t(y)*worldMotionFootprints_[3].Footprint.RowPitch+x*4,4);
    }
    D3D12_RANGE written{0,0};worldMotionReadback_->Unmap(0,&written);worldMotionReadback_.Reset();
#endif
    return result;
}

bool D3D12RenderBackend::beginWorldProbeSurface(std::uint64_t id,std::uint64_t revision,unsigned face,const float* matrix,int resolution) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!(recording_ && worldSceneColorPassActive_) || worldSurfaceActive_)return false;
    auto key=worldProbeSurfaces_.begin(id,revision,face,resolution);
    if(key.empty())return false;
    worldProbeSurfaceKey_=key;
    try {
        if(beginWorldSurfaceBuffer(matrix,resolution,resolution)){
            std::copy_n(worldSurfaceView_.begin(),8,worldScreenReflection_.begin());
            worldScreenReflection_[10][3]=-8;return true;}
    } catch(...) {worldProbeSurfaces_.cancel();worldProbeSurfaceKey_.clear();
    if(worldScreenReflection_[10][3]==-8)worldScreenReflection_[10][3]=0;throw;}
    worldProbeSurfaces_.cancel();worldProbeSurfaceKey_.clear();
    if(worldScreenReflection_[10][3]==-8)worldScreenReflection_[10][3]=0;return false;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldProbeSurface() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldProbeSurfaces_.active())return false;
    if(!endWorldSurfaceBuffer())return false;
    worldScreenReflection_[10][3]=0;
    worldProbeSurfaces_.finish();worldProbeSurfaceKey_.clear();return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::selectWorldProbeSurface(std::uint64_t id,unsigned face) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if(!(recording_ && worldSceneColorPassActive_) || worldSurfaceActive_ || worldHazeActive_ || worldShadowActive_ ||
       worldContact_[8][3]!=0 || worldScreenReflection_[8][3]!=0 || worldPlanarReflection_[4][3]<0 ||
       worldLocalReflection_[0][3]<0)return false;
    const auto key=worldProbeSurfaces_.select(id,face);
    if(key.empty())return false;
    const auto found=worldSurfaceTargets_.find(key);
    if(found==worldSurfaceTargets_.end())return false;
    if(worldSurfaceFrame_!=frameCounter_){worldSurfaceFrame_=frameCounter_;worldSurfaceSlot_=0;worldSurfaceAllocationSlots_={};}
    if(worldSurfaceSlot_>=32)throw std::runtime_error("Surface views per frame exceeded");
    ++worldSurfaceSlot_;
    auto& target=found->second;
    worldSurfaceTarget_=&target;worldSurfaceView_=target.view;worldSurfaceDepth_=target.probeDepth.descriptorIndex;
    worldSurfaceComposed_=worldSurfaceCompositionFinished_=worldSurfaceCompositionCapture_=false;
    worldSurfaceReady_=true;worldClusteredView_={};
    worldClusteredDescriptors_.fill(0xffffffffu);worldStoredSurfaceDescriptors_.fill(0xffffffffu);
    return true;
#else
    return false;
#endif
}
std::uint64_t D3D12RenderBackend::worldProbeSurfaceRevision(std::uint64_t id) const {
#if defined(_WIN32)
    return worldProbeSurfaces_.revision(id);
#else
    return 0;
#endif
}
bool D3D12RenderBackend::releaseWorldProbeSurface(std::uint64_t id) {
#if defined(_WIN32)
    if(worldSurfaceActive_)return false;
    // Release metadata only. GPU allocations remain resident and queue ordered.
    clearWorldSurfaceBuffer();
    return worldProbeSurfaces_.release(id);
#else
    return false;
#endif
}

bool D3D12RenderBackend::beginWorldPlacedProbeFace(std::uint64_t id,std::uint64_t generation,const engine::render::WorldLocalReflectionProbe& probe,unsigned face,std::span<const std::uint64_t> neighbors) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldSurfaceActive_ || !placedProbes.begin(id,generation,probe,face))return false;
    worldLocalReflectionDescriptor_=placedProbeDescriptor;
    worldLocalReflection_=placedProbes.select(neighbors,id);
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::selectWorldPlacedProbes(std::span<const std::uint64_t> ids) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || placedProbes.active())return false;
    worldLocalReflectionDescriptor_=placedProbeDescriptor;
    worldLocalReflection_=placedProbes.select(ids);return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldPlacedProbeFace() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!placedProbes.active())return false;
    const auto saved=worldLocalReflection_;
    const auto offset=placedProbes.offset();
    const auto restore=[&]{worldLocalReflection_=saved;};
    const auto copy=[&](int size,int face,int level,int base,bool clearScene=true,bool batchFaces=false){std::array<float,4> sampling{};const bool copied=captureWorldRadianceSnapshot("placed-reflection",size,size,worldLocalReflectionDescriptor_,sampling,face,level,base,offset[1],offset[0],engine::render::WorldPlacedProbeCache::atlasExtent,clearScene,batchFaces);placedProbeDescriptor=worldLocalReflectionDescriptor_;return copied;};
    try {
        if(!copy(128,int(placedProbes.face()),0,128))throw std::runtime_error("Placed probe face copy failed");
        if(placedProbes.lastFace() && !placedProbes.deferredFiltering()) {
            worldLocalReflection_=placedProbes.filterState();
            if(!engine::render::filterWorldLocalReflection(*this,worldLocalReflection_,true,copy,editorSurfaceActive_?editorSurfaceTarget_.width:width_,editorSurfaceActive_?editorSurfaceTarget_.height:height_))
                throw std::runtime_error("Placed probe convolution failed");
        }
        placedProbes.finish(!placedProbes.deferredFiltering());restore();return true;
    } catch(...) {placedProbes.cancel();restore();throw;}

#else
    return false;
#endif
}

bool D3D12RenderBackend::reserveWorldProbes(std::span<const std::uint64_t> ids,float delta) {
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldSurfaceActive_ || placedProbes.active())return false;
    if(!std::isfinite(delta)||delta<0)throw std::invalid_argument("Invalid probe clock");
    const auto retained=probeResidency.reserve(ids);
    worldProbeSurfaces_.retain(retained);placedProbes.retain(retained);
    placedProbes.deferFiltering(true);placedProbes.advanceBlend(delta);
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::worldProbePublished(std::uint64_t id) const {
#if defined(_WIN32)
    return placedProbes.published(id);
#else
    return false;
#endif
}
bool D3D12RenderBackend::worldProbeBlending(std::uint64_t id) const {
#if defined(_WIN32)
    return placedProbes.blending(id);
#else
    return false;
#endif
}
bool D3D12RenderBackend::filterWorldProbe(std::uint64_t id,unsigned jobs) {
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldSurfaceActive_ || !jobs || jobs>10)return false;
    const auto saved=worldLocalReflection_;
    for(unsigned job=0;job<jobs;++job) {
        if(!placedProbes.beginFilter(id))return false;
        const auto offset=placedProbes.offset();
    const auto copy=[&](int size,int face,int level,int base,bool clearScene=true,bool batchFaces=false){std::array<float,4> sampling{};const bool copied=captureWorldRadianceSnapshot("placed-reflection",size,size,worldLocalReflectionDescriptor_,sampling,face,level,base,offset[1],offset[0],engine::render::WorldPlacedProbeCache::atlasExtent,clearScene,batchFaces);placedProbeDescriptor=worldLocalReflectionDescriptor_;return copied;};
        worldLocalReflectionDescriptor_=placedProbeDescriptor;
        worldLocalReflection_=placedProbes.filterState();
        try {
            if(!engine::render::filterWorldLocalReflectionStep(*this,worldLocalReflection_,
                placedProbes.filterStep(),copy,editorSurfaceActive_?editorSurfaceTarget_.width:width_,editorSurfaceActive_?editorSurfaceTarget_.height:height_))
                throw std::runtime_error("Placed probe filter job failed");
            const bool done=placedProbes.finishFilterStep();worldLocalReflection_=saved;
            if(done)return true;
        } catch(...) {placedProbes.cancel();worldLocalReflection_=saved;throw;}
    }
    return false;
#else
    return false;
#endif
}
bool D3D12RenderBackend::relightWorldProbeSurface(const engine::render::WorldClusteredLightingView& view,const std::array<std::array<float,4>,32>& emission) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSurfaceReady_ || worldSurfaceActive_ || !worldSceneColorPassActive_)return false;
    const auto savedScreen=worldScreenReflection_;
    const auto savedCluster=worldClusteredDescriptors_;
    const auto savedContact=worldContactFilterDescriptors_;
    const auto savedDepth=worldDepthSnapshotDescriptor_;
    const auto restore=[&]{worldScreenReflection_=savedScreen;worldClusteredDescriptors_=savedCluster;worldContactFilterDescriptors_=savedContact;worldDepthSnapshotDescriptor_=savedDepth;};
    auto& surface=*worldSurfaceTarget_;
    worldContactFilterDescriptors_={surface.planes[1].descriptorIndex,worldSurfaceDepth_,surface.planes[2].descriptorIndex};
    worldClusteredDescriptors_={surface.planes[5].descriptorIndex,surface.planes[3].descriptorIndex,surface.planes[4].descriptorIndex};
    worldDepthSnapshotDescriptor_=worldSurfaceDepth_;
    std::copy_n(worldSurfaceView_.begin(),8,worldScreenReflection_.begin());
    const int size=int(worldSurfaceView_[8][0]);
    worldScreenReflection_[8]={float(size),float(size),0,2};worldScreenReflection_[10][3]=8;
    std::copy(emission.begin(),emission.end(),worldLocalReflection_.begin()+20);
    try {engine::render::RenderPhaseScope phase(*this,"probe-relight");engine::render::drawWorldSurfaceReflectionResolve(*this,size,size,&view);}
    catch(...){restore();throw;}
    restore();return true;
#else
    return false;
#endif
}

void D3D12RenderBackend::invalidateWorldPlacedProbes(std::span<const std::uint64_t> ids) {
#if defined(_WIN32)
    placedProbes.invalidate(ids);
#endif
}

const engine::render::WorldClusteredLightingView* D3D12RenderBackend::worldSurfaceCompositionLighting() const {
#if defined(_WIN32)
    return worldSurfaceComposed_ && worldSurfaceCompositionCapture_ && worldMaterialDebugView()==0
        ? &worldCompositionLighting : nullptr;
#else
    return nullptr;
#endif
}

bool D3D12RenderBackend::beginWorldLocalHazeComposition(int width,int height) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_ || worldHazeActive_ || worldHazeStencilActive_ ||
       width<=0 || height<=0 || width>8192 || height>8192)
        throw std::runtime_error("Invalid haze composition scope");
    auto& pool=worldHazeStencil_[frameIndex_];
    if(worldHazeStencilSlot_==pool.size())pool.emplace_back();
    auto& resource=pool[worldHazeStencilSlot_++];
    // A slot is used once per frame, after that frame's fence has completed.
    // Replacing its extent cannot retire another view's in-flight mask.
    if(resource && (resource->GetDesc().Width!=UINT64(width) || resource->GetDesc().Height!=UINT(height)))
        resource.Reset();
    if(!resource) {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;clear.DepthStencil={1.f,1};
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear,IID_PPV_ARGS(resource.GetAddressOf()))))throw std::runtime_error("Haze stencil allocation failed");
    }
    if(!worldHazeDsvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=1;
        if(FAILED(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(worldHazeDsvHeap_.GetAddressOf()))))
            throw std::runtime_error("Haze stencil descriptor allocation failed");
    }
    const auto dsv=worldHazeDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateDepthStencilView(resource.Get(),nullptr,dsv);
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_)rtv.ptr+=SIZE_T(kFrameCount)*rtvDescriptorSize_;
    commandList_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_STENCIL,1.f,1,0,nullptr);
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    worldHazeStencilActive_=true;return true;
#else
    return false;
#endif
}
void D3D12RenderBackend::endWorldLocalHazeComposition() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldHazeStencilActive_)return;
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_){rtv.ptr+=SIZE_T(kFrameCount)*rtvDescriptorSize_;dsv.ptr+=SIZE_T(frameIndex_)*dsvDescriptorSize_;}
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);commandList_->OMSetStencilRef(0);
    worldHazeStencilActive_=false;
#endif
}

bool D3D12RenderBackend::captureWorldProbeRadiance(std::span<const std::uint64_t> ids) {
#if defined(_WIN32)
    if(!recording_ || !worldSceneColorPassActive_ || placedProbes.active() || worldProbeRadianceReadback_)return false;
    const auto found=worldTextures_.find("placed-reflection/resident/"+std::to_string(engine::render::WorldPlacedProbeCache::atlasExtent));
    if(found==worldTextures_.end())return false;
    auto* image=found->second.resource.Get();const auto desc=image->GetDesc();
    if(desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT)return false;
    worldProbeDiagnostic_={};worldProbeDiagnostic_.width=unsigned(desc.Width);worldProbeDiagnostic_.height=desc.Height;
    worldProbeDiagnostic_.selection=placedProbes.select(ids);
    device_->GetCopyableFootprints(&desc,0,1,0,&worldProbeReadbackFootprint_,nullptr,nullptr,&worldProbeReadbackBytes_);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=worldProbeReadbackBytes_;
    buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
       nullptr,IID_PPV_ARGS(worldProbeRadianceReadback_.GetAddressOf()))))throw std::runtime_error("Probe readback allocation");
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    commandList_->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=image;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=worldProbeRadianceReadback_.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=worldProbeReadbackFootprint_;
    commandList_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commandList_->ResourceBarrier(1,&barrier);
    return true;
#else
    return false;
#endif
}
engine::render::WorldProbeRadianceImage D3D12RenderBackend::readWorldProbeRadiance() {
#if defined(_WIN32)
    if(recording_ || !worldProbeRadianceReadback_)return {};
    waitForGpu();
    auto result=worldProbeDiagnostic_;result.rgb111110.resize(std::size_t(result.width)*result.height);
    const void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(worldProbeReadbackBytes_)};
    if(FAILED(worldProbeRadianceReadback_->Map(0,&range,const_cast<void**>(&mapped))))throw std::runtime_error("Probe readback map");
    for(unsigned y=0;y<result.height;++y)std::memcpy(result.rgb111110.data()+std::size_t(y)*result.width,
        static_cast<const char*>(mapped)+worldProbeReadbackFootprint_.Offset+std::size_t(y)*worldProbeReadbackFootprint_.Footprint.RowPitch,
        std::size_t(result.width)*4);
    D3D12_RANGE written{0,0};worldProbeRadianceReadback_->Unmap(0,&written);worldProbeRadianceReadback_.Reset();
    worldProbeDiagnostic_={};return result;
#else
    return {};
#endif
}
