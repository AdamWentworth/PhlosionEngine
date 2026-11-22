#include "engine/render/vulkan/VulkanGraphicsPipelineLibrary.h"
#include <atomic>
#include <set>
#include <thread>
#include <stdexcept>
#include <chrono>
using engine::render::VulkanGraphicsPipelineLibrary;
namespace {
template<class T> T handle(std::uintptr_t n) {
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(n);
    else return static_cast<T>(n);
}
std::mutex driverMutex;
std::set<VkPipeline> alive;
std::atomic_uint calls{},failedCalls{};
std::atomic_bool failFragment{};
std::uintptr_t nextHandle=100;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice,VkPipelineCache,
    std::uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out) {
    check(count==1,"Expected one driver request");++calls;
    const VkGraphicsPipelineLibraryCreateInfoEXT* library=nullptr;
    for(auto* p=static_cast<const VkBaseInStructure*>(info->pNext);p;p=p->pNext)
        if(p->sType==VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT)
            library=reinterpret_cast<const VkGraphicsPipelineLibraryCreateInfoEXT*>(p);
    if(library) {
        const auto& c=*info;const auto part=library->flags;
        check(bool(c.pColorBlendState)==(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT),"Foreign blend state reached shader compilation");
        check(bool(c.pDepthStencilState)==(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT),"Foreign depth state reached another subset");
        check(bool(c.pVertexInputState)==(part==VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT),"Foreign vertex input reached another subset");
        check(bool(c.pViewportState)==(part==VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT),"Foreign viewport reached another subset");
    }
    if(failFragment && library && library->flags==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
        ++failedCalls;*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard lock(driverMutex);*out=handle<VkPipeline>(++nextHandle);alive.insert(*out);return VK_SUCCESS;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice,VkPipeline p,const VkAllocationCallbacks*) {
    std::lock_guard lock(driverMutex);check(alive.erase(p)==1,"Duplicate or unknown pipeline destruction");
}
int main() try {
    VulkanGraphicsPipelineLibrary libs;libs.enabled=true;libs.feedbackEnabled=true;
    auto device=handle<VkDevice>(1);auto vs=handle<VkShaderModule>(2);auto fs=handle<VkShaderModule>(3);
    libs.registerShader(vs,"vertex");libs.registerShader(fs,"fragment");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;
    std::uint32_t material=7;
    VkSpecializationMapEntry map{3,0,sizeof(material)};
    VkSpecializationInfo spec{1,&map,sizeof(material),&material};stages[1].pSpecializationInfo=&spec;
    VkVertexInputBindingDescription binding{0,12,VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;
    vertex.vertexAttributeDescriptionCount=1;vertex.pVertexAttributeDescriptions=&attribute;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multi{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multi.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
    VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
    VkGraphicsPipelineCreateInfo c{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    c.stageCount=2;c.pStages=stages;c.layout=handle<VkPipelineLayout>(4);c.renderPass=handle<VkRenderPass>(5);
    c.pVertexInputState=&vertex;c.pInputAssemblyState=&assembly;c.pViewportState=&viewport;c.pRasterizationState=&raster;
    c.pMultisampleState=&multi;c.pDepthStencilState=&depth;c.pColorBlendState=&blend;c.pDynamicState=&dynamic;
    auto request=[&] {
        VkPipeline p{};check(libs.prepare(device,VK_NULL_HANDLE,c,"contract",&p)==VK_SUCCESS,"Creation failed");
        vkDestroyPipeline(device,p,nullptr);
    };
    request();check(calls==5,"Initial pipeline did not create four parts and link");
    auto previous=calls.load();request();check(calls-previous==1,"Identical pipeline rebuilt a shared part");
    previous=calls;attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE;request();check(calls-previous==2,"Blend change rebuilt shader parts");
    previous=calls;depth.depthWriteEnable=VK_TRUE;request();check(calls-previous==2,"Depth change reused incompatible fragment state");
    previous=calls;++material;request();check(calls-previous==2,"Specialization identity was omitted");
    previous=calls;libs.registerShader(fs,"replacement");request();check(calls-previous==2,"Recycled module handle reused old shader");
    previous=calls;c.renderPass=handle<VkRenderPass>(6);request();check(calls-previous==4,"Render-pass identity was omitted");
    // The entire layout must participate even when only the other stage uses a binding.
    previous=calls;c.layout=handle<VkPipelineLayout>(7);request();check(calls-previous==3,"Layout change reused incompatible stages");
    previous=calls;libs.registerShader(fs,"parallel");
    std::vector<std::jthread> threads;
    for(int i=0;i<8;++i)threads.emplace_back(request);
    threads.clear();check(calls-previous==9,"Concurrent requests compiled the same fragment more than once");
    previous=calls;states[1]=VK_DYNAMIC_STATE_LINE_WIDTH;request();check(calls-previous==1,"Unknown state failed monolithic fallback");
    states[1]=VK_DYNAMIC_STATE_SCISSOR;
    libs.registerShader(fs,"failure");failFragment=true;
    for(int i=0;i<2;++i) {
        VkPipeline p{};check(libs.prepare(device,VK_NULL_HANDLE,c,"failure",&p)==VK_ERROR_OUT_OF_DEVICE_MEMORY,"Driver failure lost");
        check(!p,"Failure published executable");
    }
    check(failedCalls==1,"Failed shared compilation was repeated");
    libs.clear(device);check(alive.empty(),"Library cache leaked pipelines");
    failFragment=false;libs.enabled=false;
    previous=calls;request();check(calls-previous==1,"Unsupported device required libraries");
    check(alive.empty(),"Monolithic path leaked");
    std::cout<<"Pipeline sharing, identity, concurrent join, failure and fallback contracts passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
