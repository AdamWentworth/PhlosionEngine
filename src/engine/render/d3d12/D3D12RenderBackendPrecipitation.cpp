#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldPrecipitationShader.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include <cstring>
using namespace engine::render;
#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::PrecipitationState {
    struct Image {SpriteTexture texture;UINT uav=0;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;};
    struct Entry {
        WorldPrecipitationLightingView previous;
        std::array<unsigned,3> dimensions{};
        Image light[2],scatter,depth;
        WorldResourceCounts counts;
        unsigned bank=0;std::uint64_t frame=~std::uint64_t(0),used=0;
        bool history=false;std::string key;
    };
    struct Dispatch {ComPtr<ID3D12Resource> constants;UINT shadow=0;};
    std::array<Entry,8> entries;
    std::array<std::array<Dispatch,8>,3> dispatches;
    std::uint64_t frame=~std::uint64_t(0),clock=0;unsigned count=0;
    ComPtr<ID3D12RootSignature> root;std::array<ComPtr<ID3D12PipelineState>,3> pipelines;
    std::vector<UINT> descriptors;
    Entry* selected=nullptr;
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::uint64_t readOffset=0;
    WorldPrecipitationLightingDiagnostic diagnostic;
};
namespace {
constexpr auto volumeRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void volCheck(HRESULT hr,const char* why){if(FAILED(hr))throw std::runtime_error(why);}
void volBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cmd->ResourceBarrier(1,&v);
}
}
#endif
WorldPrecipitationLightingResult D3D12RenderBackend::updateWorldPrecipitationLighting(const WorldPrecipitationLightingView& u){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_)throw std::runtime_error("Precipitation lighting pass admission");
    WorldResourceScopeStats owner;if(!worldScopes_.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Precipitation owner");
    const auto dims=precipitationDimensions(u);
    if(!precipitation_)precipitation_=std::make_shared<PrecipitationState>();auto& t=*precipitation_;
    if(t.frame!=frameCounter_){t.frame=frameCounter_;t.count=0;}if(t.count>=8)throw std::runtime_error("Precipitation frame view budget");
    const auto cpu=[&](UINT i){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT i){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*srvDescriptorSize_;return h;};
    const auto descriptor=[&](){
        UINT d;if(!freeWorldTextureDescriptors_.empty()){d=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else{if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Precipitation descriptor budget");d=nextSrvDescriptorIndex_++;}
        t.descriptors.push_back(d);return d;
    };
    if(!t.root){
        std::array<D3D12_DESCRIPTOR_RANGE,11> ranges{};std::array<D3D12_ROOT_PARAMETER,12> params{};
        for(unsigned i=0;i<9;++i){ranges[i]={i>=7?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i>=7?i-7:i,0,0};params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
        ranges[9]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,7,0,0};params[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[10].DescriptorTable={1,&ranges[9]};
        ranges[10]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,8,0,0};
        params[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[11].DescriptorTable={1,&ranges[10]};
        params[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[9].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC samplers[3]{};
        for(unsigned i=0;i<2;++i){auto& s=samplers[i];s.ShaderRegister=i;s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;}
        samplers[2]=samplers[0];samplers[2].ShaderRegister=2;
        samplers[2].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;samplers[2].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=12;d.pParameters=params.data();d.NumStaticSamplers=3;d.pStaticSamplers=samplers;
        ComPtr<ID3DBlob> blob,error;volCheck(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Precipitation root serialize");
        volCheck(prepareRootSignature(device_.Get(),0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&t.root)),"Precipitation root");
    }
    for(unsigned i=0;i<3;++i)if(!t.pipelines[i]){
        const auto source=worldPrecipitationHlsl(i);ComPtr<ID3DBlob> shader,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Precipitation shader "+std::to_string(i)+": "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=t.root.Get();d.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        volCheck(prepareComputePipeline(device_.Get(),&d,IID_PPV_ARGS(&t.pipelines[i])),"Precipitation pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.previous.owner==u.owner&&v.previous.view.identity==u.view.identity&&v.previous.distant==u.distant;});
    if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
        if(it->frame!=frameCounter_&&(found==t.entries.end()||it->used<found->used))found=it;
    if(found==t.entries.end()||found->frame==frameCounter_)throw std::runtime_error("Precipitation view already recorded");
    auto& v=*found;const unsigned slot=unsigned(found-t.entries.begin());
    const bool history=v.used&&precipitationHistoryValid(u,v.previous)&&dims==v.dimensions;
    if(!v.used||dims!=v.dimensions||v.previous.owner!=u.owner||
       u.view.width!=v.previous.view.width||u.view.height!=v.previous.view.height){
        waitForGpu();if(v.counts.textures){worldScopes_.reclaim(v.previous.owner,v.counts);v.counts={};}
        const auto bytes=std::uint64_t(dims[0])*dims[1]*dims[2]*24+std::uint64_t((u.view.width+15)/16)*((u.view.height+15)/16)*4;
        if(bytes>worldPrecipitationLightingByteLimit)throw std::runtime_error("Precipitation view memory budget");
        const bool scope=worldScopes_.active()==0;if(scope&&!worldScopes_.begin(u.owner))throw std::runtime_error("Precipitation resource admission");
        if(worldScopes_.active()!=u.owner||!worldScopes_.admit(bytes)){if(scope)worldScopes_.end();throw std::runtime_error("Precipitation resource budget");}
        const auto create=[&](PrecipitationState::Image& image,unsigned w,unsigned h,DXGI_FORMAT format){
            image.texture.resource.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            volCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,volumeRead,nullptr,IID_PPV_ARGS(&image.texture.resource)),"Precipitation image");
            image.state=volumeRead;if(!image.uav){image.texture.descriptorIndex=descriptor();image.uav=descriptor();}
            auto& srv=image.texture.srvDesc;srv={};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
            image.texture.valid=image.texture.hasSrvDesc=true;device_->CreateShaderResourceView(image.texture.resource.Get(),&srv,cpu(image.texture.descriptorIndex));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(image.texture.resource.Get(),nullptr,&uv,cpu(image.uav));
        };
        try{
            for(auto* image:{&v.light[0],&v.light[1],&v.scatter})create(*image,dims[0]*8,dims[1]*((dims[2]+7)/8),DXGI_FORMAT_R16G16B16A16_FLOAT);
            create(v.depth,(u.view.width+15)/16,(u.view.height+15)/16,DXGI_FORMAT_R32_FLOAT);
            v.counts={};v.counts.textures=4;v.counts.textureDescriptors=8;v.counts.payloadBytes=bytes;v.counts.apiAllocationBytes=bytes;v.counts.apiAllocationBytesEstimated=true;
            worldScopes_.commit(v.counts);if(scope)worldScopes_.end();
        }catch(...){if(scope)worldScopes_.end();throw;}
        // The stable material descriptor tables retain the output SRV. Refresh
        // only this view's slot, which has not been used in the current frame.
        for(const auto& [key,base]:worldMaterialDescriptorBlocks_)
            if(key.emissiveTextureDescriptorIndex==v.scatter.texture.descriptorIndex)
                device_->CreateShaderResourceView(v.scatter.texture.resource.Get(),&v.scatter.texture.srvDesc,cpu(base+4));
        v.dimensions=dims;v.bank=0;
    }
    auto parameters=packPrecipitationLighting(u,history?v.previous:u,history,worldDiffuseProbes_,worldShadowReady_?worldShadowView_:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv());
    std::copy(worldSunPlan_.uniforms.begin(),worldSunPlan_.uniforms.end(),parameters.begin()+600);
    if(worldCloudOwner_==u.owner){parameters[592]=worldClouds_[0];parameters[593]=worldClouds_[1];}
    auto& dispatch=t.dispatches[frameIndex_][t.count++];
    if(!dispatch.constants){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=(sizeof(parameters)+255)&~255ull;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        volCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&dispatch.constants)),"Precipitation constants");
    }
    void* mapped=nullptr;volCheck(dispatch.constants->Map(0,nullptr,&mapped),"Precipitation constants map");std::memcpy(mapped,parameters.data(),sizeof(parameters));dispatch.constants->Unmap(0,nullptr);
    if(!dispatch.shadow)dispatch.shadow=descriptor();writeWorldShadowSrv(dispatch.shadow);
    UINT depthDescriptor=worldDepthSnapshotDescriptor_;
    if(u.usePreparedSurfaceDepth) {
        if(!worldSurfaceReady_ || !worldSurfaceTarget_ || worldSurfaceTarget_->probeScratch || worldSurfacePlanar_ ||
           !worldProbeSurfaceKey_.empty() || worldLocalReflection_[0][3]<0 ||
           !surfaceContactViewMatches(worldSurfaceView_,packWorldContactVisibility(u.view.viewProjection.data(),u.view.width,u.view.height,.8f,.015f)))
            throw std::runtime_error("Fog prepared opaque depth view mismatch");
        depthDescriptor=worldSurfaceDepth_;
    } else {
        if(!captureWorldDepthSnapshot())throw std::runtime_error("Precipitation opaque depth");
        depthDescriptor=worldDepthSnapshotDescriptor_;
    }
    ID3D12Resource* depth=nullptr;for(auto& item:worldTextures_)if(item.second.descriptorIndex==depthDescriptor)depth=item.second.resource.Get();
    if(!depth)throw std::runtime_error("Precipitation depth image");
    volBarrier(commandList_.Get(),depth,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,volumeRead);
    if(worldShadowDepth_)volBarrier(commandList_.Get(),worldShadowDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,volumeRead);
    const unsigned next=1-v.bank;
    const auto transition=[&](auto& image,auto state){volBarrier(commandList_.Get(),image.texture.resource.Get(),image.state,state);image.state=state;};
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetComputeRootSignature(t.root.Get());commandList_->SetComputeRootConstantBufferView(9,dispatch.constants->GetGPUVirtualAddress());
    const auto null=ensureWorldIndirectNull();
    const UINT sources[]{depthDescriptor,v.light[v.bank].texture.descriptorIndex,v.light[next].texture.descriptorIndex,v.depth.texture.descriptorIndex,dispatch.shadow,
        worldIndirectDescriptors_[0]==0xffffffffu?null:worldIndirectDescriptors_[0],worldIndirectDescriptors_[1]==0xffffffffu?null:worldIndirectDescriptors_[1]};
    for(unsigned i=0;i<7;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(sources[i]));
    commandList_->SetComputeRootDescriptorTable(8,gpu(v.depth.uav));
    commandList_->SetComputeRootDescriptorTable(10,gpu(worldCloudOwner_==u.owner?worldCloudDescriptors_[0]:dispatch.shadow));
    commandList_->SetComputeRootDescriptorTable(11,gpu(worldSunDepth_?worldSunDescriptor_:dispatch.shadow));
    const char* names[]{"rain-light-depth","rain-light-volume","rain-light-integrate"};
    for(unsigned pass=u.distant?1:0;pass<3;++pass){
        auto& output=pass==2?v.scatter:v.light[next];
        transition(output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(pass==0)transition(v.depth,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList_->SetComputeRootDescriptorTable(7,gpu(output.uav));
        RenderPhaseScope phase(*this,u.distant?(pass==1?"fog-distant-volume":"fog-distant-integrate"):names[pass]);commandList_->SetPipelineState(t.pipelines[pass].Get());
        if(pass==0)commandList_->Dispatch(((u.view.width+15)/16+7)/8,((u.view.height+15)/16+7)/8,1);
        else if(pass==1)commandList_->Dispatch((dims[0]+3)/4,(dims[1]+3)/4,(dims[2]+3)/4);
        else commandList_->Dispatch((dims[0]+7)/8,(dims[1]+7)/8,1);
        if(pass==0)transition(v.depth,volumeRead);transition(output,volumeRead);
    }
    volBarrier(commandList_.Get(),depth,volumeRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(worldShadowDepth_)volBarrier(commandList_.Get(),worldShadowDepth_.Get(),volumeRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.bank=next;v.previous=u;v.frame=frameCounter_;v.used=++t.clock;v.history=history;v.key="phlosion://precipitation/"+std::to_string(slot);t.selected=&v;
    return {v.key,{float(dims[0]),float(dims[1]),float(dims[2]),8},{u.range,1/u.depthExponent,u.environment.timeOfDayHours/24.f,1}};
#else
    throw std::runtime_error("D3D12 precipitation unavailable");
#endif
}
void D3D12RenderBackend::bindWorldFog(const WorldPrecipitationLightingView* view,const WorldPrecipitationLightingResult* nearVolume,const WorldPrecipitationLightingResult* distantVolume) {
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    worldFog_={};worldFogDescriptors_.fill(0xffffffffu);
    if(!view)return;
    if(!nearVolume&&!distantVolume&&worldSceneColorPassActive_) {
        worldFog_=packWorldFog(*view,{},{});return;
    }
    if(!nearVolume||!distantVolume||!precipitation_||!worldSceneColorPassActive_)throw std::invalid_argument("Fog view binding");
    for(unsigned i=0;i<2;++i) {
        const auto& r=i?*distantVolume:*nearVolume;
        auto found=std::find_if(precipitation_->entries.begin(),precipitation_->entries.end(),[&](const auto& v) {
            return v.key==r.texture&&fogBindingViewMatches(v.previous,*view)&&v.previous.distant==bool(i)&&v.frame==frameCounter_;
        });
        if(found==precipitation_->entries.end())throw std::invalid_argument("Stale fog view texture");
        worldFogDescriptors_[i]=found->scatter.texture.descriptorIndex;
    }
    worldFog_=packWorldFog(*view,*nearVolume,*distantVolume);
#else
    throw std::runtime_error("D3D12 fog unavailable");
#endif
}
#if defined(_WIN32)
const D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldPrecipitationDepth(
        const WorldParticleLightingView& u,std::array<float,4>& forwardRange) const {
    if(!precipitation_)return nullptr;
    for(const auto& v:precipitation_->entries) {
        const auto& p=v.previous;
        if(!v.used||v.frame!=frameCounter_||p.distant||p.owner!=u.owner||
            p.view.identity!=u.view.identity||p.view.generation!=u.view.generation||p.view.frame!=u.view.frame||
            p.view.viewProjection!=u.view.viewProjection||p.view.cameraPosition!=u.view.cameraPosition||
            p.view.width!=u.view.width||p.view.height!=u.view.height)continue;
        const auto h=glm::inverse(glm::make_mat4(u.view.viewProjection.data()))*glm::vec4(0,0,1,1);
        const auto f=glm::normalize(glm::vec3(h)/h.w-glm::make_vec3(u.view.cameraPosition.data()));
        forwardRange={f.x,f.y,f.z,p.range};return &v.depth.texture;
    }
    return nullptr;
}
const D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldPrecipitationTexture(std::uint32_t descriptor) const{
    if(precipitation_)for(const auto& v:precipitation_->entries)
        if(v.used&&v.scatter.texture.valid&&v.scatter.texture.descriptorIndex==descriptor)return &v.scatter.texture;
    return nullptr;
}
D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::findWorldPrecipitationTexture(const char* key){
    if(!key||std::string_view(key).find("phlosion://precipitation/")!=0)return nullptr;
    if(precipitation_)for(auto& v:precipitation_->entries)if(v.key==key&&v.used)return &v.scatter.texture;
    throw std::runtime_error("Precipitation lighting texture is not resident");
}
#endif
void D3D12RenderBackend::releaseWorldPrecipitation(std::uint64_t owner){
#if defined(_WIN32)
    if(!precipitation_)return;auto& t=*precipitation_;
    worldFog_={};worldFogDescriptors_.fill(0xffffffffu);
    for(auto& v:t.entries)if(owner==~std::uint64_t{0}||v.previous.owner==owner){
        for(auto* i:{&v.light[0],&v.light[1],&v.scatter,&v.depth})i->texture.resource.Reset();
        v.used=0;v.previous={};v.counts={};v.key.clear();v.dimensions={};
    }
    if(owner==~std::uint64_t{0}){for(auto d:t.descriptors)freeWorldTextureDescriptors_.push_back(d);precipitation_.reset();}
#endif
}
bool D3D12RenderBackend::captureWorldPrecipitationLighting(){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!precipitation_||!precipitation_->selected||precipitation_->readback)return false;
    auto& t=*precipitation_;auto& v=*t.selected;auto desc=v.scatter.texture.resource->GetDesc();
    UINT64 bytes=0;device_->GetCopyableFootprints(&desc,0,1,0,&t.footprint,nullptr,nullptr,&bytes);t.readOffset=(bytes+511)&~511ull;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=t.readOffset*2;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    volCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.readback)),"Precipitation readback");
    unsigned index=0;for(auto* image:{&v.scatter,&v.light[v.bank]}){
        volBarrier(commandList_.Get(),image->texture.resource.Get(),volumeRead,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=image->texture.resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=t.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=t.footprint;dst.PlacedFootprint.Offset=index++*t.readOffset;
        commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        volBarrier(commandList_.Get(),image->texture.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,volumeRead);
    }
    t.diagnostic={v.dimensions[0],v.dimensions[1],v.dimensions[2],v.counts.payloadBytes,v.previous.view.identity,v.previous.view.frame,v.history};
    return true;
#else
    return false;
#endif
}
WorldPrecipitationLightingDiagnostic D3D12RenderBackend::readWorldPrecipitationLighting(){
#if defined(_WIN32)
    if(recording_||!precipitation_||!precipitation_->readback)return {};
    waitForGpu();auto& t=*precipitation_;auto result=t.diagnostic;void* mapped=nullptr;
    volCheck(t.readback->Map(0,nullptr,&mapped),"Precipitation readback map");
    const auto count=std::size_t(result.width)*result.height*result.depth;result.scattering.resize(count);result.lighting.resize(count);
    for(unsigned z=0;z<result.depth;++z)for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x){
        const auto address=(std::size_t(z/8)*result.height+y)*t.footprint.Footprint.RowPitch+(z%8*result.width+x)*8;
        const auto index=(std::size_t(z)*result.height+y)*result.width+x;
        result.scattering[index]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(mapped)+address));
        result.lighting[index]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(mapped)+address+t.readOffset));
    }
    t.readback->Unmap(0,nullptr);t.readback.Reset();return result;
#else
    return {};
#endif
}
