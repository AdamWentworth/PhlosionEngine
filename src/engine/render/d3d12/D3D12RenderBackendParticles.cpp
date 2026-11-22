#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldParticleLightingShader.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include <cstring>
using namespace engine::render;
#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::ParticleLightingState {
    struct Entry {
        SpriteTexture texture;UINT uav=0;
        WorldResourceScopeId owner=0;WorldTemporalView view;
        WorldResourceCounts counts;
        std::uint64_t frame=~std::uint64_t(0),used=0;
        unsigned tiles=0,texels=0;std::string key;
    };
    struct Dispatch {ComPtr<ID3D12Resource> constants,quads;UINT shadow=0,data=0;};
    std::array<Entry,4> entries;
    std::array<std::array<Dispatch,4>,3> dispatches;
    std::uint64_t frame=~std::uint64_t(0),clock=0;unsigned count=0;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;
    std::vector<UINT> descriptors;
    Entry* selected=nullptr;ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    WorldParticleLightingDiagnostic diagnostic;
};
namespace {
constexpr auto particleRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void particleCheck(HRESULT hr,const char* why){if(FAILED(hr))throw std::runtime_error(why);}
void particleBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cmd->ResourceBarrier(1,&v);
}
}
#endif
WorldParticleLightingResult D3D12RenderBackend::updateWorldParticleLighting(const WorldParticleLightingView& u){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_)throw std::runtime_error("Particle lighting pass admission");
    WorldResourceScopeStats owner;if(!worldScopes_.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Particle lighting owner");
    auto layout=particleLightingLayout(u);
    if(!particleLighting_)particleLighting_=std::make_shared<ParticleLightingState>();auto& t=*particleLighting_;
    if(t.frame!=frameCounter_){t.frame=frameCounter_;t.count=0;}if(t.count>=4)throw std::runtime_error("Particle lighting frame view budget");
    const auto cpu=[&](UINT i){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT i){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*srvDescriptorSize_;return h;};
    const auto descriptor=[&](){
        UINT d;if(!freeWorldTextureDescriptors_.empty()){d=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else{if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Particle lighting descriptor budget");d=nextSrvDescriptorIndex_++;}
        t.descriptors.push_back(d);return d;
    };
    if(!t.root){
        std::array<D3D12_DESCRIPTOR_RANGE,7> ranges{};std::array<D3D12_ROOT_PARAMETER,8> params{};
        for(unsigned i=0;i<6;++i){ranges[i]={i==5?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i==5?0:i,0,0};params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
        ranges[6]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,5,0,0};params[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[7].DescriptorTable={1,&ranges[6]};
        params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[6].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=8;d.pParameters=params.data();d.NumStaticSamplers=1;d.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;particleCheck(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Particle lighting root serialize");
        particleCheck(prepareRootSignature(device_.Get(),0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&t.root)),"Particle lighting root");
    }
    if(!t.pipeline){
        const auto source=worldParticleLightingHlsl();ComPtr<ID3DBlob> shader,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Particle lighting shader: "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=t.root.Get();d.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        particleCheck(prepareComputePipeline(device_.Get(),&d,IID_PPV_ARGS(&t.pipeline)),"Particle lighting pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.owner==u.owner&&v.view.identity==u.view.identity;});
    if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
        if(it->frame!=frameCounter_&&(found==t.entries.end()||it->used<found->used))found=it;
    if(found==t.entries.end()||found->frame==frameCounter_)throw std::runtime_error("Particle lighting view already recorded");
    auto& v=*found;const unsigned slot=unsigned(found-t.entries.begin());
    if(!v.used||v.owner!=u.owner){
        waitForGpu();if(v.counts.textures){worldScopes_.reclaim(v.owner,v.counts);v.counts={};}
        constexpr auto bytes=std::uint64_t(particleLightingExtent)*particleLightingExtent*8;
        const bool scope=worldScopes_.active()==0;if(scope&&!worldScopes_.begin(u.owner))throw std::runtime_error("Particle lighting resource admission");
        if(worldScopes_.active()!=u.owner||!worldScopes_.admit(bytes)){if(scope)worldScopes_.end();throw std::runtime_error("Particle lighting resource budget");}
        try {
            v.texture.resource.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=particleLightingExtent;d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            particleCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,particleRead,nullptr,IID_PPV_ARGS(&v.texture.resource)),"Particle lighting image");
            if(!v.uav){v.texture.descriptorIndex=descriptor();v.uav=descriptor();}
            auto& srv=v.texture.srvDesc;srv={};srv.Format=d.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
            v.texture.valid=v.texture.hasSrvDesc=true;device_->CreateShaderResourceView(v.texture.resource.Get(),&srv,cpu(v.texture.descriptorIndex));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=d.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(v.texture.resource.Get(),nullptr,&uv,cpu(v.uav));
            v.counts={};v.counts.textures=1;v.counts.textureDescriptors=2;v.counts.payloadBytes=bytes;v.counts.apiAllocationBytes=bytes;v.counts.apiAllocationBytesEstimated=true;
            worldScopes_.commit(v.counts);if(scope)worldScopes_.end();
        }catch(...){if(scope)worldScopes_.end();throw;}
        for(const auto& [key,base]:worldMaterialDescriptorBlocks_)if(key.emissiveTextureDescriptorIndex==v.texture.descriptorIndex)
            device_->CreateShaderResourceView(v.texture.resource.Get(),&v.texture.srvDesc,cpu(base+4));
    }
    auto parameters=packParticleLighting(u,layout.result.tiles,worldDiffuseProbes_,worldShadowReady_?worldShadowView_:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv(),worldLocalShadows_.uniforms);
    const auto* depth=findWorldPrecipitationDepth(u,parameters[26]);parameters[25][0]=depth?1.f:0.f;layout.result.sharedDepth=depth!=nullptr;
    auto& dispatch=t.dispatches[frameIndex_][t.count++];
    const auto upload=[&](ComPtr<ID3D12Resource>& buffer,std::size_t capacity,const void* input,std::size_t bytes){
        if(!buffer){
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=(capacity+255)&~255ull;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            particleCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)),"Particle lighting upload");
        }
        void* mapped=nullptr;particleCheck(buffer->Map(0,nullptr,&mapped),"Particle lighting upload map");if(bytes)std::memcpy(mapped,input,bytes);buffer->Unmap(0,nullptr);
    };
    upload(dispatch.constants,sizeof(parameters),parameters.data(),sizeof(parameters));
    upload(dispatch.quads,particleLightingCapacity*6*16,layout.data.data(),layout.data.size()*16);
    if(!dispatch.data){dispatch.data=descriptor();D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Buffer.NumElements=particleLightingCapacity*6;d.Buffer.StructureByteStride=16;device_->CreateShaderResourceView(dispatch.quads.Get(),&d,cpu(dispatch.data));}
    if(!dispatch.shadow)dispatch.shadow=descriptor();writeWorldShadowSrv(dispatch.shadow);
    const auto null=ensureWorldIndirectNull();
    const UINT moment=worldMomentSrv_!=0xffffffffu?worldMomentSrv_+2:dispatch.shadow;
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetComputeRootSignature(t.root.Get());commandList_->SetComputeRootConstantBufferView(6,dispatch.constants->GetGPUVirtualAddress());
    const UINT sources[]{dispatch.shadow,moment,worldIndirectDescriptors_[0]==0xffffffffu?null:worldIndirectDescriptors_[0],worldIndirectDescriptors_[1]==0xffffffffu?null:worldIndirectDescriptors_[1],dispatch.data,v.uav};
    for(unsigned i=0;i<6;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(sources[i]));
    commandList_->SetComputeRootDescriptorTable(7,gpu(depth?depth->descriptorIndex:dispatch.shadow));
    if(worldShadowDepth_)particleBarrier(commandList_.Get(),worldShadowDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,particleRead);
    particleBarrier(commandList_.Get(),v.texture.resource.Get(),particleRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(layout.result.tiles){RenderPhaseScope phase(*this,"particle-lighting");commandList_->SetPipelineState(t.pipeline.Get());commandList_->Dispatch(layout.result.tiles,1,1);}
    particleBarrier(commandList_.Get(),v.texture.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,particleRead);
    if(worldShadowDepth_)particleBarrier(commandList_.Get(),worldShadowDepth_.Get(),particleRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.owner=u.owner;v.view=u.view;v.frame=frameCounter_;v.used=++t.clock;v.tiles=layout.result.tiles;v.texels=layout.result.texels;
    v.key="phlosion://particle-lighting/"+std::to_string(slot);t.selected=&v;layout.result.texture=v.key;return layout.result;
#else
    throw std::runtime_error("D3D12 particle lighting unavailable");
#endif
}
#if defined(_WIN32)
D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldParticleLightingTexture(const char* key){
    if(!key||std::string_view(key).find("phlosion://particle-lighting/")!=0)return nullptr;
    if(particleLighting_)for(auto& v:particleLighting_->entries)if(v.key==key&&v.used&&v.frame==frameCounter_)return &v.texture;
    throw std::runtime_error("Stale particle lighting texture");
}
const D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldParticleLightingTexture(std::uint32_t descriptor) const{
    if(particleLighting_)for(const auto& v:particleLighting_->entries)if(v.used&&v.texture.descriptorIndex==descriptor)return &v.texture;return nullptr;
}
#endif
void D3D12RenderBackend::releaseWorldParticleLighting(std::uint64_t owner){
#if defined(_WIN32)
    if(!particleLighting_)return;auto& t=*particleLighting_;
    for(auto& v:t.entries)if(owner==~std::uint64_t(0)||v.owner==owner){v.texture.resource.Reset();v.used=0;v.owner=0;v.counts={};v.key.clear();if(t.selected==&v)t.selected=nullptr;}
    if(owner==~std::uint64_t(0)){for(auto d:t.descriptors)freeWorldTextureDescriptors_.push_back(d);particleLighting_.reset();}
#endif
}
bool D3D12RenderBackend::captureWorldParticleLighting(){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!particleLighting_||!particleLighting_->selected||particleLighting_->readback)return false;
    auto& t=*particleLighting_;auto& v=*t.selected;auto desc=v.texture.resource->GetDesc();UINT64 bytes=0;
    device_->GetCopyableFootprints(&desc,0,1,0,&t.footprint,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    particleCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.readback)),"Particle lighting readback");
    particleBarrier(commandList_.Get(),v.texture.resource.Get(),particleRead,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=v.texture.resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=t.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=t.footprint;commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    particleBarrier(commandList_.Get(),v.texture.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,particleRead);
    t.diagnostic={particleLightingExtent,particleLightingExtent,v.tiles,v.texels,v.view.identity,v.view.frame,v.counts.payloadBytes};return true;
#else
    return false;
#endif
}
WorldParticleLightingDiagnostic D3D12RenderBackend::readWorldParticleLighting(){
#if defined(_WIN32)
    if(recording_||!particleLighting_||!particleLighting_->readback)return {};
    waitForGpu();auto& t=*particleLighting_;auto result=t.diagnostic;void* mapped=nullptr;particleCheck(t.readback->Map(0,nullptr,&mapped),"Particle lighting readback map");
    result.lighting.resize(std::size_t(result.width)*result.height);
    for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x)
        result.lighting[std::size_t(y)*result.width+x]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(mapped)+y*t.footprint.Footprint.RowPitch+x*8));
    t.readback->Unmap(0,nullptr);t.readback.Reset();return result;
#else
    return {};
#endif
}
