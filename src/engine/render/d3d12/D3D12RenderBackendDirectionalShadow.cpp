#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include <stdexcept>
#include "engine/render/ShadowMomentShader.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"

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
        throw std::invalid_argument("Invalid shadow resolution.");
    if (worldLocalShadows_.enabled) resolution = engine::render::localShadowAtlasResolution;
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
    if (worldLocalShadows_.activeLight < 0) { worldShadowReady_ = false; worldLocalShadows_.clear(); }
    if (!recording_ || !engine::render::validWorldDirectionalShadowView(view) ||
        !worldShadowDepth_ || view.resolution != (worldLocalShadows_.enabled ? (worldLocalShadows_.activeLight >= 0 ? 1024u : 2048u) : worldShadowResolution_)) return false;
    worldShadowView_ = view;
    if (worldLocalShadows_.activeLight < 0) worldLocalShadows_.directionalView = view;
    worldShadowSavedViewport_ = worldLastViewport_; worldShadowSavedScissor_ = worldLastScissor_;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldShadowDepth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList_->ResourceBarrier(1, &barrier);
    const auto dsv = worldShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    const auto xy = worldLocalShadows_.offset();
    const D3D12_RECT tile{LONG(xy[0]), LONG(xy[1]), LONG(xy[0]+view.resolution), LONG(xy[1]+view.resolution)};
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &tile);
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
    if (worldLocalShadows_.activeLight >= 0 && worldShadowView_.distanceMoments[3] > 0)
        filterWorldShadowMoments();
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
    worldShadowReady_ = false; worldShadowView_ = {}; worldLocalShadows_.clear();
#endif
}

#if defined(_WIN32)

void D3D12RenderBackend::prewarmWorldLocalShadows() {
    if (worldShadowActive_) throw std::runtime_error("Cannot prepare local shadows during a shadow pass.");
    worldLocalShadows_.enabled = true;
    prewarmWorldDirectionalShadow(engine::render::localShadowAtlasResolution);
    prewarmWorldShadowMoments();
}
void D3D12RenderBackend::clearWorldLocalShadows() {
    if (worldShadowActive_ && worldLocalShadows_.activeLight >= 0) endWorldLocalShadow();
    else if (worldShadowActive_) endWorldDirectionalShadow();
    worldLocalShadows_.clear();
    worldLocalShadows_.directionalView = worldShadowView_;
}
bool D3D12RenderBackend::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    if (worldShadowActive_ || !worldLocalShadows_.begin(index, view)) return false;
    if (beginWorldDirectionalShadow(view)) return true;
    worldLocalShadows_.activeLight = -1;
    return false;
}
bool D3D12RenderBackend::endWorldLocalShadow() {
    if (worldLocalShadows_.activeLight < 0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.finish(worldShadowView_);
    worldShadowView_ = worldLocalShadows_.directionalView;
    return true;
}
#else
void D3D12RenderBackend::prewarmWorldLocalShadows() {}
void D3D12RenderBackend::clearWorldLocalShadows() {}
bool D3D12RenderBackend::beginWorldLocalShadow(std::uint32_t, const engine::render::WorldDirectionalShadowView&) { return false; }
bool D3D12RenderBackend::endWorldLocalShadow() { return false; }
#endif

#if defined(_WIN32)
void D3D12RenderBackend::prewarmWorldShadowMoments() {
    using namespace engine::render::d3d12_internal;
    using namespace engine::render::d3d12_pipeline_compile;
    if(worldMomentPipeline_) return;
    if(recording_ || nextSrvDescriptorIndex_+3>kMaxSrvDescriptors) throw std::runtime_error("Cannot prepare shadow moment resources");
    waitForGpu();
    worldMomentSrv_=nextSrvDescriptorIndex_;nextSrvDescriptorIndex_+=3;
    writeWorldShadowSrv(worldMomentSrv_);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=2;
    if(FAILED(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(worldMomentRtvHeap_.GetAddressOf())))) throw std::runtime_error("Moment RTV allocation failed");
    for(UINT i=0;i<2;++i) {
        D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width=i?2048:1024;texture.Height=i?4096:1024;texture.DepthOrArraySize=texture.MipLevels=1;
        texture.Format=DXGI_FORMAT_R16G16_UNORM;texture.SampleDesc.Count=1;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if(FAILED(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(worldMomentTargets_[i].GetAddressOf())))) throw std::runtime_error("Moment target allocation failed");
        auto rtv=worldMomentRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(i)*rtvDescriptorSize_;
        device_->CreateRenderTargetView(worldMomentTargets_[i].Get(),nullptr,rtv);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=texture.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        auto handle=srvHeap_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=SIZE_T(worldMomentSrv_+i+1)*srvDescriptorSize_;
        device_->CreateShaderResourceView(worldMomentTargets_[i].Get(),&srv,handle);
    }
    const std::string vsSource=R"(float4 main(uint i:SV_VertexID):SV_POSITION { return float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0.0,1.0); })";
    const auto psSource=engine::render::shadowMomentShader(true);
    Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,error;
    if(!compileHlslWithCache(vsSource.data(),vsSource.size(),"main","vs_5_0",d3dCompileFlags(),0,vs,error) ||
       !compileHlslWithCache(psSource.data(),psSource.size(),"main","ps_5_0",d3dCompileFlags(),0,ps,error))
        throw std::runtime_error("Moment shader compilation failed: "+d3dCompileErrorMessage(error.Get()));
    D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,12};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=2;root.pParameters=parameters;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    if(FAILED(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error)) ||
       FAILED(device_->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(worldMomentRoot_.GetAddressOf()))))
        throw std::runtime_error("Moment root signature failed");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature=worldMomentRoot_.Get();pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    auto& blend=pipeline.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask=UINT_MAX;pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R16G16_UNORM;pipeline.SampleDesc.Count=1;
    if(FAILED(device_->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(worldMomentPipeline_.GetAddressOf())))) throw std::runtime_error("Moment pipeline failed");
}

void D3D12RenderBackend::filterWorldShadowMoments() {
    commandList_->SetGraphicsRootSignature(worldMomentRoot_.Get());commandList_->SetPipelineState(worldMomentPipeline_.Get());
    ID3D12DescriptorHeap* heaps[]={srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT pass=0;pass<2;++pass) {
        const auto data=engine::render::shadowMomentConstants(worldShadowView_,worldLocalShadows_,pass!=0);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={worldMomentTargets_[pass].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
        commandList_->ResourceBarrier(1,&barrier);
        auto rtv=worldMomentRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(pass)*rtvDescriptorSize_;
        commandList_->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        const D3D12_VIEWPORT viewport{data[6],data[7],1024,1024,0,1};const D3D12_RECT scissor{LONG(data[6]),LONG(data[7]),LONG(data[6]+1024),LONG(data[7]+1024)};
        commandList_->RSSetViewports(1,&viewport);commandList_->RSSetScissorRects(1,&scissor);
        auto srv=srvHeap_->GetGPUDescriptorHandleForHeapStart();srv.ptr+=UINT64(worldMomentSrv_+pass)*srvDescriptorSize_;
        commandList_->SetGraphicsRootDescriptorTable(0,srv);commandList_->SetGraphicsRoot32BitConstants(1,12,data.data(),0);
        commandList_->DrawInstanced(3,1,0,0);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commandList_->ResourceBarrier(1,&barrier);
    }
}
#endif
