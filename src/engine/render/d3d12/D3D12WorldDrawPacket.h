#pragma once

#include <d3d12.h>
#include <array>
#include <cstring>
#include <cstdint>

namespace engine::render::d3d12_internal {

// Native resources are borrowed from the owning frame/pass. Preparation resolves
// and uploads everything; recording neither allocates nor reads renderer state.
struct WorldDiffuseBindings {
    D3D12_GPU_VIRTUAL_ADDRESS constants = 0, wind = 0, layered = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE layeredTextures{},fogSky{};
    D3D12_GPU_VIRTUAL_ADDRESS planar=0;
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 24> tables{};
};
template<class Commands>
void recordWorldDiffuseBindings(Commands& commands, const WorldDiffuseBindings& bindings) {
    commands.SetGraphicsRootShaderResourceView(32, bindings.wind);
    commands.SetGraphicsRootShaderResourceView(33, bindings.layered);
    commands.SetGraphicsRootDescriptorTable(34, bindings.layeredTextures);
    commands.SetGraphicsRootDescriptorTable(35, bindings.fogSky);
    commands.SetGraphicsRootConstantBufferView(6, bindings.constants);
    commands.SetGraphicsRootConstantBufferView(7, bindings.planar);
    for (unsigned root = 8; root < 32; ++root)
        if (root != 16 && root != 24) commands.SetGraphicsRootDescriptorTable(root, bindings.tables[root-8]);
}

struct WorldIndexedDrawPacket {
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12RootSignature* root = nullptr;
    WorldDiffuseBindings diffuse{};
    D3D12_GPU_VIRTUAL_ADDRESS motion = 0, view = 0, object = 0, skin = 0, instances = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE material{}, displacement{};
    D3D12_VERTEX_BUFFER_VIEW vertices{};
    D3D12_INDEX_BUFFER_VIEW indices{};
    ID3D12PipelineState* surfacePipeline = nullptr;
    ID3D12PipelineState* outlinePipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants = 0, outlineConstants = 0;
    unsigned indexCount = 0, instanceCount = 0;
};
// Private to a contiguous recording range. Never carry this across external
// commands, command-list reset or bundle execution.
template<class Commands>
class WorldCommandRecorder {
    Commands& commands_;
    ID3D12DescriptorHeap* heap_=nullptr;
    ID3D12RootSignature* root_=nullptr;
    ID3D12PipelineState* pipeline_=nullptr;
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissor_{};
    D3D12_VERTEX_BUFFER_VIEW vertices_{};
    D3D12_INDEX_BUFFER_VIEW indices_{};
    std::array<std::uint64_t,36> addresses_{},tables_{};
    std::array<float,24> planar_{};
    std::uint64_t addressValid_=0,tableValid_=0;
    bool heapValid_=false,rootValid_=false,pipelineValid_=false,viewportValid_=false,
         scissorValid_=false,verticesValid_=false,indicesValid_=false,planarValid_=false,
         topologyValid_=false;
    D3D_PRIMITIVE_TOPOLOGY topology_{};
    template<class T> bool changed(T& old,const T& value,bool& valid) {
        if(valid && std::memcmp(&old,&value,sizeof(T))==0){++reuses;return false;}
        old=value;valid=true;++writes;return true;
    }
    bool address(unsigned root,std::uint64_t value) {
        const auto bit=std::uint64_t(1)<<root;
        if((addressValid_&bit) && addresses_[root]==value){++reuses;return false;}
        addresses_[root]=value;addressValid_|=bit;++writes;return true;
    }
public:
    std::uint64_t writes=0,reuses=0;
    explicit WorldCommandRecorder(Commands& commands):commands_(commands){}
    void RSSetViewports(unsigned count,const D3D12_VIEWPORT* value) {
        if(changed(viewport_,*value,viewportValid_))commands_.RSSetViewports(count,value);
    }
    void RSSetScissorRects(unsigned count,const D3D12_RECT* value) {
        if(changed(scissor_,*value,scissorValid_))commands_.RSSetScissorRects(count,value);
    }
    void SetDescriptorHeaps(unsigned count,ID3D12DescriptorHeap* const* value) {
        if(changed(heap_,*value,heapValid_)) {
            tableValid_=0;
            commands_.SetDescriptorHeaps(count,value);
        }
    }
    void SetGraphicsRootSignature(ID3D12RootSignature* value) {
        if(changed(root_,value,rootValid_)) {
            addressValid_=tableValid_=0;planarValid_=false;
            commands_.SetGraphicsRootSignature(value);
        }
    }
    void SetGraphicsRootConstantBufferView(unsigned root,D3D12_GPU_VIRTUAL_ADDRESS value) {
        if(address(root,value))commands_.SetGraphicsRootConstantBufferView(root,value);
    }
    void SetGraphicsRootShaderResourceView(unsigned root,D3D12_GPU_VIRTUAL_ADDRESS value) {
        if(address(root,value))commands_.SetGraphicsRootShaderResourceView(root,value);
    }
    void SetGraphicsRoot32BitConstants(unsigned root,unsigned count,const void* data,unsigned offset) {
        // The packet layout has one fixed block; leave unrelated callers uncached.
        if(root!=7 || count!=24 || offset!=0) {
            planarValid_=false;++writes;
            commands_.SetGraphicsRoot32BitConstants(root,count,data,offset);return;
        }
        std::array<float,24> value;std::memcpy(value.data(),data,sizeof(value));
        if(changed(planar_,value,planarValid_))
            commands_.SetGraphicsRoot32BitConstants(root,count,data,offset);
    }
    void SetGraphicsRootDescriptorTable(unsigned root,D3D12_GPU_DESCRIPTOR_HANDLE value) {
        const auto bit=std::uint64_t(1)<<root;
        if((tableValid_&bit) && tables_[root]==value.ptr){++reuses;return;}
        tables_[root]=value.ptr;tableValid_|=bit;++writes;
        commands_.SetGraphicsRootDescriptorTable(root,value);
    }
    void IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY value) {
        if(changed(topology_,value,topologyValid_))commands_.IASetPrimitiveTopology(value);
    }
    void IASetVertexBuffers(unsigned slot,unsigned count,const D3D12_VERTEX_BUFFER_VIEW* value) {
        if(changed(vertices_,*value,verticesValid_))commands_.IASetVertexBuffers(slot,count,value);
    }
    void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* value) {
        if(changed(indices_,*value,indicesValid_))commands_.IASetIndexBuffer(value);
    }
    void SetPipelineState(ID3D12PipelineState* value) {
        if(changed(pipeline_,value,pipelineValid_))commands_.SetPipelineState(value);
    }
    void DrawIndexedInstanced(unsigned count,unsigned instances,unsigned first,int base,unsigned instance) {
        commands_.DrawIndexedInstanced(count,instances,first,base,instance);
    }
};
template<class Commands>
void recordWorldIndexedDraw(Commands& commands, const WorldIndexedDrawPacket& packet, bool bundle = false) {
    // Viewport/scissor are inherited by bundles; these calls are direct-list only.
    if (!bundle) {
        commands.RSSetViewports(1, &packet.viewport);
        commands.RSSetScissorRects(1, &packet.scissor);
    }
    auto* heap = packet.heap;
    commands.SetDescriptorHeaps(1, &heap);
    commands.SetGraphicsRootSignature(packet.root);
    if (packet.motion) commands.SetGraphicsRootShaderResourceView(16, packet.motion);
    recordWorldDiffuseBindings(commands, packet.diffuse);
    commands.SetGraphicsRootConstantBufferView(24, packet.view);
    commands.SetGraphicsRootConstantBufferView(0, packet.object);
    commands.SetGraphicsRootShaderResourceView(2, packet.skin);
    commands.SetGraphicsRootShaderResourceView(4, packet.instances);
    commands.SetGraphicsRootDescriptorTable(3, packet.material);
    commands.SetGraphicsRootDescriptorTable(5, packet.displacement);
    commands.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands.IASetVertexBuffers(0, 1, &packet.vertices);
    commands.IASetIndexBuffer(&packet.indices);
    if (packet.outlinePipeline) {
        commands.SetGraphicsRootConstantBufferView(1, packet.outlineConstants);
        commands.SetPipelineState(packet.outlinePipeline);
        commands.DrawIndexedInstanced(packet.indexCount, packet.instanceCount, 0, 0, 0);
    }
    commands.SetGraphicsRootConstantBufferView(1, packet.surfaceConstants);
    commands.SetPipelineState(packet.surfacePipeline);
    commands.DrawIndexedInstanced(packet.indexCount, packet.instanceCount, 0, 0, 0);
}
} // namespace engine::render::d3d12_internal
