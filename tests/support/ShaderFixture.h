#pragma once
// Native execution of 32 synthetic compute cases, each returning two vec4 values.
#define SDL_MAIN_HANDLED
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <glad/glad.h>
#include <SDL.h>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace shader_fixture {
using Output=std::array<float,256>;
inline void check(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
inline std::string read(const char* path) {
    std::ifstream file(path,std::ios::binary);check(bool(file),std::string("Missing fixture: ")+path);
    return {std::istreambuf_iterator<char>(file),{}};
}
inline Output opengl(const std::string& source) {
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("Shader fixture",0,0,16,16,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    check(window!=nullptr,SDL_GetError());auto context=SDL_GL_CreateContext(window);check(context!=nullptr,SDL_GetError());
    check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"GL loader");
    const auto code="#version 430\n"+source+R"(
layout(local_size_x=16) in;
layout(std430,binding=0) buffer Result { vec4 values[]; };
void main() { uint i=gl_GlobalInvocationID.x;computeFixture(i,values[i*2u],values[i*2u+1u]); }
)";
    auto shader=glCreateShader(GL_COMPUTE_SHADER);const char* text=code.c_str();glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);std::array<char,8192> log{};glGetShaderInfoLog(shader,GLsizei(log.size()),nullptr,log.data());
    check(ok!=0,log.data());auto program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);
    glGetProgramiv(program,GL_LINK_STATUS,&ok);glGetProgramInfoLog(program,GLsizei(log.size()),nullptr,log.data());check(ok!=0,log.data());
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    Output result;result.fill(-999);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(result),result.data(),GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);glUseProgram(program);glDispatchCompute(2,1,1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(result),result.data());
    check(glGetError()==GL_NO_ERROR,"GL compute/readback error");
    glDeleteBuffers(1,&buffer);glDeleteProgram(program);glDeleteShader(shader);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
inline void vkcheck(VkResult result) {check(result==VK_SUCCESS,"Vulkan failure "+std::to_string(result));}
inline Output vulkan(const char* spirv) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instanceInfo.pApplicationInfo=&app;
    VkInstance instance{};vkcheck(vkCreateInstance(&instanceInfo,nullptr,&instance));
    uint32_t count=0;vkcheck(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count>0,"No Vulkan GPU");
    std::vector<VkPhysicalDevice> devices(count);vkcheck(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical{};uint32_t family=0;
    for(auto candidate:devices) {
        vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,families.data());
        for(uint32_t i=0;i<count;++i) if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT) {physical=candidate;family=i;break;}
        if(physical)break;
    }
    check(physical!=VK_NULL_HANDLE,"No compute queue");float priority=1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    VkDevice device{};vkcheck(vkCreateDevice(physical,&deviceInfo,nullptr,&device));VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=sizeof(Output);bufferInfo.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer{};vkcheck(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer));VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,buffer,&requirements);
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint32_t type=memory.memoryTypeCount;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i) if((requirements.memoryTypeBits&(1u<<i))&&
        (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {type=i;break;}
    check(type<memory.memoryTypeCount,"No coherent storage memory");VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    VkDeviceMemory data{};vkcheck(vkAllocateMemory(device,&allocation,nullptr,&data));vkcheck(vkBindBufferMemory(device,buffer,data,0));
    void* mapped=nullptr;vkcheck(vkMapMemory(device,data,0,sizeof(Output),0,&mapped));Output result;result.fill(-999);std::memcpy(mapped,result.data(),sizeof(result));
    const auto bytes=read(spirv);check(bytes.size()%4==0,"Invalid fixture SPIR-V");std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shaderInfo.codeSize=bytes.size();shaderInfo.pCode=words.data();VkShaderModule shader{};
    vkcheck(vkCreateShaderModule(device,&shaderInfo,nullptr,&shader));
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=1;setInfo.pBindings=&binding;
    VkDescriptorSetLayout setLayout{};vkcheck(vkCreateDescriptorSetLayout(device,&setInfo,nullptr,&setLayout));
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layoutInfo.setLayoutCount=1;layoutInfo.pSetLayouts=&setLayout;
    VkPipelineLayout layout{};vkcheck(vkCreatePipelineLayout(device,&layoutInfo,nullptr,&layout));
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipelineInfo.layout=layout;
    pipelineInfo.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pipelineInfo.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipelineInfo.stage.module=shader;pipelineInfo.stage.pName="main";
    VkPipeline pipeline{};vkcheck(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&pipeline));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};poolInfo.maxSets=1;poolInfo.poolSizeCount=1;poolInfo.pPoolSizes=&poolSize;
    VkDescriptorPool pool{};vkcheck(vkCreateDescriptorPool(device,&poolInfo,nullptr,&pool));
    VkDescriptorSetAllocateInfo setAllocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};setAllocation.descriptorPool=pool;setAllocation.descriptorSetCount=1;setAllocation.pSetLayouts=&setLayout;
    VkDescriptorSet set{};vkcheck(vkAllocateDescriptorSets(device,&setAllocation,&set));VkDescriptorBufferInfo bufferBinding{buffer,0,sizeof(Output)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bufferBinding;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    VkCommandPoolCreateInfo commandsInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commandsInfo.queueFamilyIndex=family;VkCommandPool commands{};vkcheck(vkCreateCommandPool(device,&commandsInfo,nullptr,&commands));
    VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandAllocation.commandPool=commands;commandAllocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandAllocation.commandBufferCount=1;
    VkCommandBuffer command{};vkcheck(vkAllocateCommandBuffers(device,&commandAllocation,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vkBeginCommandBuffer(command,&begin));
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);vkCmdDispatch(command,2,1,1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);vkcheck(vkEndCommandBuffer(command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};vkcheck(vkCreateFence(device,&fenceInfo,nullptr,&fence));vkcheck(vkQueueSubmit(queue,1,&submit,fence));
    vkcheck(vkWaitForFences(device,1,&fence,VK_TRUE,30000000000ull));std::memcpy(result.data(),mapped,sizeof(result));vkUnmapMemory(device,data);
    vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,commands,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);
    vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorSetLayout(device,setLayout,nullptr);vkDestroyShaderModule(device,shader,nullptr);
    vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,data,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);return result;
}
#ifdef _WIN32
inline void hr(HRESULT result) {check(SUCCEEDED(result),"D3D12 failure "+std::to_string(result));}
inline Output d3d12(const std::string& source) {
    using Microsoft::WRL::ComPtr;
    const auto code=std::string("#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n#define inversesqrt rsqrt\n#define floatBitsToUint asuint\n#define uintBitsToFloat asfloat\n#define FIXTURE_GLOBAL static\n")+source+R"(
RWStructuredBuffer<float4> values : register(u0);
[numthreads(16,1,1)] void main(uint3 id:SV_DispatchThreadID) { uint i=id.x;computeFixture(i,values[i*2],values[i*2+1]); }
)";
    ComPtr<ID3DBlob> shader,errors;auto compiled=D3DCompile(code.data(),code.size(),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    check(SUCCEEDED(compiled),errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"D3DCompile failed");
    ComPtr<ID3D12Device> device;hr(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rootInfo{};rootInfo.NumParameters=1;rootInfo.pParameters=&parameter;
    ComPtr<ID3DBlob> rootBytes;hr(D3D12SerializeRootSignature(&rootInfo,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&errors));
    ComPtr<ID3D12RootSignature> root;hr(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineInfo{};pipelineInfo.pRootSignature=root.Get();pipelineInfo.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;hr(device->CreateComputePipelineState(&pipelineInfo,IID_PPV_ARGS(&pipeline)));
    D3D12_RESOURCE_DESC bufferInfo{};bufferInfo.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bufferInfo.Width=sizeof(Output);bufferInfo.Height=1;bufferInfo.DepthOrArraySize=1;bufferInfo.MipLevels=1;bufferInfo.SampleDesc.Count=1;bufferInfo.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bufferInfo.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> buffer,readback;hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bufferInfo,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&buffer)));
    heap.Type=D3D12_HEAP_TYPE_READBACK;bufferInfo.Flags=D3D12_RESOURCE_FLAG_NONE;
    hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bufferInfo,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    D3D12_COMMAND_QUEUE_DESC queueInfo{};queueInfo.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;hr(device->CreateCommandQueue(&queueInfo,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands;hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pipeline.Get(),IID_PPV_ARGS(&commands)));
    commands->SetComputeRootSignature(root.Get());commands->SetComputeRootUnorderedAccessView(0,buffer->GetGPUVirtualAddress());commands->Dispatch(2,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
    commands->CopyResource(readback.Get(),buffer.Get());hr(commands->Close());ID3D12CommandList* lists[]{commands.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));auto event=CreateEvent(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"Fence event");
    hr(queue->Signal(fence.Get(),1));hr(fence->SetEventOnCompletion(1,event));const auto wait=WaitForSingleObject(event,30000);CloseHandle(event);check(wait==WAIT_OBJECT_0,"D3D12 compute timeout");hr(device->GetDeviceRemovedReason());
    void* mapped=nullptr;D3D12_RANGE range{0,sizeof(Output)};hr(readback->Map(0,&range,&mapped));Output result;std::memcpy(result.data(),mapped,sizeof(result));D3D12_RANGE none{};readback->Unmap(0,&none);return result;
}
#endif
inline Output run(const std::string& api,const std::string& source,const char* spirv) {
    if(api=="opengl") return opengl(source);
    if(api=="vulkan") return vulkan(spirv);
#ifdef _WIN32
    if(api=="d3d12") return d3d12(source);
#endif
    throw std::runtime_error("Unknown API");
}
}
