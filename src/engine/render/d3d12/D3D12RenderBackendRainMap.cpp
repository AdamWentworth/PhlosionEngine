#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include <stdexcept>
using namespace engine::render;
#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::RainMapState {
    struct Entry {
        unsigned resolution=0;
        ComPtr<ID3D12Resource> pending;
        ComPtr<ID3D12DescriptorHeap> dsv;
        std::string key;
    };
    std::unordered_map<std::uint64_t,Entry> entries;
    std::uint64_t active=0,last=0;
    WorldDirectionalShadowView savedView;
    WorldLocalShadowAtlas savedLocal;
    D3D12_VIEWPORT savedViewport{};
    D3D12_RECT savedScissor{};
    bool savedReady=false;
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    unsigned readResolution=0;
};
namespace {
constexpr auto rainRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void rainBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cmd->ResourceBarrier(1,&v);
}
void rainCheck(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(message);}
}
#endif
void D3D12RenderBackend::prewarmWorldRainMap(WorldResourceScopeId scope,unsigned res) {
#if defined(_WIN32)
    if(recording_||!scope||res<128||res>2048||(res&(res-1))||!worldScopes_.admitted()||worldScopes_.active()!=scope)
        throw std::invalid_argument("Invalid rain-map preparation");
    if(!rainMaps_)rainMaps_=std::make_shared<RainMapState>();
    auto& state=*rainMaps_;
    if(auto it=state.entries.find(scope);it!=state.entries.end()) {
        if(it->second.resolution!=res)throw std::invalid_argument("Rain-map resolution changed within its owner");
        return;
    }
    if(state.entries.size()>=4)throw std::runtime_error("Rain-map owner budget exhausted");
    const std::uint64_t bytes=std::uint64_t(res)*res*12;
    if(!worldScopes_.admit(bytes))throw std::runtime_error("Rain-map scope budget exhausted");
    RainMapState::Entry entry;entry.resolution=res;entry.key="phlosion://rain-map/"+std::to_string(scope);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    const auto create=[&](unsigned height,bool depth,ID3D12Resource** output) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=res;d.Height=height;
        d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_R32_TYPELESS;d.SampleDesc.Count=1;
        d.Flags=depth?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_NONE;
        D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
        rainCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,rainRead,depth?&clear:nullptr,
            IID_PPV_ARGS(output)),"Rain-map image allocation");
    };
    create(res,true,entry.pending.GetAddressOf());
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    rainCheck(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&entry.dsv)),"Rain-map depth descriptor");
    D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(entry.pending.Get(),&dv,entry.dsv->GetCPUDescriptorHandleForHeapStart());
    SpriteTexture atlas;create(res*2,false,atlas.resource.GetAddressOf());
    if(!freeWorldTextureDescriptors_.empty()){atlas.descriptorIndex=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
    else {if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Rain-map descriptor budget");atlas.descriptorIndex=nextSrvDescriptorIndex_++;}
    atlas.valid=atlas.hasSrvDesc=true;atlas.scope=scope;atlas.srvDesc.Format=DXGI_FORMAT_R32_FLOAT;
    atlas.srvDesc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;atlas.srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    atlas.srvDesc.Texture2D.MipLevels=1;
    auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(atlas.descriptorIndex)*srvDescriptorSize_;
    device_->CreateShaderResourceView(atlas.resource.Get(),&atlas.srvDesc,cpu);
    worldTextures_.emplace(entry.key,std::move(atlas));state.entries.emplace(scope,std::move(entry));
    WorldResourceCounts counts;counts.textures=2;counts.textureDescriptors=1;counts.payloadBytes=bytes;
    counts.apiAllocationBytes=bytes;counts.apiAllocationBytesEstimated=true;worldScopes_.commit(counts);
#else
    (void)scope;(void)res;throw std::runtime_error("D3D12 rain maps unavailable");
#endif
}
bool D3D12RenderBackend::beginWorldRainMap(WorldResourceScopeId scope,const WorldDirectionalShadowView& view,bool clear) {
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!rainMaps_||rainMaps_->active||worldShadowActive_||!recording_||!worldSceneColorPassActive_||
       !validWorldDirectionalShadowView(view))return false;
    auto it=rainMaps_->entries.find(scope);if(it==rainMaps_->entries.end()||it->second.resolution!=view.resolution)return false;
    WorldResourceScopeStats owner;if(!worldScopes_.query(scope,owner)||owner.releasePending)return false;
    auto& s=*rainMaps_;auto& entry=it->second;
    s.savedView=worldShadowView_;s.savedLocal=worldLocalShadows_;s.savedReady=worldShadowReady_;
    s.savedViewport=worldLastViewport_;s.savedScissor=worldLastScissor_;
    worldShadowView_=view;worldLocalShadows_={};worldShadowActive_=true;s.active=scope;
    rainBarrier(commandList_.Get(),entry.pending.Get(),rainRead,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto dsv=entry.dsv->GetCPUDescriptorHandleForHeapStart();commandList_->OMSetRenderTargets(0,nullptr,FALSE,&dsv);
    if(clear)commandList_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::endWorldRainMap() {
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!rainMaps_||!rainMaps_->active)return false;
    auto& s=*rainMaps_;auto& entry=s.entries.at(s.active);
    rainBarrier(commandList_.Get(),entry.pending.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,rainRead);
    auto rtv=editorSurfaceActive_?editorSurfaceTarget_.linearRtv:rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    auto dsv=editorSurfaceActive_?editorSurfaceTarget_.depthDsv:dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    if(!editorSurfaceActive_){rtv.ptr+=SIZE_T(kFrameCount)*rtvDescriptorSize_;dsv.ptr+=SIZE_T(frameIndex_)*dsvDescriptorSize_;}
    commandList_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);commandList_->RSSetViewports(1,&s.savedViewport);commandList_->RSSetScissorRects(1,&s.savedScissor);
    worldShadowView_=s.savedView;worldLocalShadows_=s.savedLocal;worldShadowReady_=s.savedReady;worldShadowActive_=false;
    s.last=s.active;s.active=0;return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::publishWorldRainMap(WorldResourceScopeId scope,unsigned cascade) {
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!rainMaps_||rainMaps_->active||rainMaps_->last!=scope||cascade>1)return false;
    auto& entry=rainMaps_->entries.at(scope);auto& atlas=worldTextures_.at(entry.key);
    rainBarrier(commandList_.Get(),entry.pending.Get(),rainRead,D3D12_RESOURCE_STATE_COPY_SOURCE);
    rainBarrier(commandList_.Get(),atlas.resource.Get(),rainRead,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=entry.pending.Get();dst.pResource=atlas.resource.Get();
    src.Type=dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commandList_->CopyTextureRegion(&dst,0,cascade*entry.resolution,0,&src,nullptr);
    rainBarrier(commandList_.Get(),entry.pending.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,rainRead);
    rainBarrier(commandList_.Get(),atlas.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,rainRead);
    rainMaps_->last=0;return true;
#else
    return false;
#endif
}
std::string D3D12RenderBackend::worldRainMapTexture(WorldResourceScopeId scope) const {
#if defined(_WIN32)
    if(rainMaps_)if(auto it=rainMaps_->entries.find(scope);it!=rainMaps_->entries.end())return it->second.key;
#endif
    return {};
}
#if defined(_WIN32)
D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldRainTexture(const char* key) {
    if(auto* particle=findWorldParticleLightingTexture(key))return particle;
    if(auto* volume=findWorldPrecipitationTexture(key))return volume;
    if(!key||std::string_view(key).find("phlosion://rain-map/")!=0)return nullptr;
    auto it=worldTextures_.find(key);if(it==worldTextures_.end())throw std::runtime_error("Rain-map texture is not resident");
    return &it->second;
}
#endif
void D3D12RenderBackend::releaseWorldRainMaps(std::uint64_t owner) {
#if defined(_WIN32)
    worldWetHistory_.release(owner);
    if(owner==~std::uint64_t{0})worldWetFallbackDescriptors_.clear();else worldWetFallbackDescriptors_.erase(owner);
    if(owner==~std::uint64_t{0}||worldWetOwner_==owner){worldWet_={};worldWetDescriptor_=0xffffffffu;worldWetOwner_=0;}
    if(!rainMaps_)return;
    if(rainMaps_->active)throw std::runtime_error("Cannot retire an active rain-map pass");
    if(owner==~std::uint64_t{0})rainMaps_.reset();
    else rainMaps_->entries.erase(owner);
#endif
}

bool D3D12RenderBackend::captureWorldRainMap(WorldResourceScopeId scope){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!rainMaps_||rainMaps_->active||rainMaps_->readback)return false;
    auto it=rainMaps_->entries.find(scope);if(it==rainMaps_->entries.end())return false;
    auto& s=*rainMaps_;auto& image=worldTextures_.at(it->second.key);auto desc=image.resource->GetDesc();UINT64 bytes=0;
    device_->GetCopyableFootprints(&desc,0,1,0,&s.footprint,nullptr,nullptr,&bytes);s.readResolution=it->second.resolution;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rainCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.readback)),"Rain-map readback");
    rainBarrier(commandList_.Get(),image.resource.Get(),rainRead,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=image.resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=s.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=s.footprint;
    commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);rainBarrier(commandList_.Get(),image.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,rainRead);return true;
#else
    return false;
#endif
}
engine::render::WorldRainMapDiagnostic D3D12RenderBackend::readWorldRainMap(){
#if defined(_WIN32)
    if(recording_||!rainMaps_||!rainMaps_->readback)return {};
    waitForGpu();auto& s=*rainMaps_;void* mapped=nullptr;rainCheck(s.readback->Map(0,nullptr,&mapped),"Rain-map readback map");
    WorldRainMapDiagnostic r;r.resolution=s.readResolution;r.depth.resize(std::size_t(r.resolution)*r.resolution*2);
    for(unsigned y=0;y<r.resolution*2;++y)std::copy_n(reinterpret_cast<const float*>(static_cast<const char*>(mapped)+y*s.footprint.Footprint.RowPitch),r.resolution,r.depth.begin()+y*r.resolution);
    s.readback->Unmap(0,nullptr);s.readback.Reset();return r;
#else
    return {};
#endif
}

void D3D12RenderBackend::bindWorldWetSurfaces(const WorldWetSurfaceView* view) {
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    worldWet_={};worldWetDescriptor_=0xffffffffu;worldWetOwner_=0;
    if(!view)return;
    WorldResourceScopeStats owner;
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||
       worldScopes_.active()!=view->owner||!worldScopes_.query(view->owner,owner)||owner.releasePending||
       view->rainMap!=worldRainMapTexture(view->owner))
        throw std::invalid_argument("Wet-surface owner or render phase");
    const auto constants=packWorldWetSurfaces(*view);
    worldWetHistory_.validate(*view,frameCounter_);
    std::array<std::uint32_t,5> descriptors{};
    for(unsigned i=0;i<4;++i) {
        auto* texture=ensureWorldTexture(&view->textures[i]);
        if(!texture||texture->scope!=worldScopes_.assetOwner())throw std::runtime_error("Wet-surface texture admission");
        descriptors[i]=texture->descriptorIndex;
    }
    auto* rain=findWorldRainTexture(view->rainMap.c_str());
    if(!rain||rain->scope!=view->owner)throw std::runtime_error("Wet-surface shelter admission");
    descriptors[4]=rain->descriptorIndex;
    // Reuse immutable scoped descriptor-block storage and its fenced retirement.
    const auto block=ensureWorldMaterialDescriptorBlock(descriptors[0],descriptors[1],descriptors[2],
        descriptors[3],descriptors[4],descriptors[0],descriptors[0],descriptors[0]);
    if(block==0xffffffffu)throw std::runtime_error("Wet-surface descriptor admission");
    worldWetInputs_=descriptors;worldWetDescriptor_=block;worldWet_=constants;worldWetOwner_=view->owner;worldWetHistory_.commit(*view,frameCounter_);
#else
    throw std::runtime_error("D3D12 wet surfaces unavailable");
#endif
}
