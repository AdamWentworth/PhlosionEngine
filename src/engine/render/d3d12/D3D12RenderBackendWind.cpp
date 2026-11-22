#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/WorldWindShader.h"
#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#include "engine/render/d3d12/D3D12RenderBackendPipelineCompile.h"
#include <cstring>
#include <iostream>
#if defined(_WIN32)
#include <d3d12sdklayers.h>
#endif
using namespace engine::render;
#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT64 windBytes=UINT64(windFieldWords)*16;
constexpr auto windRead=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
void windCheck(HRESULT h,const char* why){if(FAILED(h))throw std::runtime_error(why);}
void windBuffer(ID3D12Device* device,ComPtr<ID3D12Resource>& out,UINT64 size,D3D12_HEAP_TYPE heapType,D3D12_RESOURCE_STATES state,bool unordered=false){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=heapType;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(unordered)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    windCheck(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&out)),"Wind buffer allocation");
}
void windBarrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER b{};
    if(from==to){b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;}
    else{b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};}
    cmd->ResourceBarrier(1,&b);
}
}
struct D3D12RenderBackend::WindState {
    struct Entry {
        std::uint64_t owner=0,frame=~0ull;
        WorldWindFieldHistory history;
        WorldWindFieldDiagnostic diagnostic;
        ComPtr<ID3D12Resource> field;
        std::array<ComPtr<ID3D12Resource>,3> input;
        WorldResourceCounts counts;
    };
    std::array<Entry,8> entries;
    ComPtr<ID3D12Resource> fallback,readback;
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>,3> pipelines;
    Entry* selected=nullptr;
};
D3D12_GPU_VIRTUAL_ADDRESS D3D12RenderBackend::worldWindAddress(){
    if(!windField_)windField_=std::make_shared<WindState>();auto& w=*windField_;
    if(!w.fallback){
        windBuffer(device_.Get(),w.fallback,1024,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p=nullptr;windCheck(w.fallback->Map(0,nullptr,&p),"Wind fallback map");std::memset(p,0,1024);w.fallback->Unmap(0,nullptr);
    }
    return w.selected?w.selected->field->GetGPUVirtualAddress():w.fallback->GetGPUVirtualAddress();
}
#endif
void D3D12RenderBackend::prepareWorldWindField(const WorldWindFieldInput& u){
#if defined(_WIN32)
    WorldPreparedViewBoundary boundary([&]{invalidatePreparedWorldViews();});
    if(!recording_||!worldSceneColorPassActive_||worldShadowActive_||worldSurfaceActive_)throw std::runtime_error("Wind producer pass admission");
    WorldResourceScopeStats scope;if(!worldScopes_.query(u.owner,scope)||scope.releasePending)throw std::invalid_argument("Wind owner");
    (void)worldWindAddress();auto& w=*windField_;
    auto it=std::find_if(w.entries.begin(),w.entries.end(),[&](const auto& v){return v.owner==u.owner;});
    if(it==w.entries.end())it=std::find_if(w.entries.begin(),w.entries.end(),[](const auto& v){return !v.owner;});
    if(it==w.entries.end())throw std::runtime_error("Wind resident scope budget");
    auto& v=*it;if(v.frame==frameCounter_)throw std::runtime_error("Wind already advanced for this frame");
    const auto update=updateWorldWindField(u,v.history);const auto inputBytes=update.plan.input.size()*16;
    if(!v.field){
        const bool own=worldScopes_.active()==0;if(own&&!worldScopes_.begin(u.owner))throw std::runtime_error("Wind scope");
        if(worldScopes_.active()!=u.owner||!worldScopes_.admit(windBytes+inputBytes*3)){if(own)worldScopes_.end();throw std::runtime_error("Wind memory budget");}
        try{
            windBuffer(device_.Get(),v.field,windBytes,D3D12_HEAP_TYPE_DEFAULT,windRead,true);
            for(auto& b:v.input)windBuffer(device_.Get(),b,inputBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            v.counts.geometryBuffers=4;v.counts.payloadBytes=v.counts.apiAllocationBytes=windBytes+inputBytes*3;
            v.counts.apiAllocationBytesEstimated=true;worldScopes_.commit(v.counts);if(own)worldScopes_.end();v.owner=u.owner;
        }catch(...){if(own)worldScopes_.end();throw;}
    }
    if(!w.root){
        D3D12_ROOT_PARAMETER roots[3]{};
        roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;roots[0].Descriptor.ShaderRegister=0;
        roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;roots[1].Descriptor.ShaderRegister=0;
        roots[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;roots[2].Constants={0,0,8};
        D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=3;d.pParameters=roots;ComPtr<ID3DBlob> blob,error;
        windCheck(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Wind root serialization");
        windCheck(prepareRootSignature(device_.Get(),0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&w.root)),"Wind root");
    }
    for(unsigned pass=0;pass<3;++pass)if(!w.pipelines[pass]){
        const auto source=worldWindHlsl(pass);ComPtr<ID3DBlob> blob,error;
        if(!d3d12_pipeline_compile::compileHlslWithCache(source.data(),source.size(),"main","cs_5_0",d3d12_pipeline_compile::d3dCompileFlags(),0,blob,error))
            throw std::runtime_error("Wind shader: "+(error?std::string(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize()):"unknown"));
        D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=w.root.Get();p.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        const auto result=prepareComputePipeline(device_.Get(),&p,IID_PPV_ARGS(&w.pipelines[pass]));
        if(FAILED(result)){
            ComPtr<ID3D12InfoQueue> queue;
            if(SUCCEEDED(device_.As(&queue)))for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){
                SIZE_T bytes=0;queue->GetMessage(i,nullptr,&bytes);std::vector<char> text(bytes);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(text.data());queue->GetMessage(i,message,&bytes);
                if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::cerr<<message->pDescription<<'\n';
            }
            throw std::runtime_error("Wind compute pipeline "+std::to_string(pass)+" HRESULT "+std::to_string(result));
        }
    }
    void* mapped=nullptr;auto& input=v.input[frameIndex_];windCheck(input->Map(0,nullptr,&mapped),"Wind input map");
    std::memcpy(mapped,update.plan.input.data(),inputBytes);input->Unmap(0,nullptr);
    windBarrier(commandList_.Get(),v.field.Get(),windRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList_->SetComputeRootSignature(w.root.Get());commandList_->SetComputeRootUnorderedAccessView(0,v.field->GetGPUVirtualAddress());
    commandList_->SetComputeRootShaderResourceView(1,input->GetGPUVirtualAddress());
    const auto dispatch=[&](unsigned pass,const auto& params){
        commandList_->SetPipelineState(w.pipelines[pass].Get());commandList_->SetComputeRoot32BitConstants(2,8,params.data(),0);
        commandList_->Dispatch(windFieldCells/64,1,1);
        windBarrier(commandList_.Get(),v.field.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    };
    dispatch(0,update.steps.front());for(const auto& params:update.steps){dispatch(1,params);dispatch(2,params);}
    windBarrier(commandList_.Get(),v.field.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,windRead);
    commitWorldWindField(v.history,u,update);v.frame=frameCounter_;w.selected=&v;
    v.diagnostic={unsigned(update.steps.size()),update.plan.shapes,update.plan.overflow,u.wind.seconds,windBytes,u.owner,v.history.updates,{}};
#else
    (void)u;throw std::runtime_error("D3D12 wind unavailable");
#endif
}
void D3D12RenderBackend::clearWorldWindField(){
#if defined(_WIN32)
    if(windField_)windField_->selected=nullptr;
#endif
}
void D3D12RenderBackend::releaseWorldWindField(std::uint64_t owner){
#if defined(_WIN32)
    if(!windField_)return;auto& w=*windField_;
    for(auto& v:w.entries)if(owner==~0ull||v.owner==owner){if(w.selected==&v)w.selected=nullptr;v={};}
    if(owner==~0ull)windField_.reset();
#else
    (void)owner;
#endif
}
bool D3D12RenderBackend::captureWorldWindField(){
#if defined(_WIN32)
    if(!recording_||!windField_||!windField_->selected||windField_->readback)return false;
    auto& w=*windField_;const auto bytes=UINT64(windFieldHeader+windFieldCells*2)*16;
    windBuffer(device_.Get(),w.readback,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto* source=w.selected->field.Get();windBarrier(commandList_.Get(),source,windRead,D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->CopyBufferRegion(w.readback.Get(),0,source,0,bytes);windBarrier(commandList_.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,windRead);return true;
#else
    return false;
#endif
}
WorldWindFieldDiagnostic D3D12RenderBackend::readWorldWindField(){
#if defined(_WIN32)
    if(recording_||!windField_||!windField_->selected)return {};
    auto& w=*windField_;auto out=w.selected->diagnostic;if(!w.readback)return out;
    waitForGpu();out.field.resize(windFieldHeader+windFieldCells*2);void* mapped=nullptr;
    const auto mappedResult=w.readback->Map(0,nullptr,&mapped);
    if(FAILED(mappedResult))throw std::runtime_error("Wind readback "+std::to_string(mappedResult)+" device "+std::to_string(device_->GetDeviceRemovedReason()));std::memcpy(out.field.data(),mapped,out.field.size()*16);
    w.readback->Unmap(0,nullptr);w.readback.Reset();return out;
#else
    return {};
#endif
}
