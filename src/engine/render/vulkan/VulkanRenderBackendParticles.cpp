#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include <cstring>
using namespace engine::render;
namespace {
void particleCheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
void particleBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout a,VkImageLayout b){
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};v.image=image;v.oldLayout=a;v.newLayout=b;
    v.srcQueueFamilyIndex=v.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    v.srcAccessMask=a==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;v.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&v);
}
}
struct VulkanRenderBackendImpl::ParticleLightingState {
    struct Entry {
        Texture texture;WorldResourceScopeId owner=0;WorldTemporalView view;WorldResourceCounts counts;
        std::uint64_t frame=~std::uint64_t(0),used=0;unsigned tiles=0,texels=0;std::string key;
    };
    struct Dispatch {Buffer constants,quads;VkDescriptorSet set=VK_NULL_HANDLE;};
    std::array<Entry,4> entries;std::array<std::array<Dispatch,4>,kFramesInFlight> dispatches;
    std::uint64_t frame=~std::uint64_t(0),clock=0;unsigned count=0;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;VkSampler nearest=VK_NULL_HANDLE;
    Entry* selected=nullptr;Buffer readback;WorldParticleLightingDiagnostic diagnostic;
};
WorldParticleLightingResult VulkanRenderBackendImpl::updateWorldParticleLighting(const WorldParticleLightingView& u){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive)throw std::runtime_error("Particle lighting pass admission");
    WorldResourceScopeStats owner;if(!worldScopes.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Particle lighting owner");
    auto layout=particleLightingLayout(u);
    if(!particleLighting)particleLighting=std::make_shared<ParticleLightingState>();auto& t=*particleLighting;
    if(t.frame!=frameCounter){t.frame=frameCounter;t.count=0;}if(t.count>=4)throw std::runtime_error("Particle lighting frame view budget");
    if(!t.layout){
        std::array<VkDescriptorSetLayoutBinding,8> bindings{};
        for(unsigned i=0;i<7;++i)bindings[i]={i,i==6?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==5?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:i>=2?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[7]={7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo s{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};s.bindingCount=8;s.pBindings=bindings.data();
        particleCheck(vkCreateDescriptorSetLayout(device,&s,nullptr,&t.setLayout),"Particle lighting set layout");
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&t.setLayout;
        particleCheck(vkCreatePipelineLayout(device,&l,nullptr,&t.layout),"Particle lighting pipeline layout");
        constexpr unsigned count=4*kFramesInFlight;
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*3},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=4;pool.pPoolSizes=sizes;
        particleCheck(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"Particle lighting descriptor pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        particleCheck(vkCreateSampler(device,&sampler,nullptr,&t.nearest),"Particle lighting nearest sampler");
    }
    if(!t.pipeline){
        auto module=loadShaderModule("particle_lighting.comp.spv");
        VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=t.layout;
        p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=prepareComputePipelines(device,pipelineCache,1,&p,nullptr,&t.pipeline);vkDestroyShaderModule(device,module,nullptr);
        particleCheck(result,"Particle lighting pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.owner==u.owner&&v.view.identity==u.view.identity;});
    if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
        if(it->frame!=frameCounter&&(found==t.entries.end()||it->used<found->used))found=it;
    if(found==t.entries.end()||found->frame==frameCounter)throw std::runtime_error("Particle lighting view already recorded");
    auto& v=*found;const unsigned slot=unsigned(found-t.entries.begin());
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    if(!v.used||v.owner!=u.owner){
        particleCheck(vkDeviceWaitIdle(device),"Particle lighting allocation wait");
        if(v.counts.textures){worldScopes.reclaim(v.owner,v.counts);v.counts={};}
        constexpr auto bytes=std::uint64_t(particleLightingExtent)*particleLightingExtent*8;
        const bool scope=worldScopes.active()==0;if(scope&&!worldScopes.begin(u.owner))throw std::runtime_error("Particle lighting resource admission");
        if(worldScopes.active()!=u.owner||!worldScopes.admit(bytes)){if(scope)worldScopes.end();throw std::runtime_error("Particle lighting resource budget");}
        try {
            auto& texture=v.texture;destroyTexture(texture);
            VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=VK_FORMAT_R16G16B16A16_SFLOAT;i.extent={particleLightingExtent,particleLightingExtent,1};i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;
            i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            particleCheck(vkCreateImage(device,&i,nullptr,&texture.image),"Particle lighting image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,texture.image,&needs);
            VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=needs.size;m.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            particleCheck(vkAllocateMemory(device,&m,nullptr,&texture.memory),"Particle lighting image memory");particleCheck(vkBindImageMemory(device,texture.image,texture.memory,0),"Particle lighting image bind");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=texture.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=i.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            particleCheck(vkCreateImageView(device,&view,nullptr,&texture.view),"Particle lighting image view");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            particleCheck(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"Particle lighting texture sampler");
            texture.width=texture.height=particleLightingExtent;texture.format=i.format;texture.allocationBytes=needs.size;texture.payloadBytes=bytes;
            particleBarrier(cmd,texture.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue clear{};
            vkCmdClearColorImage(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&view.subresourceRange);
            particleBarrier(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            v.counts={};v.counts.textures=1;v.counts.textureDescriptors=2;v.counts.payloadBytes=bytes;v.counts.apiAllocationBytes=needs.size;
            worldScopes.commit(v.counts);if(scope)worldScopes.end();
        }catch(...){if(scope)worldScopes.end();throw;}
        for(auto& [key,material]:worldMaterials)if(material.textures[4]==&v.texture){
            VkDescriptorImageInfo info{v.texture.sampler,v.texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=material.descriptorSet;w.dstBinding=4;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&info;vkUpdateDescriptorSets(device,1,&w,0,nullptr);
        }
        initializeIndexedWorldMaterialSets();
    }
    auto parameters=packParticleLighting(u,layout.result.tiles,worldDiffuseProbes,worldShadowReady?worldShadowView:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv(),worldLocalShadows_.uniforms);
    auto* depth=findWorldPrecipitationDepth(u,parameters[26]);parameters[25][0]=depth?1.f:0.f;layout.result.sharedDepth=depth!=nullptr;
    auto& dispatch=t.dispatches[currentFrame][t.count++];
    if(!dispatch.constants.buffer)dispatch.constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!dispatch.quads.buffer)dispatch.quads=createBuffer(particleLightingCapacity*6*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(dispatch.constants.mapped,parameters.data(),sizeof(parameters));if(!layout.data.empty())std::memcpy(dispatch.quads.mapped,layout.data.data(),layout.data.size()*16);
    if(!worldIndirectFallbackBuffer.buffer){worldIndirectFallbackBuffer=createBuffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);std::memset(worldIndirectFallbackBuffer.mapped,0,16);}
    auto& set=dispatch.set;if(!set){VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=t.pool;a.descriptorSetCount=1;a.pSetLayouts=&t.setLayout;particleCheck(vkAllocateDescriptorSets(device,&a,&set),"Particle lighting set");}
    auto& moment=worldMomentTargets[currentFrame][1];
    VkDescriptorImageInfo images[]{
        {t.nearest,worldShadowViews[currentFrame]?worldShadowViews[currentFrame]:fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {t.nearest,moment.view?moment.view:fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE,v.texture.view,VK_IMAGE_LAYOUT_GENERAL},
        {t.nearest,depth?depth->view:fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkDescriptorBufferInfo buffers[]{
        {worldIndirectBuffers[0]?worldIndirectBuffers[0]:worldIndirectFallbackBuffer.buffer,0,VK_WHOLE_SIZE},
        {worldIndirectBuffers[1]?worldIndirectBuffers[1]:worldIndirectFallbackBuffer.buffer,0,VK_WHOLE_SIZE},
        {dispatch.quads.buffer,0,VK_WHOLE_SIZE},{dispatch.constants.buffer,0,sizeof(parameters)}};
    std::array<VkWriteDescriptorSet,8> writes{};
    for(unsigned i=0;i<7;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;
        if(i<2||i==5){w.descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i<2?i:2];}
        else{w.descriptorType=i==6?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i==6?3:i-2];}}
    auto& dw=writes[7];dw.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;dw.dstSet=set;dw.dstBinding=7;dw.descriptorCount=1;dw.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;dw.pImageInfo=&images[3];
    vkUpdateDescriptorSets(device,8,writes.data(),0,nullptr);
    VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,nullptr,0,nullptr);
    particleBarrier(cmd,v.texture.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
    if(layout.result.tiles) {
        auto& frame=frames[currentFrame];const int token=frame.timestampIssued?phaseProfile.begin(frame.phaseFrame,"particle-lighting"):-1;
        if(token>=0)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);vkCmdDispatch(cmd,layout.result.tiles,1,1);
        if(token>=0&&phaseProfile.end(frame.phaseFrame,token))vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);
    }
    particleBarrier(cmd,v.texture.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    v.owner=u.owner;v.view=u.view;v.frame=frameCounter;v.used=++t.clock;v.tiles=layout.result.tiles;v.texels=layout.result.texels;
    v.key="phlosion://particle-lighting/"+std::to_string(slot);t.selected=&v;layout.result.texture=v.key;resumeWorldSceneColor();return layout.result;
}
VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::findWorldParticleLightingTexture(const char* key){
    if(!key||std::string_view(key).find("phlosion://particle-lighting/")!=0)return nullptr;
    if(particleLighting)for(auto& v:particleLighting->entries)if(v.key==key&&v.used&&v.frame==frameCounter)return &v.texture;
    throw std::runtime_error("Stale particle lighting texture");
}
void VulkanRenderBackendImpl::releaseWorldParticleLighting(std::uint64_t owner){
    if(!particleLighting)return;auto& t=*particleLighting;
    for(auto& v:t.entries)if(owner==~std::uint64_t(0)||v.owner==owner){destroyTexture(v.texture);v.used=0;v.owner=0;v.counts={};v.key.clear();if(t.selected==&v)t.selected=nullptr;}
    if(owner!=~std::uint64_t(0))return;
    for(auto& frame:t.dispatches)for(auto& d:frame){destroyBuffer(d.constants);destroyBuffer(d.quads);}
    destroyBuffer(t.readback);if(t.pipeline)vkDestroyPipeline(device,t.pipeline,nullptr);
    if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
    if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);if(t.nearest)vkDestroySampler(device,t.nearest,nullptr);particleLighting.reset();
}
bool VulkanRenderBackendImpl::captureWorldParticleLighting(){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!particleLighting||!particleLighting->selected||particleLighting->readback.buffer)return false;
    auto& t=*particleLighting;auto& v=*t.selected;const auto bytes=std::uint64_t(particleLightingExtent)*particleLightingExtent*8;
    t.readback=createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    particleBarrier(cmd,v.texture.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={particleLightingExtent,particleLightingExtent,1};
    vkCmdCopyImageToBuffer(cmd,v.texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.readback.buffer,1,&copy);
    particleBarrier(cmd,v.texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t.diagnostic={particleLightingExtent,particleLightingExtent,v.tiles,v.texels,v.view.identity,v.view.frame,bytes};resumeWorldSceneColor();return true;
}
WorldParticleLightingDiagnostic VulkanRenderBackendImpl::readWorldParticleLighting(){
    if(frameActive||!particleLighting||!particleLighting->readback.buffer)return {};
    particleCheck(vkDeviceWaitIdle(device),"Particle lighting readback wait");auto& t=*particleLighting;auto result=t.diagnostic;
    result.lighting.resize(std::size_t(result.width)*result.height);
    for(std::size_t i=0;i<result.lighting.size();++i)result.lighting[i]=unpackWorldTemporalHalf(static_cast<const std::uint16_t*>(t.readback.mapped)+i*4);
    destroyBuffer(t.readback);return result;
}
