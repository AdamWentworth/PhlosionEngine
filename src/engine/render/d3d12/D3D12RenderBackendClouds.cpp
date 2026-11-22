#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldCloudShader.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include <cstring>
using namespace engine::render;
#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto cloudRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void cloudCheck(HRESULT r,const char* why){if(FAILED(r))throw std::runtime_error(why);}
void cloudBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cmd->ResourceBarrier(1,&v);
}
}
struct D3D12RenderBackend::CloudState {
    struct Image{SpriteTexture texture;UINT uav=0,clearIndex=0;D3D12_RESOURCE_STATES state=cloudRead;};
    struct Entry{
        WorldCloudView view;WorldCloudRows parameters{};WorldResourceCounts counts;
        Image shadow,integrated,depth,resolved,skyModel;
        ComPtr<ID3D12Resource> tiles;UINT tileSrv=0,tileUav=0;
        D3D12_RESOURCE_STATES tileState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        std::uint64_t frame=~0ull,used=0;bool resolvedThisFrame=false;
    };
    struct Dispatch{ComPtr<ID3D12Resource> constants;};
    std::array<Entry,8> entries;std::array<std::array<Dispatch,16>,3> dispatches;
    ComPtr<ID3D12RootSignature> root;std::array<ComPtr<ID3D12PipelineState>,5> pipelines;
    ComPtr<ID3D12CommandSignature> indirect;
    ComPtr<ID3D12DescriptorHeap> clearHeap;ComPtr<ID3D12Resource> clearArgs;UINT nextClear=0;
    std::vector<UINT> descriptors;std::uint64_t frame=~0ull,clock=0;unsigned count=0;
    Entry* selected=nullptr;ComPtr<ID3D12Resource> readback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> footprints{};UINT64 tileOffset=0;
    WorldCloudDiagnostic diagnostic;
};
void D3D12RenderBackend::recordWorldClouds(const WorldCloudView& u,bool resolve) {
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_)throw std::runtime_error("Cloud pass admission");
    WorldResourceScopeStats owner;if(!worldScopes_.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Cloud owner");
    auto parameters=packWorldClouds(u);
    if(u.quality==0&&resolve){worldClouds_=cloudConsumerUniforms(u,false);worldClouds_[8][3]=worldSkyModelDescriptor_!=0xffffffffu?1.f:0.f;return;}
    if(!clouds_)clouds_=std::make_shared<CloudState>();auto& t=*clouds_;
    if(t.frame!=frameCounter_){t.frame=frameCounter_;t.count=0;}
    if(t.count>=16)throw std::runtime_error("Cloud frame view budget");
    const auto cpu=[&](UINT d){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(d)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT d){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(d)*srvDescriptorSize_;return h;};
    const auto descriptor=[&](){
        UINT d;if(!freeWorldTextureDescriptors_.empty()){d=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else{if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Cloud descriptor budget");d=nextSrvDescriptorIndex_++;}
        t.descriptors.push_back(d);return d;
    };
    const auto buffer=[&](ComPtr<ID3D12Resource>& dst,UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=d.DepthOrArraySize=d.MipLevels=1;
        d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        cloudCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&dst)),"Cloud buffer");
    };
    if(!t.clearHeap){
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.NumDescriptors=40;
        cloudCheck(device_->CreateDescriptorHeap(&h,IID_PPV_ARGS(&t.clearHeap)),"Cloud CPU clear descriptors");
        buffer(t.clearArgs,256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* dst=nullptr;cloudCheck(t.clearArgs->Map(0,nullptr,&dst),"Cloud dispatch reset map");
        const UINT args[]{0,1,1};std::memcpy(dst,args,sizeof(args));t.clearArgs->Unmap(0,nullptr);
    }
    const auto clearCpu=[&](UINT index){auto h=t.clearHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*srvDescriptorSize_;return h;};
    if(!t.root){
        std::array<D3D12_DESCRIPTOR_RANGE,13> ranges{};std::array<D3D12_ROOT_PARAMETER,14> roots{};
        for(unsigned i=0;i<10;++i){
            ranges[i]={i>=7?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i>=7?i-7:i,0,0};
            roots[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[i].DescriptorTable={1,&ranges[i]};
        }
        for(unsigned i=0;i<3;++i){ranges[10+i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,7+i,0,0};roots[11+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[11+i].DescriptorTable={1,&ranges[10+i]};}
        roots[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;roots[10].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC samplers[2]{};
        for(unsigned i=0;i<2;++i){auto& s=samplers[i];s.ShaderRegister=i;s.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU=s.AddressV=s.AddressW=i?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;}
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=14;desc.pParameters=roots.data();desc.NumStaticSamplers=2;desc.pStaticSamplers=samplers;
        ComPtr<ID3DBlob> blob,error;cloudCheck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Cloud root serialize");
        cloudCheck(prepareRootSignature(device_.Get(),0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&t.root)),"Cloud root");
        D3D12_INDIRECT_ARGUMENT_DESC arg{};arg.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC signature{sizeof(D3D12_DISPATCH_ARGUMENTS),1,&arg,0};
        cloudCheck(device_->CreateCommandSignature(&signature,nullptr,IID_PPV_ARGS(&t.indirect)),"Cloud indirect signature");
    }
    for(unsigned pass=0;pass<5;++pass)if(!t.pipelines[pass]){
        auto source=worldCloudHlsl(pass);ComPtr<ID3DBlob> blob,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,blob,error))
            throw std::runtime_error("Cloud shader "+std::to_string(pass)+": "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=t.root.Get();d.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        cloudCheck(prepareComputePipeline(device_.Get(),&d,IID_PPV_ARGS(&t.pipelines[pass])),"Cloud pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.used&&v.view.owner==u.owner&&v.view.view.identity==u.view.identity;});
    if(resolve){
        if(found==t.entries.end()||found->frame!=frameCounter_||found->parameters!=parameters||found->resolvedThisFrame)
            throw std::invalid_argument("Cloud resolve has no matching current producer");
    }else{
        if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
            if(it->frame!=frameCounter_&&(found==t.entries.end()||it->used<found->used))found=it;
        if(found==t.entries.end()||found->frame==frameCounter_)throw std::runtime_error("Cloud view already prepared");
    }
    auto& v=*found;const bool enabled=u.quality!=0;
    const unsigned lowW=enabled?unsigned(parameters[0][2]):1,lowH=enabled?unsigned(parameters[0][3]):1,shadowRes=enabled?unsigned(parameters[1][0]):1;
    const unsigned fullW=enabled?unsigned(u.view.width):1,fullH=enabled?unsigned(u.view.height):1;
    if(!v.used||v.view.owner!=u.owner||v.parameters[0]!=parameters[0]||v.parameters[1]!=parameters[1]||(v.view.quality!=0)!=enabled){
        waitForGpu();if(v.counts.textures)worldScopes_.reclaim(v.view.owner,v.counts);v.counts={};
        const UINT64 tileBytes=(enabled?UINT64(parameters[23][0])+3:4)*4;
        const UINT64 bytes=UINT64(shadowRes)*shadowRes*8+UINT64(lowW)*lowH*12+UINT64(fullW)*fullH*8+tileBytes+128*64*8;
        if(bytes>256ull*1024*1024)throw std::runtime_error("Cloud view memory limit");
        const bool own=worldScopes_.active()==0;if(own&&!worldScopes_.begin(u.owner))throw std::runtime_error("Cloud scope");
        if(worldScopes_.active()!=u.owner||!worldScopes_.admit(bytes)){if(own)worldScopes_.end();throw std::runtime_error("Cloud allocation budget");}
        const auto image=[&](CloudState::Image& target,unsigned w,unsigned h,DXGI_FORMAT format){
            target.texture.resource.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;
            d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            cloudCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,cloudRead,nullptr,IID_PPV_ARGS(&target.texture.resource)),"Cloud image");
            target.state=cloudRead;if(!target.uav){target.texture.descriptorIndex=descriptor();target.uav=descriptor();target.clearIndex=t.nextClear++;}
            auto& srv=target.texture.srvDesc;srv={};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
            target.texture.valid=target.texture.hasSrvDesc=true;target.texture.scope=u.owner;
            device_->CreateShaderResourceView(target.texture.resource.Get(),&srv,cpu(target.texture.descriptorIndex));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(target.texture.resource.Get(),nullptr,&uv,cpu(target.uav));
            device_->CreateUnorderedAccessView(target.texture.resource.Get(),nullptr,&uv,clearCpu(target.clearIndex));
        };
        try{
            image(v.shadow,shadowRes,shadowRes,DXGI_FORMAT_R16G16B16A16_FLOAT);
            image(v.integrated,lowW,lowH,DXGI_FORMAT_R16G16B16A16_FLOAT);
            image(v.depth,lowW,lowH,DXGI_FORMAT_R32_FLOAT);
            image(v.resolved,fullW,fullH,DXGI_FORMAT_R16G16B16A16_FLOAT);
            image(v.skyModel,128,64,DXGI_FORMAT_R16G16B16A16_FLOAT);
            v.tiles.Reset();buffer(v.tiles,tileBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            v.tileState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;if(!v.tileSrv){v.tileSrv=descriptor();v.tileUav=descriptor();}
            D3D12_SHADER_RESOURCE_VIEW_DESC sr{};sr.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sr.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sr.Buffer.NumElements=UINT(tileBytes/4);sr.Buffer.StructureByteStride=4;device_->CreateShaderResourceView(v.tiles.Get(),&sr,cpu(v.tileSrv));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=UINT(tileBytes/4);uv.Buffer.StructureByteStride=4;
            device_->CreateUnorderedAccessView(v.tiles.Get(),nullptr,&uv,cpu(v.tileUav));
            v.counts.textures=5;v.counts.geometryBuffers=1;v.counts.textureDescriptors=12;v.counts.payloadBytes=v.counts.apiAllocationBytes=bytes;v.counts.apiAllocationBytesEstimated=true;
            worldScopes_.commit(v.counts);if(own)worldScopes_.end();
        }catch(...){if(own)worldScopes_.end();throw;}
        for(const auto& [key,base]:worldMaterialDescriptorBlocks_)if(key.lightProjectionTextureDescriptorIndex==v.skyModel.texture.descriptorIndex)
            device_->CreateShaderResourceView(v.skyModel.texture.resource.Get(),&v.skyModel.texture.srvDesc,cpu(base+6));
        for(const auto& [key,base]:worldMaterialDescriptorBlocks_)for(unsigned i=0;i<2;++i){
            const auto& img=i?v.resolved:v.shadow;
            if((i?key.lightProjectionTextureDescriptorIndex:key.envTextureDescriptorIndex)==img.texture.descriptorIndex)
                device_->CreateShaderResourceView(img.texture.resource.Get(),&img.texture.srvDesc,cpu(base+5+i));
        }
    }
    const bool own=worldScopes_.active()==0;if(own&&!worldScopes_.begin(u.owner))throw std::runtime_error("Cloud texture scope");
    const auto assetOwner=worldScopes_.assetOwner();
    auto* weather=ensureWorldTexture(&u.weather);auto* noise=ensureWorldTexture(&u.noise);if(own)worldScopes_.end();
    if(!weather||!noise||weather->scope!=assetOwner||noise->scope!=assetOwner)throw std::runtime_error("Cloud texture inputs");
    cloudBarrier(commandList_.Get(),weather->resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,cloudRead);
    if(noise!=weather)cloudBarrier(commandList_.Get(),noise->resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,cloudRead);
    if(resolve)std::copy(worldFog_.begin(),worldFog_.end(),parameters.begin()+32);
    auto& dispatch=t.dispatches[frameIndex_][t.count++];
    if(!dispatch.constants)buffer(dispatch.constants,(sizeof(parameters)+255)&~255ull,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* map=nullptr;cloudCheck(dispatch.constants->Map(0,nullptr,&map),"Cloud constants map");std::memcpy(map,parameters.data(),sizeof(parameters));dispatch.constants->Unmap(0,nullptr);
    const auto transition=[&](CloudState::Image& image,D3D12_RESOURCE_STATES state){cloudBarrier(commandList_.Get(),image.texture.resource.Get(),image.state,state);image.state=state;};
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    auto depthDescriptor=weather->descriptorIndex;ID3D12Resource* opaque=nullptr;
    if(resolve){
        if(!worldSurfaceReady_||!worldSurfaceTarget_||worldSurfaceTarget_->probeScratch||worldSurfacePlanar_||
           !surfaceContactViewMatches(worldSurfaceView_,packWorldContactVisibility(u.view.viewProjection.data(),u.view.width,u.view.height,.8f,.015f)))
            throw std::runtime_error("Cloud primary depth mismatch");
        depthDescriptor=worldSurfaceDepth_;
        for(auto& [key,img]:worldTextures_)if(img.descriptorIndex==depthDescriptor)opaque=img.resource.Get();
        if(!opaque)throw std::runtime_error("Cloud primary depth unavailable");
        cloudBarrier(commandList_.Get(),opaque,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,cloudRead);
    }
    commandList_->SetComputeRootSignature(t.root.Get());commandList_->SetComputeRootConstantBufferView(10,dispatch.constants->GetGPUVirtualAddress());
    const UINT inputs[]{depthDescriptor,weather->descriptorIndex,noise->descriptorIndex,v.shadow.texture.descriptorIndex,
        v.integrated.texture.descriptorIndex,v.depth.texture.descriptorIndex,v.tileSrv};
    for(unsigned i=0;i<7;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(inputs[i]));
    commandList_->SetComputeRootDescriptorTable(8,gpu(v.depth.uav));commandList_->SetComputeRootDescriptorTable(9,gpu(v.tileUav));
    for(unsigned i=0;i<2;++i)commandList_->SetComputeRootDescriptorTable(11+i,gpu(resolve&&worldFogDescriptors_[i]!=0xffffffffu?worldFogDescriptors_[i]:weather->descriptorIndex));
    commandList_->SetComputeRootDescriptorTable(13,gpu(resolve?v.skyModel.texture.descriptorIndex:weather->descriptorIndex));
    if(!resolve){
        transition(v.skyModel,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);commandList_->SetComputeRootDescriptorTable(7,gpu(v.skyModel.uav));
        commandList_->SetPipelineState(t.pipelines[4].Get());{RenderPhaseScope phase(*this,"sky-model-filter");commandList_->Dispatch(16,8,1);}
        transition(v.skyModel,cloudRead);
        if(enabled){
        transition(v.shadow,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);commandList_->SetComputeRootDescriptorTable(7,gpu(v.shadow.uav));
        commandList_->SetPipelineState(t.pipelines[0].Get());{RenderPhaseScope phase(*this,"cloud-shadow");commandList_->Dispatch((shadowRes+7)/8,(shadowRes+7)/8,1);}
        transition(v.shadow,cloudRead);}
        v.view=u;v.parameters=packWorldClouds(u);v.frame=frameCounter_;v.used=++t.clock;v.resolvedThisFrame=false;
    }else{
        cloudBarrier(commandList_.Get(),v.tiles.Get(),v.tileState,D3D12_RESOURCE_STATE_COPY_DEST);
        commandList_->CopyBufferRegion(v.tiles.Get(),0,t.clearArgs.Get(),0,12);
        cloudBarrier(commandList_.Get(),v.tiles.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);v.tileState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition(v.integrated,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(v.depth,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float empty[]{0,0,0,1},farDepth[]{1e7f,0,0,0};
        commandList_->ClearUnorderedAccessViewFloat(gpu(v.integrated.uav),clearCpu(v.integrated.clearIndex),v.integrated.texture.resource.Get(),empty,0,nullptr);
        commandList_->ClearUnorderedAccessViewFloat(gpu(v.depth.uav),clearCpu(v.depth.clearIndex),v.depth.texture.resource.Get(),farDepth,0,nullptr);
        D3D12_RESOURCE_BARRIER ready{};ready.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;commandList_->ResourceBarrier(1,&ready);
        commandList_->SetComputeRootDescriptorTable(7,gpu(v.integrated.uav));commandList_->SetPipelineState(t.pipelines[1].Get());
        {RenderPhaseScope phase(*this,"cloud-visible");commandList_->Dispatch((UINT(parameters[1][2])+7)/8,(UINT(parameters[1][3])+7)/8,1);}
        const auto tileRead=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        cloudBarrier(commandList_.Get(),v.tiles.Get(),v.tileState,tileRead);v.tileState=tileRead;
        commandList_->SetPipelineState(t.pipelines[2].Get());
        {RenderPhaseScope phase(*this,"cloud-integrate");commandList_->ExecuteIndirect(t.indirect.Get(),1,v.tiles.Get(),0,nullptr,0);}
        transition(v.integrated,cloudRead);transition(v.depth,cloudRead);transition(v.resolved,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList_->SetComputeRootDescriptorTable(7,gpu(v.resolved.uav));commandList_->SetPipelineState(t.pipelines[3].Get());
        {RenderPhaseScope phase(*this,"cloud-reconstruct");commandList_->Dispatch((UINT(u.view.width)+7)/8,(UINT(u.view.height)+7)/8,1);}
        transition(v.resolved,cloudRead);v.resolvedThisFrame=true;
        cloudBarrier(commandList_.Get(),opaque,cloudRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    cloudBarrier(commandList_.Get(),weather->resource.Get(),cloudRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(noise!=weather)cloudBarrier(commandList_.Get(),noise->resource.Get(),cloudRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    worldSkyCloud_=parameters;worldClouds_=cloudConsumerUniforms(u,resolve);worldCloudDescriptors_={v.shadow.texture.descriptorIndex,v.resolved.texture.descriptorIndex};
    worldCloudOwner_=u.owner;worldSkyModelDescriptor_=v.skyModel.texture.descriptorIndex;worldClouds_[8][3]=1;t.selected=enabled?&v:nullptr;
}
#endif
void D3D12RenderBackend::prepareWorldClouds(const WorldCloudView& view){
#if defined(_WIN32)
    recordWorldClouds(view,false);
#else
    (void)view;throw std::runtime_error("D3D12 clouds unavailable");
#endif
}
void D3D12RenderBackend::resolveWorldClouds(const WorldCloudView& view){
#if defined(_WIN32)
    recordWorldClouds(view,true);
#else
    (void)view;throw std::runtime_error("D3D12 clouds unavailable");
#endif
}
void D3D12RenderBackend::clearWorldClouds(){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    worldSkyModelDescriptor_=0xffffffffu;worldSkyCloud_={};worldClouds_={};worldCloudDescriptors_.fill(0xffffffffu);worldCloudOwner_=0;
#endif
}
#if defined(_WIN32)
const D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldCloudTexture(std::uint32_t d) const{
    if(clouds_)for(const auto& v:clouds_->entries)if(v.used)
        for(const auto* i:{&v.shadow,&v.resolved,&v.skyModel})if(i->texture.descriptorIndex==d)return &i->texture;
    return nullptr;
}
#endif
void D3D12RenderBackend::releaseWorldClouds(std::uint64_t owner){
#if defined(_WIN32)
    if(!clouds_)return;auto& t=*clouds_;
    if(owner==~0ull||owner==worldCloudOwner_){worldSkyModelDescriptor_=0xffffffffu;worldSkyCloud_={};worldClouds_={};worldCloudDescriptors_.fill(0xffffffffu);worldCloudOwner_=0;}
    for(auto& v:t.entries)if(owner==~0ull||v.view.owner==owner){
        for(auto* i:{&v.shadow,&v.integrated,&v.depth,&v.resolved,&v.skyModel})i->texture.resource.Reset();
        v.tiles.Reset();v.used=0;v.view={};v.parameters={};v.counts={};if(t.selected==&v)t.selected=nullptr;
    }
    if(owner==~0ull){for(UINT d:t.descriptors)freeWorldTextureDescriptors_.push_back(d);clouds_.reset();}
#else
    (void)owner;
#endif
}
bool D3D12RenderBackend::captureWorldClouds(){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!clouds_||!clouds_->selected||clouds_->readback||!clouds_->selected->resolvedThisFrame)return false;
    auto& t=*clouds_;auto& v=*t.selected;UINT64 total=0;unsigned index=0;
    for(auto* image:{&v.shadow,&v.integrated,&v.resolved,&v.skyModel}){
        auto desc=image->texture.resource->GetDesc();UINT64 bytes=0;
        device_->GetCopyableFootprints(&desc,0,1,0,&t.footprints[index],nullptr,nullptr,&bytes);
        t.footprints[index++].Offset=total;total=(total+bytes+511)&~511ull;
    }
    t.tileOffset=total;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=total+512;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    cloudCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.readback)),"Cloud readback");
    index=0;for(auto* image:{&v.shadow,&v.integrated,&v.resolved,&v.skyModel}){
        cloudBarrier(commandList_.Get(),image->texture.resource.Get(),image->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=image->texture.resource.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=t.readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=t.footprints[index++];
        commandList_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        cloudBarrier(commandList_.Get(),image->texture.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,image->state);
    }
    cloudBarrier(commandList_.Get(),v.tiles.Get(),v.tileState,D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->CopyBufferRegion(t.readback.Get(),t.tileOffset,v.tiles.Get(),0,4);
    cloudBarrier(commandList_.Get(),v.tiles.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,v.tileState);
    t.diagnostic={unsigned(v.view.view.width),unsigned(v.view.view.height),unsigned(v.parameters[1][0]),0,v.counts.payloadBytes,v.view.owner,v.view.view.identity,v.view.view.frame};
    return true;
#else
    return false;
#endif
}
WorldCloudDiagnostic D3D12RenderBackend::readWorldClouds(){
#if defined(_WIN32)
    if(recording_||!clouds_||!clouds_->readback)return {};
    waitForGpu();auto& t=*clouds_;auto out=t.diagnostic;void* data=nullptr;cloudCheck(t.readback->Map(0,nullptr,&data),"Cloud readback map");
    unsigned index=0;for(auto* output:{&out.shadows,&out.integrated,&out.resolved,&out.skyModel}){
        const auto& f=t.footprints[index++];output->resize(std::size_t(f.Footprint.Width)*f.Footprint.Height);
        for(unsigned y=0;y<f.Footprint.Height;++y)for(unsigned x=0;x<f.Footprint.Width;++x)
            (*output)[std::size_t(y)*f.Footprint.Width+x]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(
                static_cast<const char*>(data)+f.Offset+std::size_t(y)*f.Footprint.RowPitch+x*8));
    }
    std::memcpy(&out.visibleTiles,static_cast<const char*>(data)+t.tileOffset,4);t.readback->Unmap(0,nullptr);t.readback.Reset();return out;
#else
    return {};
#endif
}
