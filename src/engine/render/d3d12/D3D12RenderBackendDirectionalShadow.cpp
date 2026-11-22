#include "engine/render/d3d12/D3D12PipelinePreparation.h"
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
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

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
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if (worldShadowActive_) return false;
    if (view.sunTile==~0u && worldLocalShadows_.activeLight < 0) { worldShadowReady_ = false; worldLocalShadows_.clear(); worldShadowCache_.resetPass(); }
    if (!recording_ || !engine::render::validWorldDirectionalShadowView(view) ||
        (view.sunTile!=~0u ? (!worldSunDepth_ || view.sunTile>=9 || !(worldSunPending_&(1u<<view.sunTile)) ||
            view.resolution!=(view.sunTile<4?worldSunPlan_.budget.resolution:worldSunPlan_.budget.distantResolution)) :
            (!worldShadowDepth_ || view.resolution != (worldLocalShadows_.enabled ? (worldLocalShadows_.activeLight >= 0 ? view.resolution : 2048u) : worldShadowResolution_)))) return false;
    if(view.sunTile!=~0u){worldSunSavedView_=worldShadowView_;worldSunSavedReady_=worldShadowReady_;}
    worldShadowView_ = view;
    if (view.sunTile==~0u && worldLocalShadows_.activeLight < 0) worldLocalShadows_.directionalView = view;
    worldShadowSavedViewport_ = worldLastViewport_; worldShadowSavedScissor_ = worldLastScissor_;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldShadowView_.sunTile!=~0u?worldSunDepth_.Get():worldShadowDepth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = worldShadowView_.sunTile!=~0u?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList_->ResourceBarrier(1, &barrier);
    const auto dsv = (view.sunTile!=~0u?worldSunDsv_:worldShadowDsvHeap_)->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    const auto xy = worldShadowOffset();
    const D3D12_RECT tile{LONG(xy[0]), LONG(xy[1]), LONG(xy[0]+view.resolution), LONG(xy[1]+view.resolution)};
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 1, &tile);
    worldShadowActive_ = true;
    return true;
#else
    (void)view; return false;
#endif
}

bool D3D12RenderBackend::endWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if (!worldShadowActive_) return false;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = worldShadowView_.sunTile!=~0u?worldSunDepth_.Get():worldShadowDepth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = worldShadowView_.sunTile!=~0u?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
    worldShadowActive_ = false;
    if(worldShadowView_.sunTile!=~0u){worldSunPending_&=~(1u<<worldShadowView_.sunTile);worldShadowView_=worldSunSavedView_;worldShadowReady_=worldSunSavedReady_;}
    else worldShadowReady_ = true;
    return true;
#else
    return false;
#endif
}

void D3D12RenderBackend::clearWorldDirectionalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if (worldShadowActive_) endWorldDirectionalShadow();
    worldShadowReady_ = false; worldShadowView_ = {}; worldLocalShadows_.clear(); worldShadowCache_.resetPass();
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
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

    if (worldShadowActive_ && worldLocalShadows_.activeLight >= 0) endWorldLocalShadow();
    else if (worldShadowActive_) endWorldDirectionalShadow();
    worldLocalShadows_.clear();
    worldShadowCache_.resetPass();
    worldLocalShadows_.directionalView = worldShadowView_;
}
bool D3D12RenderBackend::beginWorldLocalShadow(std::uint32_t index, const engine::render::WorldDirectionalShadowView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    if (worldShadowActive_ || !worldLocalShadows_.begin(index, view)) return false;
    worldShadowUpdates_.erase(worldLocalShadows_.slot(index));
    worldShadowCache_.activate(worldLocalShadows_.slot(index),worldLocalShadows_.owner(index),view);
    if (beginWorldDirectionalShadow(view)) return true;
    worldLocalShadows_.activeLight = -1;
    return false;
}
bool D3D12RenderBackend::endWorldLocalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    if (worldLocalShadows_.activeLight < 0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.finish(worldShadowView_);
    worldShadowView_ = worldLocalShadows_.directionalView;
    worldShadowCache_.resetPass();
    return true;
}
engine::render::WorldShadowCacheResult D3D12RenderBackend::beginWorldStaticShadow(std::uint32_t index,
        const engine::render::WorldDirectionalShadowView& view, engine::render::WorldShadowCacheKey key) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    using engine::render::WorldShadowCacheResult;
    if(!key.scene || !key.revision || view.distanceMoments[3]<=0) {
        worldShadowCache_.resetPass(); return WorldShadowCacheResult::Unavailable;
    }
    if(worldShadowActive_ || !recording_ || !worldMomentPipeline_ || !engine::render::validWorldDirectionalShadowView(view) ||
       (view.resolution!=512 && view.resolution!=1024) || index>=engine::render::worldLocalShadowSourceCapacity || worldLocalShadows_.tileCount>=engine::render::localShadowCapacity)
        throw std::runtime_error("Invalid static shadow pass");
    if(!worldShadowCache_.prepare(worldLocalShadows_.slot(index),worldLocalShadows_.owner(index),view,key)) return WorldShadowCacheResult::Reuse;
    if(!worldLocalShadows_.begin(index,view) || !beginWorldDirectionalShadow(view)) {
        worldLocalShadows_.activeLight=-1; worldShadowCache_.invalidate();
        throw std::runtime_error("Static shadow pass admission failed");
    }
    return WorldShadowCacheResult::Draw;
}
bool D3D12RenderBackend::endWorldStaticShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
    if(!worldShadowCache_.writing || worldLocalShadows_.activeLight<0 || !endWorldDirectionalShadow()) return false;
    worldLocalShadows_.activeLight=-1;
    worldShadowView_=worldLocalShadows_.directionalView;
    worldShadowCache_.commit(); return true;
}
engine::render::WorldShadowCacheResult D3D12RenderBackend::queryWorldStaticShadow(
    std::span<const std::uint32_t> sources,std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowCacheKey key) const {
    using engine::render::WorldShadowCacheResult;
    if(!key.scene || !key.revision || view.distanceMoments[3]<=0)return WorldShadowCacheResult::Unavailable;
    if(!recording_ || !worldMomentPipeline_ || worldShadowActive_)
        throw std::runtime_error("Static shadow planning requires an idle pass in an active frame");
    auto allocation=worldLocalShadows_;allocation.clear();allocation.plan(sources);
    return worldShadowCache_.matches(allocation.slot(light),allocation.owner(light),view,key)?WorldShadowCacheResult::Reuse:WorldShadowCacheResult::Draw;
}
void D3D12RenderBackend::setWorldLocalShadowIdentities(std::span<const std::uint64_t> ids) { worldLocalShadows_.identities(ids); }
void D3D12RenderBackend::planWorldLocalShadowSources(std::span<const std::uint32_t> sources) { worldLocalShadows_.plan(sources); }
void D3D12RenderBackend::invalidateWorldStaticShadows() { worldShadowCache_.invalidate();worldShadowUpdates_.clear(); }
#else
void D3D12RenderBackend::prewarmWorldLocalShadows() {}
void D3D12RenderBackend::clearWorldLocalShadows() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
}
bool D3D12RenderBackend::beginWorldLocalShadow(std::uint32_t, const engine::render::WorldDirectionalShadowView&) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
 return false; }
bool D3D12RenderBackend::endWorldLocalShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
 return false; }
engine::render::WorldShadowCacheResult D3D12RenderBackend::beginWorldStaticShadow(std::uint32_t,
    const engine::render::WorldDirectionalShadowView&, engine::render::WorldShadowCacheKey) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
 return engine::render::WorldShadowCacheResult::Unavailable; }
bool D3D12RenderBackend::endWorldStaticShadow() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
 return false; }
engine::render::WorldShadowCacheResult D3D12RenderBackend::queryWorldStaticShadow(std::span<const std::uint32_t>,
    std::uint32_t,const engine::render::WorldDirectionalShadowView&,engine::render::WorldShadowCacheKey) const {
    return engine::render::WorldShadowCacheResult::Unavailable;
}
void D3D12RenderBackend::setWorldLocalShadowIdentities(std::span<const std::uint64_t>) {}
void D3D12RenderBackend::planWorldLocalShadowSources(std::span<const std::uint32_t>) {}
void D3D12RenderBackend::invalidateWorldStaticShadows() {}
#endif

#if defined(_WIN32)
void D3D12RenderBackend::prewarmWorldShadowMoments() {
    using namespace engine::render::d3d12_internal;
    using namespace engine::render::d3d12_pipeline_compile;
    if(worldMomentPipeline_) return;
    if(recording_ || nextSrvDescriptorIndex_+4>kMaxSrvDescriptors) throw std::runtime_error("Cannot prepare shadow moment resources");
    waitForGpu();
    worldShadowCache_.invalidate();
    worldMomentSrv_=nextSrvDescriptorIndex_;nextSrvDescriptorIndex_+=4;
    writeWorldShadowSrv(worldMomentSrv_);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=3;
    if(FAILED(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(worldMomentRtvHeap_.GetAddressOf())))) throw std::runtime_error("Moment RTV allocation failed");
    for(UINT i=0;i<3;++i) {
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
    D3D12_DESCRIPTOR_RANGE cacheRange=range;cacheRange.BaseShaderRegister=1;
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,12};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[2].DescriptorTable={1,&cacheRange};parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=3;root.pParameters=parameters;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    if(FAILED(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error)) ||
       FAILED(engine::render::prepareRootSignature(device_.Get(), 0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(worldMomentRoot_.GetAddressOf()))))
        throw std::runtime_error("Moment root signature failed");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature=worldMomentRoot_.Get();pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    auto& blend=pipeline.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask=UINT_MAX;pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R16G16_UNORM;pipeline.SampleDesc.Count=1;
    if(FAILED(engine::render::prepareGraphicsPipeline(device_.Get(), &pipeline,IID_PPV_ARGS(worldMomentPipeline_.GetAddressOf())))) throw std::runtime_error("Moment pipeline failed");
}

void D3D12RenderBackend::filterWorldShadowMoments() {
    commandList_->SetGraphicsRootSignature(worldMomentRoot_.Get());commandList_->SetPipelineState(worldMomentPipeline_.Get());
    ID3D12DescriptorHeap* heaps[]={srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT pass=worldShadowCache_.writing?2u:0u;pass<(worldShadowCache_.writing?3u:2u);++pass) {
        const auto data=engine::render::shadowMomentCacheConstants(worldShadowView_,worldLocalShadows_,pass,worldShadowCache_.reading);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={worldMomentTargets_[pass].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
        commandList_->ResourceBarrier(1,&barrier);
        auto rtv=worldMomentRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(pass)*rtvDescriptorSize_;
        commandList_->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        const D3D12_VIEWPORT viewport{data[6],data[7],data[9],data[9],0,1};const D3D12_RECT scissor{LONG(data[6]),LONG(data[7]),LONG(data[6]+data[9]),LONG(data[7]+data[9])};
        commandList_->RSSetViewports(1,&viewport);commandList_->RSSetScissorRects(1,&scissor);
        auto srv=srvHeap_->GetGPUDescriptorHandleForHeapStart();srv.ptr+=UINT64(worldMomentSrv_+(pass==1?1:0))*srvDescriptorSize_;
        commandList_->SetGraphicsRootDescriptorTable(0,srv);commandList_->SetGraphicsRoot32BitConstants(1,12,data.data(),0);
        // The writer binds depth as the unused cache input, never its own RTV.
        auto cached=srvHeap_->GetGPUDescriptorHandleForHeapStart();cached.ptr+=UINT64(worldMomentSrv_+(pass==0?3:0))*srvDescriptorSize_;
        commandList_->SetGraphicsRootDescriptorTable(2,cached);
        commandList_->DrawInstanced(3,1,0,0);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commandList_->ResourceBarrier(1,&barrier);
    }
}
#endif


engine::render::WorldShadowUpdateQuery D3D12RenderBackend::queryWorldShadowUpdate(
    std::span<const std::uint32_t> sources,std::uint32_t light,const engine::render::WorldDirectionalShadowView& view,
    engine::render::WorldShadowUpdateKey key) const {
#if defined(_WIN32)
    auto allocation=worldLocalShadows_;allocation.clear();allocation.plan(sources);
    return worldShadowUpdates_.query(allocation.slot(light),view,key);
#else
    return {};
#endif
}
bool D3D12RenderBackend::reuseWorldShadowUpdate(std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowUpdateKey key) {
#if defined(_WIN32)
    if(worldShadowActive_ || !worldShadowUpdates_.query(worldLocalShadows_.slot(light),view,key).valid ||
        !worldLocalShadows_.begin(light,view))return false;
    worldLocalShadows_.finish(view);return true;
#else
    return {};
#endif
}
void D3D12RenderBackend::commitWorldShadowUpdate(std::uint32_t light,
    const engine::render::WorldDirectionalShadowView& view,engine::render::WorldShadowUpdateKey key) {
#if defined(_WIN32)
    if(worldShadowActive_)throw std::logic_error("Shadow update committed before completion");
    worldShadowUpdates_.commit(worldLocalShadows_.slot(light),view,key);
#endif
}


void D3D12RenderBackend::prewarmWorldSunShadows(unsigned quality) {
#if defined(_WIN32)
    const auto budget=engine::render::worldSunShadowBudget(quality);
    if(worldSunDepth_&&worldSunQuality_==quality)return;
    if(recording_||!device_)throw std::runtime_error("Sun shadows need an idle prepared renderer");
    waitForGpu();
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=1;
    if(FAILED(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(worldSunDsv_.ReleaseAndGetAddressOf()))))throw std::runtime_error("Sun DSV");
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=budget.width();image.Height=budget.height();
    image.DepthOrArraySize=image.MipLevels=1;image.Format=DXGI_FORMAT_R32_TYPELESS;image.SampleDesc.Count=1;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
    if(FAILED(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        &clear,IID_PPV_ARGS(worldSunDepth_.ReleaseAndGetAddressOf()))))throw std::runtime_error("Sun atlas allocation");
    D3D12_DEPTH_STENCIL_VIEW_DESC depth{};depth.Format=DXGI_FORMAT_D32_FLOAT;depth.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(worldSunDepth_.Get(),&depth,worldSunDsv_->GetCPUDescriptorHandleForHeapStart());
    if(worldSunDescriptor_==0xffffffffu){if(nextSrvDescriptorIndex_>=engine::render::d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Sun descriptor budget");worldSunDescriptor_=nextSrvDescriptorIndex_++;}
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    auto handle=srvHeap_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=SIZE_T(worldSunDescriptor_)*srvDescriptorSize_;
    device_->CreateShaderResourceView(worldSunDepth_.Get(),&srv,handle);
    for(const auto& [key,base]:worldMaterialDescriptorBlocks_)if(key.projectedShadowTextureDescriptorIndex==worldSunDescriptor_) {
        auto destination=srvHeap_->GetCPUDescriptorHandleForHeapStart();destination.ptr+=SIZE_T(base+7)*srvDescriptorSize_;
        device_->CreateShaderResourceView(worldSunDepth_.Get(),&srv,destination);
    }
    worldSunQuality_=quality;worldSunPlan_={};invalidatePreparedWorldViews();
#else
    (void)quality;
#endif
}
bool D3D12RenderBackend::setWorldSunShadowPlan(const engine::render::WorldSunShadowPlan& plan) {
#if defined(_WIN32)
    engine::render::validateWorldSunShadowPlan(plan);
    if(!recording_||worldShadowActive_||!worldSunDepth_||engine::render::worldSunShadowBudget(worldSunQuality_).width()!=plan.budget.width())return false;
    worldSunPlan_=plan;worldSunPending_=plan.drawMask;invalidatePreparedWorldViews();return true;
#else
    (void)plan;return false;
#endif
}
void D3D12RenderBackend::clearWorldSunShadows() {
#if defined(_WIN32)
    worldSunPlan_={};invalidatePreparedWorldViews();
#endif
}
#if defined(_WIN32)
std::array<std::uint32_t,2> D3D12RenderBackend::worldShadowOffset() const {
    if(worldShadowView_.sunTile==~0u)return worldLocalShadows_.offset();
    const unsigned slot=worldShadowView_.sunTile;const auto& b=worldSunPlan_.budget;
    if(slot<4)return {(slot%2)*b.resolution,(slot/2)*b.resolution};
    return {((slot-4)%(b.width()/b.distantResolution))*b.distantResolution,
        2*b.resolution+((slot-4)/(b.width()/b.distantResolution))*b.distantResolution};
}
#endif

std::uint64_t D3D12RenderBackend::worldSunShadowIdentity() const {
#if defined(_WIN32)
    return worldSunPending_?0:worldSunPlan_.identity;
#else
    return 0;
#endif
}
