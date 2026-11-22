#include "engine/render/D3D12RenderBackend.h"
#include <cstring>
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif
using namespace engine::render;
#if defined(_WIN32)
namespace {
using Microsoft::WRL::ComPtr;
void layerCheck(HRESULT h,const char* why){if(FAILED(h))throw std::runtime_error(why);}
void layerBuffer(ID3D12Device* device,ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
    d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    layerCheck(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&out)),"Layered material buffer");
}
}
struct D3D12RenderBackend::LayeredState {
    struct Entry {
        std::uint64_t owner=0;
        const WorldLayerMaterialSet* source=nullptr;
        std::string identity;
        ComPtr<ID3D12Resource> parameters,upload;
        std::uint32_t table=~0u;
    };
    std::array<Entry,8> entries;
    Entry* selected=nullptr;
    ComPtr<ID3D12Resource> fallback;
    std::uint32_t fallbackTable=~0u;
    std::vector<std::uint32_t> freeTables;
};
D3D12_GPU_VIRTUAL_ADDRESS D3D12RenderBackend::worldLayeredAddress(){
    if(!layeredMaterials_)layeredMaterials_=std::make_shared<LayeredState>();auto& state=*layeredMaterials_;
    if(!state.fallback){
        layerBuffer(device_.Get(),state.fallback,256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p=nullptr;layerCheck(state.fallback->Map(0,nullptr,&p),"Layered fallback map");
        std::memset(p,0,256);state.fallback->Unmap(0,nullptr);
    }
    return state.selected?state.selected->parameters->GetGPUVirtualAddress():state.fallback->GetGPUVirtualAddress();
}
D3D12_GPU_DESCRIPTOR_HANDLE D3D12RenderBackend::worldLayeredTextures(){
    (void)worldLayeredAddress();auto& state=*layeredMaterials_;
    if(state.fallbackTable==~0u){
        if(nextSrvDescriptorIndex_+kWorldLayerTextureLimit>kMaxSrvDescriptors)throw std::runtime_error("Layered fallback descriptor budget");
        state.fallbackTable=nextSrvDescriptorIndex_;nextSrvDescriptorIndex_+=kWorldLayerTextureLimit;
        const auto* texture=findWorldTextureByDescriptorIndex(worldFallbackTextureDescriptorIndex_);
        if(!texture||!texture->resource)throw std::runtime_error("Layered fallback texture unavailable");
        auto dst=srvHeap_->GetCPUDescriptorHandleForHeapStart();dst.ptr+=SIZE_T(state.fallbackTable)*srvDescriptorSize_;
        for(unsigned i=0;i<kWorldLayerTextureLimit;++i){device_->CreateShaderResourceView(texture->resource.Get(),&texture->srvDesc,dst);dst.ptr+=srvDescriptorSize_;}
    }
    auto gpu=srvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr+=UINT64(state.selected?state.selected->table:state.fallbackTable)*srvDescriptorSize_;return gpu;
}
#endif
void D3D12RenderBackend::bindWorldLayeredMaterials(std::uint64_t owner,std::shared_ptr<const WorldLayerMaterialSet> set){
#if defined(_WIN32)
    (void)worldLayeredAddress();auto& state=*layeredMaterials_;
    if(set && (!owner || worldScopes_.active()!=owner || !worldScopes_.admitted()))
        throw std::invalid_argument("Layered material owner or phase");
    engine::render::WorldResourceScopeState::AssetResources assets(worldScopes_);
    if(set)owner=worldScopes_.active();
    if(!set&&!state.selected)return;
    if(set&&state.selected&&state.selected->owner==owner&&state.selected->source==set.get()&&state.selected->identity==set->identity)return;
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!set){state.selected=nullptr;return;}
    WorldResourceScopeStats scope;
    if(!owner||!worldScopes_.query(owner,scope)||scope.releasePending||worldScopes_.active()!=owner)
        throw std::invalid_argument("Layered material owner or phase");
    for(auto& entry:state.entries)if(entry.owner==owner){
        if(entry.source!=set.get()||entry.identity!=set->identity)throw std::invalid_argument("Immutable layered material scope changed");
        state.selected=&entry;return;
    }
    auto it=std::find_if(state.entries.begin(),state.entries.end(),[](const auto& v){return !v.owner;});
    if(it==state.entries.end())throw std::runtime_error("Layered material scope budget");
    const auto packed=packWorldLayerMaterialSet(*set);const UINT64 bytes=packed.words.size()*4;
    if(!worldScopes_.admit(bytes*2+packed.textureBytes))throw std::runtime_error("Layered material memory budget");
    std::vector<const SpriteTexture*> textures;
    for(const auto& t:set->textures){
        const auto index=std::uint32_t(&t-set->textures.data());
        const bool materialImage=std::none_of(set->materials.begin(),set->materials.end(),[&](const auto& m){
            return m.maskTexture==index||m.tilingMap==index;
        });
        std::vector<WorldTextureMipLevel> levels;
        for(const auto& mip:t.mips)levels.push_back({mip.rgba.data(),static_cast<int>(mip.width),static_cast<int>(mip.height)});
        const auto& mip=t.mips.front();
        auto* texture=ensureWorldTextureRaw(t.identity.c_str(),nullptr,mip.rgba.data(),int(mip.width),int(mip.height),
            10497,10497,t.srgb,levels.data(),static_cast<std::uint32_t>(levels.size()),materialImage);
        if(!texture||texture->scope!=owner)throw std::runtime_error("Layered texture admission");
        textures.push_back(texture);
    }
    LayeredState::Entry entry;entry.owner=owner;entry.source=set.get();entry.identity=set->identity;
    layerBuffer(device_.Get(),entry.parameters,bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON);
    layerBuffer(device_.Get(),entry.upload,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* p=nullptr;layerCheck(entry.upload->Map(0,nullptr,&p),"Layered parameter upload");
    std::memcpy(p,packed.words.data(),bytes);entry.upload->Unmap(0,nullptr);
    if(state.freeTables.empty()){
        if(nextSrvDescriptorIndex_+kWorldLayerTextureLimit>kMaxSrvDescriptors)throw std::runtime_error("Layered descriptor budget");
        entry.table=nextSrvDescriptorIndex_;nextSrvDescriptorIndex_+=kWorldLayerTextureLimit;
    }else{entry.table=state.freeTables.back();state.freeTables.pop_back();}
    auto dst=srvHeap_->GetCPUDescriptorHandleForHeapStart();dst.ptr+=SIZE_T(entry.table)*srvDescriptorSize_;
    for(unsigned i=0;i<kWorldLayerTextureLimit;++i){
        const auto* t=textures[i<textures.size()?i:0];device_->CreateShaderResourceView(t->resource.Get(),&t->srvDesc,dst);dst.ptr+=srvDescriptorSize_;
    }
    ComPtr<ID3D12CommandAllocator> uploadAllocator;ComPtr<ID3D12GraphicsCommandList> uploadCommands;
    auto* commands=commandList_.Get();
    if(!recording_){
        layerCheck(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&uploadAllocator)),"Layered prewarm allocator");
        layerCheck(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,uploadAllocator.Get(),nullptr,IID_PPV_ARGS(&uploadCommands)),"Layered prewarm commands");
        commands=uploadCommands.Get();
    }
    commands->CopyBufferRegion(entry.parameters.Get(),0,entry.upload.Get(),0,bytes);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={entry.parameters.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    commands->ResourceBarrier(1,&barrier);
    if(uploadCommands){
        layerCheck(uploadCommands->Close(),"Layered prewarm close");
        ID3D12CommandList* lists[]{uploadCommands.Get()};commandQueue_->ExecuteCommandLists(1,lists);
        const auto signal=++fenceValue_;layerCheck(commandQueue_->Signal(fence_.Get(),signal),"Layered prewarm signal");
        if(fence_->GetCompletedValue()<signal){
            layerCheck(fence_->SetEventOnCompletion(signal,static_cast<HANDLE>(fenceEvent_)),"Layered prewarm event");
            if(WaitForSingleObject(static_cast<HANDLE>(fenceEvent_),INFINITE)!=WAIT_OBJECT_0)throw std::runtime_error("Layered prewarm wait");
        }
    }
    WorldResourceCounts counts;counts.geometryBuffers=2;counts.payloadBytes=counts.apiAllocationBytes=bytes*2;
    counts.apiAllocationBytesEstimated=true;counts.materialDescriptors=kWorldLayerTextureLimit;worldScopes_.commit(counts);
    *it=std::move(entry);state.selected=&*it;
#else
    (void)owner;(void)set;throw std::runtime_error("D3D12 layered materials unavailable");
#endif
}
void D3D12RenderBackend::releaseWorldLayeredMaterials(std::uint64_t owner){
#if defined(_WIN32)
    if(!layeredMaterials_)return;auto& state=*layeredMaterials_;
    for(auto& entry:state.entries)if(owner==~0ull||entry.owner==owner){
        if(state.selected==&entry)state.selected=nullptr;
        if(entry.table!=~0u)state.freeTables.push_back(entry.table);
        entry={};
    }
    if(owner==~0ull)layeredMaterials_.reset();
#else
    (void)owner;
#endif
}
