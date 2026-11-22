#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include <cstring>
using namespace engine::render;
namespace {
constexpr VkDeviceSize windBytes=VkDeviceSize(windFieldWords)*16;
void windCheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
void windBarrier(VkCommandBuffer cmd){
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
}
}
struct VulkanRenderBackendImpl::WindState {
    struct Entry {
        std::uint64_t owner=0,frame=~0ull;
        WorldWindFieldHistory history;WorldWindFieldDiagnostic diagnostic;
        Buffer field;std::array<Buffer,kFramesInFlight> input;
        std::array<VkDescriptorSet,kFramesInFlight> sets{};
        WorldResourceCounts counts;VkDescriptorSet drawSet=VK_NULL_HANDLE;
    };
    std::array<Entry,8> entries;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
    std::array<VkPipeline,3> pipelines{};Buffer readback;Entry* selected=nullptr;
};
VkDescriptorSet VulkanRenderBackendImpl::worldWindDescriptorSet() {
    if(windField&&windField->selected)return worldLayeredDescriptorSet(windField->selected->field,windField->selected->owner);
    return worldLayeredDescriptorSet(windFallback,0);
}
void VulkanRenderBackendImpl::prepareWorldWindField(const WorldWindFieldInput& u){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive)throw std::runtime_error("Wind producer pass admission");
    WorldResourceScopeStats scope;if(!worldScopes.query(u.owner,scope)||scope.releasePending)throw std::invalid_argument("Wind owner");
    if(!windField)windField=std::make_shared<WindState>();auto& w=*windField;
    auto it=std::find_if(w.entries.begin(),w.entries.end(),[&](const auto& v){return v.owner==u.owner;});
    if(it==w.entries.end())it=std::find_if(w.entries.begin(),w.entries.end(),[](const auto& v){return !v.owner;});
    if(it==w.entries.end())throw std::runtime_error("Wind resident scope budget");
    auto& v=*it;if(v.frame==frameCounter)throw std::runtime_error("Wind already advanced for this frame");
    const auto update=updateWorldWindField(u,v.history);const auto inputBytes=update.plan.input.size()*16;
    if(!w.layout){
        VkDescriptorSetLayoutBinding bindings[]{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.bindingCount=2;d.pBindings=bindings;
        windCheck(vkCreateDescriptorSetLayout(device,&d,nullptr,&w.setLayout),"Wind set layout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,32};
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&w.setLayout;l.pushConstantRangeCount=1;l.pPushConstantRanges=&push;
        windCheck(vkCreatePipelineLayout(device,&l,nullptr,&w.layout),"Wind pipeline layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8*kFramesInFlight*2};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=8*kFramesInFlight;pool.poolSizeCount=1;pool.pPoolSizes=&size;
        windCheck(vkCreateDescriptorPool(device,&pool,nullptr,&w.pool),"Wind pool");
    }
    if(!v.field.buffer){
        const bool own=worldScopes.active()==0;if(own&&!worldScopes.begin(u.owner))throw std::runtime_error("Wind scope");
        if(worldScopes.active()!=u.owner||!worldScopes.admit(windBytes+inputBytes*kFramesInFlight)){if(own)worldScopes.end();throw std::runtime_error("Wind memory budget");}
        try{
            v.field=createBuffer(windBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            for(unsigned i=0;i<kFramesInFlight;++i){
                v.input[i]=createBuffer(inputBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                if(!v.sets[i]){
                    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=w.pool;a.descriptorSetCount=1;a.pSetLayouts=&w.setLayout;
                    windCheck(vkAllocateDescriptorSets(device,&a,&v.sets[i]),"Wind compute set");
                }
                VkDescriptorBufferInfo infos[]{{v.field.buffer,0,windBytes},{v.input[i].buffer,0,inputBytes}};
                for(unsigned j=0;j<2;++j){VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=v.sets[i];write.dstBinding=j;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&infos[j];vkUpdateDescriptorSets(device,1,&write,0,nullptr);}
            }
            v.counts.geometryBuffers=1+kFramesInFlight;v.counts.payloadBytes=windBytes+inputBytes*kFramesInFlight;v.counts.apiAllocationBytes=v.field.allocationBytes;
            for(const auto& b:v.input)v.counts.apiAllocationBytes+=b.allocationBytes;
            worldScopes.commit(v.counts);if(own)worldScopes.end();v.owner=u.owner;
        }catch(...){if(own)worldScopes.end();throw;}
    }
    for(unsigned pass=0;pass<3;++pass)if(!w.pipelines[pass]){
        auto module=loadShaderModule(("wind_field_"+std::to_string(pass)+".comp.spv").c_str());
        VkComputePipelineCreateInfo d{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};d.layout=w.layout;
        d.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=prepareComputePipelines(device,pipelineCache,1,&d,nullptr,&w.pipelines[pass]);vkDestroyShaderModule(device,module,nullptr);windCheck(result,"Wind compute pipeline");
    }
    std::memcpy(v.input[currentFrame].mapped,update.plan.input.data(),inputBytes);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    windBarrier(cmd);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,w.layout,0,1,&v.sets[currentFrame],0,nullptr);
    const auto dispatch=[&](unsigned pass,const auto& params){
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,w.pipelines[pass]);
        vkCmdPushConstants(cmd,w.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,params.data());vkCmdDispatch(cmd,windFieldCells/64,1,1);windBarrier(cmd);
    };
    dispatch(0,update.steps.front());for(const auto& params:update.steps){dispatch(1,params);dispatch(2,params);}
    commitWorldWindField(v.history,u,update);v.frame=frameCounter;w.selected=&v;
    v.diagnostic={unsigned(update.steps.size()),update.plan.shapes,update.plan.overflow,u.wind.seconds,windBytes,u.owner,v.history.updates,{}};
    if(worldSceneColorPassActive)resumeWorldSceneColor();
}
void VulkanRenderBackendImpl::clearWorldWindField(){if(windField)windField->selected=nullptr;}
void VulkanRenderBackendImpl::releaseWorldWindField(std::uint64_t owner){
    if(!windField){if(owner==~0ull)destroyBuffer(windFallback);return;}auto& w=*windField;
    windCheck(vkDeviceWaitIdle(device),"Wind scope retirement wait");
    for(auto& v:w.entries)if(owner==~0ull||v.owner==owner){
        if(w.selected==&v)w.selected=nullptr;destroyBuffer(v.field);for(auto& b:v.input)destroyBuffer(b);
        v.owner=0;v.frame=~0ull;v.history={};v.counts={};
    }
    if(owner!=~0ull)return;
    destroyBuffer(w.readback);for(auto p:w.pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
    if(w.pool)vkDestroyDescriptorPool(device,w.pool,nullptr);if(w.layout)vkDestroyPipelineLayout(device,w.layout,nullptr);
    if(w.setLayout)vkDestroyDescriptorSetLayout(device,w.setLayout,nullptr);windField.reset();destroyBuffer(windFallback);
}
bool VulkanRenderBackendImpl::captureWorldWindField(){
    if(!frameActive||!windField||!windField->selected||windField->readback.buffer)return false;
    auto& w=*windField;const VkDeviceSize bytes=VkDeviceSize(windFieldHeader+windFieldCells*2)*16;
    w.readback=createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);windBarrier(cmd);
    VkBufferCopy copy{0,0,bytes};vkCmdCopyBuffer(cmd,w.selected->field.buffer,w.readback.buffer,1,&copy);windBarrier(cmd);
    if(worldSceneColorPassActive)resumeWorldSceneColor();return true;
}
WorldWindFieldDiagnostic VulkanRenderBackendImpl::readWorldWindField(){
    if(frameActive||!windField||!windField->selected)return {};
    auto& w=*windField;auto out=w.selected->diagnostic;if(!w.readback.buffer)return out;
    windCheck(vkDeviceWaitIdle(device),"Wind readback wait");out.field.resize(windFieldHeader+windFieldCells*2);
    std::memcpy(out.field.data(),w.readback.mapped,out.field.size()*16);destroyBuffer(w.readback);return out;
}
