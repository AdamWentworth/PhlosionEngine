#include "engine/render/WorldDisplayPass.h"
#include "engine/render/WorldDisplayShader.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/render/WorldContactComputeShader.h"
#include "engine/render/WorldContactCompute.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldTemporalShader.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include <cstring>
#include "engine/render/WorldTemporalReflectionShader.h"
#include "engine/render/WorldReflectionBlueNoise.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#if defined(_WIN32)
using namespace engine::render;
using Microsoft::WRL::ComPtr;
struct D3D12RenderBackend::TemporalState {
    struct Image {
        ComPtr<ID3D12Resource> resource;
        UINT srv=0,uav=0;int width=0,height=0;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    };
    struct View {
        WorldTemporalHistory history;
        WorldDisplayResources<Image> display;
        WorldTemporalHistory contactHistory;
        WorldContactUniforms contactView{};
        std::array<std::array<float,4>,32> contactParameters{};
        std::array<Image,2> contactDepth,contactVelocity,contactCone;
        Image contactNormals,contactRaw,contactSpatial,contactFull,contactDummyDepth,contactDummyVelocity,contactDummyNormal;
        int contactSlot=0;
        std::array<ComPtr<ID3D12Resource>,kFrameCount> contactConstants;

        bool retired=false;std::uint64_t retiredFrame=0;
        std::chrono::steady_clock::time_point renderedAt{};
        std::array<Image,2> meta,id,color;
        Image rawMask,mask,dummyId,traceDepth,reflection,confidence;
        std::array<ComPtr<ID3D12Resource>,kFrameCount> reflectionConstants;
        std::array<std::vector<Image>,2> feedback;
        int slot=0,width=0,height=0;
    };
    std::unordered_map<std::uint64_t,View> views;
    ComPtr<ID3D12RootSignature> contactRoot;
    std::array<ComPtr<ID3D12PipelineState>,7> contactPipelines;
    WorldTemporalView pending{};
    std::chrono::steady_clock::time_point beganAt{};
    bool active=false,valid=false,prepared=false,reflected=false;
    ComPtr<ID3D12RootSignature> reflectionRoot;
    std::array<ComPtr<ID3D12PipelineState>,5> reflectionPipelines;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    std::array<ComPtr<ID3D12PipelineState>,10> displayPipelines;
    bool heatApplied=false;
    ComPtr<ID3D12Resource> readback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,11> footprints{};
    unsigned displayBase=0;
    ComPtr<ID3D12Resource> contactReadback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,8> contactFootprints{};
    WorldContactImage contactImage;
    WorldTemporalImage image;
    std::vector<UINT> descriptors;
};
namespace {
void temporalBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* resource,
                     D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    if(before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    cmd->ResourceBarrier(1,&b);
}
}
#endif
bool D3D12RenderBackend::beginWorldTemporalScene(const engine::render::WorldTemporalView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!worldSceneColorPassActive_||!worldSurfaceReady_||worldSurfaceActive_)return false;
    if(!temporal_)temporal_=std::make_shared<TemporalState>();
    collectWorldTemporalViews();
    auto& t=*temporal_;
    if(t.active)throw std::logic_error("Nested temporal scene");
    if(t.views.size()>=WorldMotionHistory::maxViews&&!t.views.contains(view.identity))
        throw std::runtime_error("Temporal scene view budget exhausted");
    auto& state=t.views[view.identity];state.history.validate(view);
    if(view.width!=int(worldSurfaceView_[8][0])||view.height!=int(worldSurfaceView_[8][1]))return false;
    t.pending=view;t.beganAt=std::chrono::steady_clock::now();
    if(view.deltaSeconds<0)t.pending.deltaSeconds=std::chrono::duration<float>(t.beganAt-state.renderedAt).count();
    t.reflected=false;t.prepared=false;t.heatApplied=false;t.active=true;t.valid=state.history.accepts(view);worldTemporalOutput_=0xffffffffu;worldDisplayReady_=false;
    return true;
#else
    return false;
#endif
}
engine::render::WorldTemporalFeedback D3D12RenderBackend::worldTemporalFeedback() const {
#if defined(_WIN32)
    if(!temporal_||!temporal_->active||!temporal_->valid)return {};
    const auto& v=temporal_->views.at(temporal_->pending.identity);
    return {v.history.previous.identity,v.history.previous.frame,
        std::max(1,v.width/2),std::max(1,v.height/2),worldTemporalMipCount(v.width,v.height),v.history.previous.preExposure,true};
#else
    return {};
#endif
}
bool D3D12RenderBackend::resolveWorldTemporalScene() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
return processWorldTemporalScene(true);}
bool D3D12RenderBackend::resolveWorldHeatHaze() {
    engine::render::WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(worldDisplaySettings_.heatHaze.strength==0)return true;
    return processWorldTemporalScene(false,true);
}
bool D3D12RenderBackend::processWorldTemporalScene(bool finish,bool distort) {
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!temporal_||!temporal_->active||!worldSceneColorPassActive_||!worldSurfaceReady_)return false;
    auto& t=*temporal_;auto& v=t.views.at(t.pending.identity);
    if((t.prepared&&!finish&&!distort)||(distort&&t.heatApplied))return true;
    engine::render::RenderPhaseScope phase(*this,finish?"temporal-scene":"temporal-invalidation");
    const auto cpu=[&](UINT index){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT index){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(index)*srvDescriptorSize_;return h;};
    if(!t.pipeline) {
        std::array<D3D12_DESCRIPTOR_RANGE,12> ranges{};
        std::array<D3D12_ROOT_PARAMETER,13> params{};
        for(UINT i=0;i<12;++i) {
            auto& range=ranges[i];range.RangeType=i<10?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors=1;range.BaseShaderRegister=i<10?i:i-10;range.OffsetInDescriptorsFromTableStart=0;
            params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable={1,&range};params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        }
        params[12].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[12].Constants={0,0,32};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=UINT(params.size());desc.pParameters=params.data();
        desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error,shader;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,blob.GetAddressOf(),error.GetAddressOf()))||
           FAILED(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(t.root.GetAddressOf()))))
            throw std::runtime_error("Temporal root signature failed");
        const auto source=worldTemporalHlsl();error.Reset();
        if(!engine::render::d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",
            engine::render::d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Temporal shader: "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=t.root.Get();p.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        if(FAILED(engine::render::prepareComputePipeline(device_.Get(), &p,IID_PPV_ARGS(t.pipeline.GetAddressOf()))))
            throw std::runtime_error("Temporal compute pipeline failed");
    }
    const auto allocate=[&](TemporalState::Image& image,int w,int h,DXGI_FORMAT format) {
        if(image.resource&&image.width==w&&image.height==h&&image.resource->GetDesc().Format==format)return;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
        if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            throw std::runtime_error("Temporal history storage format unsupported");
        if(image.resource)waitForGpu();
        if(!image.resource) {
            if(nextSrvDescriptorIndex_+2>engine::render::d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Temporal descriptor budget exhausted");
            const auto descriptor=[&](){if(freeWorldTextureDescriptors_.empty())return nextSrvDescriptorIndex_++;
                const auto index=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();return index;};
            image.srv=descriptor();image.uav=descriptor();
            t.descriptors.push_back(image.srv);t.descriptors.push_back(image.uav);
        }
        image.resource.Reset();image.width=w;image.height=h;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        image.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,image.state,nullptr,
            IID_PPV_ARGS(image.resource.GetAddressOf()))))throw std::runtime_error("Temporal image allocation failed");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device_->CreateShaderResourceView(image.resource.Get(),&srv,cpu(image.srv));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=desc.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(image.resource.Get(),nullptr,&uav,cpu(image.uav));
    };
    const int width=t.pending.width,height=t.pending.height,hw=std::max(1,width/2),hh=std::max(1,height/2);
    for(int i=0;i<2;++i) {
        // Preserve full depth precision for history rejection and exact object IDs.
        allocate(v.meta[i],hw,hh,DXGI_FORMAT_R32G32B32A32_FLOAT);allocate(v.id[i],hw,hh,DXGI_FORMAT_R32G32_UINT);
        allocate(v.color[i],width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
        int w=hw,h=hh;std::size_t level=0;
        do {
            if(v.feedback[i].size()==level)v.feedback[i].emplace_back();
            allocate(v.feedback[i][level++],w,h,DXGI_FORMAT_R11G11B10_FLOAT);if(w==1&&h==1)break;
            w=std::max(1,w/2);h=std::max(1,h/2);
        } while(true);
        // Retain unused allocation slots on shrink; only active levels are used.
    }
    allocate(v.rawMask,hw,hh,DXGI_FORMAT_R16G16B16A16_FLOAT);allocate(v.mask,hw,hh,DXGI_FORMAT_R16G16B16A16_FLOAT);
    allocate(v.dummyId,1,1,DXGI_FORMAT_R32G32_UINT);v.width=width;v.height=height;
    const int previous=v.slot,current=1-previous;
    auto* scene=editorSurfaceActive_?editorSurfaceTarget_.linearColor:worldSceneColorTarget_.Get();
    const UINT sceneSrv=editorSurfaceActive_?editorSurfaceTarget_.linearSrvDescriptorIndex:worldSceneColorSrvDescriptorIndex_;
    ID3D12Resource* depth=nullptr;
    for(auto& [key,texture]:worldTextures_)if(texture.descriptorIndex==worldSurfaceDepth_)depth=texture.resource.Get();
    if(!depth)throw std::runtime_error("Temporal surface depth resource missing");
    std::array<ID3D12Resource*,3> inputs{worldSurfaceTarget_->planes[4].resource.Get(),worldSurfaceTarget_->planes[5].resource.Get(),depth};
    for(auto* input:inputs)temporalBarrier(commandList_.Get(),input,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    temporalBarrier(commandList_.Get(),scene,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // A past output may still be in pixel-read state after the display pass.
    for(auto& color:v.color) {temporalBarrier(commandList_.Get(),color.resource.Get(),color.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);color.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
    std::array<UINT,10> source{sceneSrv,worldSurfaceTarget_->planes[4].descriptorIndex,worldSurfaceDepth_,t.valid?v.color[previous].srv:sceneSrv,
        sceneSrv,t.valid?v.meta[previous].srv:sceneSrv,sceneSrv,sceneSrv,worldSurfaceTarget_->planes[5].descriptorIndex,t.valid?v.id[previous].srv:worldSurfaceTarget_->planes[5].descriptorIndex};
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetComputeRootSignature(t.root.Get());commandList_->SetPipelineState(t.pipeline.Get());
    temporalBarrier(commandList_.Get(),v.dummyId.resource.Get(),v.dummyId.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    v.dummyId.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const auto camera=worldTemporalCameraConstants(t.pending,v.history.previous,t.valid);
    const auto dispatch=[&](int stage,TemporalState::Image& output,int iw,int ih) {
        temporalBarrier(commandList_.Get(),output.resource.Get(),output.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        output.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        auto& identity=stage==0?v.id[current]:v.dummyId;
        if(stage==0){temporalBarrier(commandList_.Get(),identity.resource.Get(),identity.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);identity.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;}
        for(UINT i=0;i<10;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(source[i]));
        commandList_->SetComputeRootDescriptorTable(10,gpu(output.uav));commandList_->SetComputeRootDescriptorTable(11,gpu(identity.uav));
        float p[32]{float(output.width),float(output.height),float(iw),float(ih),float(t.valid),
            worldTemporalCurrentWeight(t.pending.deltaSeconds),t.valid?t.pending.preExposure/v.history.previous.preExposure:1.f,float(stage),
            };
        std::copy(camera.begin(),camera.end(),p+8);
        commandList_->SetComputeRoot32BitConstants(12,32,p,0);
        commandList_->Dispatch((output.width+7)/8,(output.height+7)/8,1);
        temporalBarrier(commandList_.Get(),output.resource.Get(),output.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        output.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(stage==0){temporalBarrier(commandList_.Get(),identity.resource.Get(),identity.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);identity.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
    };
    if(distort) {
        RenderPhaseScope heatPhase(*this,"heat-haze");
        const auto constants=worldHeatHazeParameters(worldDisplaySettings_.heatHaze,t.pending,worldHeatHazeNoise_);
        if(!t.displayPipelines[9]) {
            ComPtr<ID3DBlob> shader,error;const auto heatSource=worldDisplayHlsl(9);
            if(!d3d12_pipeline_compile::compileHlslWithCache(heatSource.data(),heatSource.size(),"main","cs_5_0",
                d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
                throw std::runtime_error("Heat haze shader: "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=t.root.Get();
            pipeline.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
            if(FAILED(prepareComputePipeline(device_.Get(),&pipeline,IID_PPV_ARGS(t.displayPipelines[9].GetAddressOf()))))
                throw std::runtime_error("Heat haze pipeline");
        }
        WorldTextureData data;data.key=data.cacheKey=worldHeatHazeNoise_.key.c_str();
        data.rgba=worldHeatHazeNoise_.rgba.data();data.width=worldHeatHazeNoise_.width;
        data.height=worldHeatHazeNoise_.height*worldHeatHazeNoise_.layers;
        data.textureSrgb=0;data.linearMipSampling=1;
        auto* noise=ensureWorldTexture(&data);if(!noise)throw std::runtime_error("Heat haze noise upload");
        temporalBarrier(commandList_.Get(),noise->resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        allocate(v.display.heat,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto& heat=v.display.heat;
        temporalBarrier(commandList_.Get(),heat.resource.Get(),heat.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList_->SetComputeRootSignature(t.root.Get());
        for(UINT i=0;i<8;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(i==1?worldSurfaceDepth_:i==2?noise->descriptorIndex:sceneSrv));
        commandList_->SetComputeRootDescriptorTable(10,gpu(heat.uav));
        commandList_->SetComputeRoot32BitConstants(12,32,constants.data(),0);
        commandList_->SetPipelineState(t.displayPipelines[9].Get());
        commandList_->Dispatch((width+7)/8,(height+7)/8,1);
        temporalBarrier(commandList_.Get(),heat.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        temporalBarrier(commandList_.Get(),scene,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        commandList_->CopyResource(scene,heat.resource.Get());
        temporalBarrier(commandList_.Get(),scene,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
        temporalBarrier(commandList_.Get(),heat.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        heat.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for(auto* input:inputs)temporalBarrier(commandList_.Get(),input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        temporalBarrier(commandList_.Get(),noise->resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        t.heatApplied=true;return true;
    }
    if(!t.prepared) {
    dispatch(0,v.meta[current],width,height);
    source[4]=v.meta[current].srv;source[5]=t.valid?v.meta[previous].srv:v.meta[current].srv;
    source[8]=v.id[current].srv;source[9]=t.valid?v.id[previous].srv:v.id[current].srv;
    dispatch(1,v.rawMask,hw,hh);source[6]=v.rawMask.srv;dispatch(2,v.mask,hw,hh);
        t.prepared=true;
    }
    source[4]=v.meta[current].srv;source[5]=t.valid?v.meta[previous].srv:v.meta[current].srv;
    if(finish) {
    source[6]=v.mask.srv;source[8]=worldSurfaceTarget_->planes[5].descriptorIndex;
    dispatch(3,v.color[current],width,height);
    {engine::render::RenderPhaseScope feedbackPhase(*this,"temporal-feedback");
        source[0]=v.color[current].srv;int w=width,h=height;
        int level=0;
        for(auto& mip:v.feedback[current]) {
            dispatch((level++%4)==0?4:5,mip,w,h);source[0]=mip.srv;w=mip.width;h=mip.height;if(w==1&&h==1)break;
        }
    }
    }

    if(finish&&worldSceneRadianceExposure_>0) {
        RenderPhaseScope displayPhase(*this,"display");
        if(!t.displayPipelines[0])for(int i=0;i<9;++i){
            ComPtr<ID3DBlob> shader,error;
            const auto displaySource=worldDisplayHlsl(i);
            if(!engine::render::d3d12_pipeline_compile::compileHlslWithCache(displaySource.data(),displaySource.size(),"main","cs_5_0",
                engine::render::d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
                throw std::runtime_error("Display shader "+std::to_string(i)+": "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=t.root.Get();p.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
            if(FAILED(engine::render::prepareComputePipeline(device_.Get(),&p,IID_PPV_ARGS(t.displayPipelines[i].GetAddressOf()))))
                throw std::runtime_error("Display pipeline creation failed");
        }
        TemporalState::Image emissionImage,depthImage;
        emissionImage.resource=worldSurfaceTarget_->planes[3].resource;
        emissionImage.srv=worldSurfaceTarget_->planes[3].descriptorIndex;
        depthImage.resource=depth;depthImage.srv=worldSurfaceDepth_;
        temporalBarrier(commandList_.Get(),emissionImage.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const auto displayAllocate=[&](auto& image,int w,int h,int format){
            allocate(image,w,h,format==0?DXGI_FORMAT_R16G16B16A16_FLOAT:format==1?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R11G11B10_FLOAT);
        };
        const auto displayDispatch=[&](int stage,auto& output,const auto& sources,const auto& constants){
            for(UINT i=0;i<8;++i) {
                auto& input=*sources[i];
                temporalBarrier(commandList_.Get(),input.resource.Get(),input.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                input.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                commandList_->SetComputeRootDescriptorTable(i,gpu(input.srv));
            }
            temporalBarrier(commandList_.Get(),output.resource.Get(),output.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            output.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            commandList_->SetComputeRootDescriptorTable(10,gpu(output.uav));
            commandList_->SetComputeRoot32BitConstants(12,32,constants.data(),0);
            commandList_->SetPipelineState(t.displayPipelines[stage].Get());
            commandList_->Dispatch(stage==1?64:(output.width+7)/8,stage==1?1:(output.height+7)/8,1);
            temporalBarrier(commandList_.Get(),output.resource.Get(),output.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            output.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        };
        runWorldDisplay(v.display,worldDisplaySettings_,t.pending,v.history.previous,t.valid,
            worldSceneRadianceExposure_,worldBloomStrength_,v.color[current],emissionImage,depthImage,displayAllocate,displayDispatch);
        temporalBarrier(commandList_.Get(),emissionImage.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        temporalBarrier(commandList_.Get(),v.display.output.resource.Get(),v.display.output.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        v.display.output.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;worldDisplayReady_=true;
    }
    temporalBarrier(commandList_.Get(),scene,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    for(auto* input:inputs)temporalBarrier(commandList_.Get(),input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(!finish)return true;
    temporalBarrier(commandList_.Get(),v.color[current].resource.Get(),v.color[current].state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.color[current].state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;worldTemporalOutput_=worldDisplayReady_?v.display.output.srv:v.color[current].srv;
    v.slot=current;v.history.commit(t.pending);v.renderedAt=t.beganAt;t.active=false;return true;
#else
    return false;
#endif
}
bool D3D12RenderBackend::captureWorldTemporalScene() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(!temporal_||worldTemporalOutput_==0xffffffffu||!worldSceneColorPassActive_)return false;
    auto& t=*temporal_;auto& v=t.views.at(t.pending.identity);
    std::vector<TemporalState::Image*> images{&v.color[v.slot],&v.mask,&v.feedback[v.slot][0]};
    if(t.reflected)images.insert(images.end(),{&v.reflection,&v.confidence,&v.traceDepth});
    t.displayBase=0;
    if(worldDisplayReady_){
        t.displayBase=unsigned(images.size());
        images.insert(images.end(),{&v.display.output,&v.display.meter,&v.display.exposure[v.display.slot],&v.display.reduced,&v.display.lut});
    }
    const unsigned count=unsigned(images.size());
    UINT64 size=0;
    for(unsigned i=0;i<count;++i) {auto desc=images[i]->resource->GetDesc();UINT64 bytes=0;
        device_->GetCopyableFootprints(&desc,0,1,size,&t.footprints[i],nullptr,nullptr,&bytes);size+=bytes;size=(size+511)&~UINT64(511);}
    t.readback.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(t.readback.GetAddressOf()))))throw std::runtime_error("Temporal readback allocation failed");
    for(unsigned i=0;i<count;++i) {
        auto& image=*images[i];temporalBarrier(commandList_.Get(),image.resource.Get(),image.state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=image.resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=t.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=t.footprints[i];
        commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        temporalBarrier(commandList_.Get(),image.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,image.state);
    }
    t.image={v.width,v.height,v.mask.width,v.mask.height,v.feedback[v.slot][0].width,v.feedback[v.slot][0].height};
    const auto payload=[](const TemporalState::Image& image) {
        if(!image.resource)return UINT64(0);
        const auto d=image.resource->GetDesc();
        const auto bpp=d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R32G32_UINT)?8:4;
        return UINT64(d.Width)*d.Height*bpp;
    };
    for(auto& image:v.meta)t.image.historyTexelBytes+=payload(image);
    for(auto& image:v.id)t.image.historyTexelBytes+=payload(image);
    for(auto& image:v.color)t.image.historyTexelBytes+=payload(image);
    for(auto& levels:v.feedback)for(auto& image:levels)t.image.historyTexelBytes+=payload(image);
    t.image.historyTexelBytes+=payload(v.rawMask)+payload(v.mask)+payload(v.dummyId);
    if(t.reflected){t.image.reflectionDepthWidth=v.traceDepth.width;t.image.reflectionDepthHeight=v.traceDepth.height;}
    return true;
#else
    return false;
#endif
}
engine::render::WorldTemporalImage D3D12RenderBackend::readWorldTemporalScene() {
#if defined(_WIN32)
    if(!temporal_||!temporal_->readback||recording_)return {};
    waitForGpu();auto& t=*temporal_;void* data=nullptr;D3D12_RANGE range{0,SIZE_T(t.readback->GetDesc().Width)};
    if(FAILED(t.readback->Map(0,&range,&data)))throw std::runtime_error("Temporal readback map failed");
    std::array<std::vector<std::array<float,4>>*,3> outputs{&t.image.color,&t.image.mask,&t.image.feedback};
    for(unsigned i=0;i<3;++i){const auto& f=t.footprints[i];auto& out=*outputs[i];out.resize(f.Footprint.Width*f.Footprint.Height);
        for(UINT y=0;y<f.Footprint.Height;++y) {
            const auto* row=static_cast<const char*>(data)+f.Offset+y*f.Footprint.RowPitch;
            for(UINT x=0;x<f.Footprint.Width;++x)
                out[y*f.Footprint.Width+x]=i==2?unpackWorldReflection(reinterpret_cast<const std::uint32_t*>(row)[x],255):
                    unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(row)+x*4);
        }}
    if(t.image.reflectionDepthWidth) {
        const auto row=[&](unsigned image,int y){return static_cast<const char*>(data)+t.footprints[image].Offset+y*t.footprints[image].Footprint.RowPitch;};
        t.image.reflection.resize(t.image.width*t.image.height);
        for(int y=0;y<t.image.height;++y)for(int x=0;x<t.image.width;++x)
            t.image.reflection[y*t.image.width+x]=unpackWorldReflection(reinterpret_cast<const std::uint32_t*>(row(3,y))[x],reinterpret_cast<const unsigned char*>(row(4,y))[x]);
        t.image.reflectionDepth.resize(t.image.reflectionDepthWidth*t.image.reflectionDepthHeight);
        for(int y=0;y<t.image.reflectionDepthHeight;++y)for(int x=0;x<t.image.reflectionDepthWidth;++x)
            t.image.reflectionDepth[y*t.image.reflectionDepthWidth+x]=reinterpret_cast<const std::uint16_t*>(row(5,y))[x]/65535.f;
    }
    if(t.displayBase) {
        std::array<std::vector<std::array<float,4>>*,5> out{&t.image.display,&t.image.meter,&t.image.exposure,&t.image.histogram,&t.image.displayLut};
        const int widths[]{t.image.width,256,1,512,2304},heights[]{t.image.height,256,1,1,48};
        for(unsigned n=0;n<5;++n){
            out[n]->resize(std::size_t(widths[n])*heights[n]);
            for(int y=0;y<heights[n];++y)for(int x=0;x<widths[n];++x){
                const auto& fp=t.footprints[t.displayBase+n];const auto* row=static_cast<const char*>(data)+fp.Offset+y*fp.Footprint.RowPitch;
                if(n==2||n==3)std::memcpy((*out[n])[y*widths[n]+x].data(),row+x*16,16);
                else (*out[n])[y*widths[n]+x]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(row)+x*4);
            }
        }
    }
    D3D12_RANGE empty{0,0};t.readback->Unmap(0,&empty);t.readback.Reset();return std::exchange(t.image,{});
#else
    return {};
#endif
}
void D3D12RenderBackend::clearWorldTemporalScene() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });

#if defined(_WIN32)
    if(temporal_)temporal_->active=false;
    worldTemporalOutput_=0xffffffffu;worldDisplayReady_=false;
#endif
}
void D3D12RenderBackend::releaseWorldTemporalScene() {
#if defined(_WIN32)
    if(temporal_)for(auto d:temporal_->descriptors)freeWorldTextureDescriptors_.push_back(d);
    destroyWorldIndirectLighting();
    destroyWorldClusteredLighting();
    temporal_.reset();worldTemporalOutput_=0xffffffffu;worldDisplayReady_=false;
#endif
}

void D3D12RenderBackend::releaseWorldTemporalView(std::uint64_t id) {
#if defined(_WIN32)
    if(!temporal_)return;
    if(auto it=temporal_->views.find(id);it!=temporal_->views.end()) {
        it->second.retired=true;it->second.retiredFrame=frameCounter_;
        if(temporal_->active&&temporal_->pending.identity==id)temporal_->active=false;
    }
#endif
}
void D3D12RenderBackend::collectWorldTemporalViews() {
#if defined(_WIN32)
    if(!temporal_)return;auto& t=*temporal_;bool waited=false;
    for(auto it=t.views.begin();it!=t.views.end();) {
        auto& v=it->second;
        if(!v.retired||v.retiredFrame>=frameCounter_){++it;continue;}
        if(!waited){waitForGpu();waited=true;}
        const auto destroy=[&](TemporalState::Image& image) {
            if(!image.resource)return;
            for(auto index:{image.srv,image.uav}) {
                freeWorldTextureDescriptors_.push_back(index);
                std::erase(t.descriptors,index);
            }
            image.resource.Reset();
        };
        for(auto& i:v.meta)destroy(i);for(auto& i:v.id)destroy(i);for(auto& i:v.color)destroy(i);
        destroy(v.rawMask);destroy(v.mask);destroy(v.dummyId);
        destroy(v.traceDepth);destroy(v.reflection);destroy(v.confidence);
        for(auto& i:v.contactDepth)destroy(i);for(auto& i:v.contactVelocity)destroy(i);for(auto& i:v.contactCone)destroy(i);
        destroy(v.contactNormals);destroy(v.contactRaw);destroy(v.contactSpatial);destroy(v.contactFull);
        destroy(v.contactDummyDepth);destroy(v.contactDummyVelocity);destroy(v.contactDummyNormal);

        v.display.each(destroy);
        for(auto& mips:v.feedback)for(auto& i:mips)destroy(i);
        it=t.views.erase(it);
    }
#endif
}

bool D3D12RenderBackend::resolveWorldTemporalReflection(const engine::render::WorldTemporalReflectionView& camera) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { invalidatePreparedWorldViews(); });
#if defined(_WIN32)
    if(worldSurfaceTarget_ && worldSurfaceTarget_->probeScratch)return false;
    if(!temporal_||!temporal_->active||temporal_->reflected)return false;
    if(!processWorldTemporalScene(false))return false;
    auto& t=*temporal_;auto& v=t.views.at(t.pending.identity);
    const auto parameters=packWorldTemporalReflection(worldSurfaceView_,camera,t.pending,worldTemporalFeedback());
    const auto budget=worldTemporalReflectionBudget(camera.quality);
    const auto cpu=[&](UINT index){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT index){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(index)*srvDescriptorSize_;return h;};
    if(!t.reflectionRoot) {
        std::array<D3D12_DESCRIPTOR_RANGE,15> ranges{};
        std::array<D3D12_ROOT_PARAMETER,16> params{};
        for(UINT i=0;i<15;++i) {
            auto& range=ranges[i];range.RangeType=i<12?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors=1;range.BaseShaderRegister=i<12?i:i-12;range.OffsetInDescriptorsFromTableStart=0;
            params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&range};
        }
        params[15].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[15].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=UINT(params.size());desc.pParameters=params.data();
        desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,blob.GetAddressOf(),error.GetAddressOf()))||
           FAILED(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(t.reflectionRoot.GetAddressOf()))))
            throw std::runtime_error("Temporal reflection root signature failed");
    }
    const unsigned quality=unsigned(camera.quality);
    for(unsigned i:{4u,quality})if(!t.reflectionPipelines[i]) {
        auto source=worldTemporalReflectionHlsl(int(i),i==4);ComPtr<ID3DBlob> shader,error;
        if(!engine::render::d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",
            engine::render::d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Temporal reflection shader: "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=t.reflectionRoot.Get();desc.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        if(FAILED(engine::render::prepareComputePipeline(device_.Get(), &desc,IID_PPV_ARGS(t.reflectionPipelines[i].GetAddressOf()))))
            throw std::runtime_error("Temporal reflection pipeline failed");
    }
    const auto allocate=[&](TemporalState::Image& image,int width,int height,DXGI_FORMAT format) {
        if(image.resource&&image.width==width&&image.height==height)return;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
        if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            throw std::runtime_error("Temporal reflection compact storage format unsupported");
        if(image.resource)waitForGpu();
        else {
            const auto descriptor=[&](){if(!freeWorldTextureDescriptors_.empty()){auto value=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();return value;}
                if(nextSrvDescriptorIndex_>=engine::render::d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Reflection descriptor budget");
                return nextSrvDescriptorIndex_++;};
            image.srv=descriptor();image.uav=descriptor();t.descriptors.push_back(image.srv);t.descriptors.push_back(image.uav);
        }
        image.resource.Reset();image.width=width;image.height=height;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        image.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,image.state,nullptr,IID_PPV_ARGS(image.resource.GetAddressOf()))))
            throw std::runtime_error("Reflection image allocation failed");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device_->CreateShaderResourceView(image.resource.Get(),&srv,cpu(image.srv));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(image.resource.Get(),nullptr,&uav,cpu(image.uav));
    };
    const int width=t.pending.width,height=t.pending.height;
    allocate(v.traceDepth,std::max(1,width/4),std::max(1,height/4),DXGI_FORMAT_R16_UNORM);
    allocate(v.reflection,width,height,DXGI_FORMAT_R11G11B10_FLOAT);
    allocate(v.confidence,width,height,DXGI_FORMAT_R8_UNORM);
    auto& constants=v.reflectionConstants[frameIndex_];
    if(!constants) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(constants.GetAddressOf()))))
            throw std::runtime_error("Reflection constants allocation failed");
    }
    void* mapped=nullptr;D3D12_RANGE empty{0,0};
    if(FAILED(constants->Map(0,&empty,&mapped)))throw std::runtime_error("Reflection constants map failed");
    std::memcpy(mapped,parameters.data(),sizeof(parameters));constants->Unmap(0,nullptr);
    auto* noiseTexture = [&] {
        engine::render::WorldResourceScopeState::SharedResources shared(worldScopes_);
        static const auto noise=worldReflectionBlueNoise();
        return ensureWorldTextureRaw("__temporal_reflection_noise_u16_v2",noise.data(),64,4096,1,1,false);
    }();
    if(!noiseTexture)throw std::runtime_error("Reflection blue noise upload failed");
    ID3D12Resource* depth=nullptr;
    for(auto& [key,texture]:worldTextures_)if(texture.descriptorIndex==worldSurfaceDepth_)depth=texture.resource.Get();
    if(!depth)throw std::runtime_error("Reflection depth unavailable");
    std::array<ID3D12Resource*,5> sources{depth,worldSurfaceTarget_->planes[1].resource.Get(),
        worldSurfaceTarget_->planes[2].resource.Get(),worldSurfaceTarget_->planes[4].resource.Get(),noiseTexture->resource.Get()};
    for(auto* source:sources)temporalBarrier(commandList_.Get(),source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto ready=[&](TemporalState::Image& image,D3D12_RESOURCE_STATES state){
        temporalBarrier(commandList_.Get(),image.resource.Get(),image.state,state);image.state=state;};
    ready(v.reflection,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);ready(v.confidence,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ready(v.traceDepth,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::array<UINT,12> inputs{worldSurfaceDepth_,worldSurfaceTarget_->planes[1].descriptorIndex,
        worldSurfaceTarget_->planes[4].descriptorIndex,v.mask.srv,worldSurfaceDepth_};
    for(int i=0;i<5;++i)inputs[5+i]=t.valid?v.feedback[v.slot][std::min(i,worldTemporalMipCount(width,height)-1)].srv:worldSurfaceTarget_->planes[1].descriptorIndex;
    inputs[10]=noiseTexture->descriptorIndex;inputs[11]=worldSurfaceTarget_->planes[2].descriptorIndex;
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetComputeRootSignature(t.reflectionRoot.Get());commandList_->SetComputeRootConstantBufferView(15,constants->GetGPUVirtualAddress());
    for(UINT i=0;i<12;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(inputs[i]));
    commandList_->SetComputeRootDescriptorTable(12,gpu(v.reflection.uav));
    commandList_->SetComputeRootDescriptorTable(13,gpu(v.confidence.uav));
    commandList_->SetComputeRootDescriptorTable(14,gpu(v.traceDepth.uav));
    {RenderPhaseScope phase(*this,"temporal-reflection-depth");commandList_->SetPipelineState(t.reflectionPipelines[4].Get());
        commandList_->Dispatch((v.traceDepth.width+7)/8,(v.traceDepth.height+7)/8,1);}
    ready(v.traceDepth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList_->SetComputeRootDescriptorTable(4,gpu(v.traceDepth.srv));
    {RenderPhaseScope phase(*this,"temporal-reflection-trace");commandList_->SetPipelineState(t.reflectionPipelines[quality].Get());
        commandList_->Dispatch((width+7)/8,(height+7)/8,1);}
    ready(v.reflection,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);ready(v.confidence,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for(auto* source:sources)temporalBarrier(commandList_.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    worldScreenReflection_={};std::copy_n(worldSurfaceView_.begin(),8,worldScreenReflection_.begin());
    worldScreenReflection_[8]={float(width),float(height),0,4};
    worldScreenReflection_[10]={1,1,float(budget.steps),budget.probeRoughness};
    worldScreenReflectionDescriptor_=v.reflection.srv;
    worldDepthSnapshotDescriptor_=worldSurfaceDepth_;
    worldContactFilterDescriptors_[1]=v.reflection.srv;worldContactFilterDescriptors_[2]=v.confidence.srv;
    t.reflected=true;return true;
#else
    return false;
#endif
}


bool D3D12RenderBackend::processWorldContactVisibility(){
#if defined(_WIN32)
    if(!temporal_||!temporal_->active||worldContact_[9][3]<.5f)return false;
    auto& t=*temporal_;auto& v=t.views.at(t.pending.identity);
    v.contactHistory.validate(t.pending);
    const bool valid=v.contactHistory.accepts(t.pending)&&v.contactView[10][3]==worldContact_[10][3]&&
        v.contactView[8][2]==worldContact_[8][2]&&!engine::env::flagEnabled("PHLOSION_DISABLE_CONTACT_HISTORY");
    const unsigned quality=unsigned(worldContact_[10][3]);
    if(quality>2)throw std::invalid_argument("Contact quality");
    const auto parameters=worldContactComputeParameters(worldContact_,valid?v.contactView:worldContact_,t.pending.frame,valid);
    v.contactParameters=parameters;
    const auto cpu=[&](UINT index){auto h=srvHeap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*srvDescriptorSize_;return h;};
    const auto gpu=[&](UINT index){auto h=srvHeap_->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(index)*srvDescriptorSize_;return h;};
    if(!t.contactRoot) {
        std::array<D3D12_DESCRIPTOR_RANGE,15> ranges{};
        std::array<D3D12_ROOT_PARAMETER,16> params{};
        for(UINT i=0;i<15;++i) {
            auto& range=ranges[i];range.RangeType=i<11?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors=1;range.BaseShaderRegister=i<11?i:i-11;range.OffsetInDescriptorsFromTableStart=0;
            params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&range};
        }
        params[15].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[15].Descriptor.ShaderRegister=0;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=UINT(params.size());desc.pParameters=params.data();
        desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,blob.GetAddressOf(),error.GetAddressOf()))||
           FAILED(engine::render::prepareRootSignature(device_.Get(), 0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(t.contactRoot.GetAddressOf()))))
            throw std::runtime_error("Temporal contact root signature failed");
    }
    for(unsigned i:{0u,1u+quality,4u,5u,6u})if(!t.contactPipelines[i]){
        const int stage=i==0?0:i<=3?1:int(i)-2, q=i>=1&&i<=3?int(i)-1:2;
        auto source=worldContactComputeHlsl(stage,q);ComPtr<ID3DBlob> shader,error;
        if(!engine::render::d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",
            engine::render::d3d12_pipeline_compile::d3dCompileFlags(),0,shader,error))
            throw std::runtime_error("Contact shader: "+(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"no diagnostics"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=t.contactRoot.Get();desc.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        if(FAILED(engine::render::prepareComputePipeline(device_.Get(), &desc,IID_PPV_ARGS(t.contactPipelines[i].GetAddressOf()))))
            throw std::runtime_error("Contact compute pipeline failed");
    }
    const auto allocate=[&](TemporalState::Image& image,int width,int height,DXGI_FORMAT format) {
        if(image.resource&&image.width==width&&image.height==height)return;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
        if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            throw std::runtime_error("Temporal contact compact storage format unsupported");
        if(image.resource)waitForGpu();
        else {
            const auto descriptor=[&](){if(!freeWorldTextureDescriptors_.empty()){auto value=freeWorldTextureDescriptors_.back();freeWorldTextureDescriptors_.pop_back();return value;}
                if(nextSrvDescriptorIndex_>=engine::render::d3d12_internal::kMaxSrvDescriptors)throw std::runtime_error("Contact descriptor budget");
                return nextSrvDescriptorIndex_++;};
            image.srv=descriptor();image.uav=descriptor();t.descriptors.push_back(image.srv);t.descriptors.push_back(image.uav);
        }
        image.resource.Reset();image.width=width;image.height=height;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        image.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,image.state,nullptr,IID_PPV_ARGS(image.resource.GetAddressOf()))))
            throw std::runtime_error("Contact image allocation failed");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device_->CreateShaderResourceView(image.resource.Get(),&srv,cpu(image.srv));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(image.resource.Get(),nullptr,&uav,cpu(image.uav));
    };

    const int width=t.pending.width,height=t.pending.height,hw=(width+1)/2,hh=(height+1)/2;
    for(int i=0;i<2;++i){
        allocate(v.contactDepth[i],hw,hh,DXGI_FORMAT_R32_FLOAT);
        allocate(v.contactVelocity[i],hw,hh,DXGI_FORMAT_R16G16_FLOAT);
        allocate(v.contactCone[i],hw,hh,DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    allocate(v.contactNormals,hw,hh,DXGI_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactRaw,hw,hh,DXGI_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactSpatial,hw,hh,DXGI_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactFull,width,height,DXGI_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactDummyDepth,1,1,DXGI_FORMAT_R32_FLOAT);
    allocate(v.contactDummyVelocity,1,1,DXGI_FORMAT_R16G16_FLOAT);
    allocate(v.contactDummyNormal,1,1,DXGI_FORMAT_R8G8B8A8_UNORM);
    auto& constants=v.contactConstants[frameIndex_];
    if(!constants) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=512;desc.Height=1;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(constants.GetAddressOf()))))
            throw std::runtime_error("Contact constants allocation failed");
    }
    void* mapped=nullptr;D3D12_RANGE empty{0,0};
    if(FAILED(constants->Map(0,&empty,&mapped)))throw std::runtime_error("Contact constants map failed");
    std::memcpy(mapped,parameters.data(),sizeof(parameters));constants->Unmap(0,nullptr);

    ID3D12Resource* depth=nullptr;
    for(auto& [key,texture]:worldTextures_)if(texture.descriptorIndex==worldSurfaceDepth_)depth=texture.resource.Get();
    if(!depth)throw std::runtime_error("Contact depth unavailable");
    std::array<ID3D12Resource*,4> sources{depth,worldSurfaceTarget_->planes[1].resource.Get(),
        worldSurfaceTarget_->planes[4].resource.Get(),worldSurfaceTarget_->planes[5].resource.Get()};
    for(auto* source:sources)temporalBarrier(commandList_.Get(),source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto ready=[&](TemporalState::Image& image,D3D12_RESOURCE_STATES state){
        temporalBarrier(commandList_.Get(),image.resource.Get(),image.state,state);image.state=state;};
    const int previous=v.contactSlot,current=1-previous;
    for(auto* image:{&v.contactDepth[previous],&v.contactVelocity[previous],&v.contactCone[previous]})
        ready(*image,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    std::array<UINT,11> inputs{worldSurfaceDepth_,worldSurfaceTarget_->planes[1].descriptorIndex,
        worldSurfaceTarget_->planes[4].descriptorIndex,worldSurfaceTarget_->planes[5].descriptorIndex,
        worldSurfaceDepth_,worldSurfaceTarget_->planes[1].descriptorIndex,worldSurfaceTarget_->planes[4].descriptorIndex,
        worldSurfaceTarget_->planes[1].descriptorIndex,valid?v.contactDepth[previous].srv:worldSurfaceDepth_,
        valid?v.contactVelocity[previous].srv:worldSurfaceTarget_->planes[4].descriptorIndex,
        valid?v.contactCone[previous].srv:worldSurfaceTarget_->planes[1].descriptorIndex};
    ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};commandList_->SetDescriptorHeaps(1,heaps);
    commandList_->SetComputeRootSignature(t.contactRoot.Get());commandList_->SetComputeRootConstantBufferView(15,constants->GetGPUVirtualAddress());
    const char* names[]{"contact-prepare","contact-horizon","contact-spatial","contact-temporal","contact-upsample"};
    const auto dispatch=[&](unsigned stage,unsigned pipeline,TemporalState::Image& cone){
        std::array<TemporalState::Image*,4> out{&cone,stage==0?&v.contactDepth[current]:&v.contactDummyDepth,
            stage==0?&v.contactVelocity[current]:&v.contactDummyVelocity,stage==0?&v.contactNormals:&v.contactDummyNormal};
        for(auto* image:out)ready(*image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        for(UINT i=0;i<11;++i)commandList_->SetComputeRootDescriptorTable(i,gpu(inputs[i]));
        for(UINT i=0;i<4;++i)commandList_->SetComputeRootDescriptorTable(11+i,gpu(out[i]->uav));
        RenderPhaseScope phase(*this,names[stage]);commandList_->SetPipelineState(t.contactPipelines[pipeline].Get());
        commandList_->Dispatch((cone.width+7)/8,(cone.height+7)/8,1);
        for(auto* image:out)ready(*image,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    dispatch(0,0,v.contactRaw);
    inputs[4]=v.contactDepth[current].srv;inputs[5]=v.contactNormals.srv;inputs[6]=v.contactVelocity[current].srv;
    dispatch(1,1+quality,v.contactRaw);inputs[7]=v.contactRaw.srv;
    dispatch(2,4,v.contactSpatial);inputs[7]=v.contactSpatial.srv;
    dispatch(3,5,v.contactCone[current]);inputs[7]=v.contactCone[current].srv;
    dispatch(4,6,v.contactFull);
    ready(v.contactFull,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for(auto* source:sources)temporalBarrier(commandList_.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.contactSlot=current;v.contactHistory.commit(t.pending);v.contactView=worldContact_;
    worldContact_[8][3]=1;worldContact_[10][3]=-1;worldContact_[9][3]=1;
    worldContactPreviousVisibility_=v.contactFull.srv;
    worldContactFilterDescriptors_={v.contactFull.srv,v.contactFull.srv,v.contactFull.srv};
    worldDepthSnapshotDescriptor_=worldSurfaceDepth_;
    return true;
#else
    return false;
#endif
}

bool D3D12RenderBackend::captureWorldContactVisibility(){
#if defined(_WIN32)
    if(!temporal_||!worldSceneColorPassActive_)return false;
    auto& t=*temporal_;auto& v=t.views.at(t.pending.identity);
    if(!v.contactFull.resource)return false;
    std::array<TemporalState::Image*,7> images{&v.contactRaw,&v.contactSpatial,&v.contactCone[v.contactSlot],&v.contactFull,
        &v.contactDepth[v.contactSlot],&v.contactVelocity[v.contactSlot],&v.contactNormals};
    ID3D12Resource* depth=nullptr;
    for(auto& [key,texture]:worldTextures_)if(texture.descriptorIndex==worldSurfaceDepth_)depth=texture.resource.Get();
    if(!depth)return false;
    UINT64 bytes=0;
    for(unsigned i=0;i<8;++i){
        auto d=i==7?depth->GetDesc():images[i]->resource->GetDesc();UINT64 size=0;
        device_->GetCopyableFootprints(&d,0,1,bytes,&t.contactFootprints[i],nullptr,nullptr,&size);
        bytes=(bytes+size+511)&~UINT64(511);
    }
    t.contactReadback.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(t.contactReadback.GetAddressOf()))))throw std::runtime_error("Contact readback allocation failed");
    for(unsigned i=0;i<8;++i){
        auto* resource=i==7?depth:images[i]->resource.Get();
        auto state=i==7?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:images[i]->state;
        temporalBarrier(commandList_.Get(),resource,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=t.contactReadback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint=t.contactFootprints[i];commandList_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        temporalBarrier(commandList_.Get(),resource,D3D12_RESOURCE_STATE_COPY_SOURCE,state);
    }
    t.contactImage={v.contactFull.width,v.contactFull.height,v.contactRaw.width,v.contactRaw.height,v.contactParameters};
    t.contactImage.texelBytes=std::uint64_t(v.contactRaw.width)*v.contactRaw.height*36+
        std::uint64_t(v.contactFull.width)*v.contactFull.height*4+12;
    return true;
#else
    return false;
#endif
}
engine::render::WorldContactImage D3D12RenderBackend::readWorldContactVisibility(){
#if defined(_WIN32)
    if(!temporal_||!temporal_->contactReadback||recording_)return {};
    waitForGpu();auto& t=*temporal_;void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(t.contactReadback->GetDesc().Width)};
    if(FAILED(t.contactReadback->Map(0,&range,&mapped)))throw std::runtime_error("Contact readback map failed");
    for(unsigned i=0;i<8;++i){
        const auto& f=t.contactFootprints[i];auto& data=t.contactImage.planes[i];
        const UINT width=i==7?UINT(t.contactImage.width):f.Footprint.Width,height=i==7?UINT(t.contactImage.height):f.Footprint.Height;
        data.resize(width*height);
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){
            auto* row=static_cast<const unsigned char*>(mapped)+f.Offset+y*f.Footprint.RowPitch;
            auto& out=data[y*width+x];
            if(i==4||i==7)out[0]=reinterpret_cast<const float*>(row)[x];
            else if(i==5){auto* half=reinterpret_cast<const std::uint16_t*>(row)+x*2;out={decodeMotionHalf(half[0]),decodeMotionHalf(half[1]),0,0};}
            else for(unsigned c=0;c<4;++c)out[c]=row[x*4+c]/255.f;
        }
    }
    D3D12_RANGE empty{0,0};t.contactReadback->Unmap(0,&empty);t.contactReadback.Reset();
    return std::exchange(t.contactImage,{});
#else
    return {};
#endif
}
