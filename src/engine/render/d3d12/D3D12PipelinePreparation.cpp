#include "engine/render/d3d12/D3D12PipelinePreparation.h"
#if defined(_WIN32)
#include "engine/render/ShaderCacheStorage.h"
#include "engine/render/ShaderPreparation.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <mutex>
#include <type_traits>

namespace engine::render {
namespace {
using namespace shader_cache;
using Microsoft::WRL::ComPtr;
constexpr GUID rootIdentityGuid{0x04e987b1,0xa158,0x44b9,{0x88,0x81,0x98,0x0a,0xea,0x57,0x1f,0x02}};
constexpr GUID deviceIdentityGuid{0x28be64e0,0x31db,0x49b3,{0x9a,0x82,0x3d,0x0f,0x8a,0x66,0x2b,0x19}};
constexpr std::size_t maxPsoBytes=256u*1024u*1024u;
std::uint64_t deviceIdentity(ID3D12Device* device) {
    std::uint64_t identity=0;UINT size=sizeof(identity);
    if(SUCCEEDED(device->GetPrivateData(deviceIdentityGuid,&size,&identity))&&size==sizeof(identity))return identity;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};LARGE_INTEGER driver{};
    const auto luid=device->GetAdapterLuid();
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
       FAILED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter))) ||
       FAILED(adapter->GetDesc1(&desc)) ||
       FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver)))return 0;
    Key k;k.field(desc.VendorId);k.field(desc.DeviceId);k.field(desc.SubSysId);k.field(desc.Revision);
    k.field(driver.QuadPart);k.field(luid.HighPart);k.field(luid.LowPart);
    identity=k.value;device->SetPrivateData(deviceIdentityGuid,sizeof(identity),&identity);return identity;
}
bool commonKey(Key& k,ID3D12Device* device,ID3D12RootSignature* root) {
    const auto id=deviceIdentity(device);if(!id||!root)return false;
    std::uint64_t rootKey=0;UINT size=sizeof(rootKey);
    if(FAILED(root->GetPrivateData(rootIdentityGuid,&size,&rootKey))||size!=sizeof(rootKey))return false;
    k.field(std::uint64_t{1});k.field(id);k.field(rootKey);return true;
}
void shader(Key& k,const D3D12_SHADER_BYTECODE& code){k.bytes(code.pShaderBytecode,code.BytecodeLength);}
void stencil(Key& k,const D3D12_DEPTH_STENCILOP_DESC& d){
    k.field(d.StencilFailOp);k.field(d.StencilDepthFailOp);k.field(d.StencilPassOp);k.field(d.StencilFunc);
}
bool graphicsKey(Key& k,ID3D12Device* device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
    if(!commonKey(k,device,d.pRootSignature))return false;
    k.text("graphics");shader(k,d.VS);shader(k,d.PS);shader(k,d.DS);shader(k,d.HS);shader(k,d.GS);
    k.field(d.StreamOutput.NumEntries);k.field(d.StreamOutput.NumStrides);k.field(d.StreamOutput.RasterizedStream);
    for(UINT i=0;i<d.StreamOutput.NumEntries;++i){
        const auto& e=d.StreamOutput.pSODeclaration[i];k.field(e.Stream);k.text(e.SemanticName?e.SemanticName:"");
        k.field(e.SemanticIndex);k.field(e.StartComponent);k.field(e.ComponentCount);k.field(e.OutputSlot);
    }
    for(UINT i=0;i<d.StreamOutput.NumStrides;++i)k.field(d.StreamOutput.pBufferStrides[i]);
    k.field(d.BlendState.AlphaToCoverageEnable);k.field(d.BlendState.IndependentBlendEnable);
    for(const auto& b:d.BlendState.RenderTarget){
        k.field(b.BlendEnable);k.field(b.LogicOpEnable);k.field(b.SrcBlend);k.field(b.DestBlend);k.field(b.BlendOp);
        k.field(b.SrcBlendAlpha);k.field(b.DestBlendAlpha);k.field(b.BlendOpAlpha);k.field(b.LogicOp);k.field(b.RenderTargetWriteMask);
    }
    k.field(d.SampleMask);const auto& r=d.RasterizerState;
    k.field(r.FillMode);k.field(r.CullMode);k.field(r.FrontCounterClockwise);k.field(r.DepthBias);k.field(r.DepthBiasClamp);
    k.field(r.SlopeScaledDepthBias);k.field(r.DepthClipEnable);k.field(r.MultisampleEnable);k.field(r.AntialiasedLineEnable);
    k.field(r.ForcedSampleCount);k.field(r.ConservativeRaster);
    const auto& z=d.DepthStencilState;
    k.field(z.DepthEnable);k.field(z.DepthWriteMask);k.field(z.DepthFunc);k.field(z.StencilEnable);
    k.field(z.StencilReadMask);k.field(z.StencilWriteMask);stencil(k,z.FrontFace);stencil(k,z.BackFace);
    k.field(d.InputLayout.NumElements);
    for(UINT i=0;i<d.InputLayout.NumElements;++i){const auto& e=d.InputLayout.pInputElementDescs[i];
        k.text(e.SemanticName?e.SemanticName:"");k.field(e.SemanticIndex);k.field(e.Format);k.field(e.InputSlot);
        k.field(e.AlignedByteOffset);k.field(e.InputSlotClass);k.field(e.InstanceDataStepRate);}
    k.field(d.IBStripCutValue);k.field(d.PrimitiveTopologyType);k.field(d.NumRenderTargets);
    for(const auto f:d.RTVFormats)k.field(f);
    k.field(d.DSVFormat);k.field(d.SampleDesc.Count);k.field(d.SampleDesc.Quality);k.field(d.NodeMask);k.field(d.Flags);
    return true;
}
template<class Desc,class Create>
HRESULT prepare(ID3D12Device* device,const Desc* input,REFIID iid,void** out,Key key,bool cacheable,Create create) {
    if(!input||!out)return E_INVALIDARG;
    ShaderPreparationScope trace("d3d12-pso",std::is_same_v<Desc,D3D12_GRAPHICS_PIPELINE_STATE_DESC>?"graphics":"compute");
    Desc desc=*input;
    std::vector<std::uint8_t> cached;std::filesystem::path path;
    if(cacheable&&!engine::env::flagEnabled("PHLOSION_DISABLE_D3D12_PIPELINE_CACHE")){
        path=writableRoot()/"d3d12-pso"/(name(key.value)+".pso");
        cached=read(path,2,key.value,maxPsoBytes);
    }
    ComPtr<ID3D12PipelineState> pipeline;
    HRESULT result=E_FAIL;
    if(!cached.empty()) {
        desc.CachedPSO={cached.data(),cached.size()};
        result=create(device,&desc,IID_PPV_ARGS(&pipeline));
        if(SUCCEEDED(result))trace.cacheHit(true);
        else {
            pipeline.Reset();
            std::clog<<"[ShaderPreparation] rejected device pipeline key="<<name(key.value)<<" hr="<<result<<'\n';
        }
    }
    if(!pipeline) {
        desc.CachedPSO=input->CachedPSO;
        result=create(device,&desc,IID_PPV_ARGS(&pipeline));
        if(FAILED(result))return result;
        if(!path.empty()) {
            ComPtr<ID3DBlob> blob;
            if(SUCCEEDED(pipeline->GetCachedBlob(&blob))&&blob&&blob->GetBufferSize()<=maxPsoBytes)
                (void)write(path,2,key.value,{static_cast<const std::uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()});
        }
    }
    return pipeline->QueryInterface(iid,out);
}
}
HRESULT prepareRootSignature(ID3D12Device* device,UINT node,const void* bytes,SIZE_T size,REFIID iid,void** out) {
    const auto result=device->CreateRootSignature(node,bytes,size,iid,out);
    if(SUCCEEDED(result)&&out&&*out) {
        ComPtr<ID3D12RootSignature> root;
        if(SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&root)))) {
            Key k;k.field(node);k.bytes(bytes,size);
            root->SetPrivateData(rootIdentityGuid,sizeof(k.value),&k.value);
        }
    }
    return result;
}
HRESULT prepareGraphicsPipeline(ID3D12Device* device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** out) {
    if(!device||!desc||!out)return E_INVALIDARG;
    Key k;const bool cacheable=graphicsKey(k,device,*desc);
    return prepare(device,desc,iid,out,k,cacheable,[](auto* d,const auto* info,REFIID riid,void** result){
        return d->CreateGraphicsPipelineState(info,riid,result);
    });
}
HRESULT prepareComputePipeline(ID3D12Device* device,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID iid,void** out) {
    if(!device||!desc||!out)return E_INVALIDARG;
    Key k;const bool cacheable=commonKey(k,device,desc->pRootSignature);
    k.text("compute");shader(k,desc->CS);k.field(desc->NodeMask);k.field(desc->Flags);
    return prepare(device,desc,iid,out,k,cacheable,[](auto* d,const auto* info,REFIID riid,void** result){
        return d->CreateComputePipelineState(info,riid,result);
    });
}
}
#endif
