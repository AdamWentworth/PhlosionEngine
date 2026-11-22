#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldIndirectGpu.h"
#include "engine/render/WorldIndirectShader.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include <cstring>
#if defined(_WIN32)
using namespace engine::render;
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::IndirectState {
    struct Resource {ComPtr<ID3D12Resource> object;UINT srv=0,uav=0;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;};
    struct Entry {
        std::uint64_t id=0,revision=0,generation=0,used=0,lastRelight=0,frame=~std::uint64_t(0);
        std::shared_ptr<const IndirectTransport> source;
        std::uint64_t owner=0;
        std::array<unsigned,4> counts{};
        IndirectGpuData data;
        Resource transport,upload,output[2];
        unsigned bank=0;
        std::array<IndirectRow,69> ambientKey{};
    };
    struct Dispatch {ComPtr<ID3D12Resource> constants;UINT shadow=0;};
    std::array<std::array<Dispatch,64>,3> dispatches{};
    std::uint64_t frame=~std::uint64_t(0),scheduleFrame=~std::uint64_t(0);unsigned dispatchCount=0,scheduledCount=0;
    std::vector<std::uint64_t> scheduled;
    std::array<Entry,8> entries{};
    std::vector<UINT> descriptors;
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>,4> pipelines;
    ComPtr<ID3D12Resource> readback;
    WorldIndirectLightingDiagnostic diagnostic;
    UINT null=0;
    Entry* selected=nullptr;
    std::uint64_t clock=0;
};
namespace {
void giCheck(HRESULT value,const char* reason){if(FAILED(value))throw std::runtime_error(reason);}
void giBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};cmd->ResourceBarrier(1,&b);
}
constexpr auto giRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}
#endif
void D3D12RenderBackend::clearWorldIndirectLighting() {
#if defined(_WIN32)
    invalidatePreparedWorldViews();worldIndirectReceiver_=0xffffffffu;worldIndirectDescriptors_={0xffffffffu,0xffffffffu};
    if(indirect_)indirect_->selected=nullptr;
    if(worldDiffuseProbes_[0][3]>1.5f)worldDiffuseProbes_={};
#endif
}
void D3D12RenderBackend::destroyWorldIndirectLighting() {
#if defined(_WIN32)
    if(indirect_)for(auto d:indirect_->descriptors)freeWorldTextureDescriptors_.push_back(d);
    indirect_.reset();worldIndirectReceiver_=0xffffffffu;worldIndirectDescriptors_={0xffffffffu,0xffffffffu};
#endif
}
std::uint32_t D3D12RenderBackend::ensureWorldIndirectNull() {
#if defined(_WIN32)
    if(!indirect_)indirect_=std::make_shared<IndirectState>();
    if(!indirect_->null) {
        UINT d;if(!freeWorldTextureDescriptors_.empty()){d=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else {if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Indirect null descriptor budget");d=nextSrvDescriptorIndex_++;}
        indirect_->null=d;indirect_->descriptors.push_back(d);
        auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(d)*srvDescriptorSize_;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Buffer.NumElements=1;view.Buffer.StructureByteStride=16;
        device_->CreateShaderResourceView(nullptr,&view,cpu);
    }
    return indirect_->null;
#else
    return 0;
#endif
}
bool D3D12RenderBackend::updateWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate& update) {
#if defined(_WIN32)
    engine::render::WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_||placedProbes.active())return false;
    if(!update.transport||!update.environment.enabled){clearWorldIndirectLighting();return true;}
    if(update.resourceScope) {
        WorldResourceScopeStats owner;
        if(!worldScopes_.query(update.resourceScope,owner)||owner.releasePending||owner.status!=WorldResourceScopeStatus::Ready)
            throw std::invalid_argument("Indirect owner is not live");
    }
    ensureWorldIndirectNull();auto& t=*indirect_;
    if(t.frame!=frameCounter_){t.frame=frameCounter_;t.dispatchCount=0;}
    if(t.dispatchCount>=64)throw std::runtime_error("Indirect frame dispatch budget");
    std::vector<std::uint64_t> ids;for(auto id:update.environmentProbes)if(id)ids.push_back(id);
    const auto selection=placedProbes.select(ids);
    auto parameters=packIndirectUpdate(update,selection,worldShadowReady_?worldShadowView_:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv(),false);
    std::copy(worldSunPlan_.uniforms.begin(),worldSunPlan_.uniforms.end(),parameters.begin()+162);
    const bool hasCloudShadow=worldCloudOwner_==update.resourceScope && worldCloudDescriptors_[0]!=0xffffffffu;
    if(hasCloudShadow){parameters[160]=worldClouds_[0];parameters[161]=worldClouds_[1];}
    const auto cpu=[&](UINT i){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT i){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*srvDescriptorSize_;return h;};
    const auto descriptor=[&](){
        UINT d;if(!freeWorldTextureDescriptors_.empty()){d=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else {if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Indirect descriptor budget");d=nextSrvDescriptorIndex_++;}
        t.descriptors.push_back(d);return d;
    };
    if(!t.root) {
        std::array<D3D12_DESCRIPTOR_RANGE,7> ranges{};std::array<D3D12_ROOT_PARAMETER,8> params{};
        for(unsigned i=0;i<5;++i){ranges[i]={i==4?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i==4?0u:i,0,0};params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
        ranges[5]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,4,0,0};params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[6].DescriptorTable={1,&ranges[5]};
        ranges[6]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,5,0,0};
        params[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[7].DescriptorTable={1,&ranges[6]};
        params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[5].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC samplers[3]{};
        for(unsigned i=0;i<2;++i){auto& s=samplers[i];s.ShaderRegister=i;s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;}
        samplers[2]=samplers[0];samplers[2].ShaderRegister=2;
        samplers[2].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;samplers[2].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=8;desc.pParameters=params.data();desc.NumStaticSamplers=3;desc.pStaticSamplers=samplers;
        ComPtr<ID3DBlob> blob,error;giCheck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Indirect root serialize");
        giCheck(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&t.root)),"Indirect root");
    }
    for(unsigned i=0;i<4;++i)if(!t.pipelines[i]) {
        const auto source=worldIndirectHlsl(i);ComPtr<ID3DBlob> shader,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Indirect shader "+std::to_string(i)+": "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=t.root.Get();desc.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        giCheck(engine::render::prepareComputePipeline(device_.Get(), &desc,IID_PPV_ARGS(&t.pipelines[i])),"Indirect pipeline");
    }
    auto entry=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& e){
        return e.id==update.identity&&e.revision==update.revision&&e.owner==update.resourceScope&&e.source==update.transport;});
    if(entry==t.entries.end()) {
        for(auto it=t.entries.begin();it!=t.entries.end();++it)
            if(it->frame!=frameCounter_&&(entry==t.entries.end()||it->used<entry->used))entry=it;
    }
    if(entry==t.entries.end())throw std::runtime_error("Indirect resident entries pinned by this frame");
    auto& v=*entry;v.used=++t.clock;v.frame=frameCounter_;
    const auto create=[&](IndirectState::Resource& r,UINT64 size,D3D12_HEAP_TYPE heapType,bool writable){
        if(r.object)waitForGpu();r.object.Reset();
        D3D12_HEAP_PROPERTIES heap{};heap.Type=heapType;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;
        d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(writable)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        r.state=heapType==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COMMON;
        giCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,r.state,nullptr,IID_PPV_ARGS(&r.object)),"Indirect buffer");
        if(heapType==D3D12_HEAP_TYPE_DEFAULT) {
            if(!r.srv)r.srv=descriptor();
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=UINT(size/16);view.Buffer.StructureByteStride=16;device_->CreateShaderResourceView(r.object.Get(),&view,cpu(r.srv));
            if(writable){if(!r.uav)r.uav=descriptor();D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=UINT(size/16);u.Buffer.StructureByteStride=16;device_->CreateUnorderedAccessView(r.object.Get(),nullptr,&u,cpu(r.uav));}
        }
    };
    const auto ready=[&](IndirectState::Resource& r,D3D12_RESOURCE_STATES state){giBarrier(commandList_.Get(),r.object.Get(),r.state,state);r.state=state;};
    if(v.id!=update.identity||v.revision!=update.revision||v.owner!=update.resourceScope||v.source!=update.transport) {
        auto prepared=update.preparedTransport ? update.preparedTransport->dataFor(update.transport)
            : packIndirectTransport(*update.transport);
        if(prepared.rows.size()*16ull>64u*1024u*1024u||prepared.outputRows*32ull>64u*1024u*1024u)throw std::runtime_error("Indirect resident budget");
        v.data=std::move(prepared);
        create(v.transport,v.data.rows.size()*16,D3D12_HEAP_TYPE_DEFAULT,false);
        create(v.upload,v.data.rows.size()*16,D3D12_HEAP_TYPE_UPLOAD,false);
        void* mapped=nullptr;giCheck(v.upload.object->Map(0,nullptr,&mapped),"Indirect transport map");
        std::memcpy(mapped,v.data.rows.data(),v.data.rows.size()*16);v.upload.object->Unmap(0,nullptr);
        ready(v.transport,D3D12_RESOURCE_STATE_COPY_DEST);commandList_->CopyBufferRegion(v.transport.object.Get(),0,v.upload.object.Get(),0,v.data.rows.size()*16);ready(v.transport,giRead);
        for(auto& output:v.output)create(output,v.data.outputRows*16ull,D3D12_HEAP_TYPE_DEFAULT,true);
        v.id=update.identity;v.revision=update.revision;v.source=update.transport;v.owner=update.resourceScope;v.counts={unsigned(update.transport->bricks.size()),unsigned(update.transport->probes.size()),unsigned(update.transport->surfels.size()),update.transport->terrain.empty()?3u:4u};v.generation=0;v.bank=0;
    }
    if(update.reset)v.generation=0;
    parameters[0][0]=v.generation?1.f:0.f;
    auto& dispatch=t.dispatches[frameIndex_][t.dispatchCount++];
    std::array<IndirectRow,69> ambientKey{};ambientKey[0]=parameters[3];
    std::copy_n(parameters.begin()+84,68,ambientKey.begin()+1);
    parameters[5][3]=v.generation&&ambientKey==v.ambientKey?0.f:1.f;
    auto& constants=dispatch.constants;
    if(!constants) {
        IndirectState::Resource temp;create(temp,(sizeof(parameters)+255)&~255ull,D3D12_HEAP_TYPE_UPLOAD,false);constants=temp.object;
    }
    void* mapped=nullptr;giCheck(constants->Map(0,nullptr,&mapped),"Indirect constants map");std::memcpy(mapped,parameters.data(),sizeof(parameters));constants->Unmap(0,nullptr);
    const unsigned next=1-v.bank;
    ready(v.output[v.bank],giRead);ready(v.output[next],D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);commandList_->SetComputeRootSignature(t.root.Get());
    commandList_->SetComputeRootDescriptorTable(0,gpu(v.transport.srv));commandList_->SetComputeRootDescriptorTable(1,gpu(v.output[v.bank].srv));
    const auto fallback=ensureFallbackSpriteTexture()->descriptorIndex;
    commandList_->SetComputeRootDescriptorTable(2,gpu(placedProbeDescriptor!=0xffffffffu?placedProbeDescriptor:fallback));
    if(!dispatch.shadow)dispatch.shadow=descriptor();
    writeWorldShadowSrv(dispatch.shadow);commandList_->SetComputeRootDescriptorTable(3,gpu(dispatch.shadow));
    commandList_->SetComputeRootDescriptorTable(6,gpu(hasCloudShadow?worldCloudDescriptors_[0]:dispatch.shadow));
    commandList_->SetComputeRootDescriptorTable(7,gpu(worldSunDepth_?worldSunDescriptor_:dispatch.shadow));
    commandList_->SetComputeRootDescriptorTable(4,gpu(v.output[next].uav));commandList_->SetComputeRootConstantBufferView(5,constants->GetGPUVirtualAddress());
    // Published probe atlas and current shadow map are shared read-only inputs.
    ID3D12Resource* probeResource=nullptr;
    for(auto& item:worldTextures_)if(item.second.descriptorIndex==placedProbeDescriptor)probeResource=item.second.resource.Get();
    if(probeResource)giBarrier(commandList_.Get(),probeResource,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,giRead);
    if(worldShadowDepth_)giBarrier(commandList_.Get(),worldShadowDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,giRead);
    const char* names[]{"gi-environment-ambient","gi-brick-relight","gi-probe-gather","gi-distant-relight"};
    const UINT groups[]{30,UINT(update.transport->bricks.size()),UINT(update.transport->probes.size()*6),UINT((update.transport->terrain.size()+63)/64)};
    for(unsigned pass=0;pass<4;++pass)if(groups[pass]) {
        RenderPhaseScope phase(*this,names[pass]);commandList_->SetPipelineState(t.pipelines[pass].Get());commandList_->Dispatch(groups[pass],1,1);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=v.output[next].object.Get();commandList_->ResourceBarrier(1,&b);
    }
    ready(v.output[next],giRead);
    if(probeResource)giBarrier(commandList_.Get(),probeResource,giRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(worldShadowDepth_)giBarrier(commandList_.Get(),worldShadowDepth_.Get(),giRead,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.bank=next;++v.generation;v.lastRelight=++t.clock;v.ambientKey=ambientKey;t.selected=&v;
    worldIndirectReceiver_=0xffffffffu;worldIndirectDescriptors_={v.transport.srv,v.output[v.bank].srv};
    worldDiffuseProbes_={};worldDiffuseProbes_[0]={update.camera[0],update.camera[1],update.camera[2],2};
    worldDiffuseProbes_[1]=parameters[6];worldDiffuseProbes_[2]={update.controls.threshold,float(update.volume),1,update.controls.reflectionDimming};
    worldDiffuseProbes_[3]=update.controls.distantRange;worldDiffuseProbes_[4]=update.controls.distantControls;
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::captureWorldIndirectLighting() {
#if defined(_WIN32)
    if(!recording_||!indirect_||!indirect_->selected||indirect_->readback)return false;
    auto& t=*indirect_;auto& v=*t.selected;auto& output=v.output[v.bank];
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=v.data.outputRows*16ull;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    giCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.readback)),"Indirect readback");
    giBarrier(commandList_.Get(),output.object.Get(),output.state,D3D12_RESOURCE_STATE_COPY_SOURCE);commandList_->CopyBufferRegion(t.readback.Get(),0,output.object.Get(),0,d.Width);
    giBarrier(commandList_.Get(),output.object.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,output.state);
    t.diagnostic={v.id,v.revision,v.generation,v.data.rows.size()*16,v.data.outputRows*32,
        v.counts[0],v.counts[1],v.counts[2],v.counts[3],{}};
    t.diagnostic.output.resize(v.data.outputRows);return true;
#else
    return false;
#endif
}
engine::render::WorldIndirectLightingDiagnostic D3D12RenderBackend::readWorldIndirectLighting() {
#if defined(_WIN32)
    if(recording_||!indirect_||!indirect_->readback)return {};
    waitForGpu();auto& t=*indirect_;auto result=t.diagnostic;void* data=nullptr;D3D12_RANGE read{0,result.output.size()*16};
    giCheck(t.readback->Map(0,&read,&data),"Indirect readback map");std::memcpy(result.output.data(),data,read.End);
    D3D12_RANGE written{0,0};t.readback->Unmap(0,&written);t.readback.Reset();return result;
#else
    return {};
#endif
}

bool D3D12RenderBackend::bindWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate& update) {
#if defined(_WIN32)
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_||placedProbes.active())return false;
    if(!indirect_)return false;
    validateIndirectControls(update.controls);
    auto& t=*indirect_;
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& e){return e.id==update.identity&&e.revision==update.revision&&e.owner==update.resourceScope&&e.source==update.transport&&e.generation;});
    if(found==t.entries.end())return false;
    invalidatePreparedWorldViews();
    auto& v=*found;v.frame=frameCounter_;v.used=++t.clock;t.selected=&v;worldIndirectReceiver_=0xffffffffu;worldIndirectDescriptors_={v.transport.srv,v.output[v.bank].srv};
    worldDiffuseProbes_={};worldDiffuseProbes_[0]={update.camera[0],update.camera[1],update.camera[2],2};
    worldDiffuseProbes_[1]=IndirectRow{update.controls.shadowDistanceWeight,update.controls.normalWeight,update.controls.probeDistanceWeight,update.controls.visibilityBias};worldDiffuseProbes_[2]={update.controls.threshold,float(update.volume),1,update.controls.reflectionDimming};
    worldDiffuseProbes_[3]=update.controls.distantRange;worldDiffuseProbes_[4]=update.controls.distantControls;
    return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::prepareWorldIndirectLighting(std::span<const engine::render::WorldIndirectLightingUpdate> updates,std::uint64_t consumer) {
#if defined(_WIN32)
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_||placedProbes.active())return false;
    auto selected=std::find_if(updates.begin(),updates.end(),[&](const auto& u){return u.identity==consumer;});
    if(selected==updates.end()||selected->quality>3){clearWorldIndirectLighting();return false;}
    if(!selected->environment.enabled){clearWorldIndirectLighting();return true;}
    try {
        if(indirect_&&indirect_->scheduleFrame!=frameCounter_) {
            indirect_->scheduleFrame=frameCounter_;indirect_->scheduledCount=0;indirect_->scheduled.clear();
        }
        std::vector<IndirectScheduleCandidate> candidates;
        for(const auto& u:updates) {
            validateIndirectControls(u.controls);
            if(!u.environment.enabled)continue;
            IndirectScheduleCandidate c{&u,0,0};
            if(indirect_)for(const auto& e:indirect_->entries)
                if(e.id==u.identity&&e.revision==u.revision&&e.owner==u.resourceScope&&e.source==u.transport){c.generation=e.generation;c.lastRelight=e.lastRelight;break;}
            candidates.push_back(c);
        }
        // Validate all requests before recording; an invalid later sector must
        // not publish a different sector as the requested consumer.
        scheduleIndirectLighting(candidates,8);
        const bool coldConsumer=std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){
            return c.update->identity==consumer&&c.generation==0;
        });
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& c){
            return indirect_&&std::find(indirect_->scheduled.begin(),indirect_->scheduled.end(),c.update->identity)!=indirect_->scheduled.end();
        }),candidates.end());
        constexpr unsigned budgets[]{1,2,4,8};
        const unsigned spent=indirect_?indirect_->scheduledCount:0;
        if(spent<budgets[selected->quality])
            for(unsigned i:scheduleIndirectLighting(candidates,budgets[selected->quality]-spent)) {
                if(!updateWorldIndirectLighting(*candidates[i].update)){clearWorldIndirectLighting();return false;}
                indirect_->scheduleFrame=frameCounter_;++indirect_->scheduledCount;indirect_->scheduled.push_back(candidates[i].update->identity);
            }
        if(bindWorldIndirectLighting(*selected))return true;
        // Refresh budgets may be spent by the outgoing scene. A new consumer
        // still needs its first publication before it can render.
        if(coldConsumer&&(!indirect_||std::find(indirect_->scheduled.begin(),indirect_->scheduled.end(),consumer)==indirect_->scheduled.end())) {
            if(!updateWorldIndirectLighting(*selected)){clearWorldIndirectLighting();return false;}
            indirect_->scheduleFrame=frameCounter_;++indirect_->scheduledCount;indirect_->scheduled.push_back(consumer);
            if(bindWorldIndirectLighting(*selected))return true;
        }
        clearWorldIndirectLighting();return false;
    } catch(...) {clearWorldIndirectLighting();throw;}
#else
    return false;
#endif
}

void D3D12RenderBackend::releaseWorldIndirectScope(std::uint64_t owner) {
#if defined(_WIN32)
    if(!indirect_||!owner)return;
    for(auto& entry:indirect_->entries)if(entry.owner==owner) {
        if(indirect_->selected==&entry)clearWorldIndirectLighting();
        // CPU control blocks may execute plugin code: release them before unload.
        // GPU buffers remain fenced by the ordinary cache replacement path.
        entry.source.reset();entry.id=entry.revision=entry.generation=entry.owner=0;
    }
#endif
}
