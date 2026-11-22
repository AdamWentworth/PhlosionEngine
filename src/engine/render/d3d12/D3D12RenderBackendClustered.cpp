#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/render/WorldSurfaceComposition.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldClusteredShader.h"
#include "engine/render/WorldIndirectReceiverShader.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include <cstring>
#if defined(_WIN32)
using namespace engine::render;
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::ClusteredState {
    struct Resource {
        ComPtr<ID3D12Resource> object;
        UINT srv=0,uav=0;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    };
    struct View {
        int width=0,height=0;
        Resource diffuse,specular,indirect,work,args;
        ComPtr<ID3D12Resource> constants;
    };
    std::unordered_map<unsigned,View> views;
    std::uint64_t frame=~std::uint64_t(0);
    unsigned resolves=0;
    View* selected=nullptr;
    std::vector<UINT> descriptors;
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>,7> pipelines;
    ComPtr<ID3D12CommandSignature> indirect;
    UINT shadow=0;
    ComPtr<ID3D12Resource> readback;
    WorldClusteredLightingDiagnostic diagnostic;
};
namespace {
void clusteredBarrier(ID3D12GraphicsCommandList* command,ID3D12Resource* resource,
        D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    if(from==to)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};command->ResourceBarrier(1,&b);
}
void clusteredCheck(HRESULT result,const char* message){if(FAILED(result))throw std::runtime_error(message);}
}
#endif
void D3D12RenderBackend::destroyWorldClusteredLighting() {
#if defined(_WIN32)
    if(clustered_)for(auto d:clustered_->descriptors)freeWorldTextureDescriptors_.push_back(d);
    clustered_.reset();worldIndirectReceiver_=0xffffffffu;worldClusteredView_={};worldClusteredDescriptors_.fill(0xffffffffu);worldStoredSurfaceDescriptors_.fill(0xffffffffu);
#endif
}
bool D3D12RenderBackend::resolveWorldClusteredLighting(const engine::render::WorldClusteredLightingView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!recording_||!worldSceneColorPassActive_||!worldSurfaceReady_||worldSurfaceActive_||!worldSurfaceTarget_)return false;
    if(worldSurfaceCompositionCapture_ && !engine::render::worldSurfaceCompositionLightingMatches(worldCompositionLighting,view))return false;
    const auto packed=packWorldClusteredLighting(worldSurfaceView_,view,worldLocalShadows_.uniforms);
    std::array<std::array<float,4 >,269> parameters{};
    std::copy(packed.begin(),packed.end(),parameters.begin());
    std::copy_n(worldDiffuseProbes_.begin(),5,parameters.begin()+264);
    const bool gi=view.environment.enabled && worldDiffuseProbes_[0][3]>1.5f;
    if(gi && (worldIndirectDescriptors_[0]==0xffffffffu||worldIndirectDescriptors_[1]==0xffffffffu))
        throw std::logic_error("Indirect receiver field is not bound");
    worldIndirectReceiver_=0xffffffffu;
    const int width=int(parameters[8][0]),height=int(parameters[8][1]);
    const WorldClusteredLayout layout(width,height);
    if(!clustered_)clustered_=std::make_shared<ClusteredState>();
    auto& t=*clustered_;
    const auto cpu=[&](UINT i){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT i){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*srvDescriptorSize_;return h;};
    const auto descriptor=[&](){
        UINT value;
        if(!freeWorldTextureDescriptors_.empty()){value=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();}
        else {if(nextSrvDescriptorIndex_>=d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Cluster descriptor budget");value=nextSrvDescriptorIndex_++;}
        t.descriptors.push_back(value);return value;
    };
    if(!t.root) {
        std::array<D3D12_DESCRIPTOR_RANGE,15> ranges{};
        std::array<D3D12_ROOT_PARAMETER,16> params{};
        for(UINT i=0;i<12;++i) {
            ranges[i].RangeType=i<8?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[i].NumDescriptors=1;ranges[i].BaseShaderRegister=i<8?i:i-8;
            params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};
        }
        for(UINT i=0;i<3;++i) {
            ranges[12+i].RangeType=i<2?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[12+i].NumDescriptors=1;ranges[12+i].BaseShaderRegister=i<2?8+i:4;
            params[13+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[13+i].DescriptorTable={1,&ranges[12+i]};
        }
        params[12].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[12].Descriptor.ShaderRegister=0;
        std::array<D3D12_STATIC_SAMPLER_DESC,2> samplers{};
        for(UINT i=0;i<2;++i){
            auto& s=samplers[i];s.ShaderRegister=i;s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        }
        D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=UINT(params.size());d.pParameters=params.data();d.NumStaticSamplers=2;d.pStaticSamplers=samplers.data();
        ComPtr<ID3DBlob> blob,error;
        clusteredCheck(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,blob.GetAddressOf(),error.GetAddressOf()),"Cluster root serialization");
        clusteredCheck(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(t.root.GetAddressOf())),"Cluster root");
        D3D12_INDIRECT_ARGUMENT_DESC argument{};argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC signature{};signature.ByteStride=12;signature.NumArgumentDescs=1;signature.pArgumentDescs=&argument;
        clusteredCheck(device_->CreateCommandSignature(&signature,nullptr,IID_PPV_ARGS(t.indirect.GetAddressOf())),"Cluster indirect signature");
        t.shadow=descriptor();
    }
    for(int i=0;i<(gi?7:6);++i)if(!t.pipelines[i]) {
        const auto source=i==6?worldIndirectReceiverHlsl():worldClusteredHlsl(i);ComPtr<ID3DBlob> shader,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Cluster shader "+std::to_string(i)+": "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=t.root.Get();d.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        clusteredCheck(engine::render::prepareComputePipeline(device_.Get(), &d,IID_PPV_ARGS(t.pipelines[i].GetAddressOf())),"Cluster pipeline");
    }
    // Every resolve owns its descriptors, constants and outputs until its frame
    // fence retires, including re-resolving a surface after its GI field changes.
    if(t.frame!=frameCounter_){t.frame=frameCounter_;t.resolves=0;t.selected=nullptr;}
    if(t.resolves>=16)throw std::runtime_error("Clustered resolves per frame exceeded");
    auto& v=t.views[frameIndex_*16+t.resolves++];t.selected=&v;
    const bool resized=v.width!=width||v.height!=height;
    if(resized && v.width)waitForGpu();
    const auto allocate=[&](ClusteredState::Resource& r,UINT64 size,bool image,int planes=1) {
            if(!r.object){r.uav=descriptor();if(image)r.srv=descriptor();}
            r.object.Reset();D3D12_RESOURCE_DESC d{};
            d.Dimension=image?D3D12_RESOURCE_DIMENSION_TEXTURE2D:D3D12_RESOURCE_DIMENSION_BUFFER;
            d.Width=image?UINT64(width):size;d.Height=image?height*planes:1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
            d.Format=image?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_UNKNOWN;
            d.Layout=image?D3D12_TEXTURE_LAYOUT_UNKNOWN:D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            r.state=image?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COMMON;
            clusteredCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,r.state,nullptr,IID_PPV_ARGS(r.object.GetAddressOf())),"Cluster resource");
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=d.Format;u.ViewDimension=image?D3D12_UAV_DIMENSION_TEXTURE2D:D3D12_UAV_DIMENSION_BUFFER;
            if(!image){u.Buffer.NumElements=UINT(size/4);u.Buffer.StructureByteStride=4;}
            device_->CreateUnorderedAccessView(r.object.Get(),nullptr,&u,cpu(r.uav));
            if(image) {
                D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=d.Format;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
                s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Texture2D.MipLevels=1;
                device_->CreateShaderResourceView(r.object.Get(),&s,cpu(r.srv));
            }
        };
    if(resized) {
        allocate(v.diffuse,0,true);allocate(v.specular,0,true);allocate(v.work,layout.words*4ull,false);allocate(v.args,24,false);
        v.width=width;v.height=height;
        if(!v.constants) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=(sizeof(parameters)+255)&~255ull;
            d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            clusteredCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(v.constants.GetAddressOf())),"Cluster constants");
        }
    }
    if(gi && (!v.indirect.object || v.indirect.object->GetDesc().Width!=UINT64(width) || v.indirect.object->GetDesc().Height!=UINT(height*2))) {
        if(v.indirect.object && !resized)waitForGpu();
        allocate(v.indirect,0,true,2);
    }
    void* mapped=nullptr;clusteredCheck(v.constants->Map(0,nullptr,&mapped),"Cluster constants map");std::memcpy(mapped,parameters.data(),sizeof(parameters));v.constants->Unmap(0,nullptr);
    const auto textureResource=[&](UINT descriptor)->ID3D12Resource* {
        for(auto& item:worldTextures_)if(item.second.descriptorIndex==descriptor)return item.second.resource.Get();
        return nullptr;
    };
    auto* depth=worldSurfaceTarget_->probeDepth.resource
        ?worldSurfaceTarget_->probeDepth.resource.Get():textureResource(worldSurfaceDepth_);
    auto* lut=textureResource(worldRectLightLutDescriptor_);
    if(!depth||!lut)throw std::runtime_error("Cluster input texture missing");
    auto* fallback=ensureFallbackSpriteTexture();
    writeWorldShadowSrv(t.shadow);
    const UINT moment=worldMomentSrv_!=0xffffffffu?worldMomentSrv_+2:fallback->descriptorIndex;
    std::array<UINT,8> inputs{worldSurfaceDepth_,worldSurfaceTarget_->planes[0].descriptorIndex,worldSurfaceTarget_->planes[1].descriptorIndex,
        worldSurfaceTarget_->planes[2].descriptorIndex,worldRectLightLutDescriptor_,t.shadow,moment,worldSurfaceTarget_->planes[5].descriptorIndex};
    std::vector<ID3D12Resource*> sources{depth,lut,worldSurfaceTarget_->planes[0].resource.Get(),worldSurfaceTarget_->planes[1].resource.Get(),
        worldSurfaceTarget_->planes[2].resource.Get(),worldSurfaceTarget_->planes[5].resource.Get()};
    if(worldShadowDepth_)sources.push_back(worldShadowDepth_.Get());
    if(worldMomentTargets_[2])sources.push_back(worldMomentTargets_[2].Get());
    for(auto* r:sources)clusteredBarrier(commandList_.Get(),r,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto ready=[&](ClusteredState::Resource& r,D3D12_RESOURCE_STATES state){clusteredBarrier(commandList_.Get(),r.object.Get(),r.state,state);r.state=state;};
    ready(v.work,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);ready(v.diffuse,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);ready(v.specular,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);ready(v.args,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);commandList_->SetComputeRootSignature(t.root.Get());
    commandList_->SetComputeRootConstantBufferView(12,v.constants->GetGPUVirtualAddress());
    for(UINT i=0;i<8;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(inputs[i]));
    commandList_->SetComputeRootDescriptorTable(8,gpu(v.diffuse.uav));commandList_->SetComputeRootDescriptorTable(9,gpu(v.specular.uav));
    commandList_->SetComputeRootDescriptorTable(10,gpu(v.work.uav));commandList_->SetComputeRootDescriptorTable(11,gpu(v.args.uav));
    const auto barrier=[&](){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;commandList_->ResourceBarrier(1,&b);};
    const auto dispatch=[&](unsigned pass,UINT x,UINT y,UINT z){commandList_->SetPipelineState(t.pipelines[pass].Get());commandList_->Dispatch(x,y,z);barrier();};
    {RenderPhaseScope p(*this,"cluster-cull");dispatch(0,(layout.coarseX+7)/8,(layout.coarseY+7)/8,1);dispatch(1,(layout.x+7)/8,(layout.y+7)/8,4);}
    {RenderPhaseScope p(*this,"cluster-classify");dispatch(2,layout.x,layout.y,1);dispatch(3,1,1,1);}
    ready(v.args,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    {RenderPhaseScope p(*this,"cluster-shade");for(unsigned c=0;c<2;++c){
        commandList_->SetPipelineState(t.pipelines[4+c].Get());commandList_->ExecuteIndirect(t.indirect.Get(),1,v.args.object.Get(),c*12,nullptr,0);
    }}
    if(gi) {
        RenderPhaseScope p(*this,"gi-receiver-prepare");
        ready(v.indirect,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList_->SetComputeRootDescriptorTable(13,gpu(worldIndirectDescriptors_[0]));
        commandList_->SetComputeRootDescriptorTable(14,gpu(worldIndirectDescriptors_[1]));
        commandList_->SetComputeRootDescriptorTable(15,gpu(v.indirect.uav));
        dispatch(6,(width+7)/8,(height+7)/8,1);
        ready(v.indirect,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        worldIndirectReceiver_=v.indirect.srv;
    }
    ready(v.diffuse,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);ready(v.specular,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for(auto* r:sources)clusteredBarrier(commandList_.Get(),r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(!worldSurfaceCompositionCapture_)worldCompositionLighting=view;
    std::copy_n(worldSurfaceView_.begin(),4,worldClusteredView_.begin());worldClusteredView_[4]={float(width),float(height),0,1};
    worldClusteredDescriptors_={v.diffuse.srv,v.specular.srv,worldSurfaceDepth_};
    for(unsigned i=0;i<4;++i)worldStoredSurfaceDescriptors_[i]=worldSurfaceTarget_->planes[i].descriptorIndex;
    return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::captureWorldClusteredLighting() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!recording_||!worldSceneColorPassActive_||!worldSurfaceReady_||!clustered_||!clustered_->selected||worldClusteredView_[4][3]<.5f)return false;
    auto& t=*clustered_;auto& v=*t.selected;
    const WorldClusteredLayout layout(v.width,v.height);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=layout.words*4ull;
    d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(t.readback)throw std::logic_error("Unread clustered diagnostic");
    clusteredCheck(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(t.readback.GetAddressOf())),"Cluster readback");
    clusteredBarrier(commandList_.Get(),v.work.object.Get(),v.work.state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->CopyBufferRegion(t.readback.Get(),0,v.work.object.Get(),0,d.Width);
    clusteredBarrier(commandList_.Get(),v.work.object.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,v.work.state);
    t.diagnostic={v.width,v.height,{}};return true;
#else
    return false;
#endif
}
engine::render::WorldClusteredLightingDiagnostic D3D12RenderBackend::readWorldClusteredLighting() {
#if defined(_WIN32)
    if(recording_||!clustered_||!clustered_->readback)return {};
    waitForGpu();auto& t=*clustered_;auto result=t.diagnostic;
    const WorldClusteredLayout layout(result.width,result.height);result.words.resize(layout.words);
    void* data=nullptr;D3D12_RANGE range{0,layout.words*4ull};
    clusteredCheck(t.readback->Map(0,&range,&data),"Cluster readback map");
    std::memcpy(result.words.data(),data,layout.words*4ull);D3D12_RANGE written{0,0};t.readback->Unmap(0,&written);t.readback.Reset();return result;
#else
    return {};
#endif
}
