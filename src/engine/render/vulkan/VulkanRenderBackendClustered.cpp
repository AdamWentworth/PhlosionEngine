#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include "engine/render/WorldSurfaceComposition.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/WorldClusteredLighting.h"
#include <cstring>
using namespace engine::render;
namespace {
void clusterVk(VkResult r,const char* message){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(message)+": "+std::to_string(r));}
void clusterImageBarrier(VkCommandBuffer command,VkImage image,VkImageLayout from,VkImageLayout to,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=from;b.newLayout=to;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={aspect,0,1,0,1};
    b.srcAccessMask=from==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
void clusterWorkBarrier(VkCommandBuffer command) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&b,0,nullptr,0,nullptr);
}
}
struct VulkanRenderBackendImpl::ClusteredState {
    struct Image {Texture texture;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;};
    struct View {Image diffuse,specular,indirect;Buffer work,args,constants;VkDescriptorSet set=VK_NULL_HANDLE;int width=0,height=0;};
    std::unordered_map<unsigned,View> views;
    std::uint64_t frame=~std::uint64_t(0);
    unsigned resolves=0;
    View* selected=nullptr;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    std::array<VkPipeline,7> pipelines{};
    VkSampler linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
    Buffer readback;WorldClusteredLightingDiagnostic diagnostic;
};
void VulkanRenderBackendImpl::destroyWorldClusteredLighting() {
    if(!clustered)return;
    auto& t=*clustered;
    for(auto& item:t.views) {
        auto& v=item.second;
        for(auto* image:{&v.diffuse,&v.specular,&v.indirect}) {
            if(image->texture.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&image->texture.descriptorSet);
            destroyTexture(image->texture);
        }
        destroyBuffer(v.work);destroyBuffer(v.args);destroyBuffer(v.constants);
    }
    destroyBuffer(t.readback);
    for(auto p:t.pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
    if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);
    if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
    if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);
    if(t.linear)vkDestroySampler(device,t.linear,nullptr);
    if(t.nearest)vkDestroySampler(device,t.nearest,nullptr);
    clustered.reset();worldIndirectReceiver=VK_NULL_HANDLE;worldClusteredView={};worldClusteredDescriptors={};worldStoredSurfaceDescriptors={};
}
bool VulkanRenderBackendImpl::resolveWorldClusteredLighting(const WorldClusteredLightingView& view) {
    if(worldSurfaceTarget && worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!frameActive||!worldSceneColorPassActive||!worldSurfaceReady||worldSurfaceActive||!worldSurfaceTarget||!worldSurfaceDepth)return false;
    if(worldSurfaceCompositionCapture && !engine::render::worldSurfaceCompositionLightingMatches(worldCompositionLighting,view))return false;
    const auto packed=packWorldClusteredLighting(worldSurfaceView,view,worldLocalShadows_.uniforms);
    std::array<std::array<float,4 >,269> parameters{};
    std::copy(packed.begin(),packed.end(),parameters.begin());
    std::copy_n(worldDiffuseProbes.begin(),5,parameters.begin()+264);
    const bool gi=view.environment.enabled && worldDiffuseProbes[0][3]>1.5f;
    if(gi && (!worldIndirectBuffers[0]||!worldIndirectBuffers[1]))
        throw std::logic_error("Indirect receiver field is not bound");
    worldIndirectReceiver=VK_NULL_HANDLE;
    const int width=int(parameters[8][0]),height=int(parameters[8][1]);const WorldClusteredLayout grid(width,height);
    if(!clustered)clustered=std::make_shared<ClusteredState>();
    auto& t=*clustered;
    if(!t.layout) {
        std::array<VkDescriptorSetLayoutBinding,16> bindings{};
        for(unsigned i=0;i<13;++i)bindings[i]={i,i<8?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:i<10?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:i<12?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        for(unsigned i=13;i<16;++i)bindings[i]={i,i==15?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=16;set.pBindings=bindings.data();
        clusterVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&t.setLayout),"Cluster set layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.setLayout;
        clusterVk(vkCreatePipelineLayout(device,&layout,nullptr,&t.layout),"Cluster pipeline layout");
        constexpr unsigned count=16*kFramesInFlight;
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*8},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=4;pool.pPoolSizes=sizes;
        clusterVk(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"Cluster descriptor pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        clusterVk(vkCreateSampler(device,&sampler,nullptr,&t.linear),"Cluster linear sampler");
        sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
        clusterVk(vkCreateSampler(device,&sampler,nullptr,&t.nearest),"Cluster nearest sampler");
    }
    for(unsigned i=0;i<(gi?7u:6u);++i)if(!t.pipelines[i]) {
        const auto name=i==6?std::string("indirect_receiver.comp.spv"):"clustered_"+std::to_string(i)+".comp.spv";auto shader=loadShaderModule(name.c_str());
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.layout;
        info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};
        const auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.pipelines[i]);vkDestroyShaderModule(device,shader,nullptr);
        clusterVk(result,"Cluster compute pipeline");
    }
    // Descriptor updates cannot invalidate earlier commands in this frame.
    if(t.frame!=frameCounter){t.frame=frameCounter;t.resolves=0;t.selected=nullptr;}
    if(t.resolves>=16)throw std::runtime_error("Clustered resolves per frame exceeded");
    auto& v=t.views[currentFrame*16+t.resolves++];t.selected=&v;
    const bool resized=v.width!=width||v.height!=height;
    if(resized && v.width)clusterVk(vkDeviceWaitIdle(device),"Cluster resize");
    const auto allocate=[&](ClusteredState::Image& image,int planes=1) {
            auto& texture=image.texture;
            if(texture.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&texture.descriptorSet);
            destroyTexture(texture);image.layout=VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=VK_FORMAT_R16G16B16A16_SFLOAT;
            info.extent={unsigned(width),unsigned(height*planes),1};info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            clusterVk(vkCreateImage(device,&info,nullptr,&texture.image),"Cluster image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,texture.image,&needs);
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=needs.size;memory.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            clusterVk(vkAllocateMemory(device,&memory,nullptr,&texture.memory),"Cluster image memory");
            clusterVk(vkBindImageMemory(device,texture.image,texture.memory,0),"Cluster image binding");
            VkImageViewCreateInfo imageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};imageView.image=texture.image;imageView.format=info.format;
            imageView.viewType=VK_IMAGE_VIEW_TYPE_2D;imageView.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            clusterVk(vkCreateImageView(device,&imageView,nullptr,&texture.view),"Cluster image view");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            clusterVk(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"Cluster output sampler");
            texture.width=width;texture.height=height*planes;texture.format=info.format;texture.allocationBytes=needs.size;
            texture.descriptorSet=createPlanarDescriptor(texture);
        };
    if(resized) {
        allocate(v.diffuse);allocate(v.specular);destroyBuffer(v.work);destroyBuffer(v.args);
        v.work=createBuffer(grid.words*4ull,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        v.args=createBuffer(24,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(!v.constants.buffer)v.constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        v.width=width;v.height=height;
    }
    if(gi && (!v.indirect.texture.image || v.indirect.texture.width!=width || v.indirect.texture.height!=height*2)) {
        if(v.indirect.texture.image && !resized)clusterVk(vkDeviceWaitIdle(device),"Indirect receiver resize");
        allocate(v.indirect,2);
    }
    std::memcpy(v.constants.mapped,parameters.data(),sizeof(parameters));
    if(!v.set) {
        VkDescriptorSetAllocateInfo setAllocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};setAllocation.descriptorPool=t.pool;setAllocation.descriptorSetCount=1;setAllocation.pSetLayouts=&t.setLayout;
        clusterVk(vkAllocateDescriptorSets(device,&setAllocation,&v.set),"Cluster dispatch set");
    }
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    const auto ready=[&](ClusteredState::Image& image,VkImageLayout layout){clusterImageBarrier(command,image.texture.image,image.layout,layout);image.layout=layout;};
    ready(v.diffuse,VK_IMAGE_LAYOUT_GENERAL);ready(v.specular,VK_IMAGE_LAYOUT_GENERAL);
    clusterImageBarrier(command,worldSurfaceDepth->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT);
    for(unsigned i:{0u,1u,2u,5u})clusterImageBarrier(command,worldSurfaceTarget->planes[i].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if(worldShadowImages[currentFrame])clusterImageBarrier(command,worldShadowImages[currentFrame],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT);
    auto& moment=worldMomentTargets[currentFrame][1];
    if(moment.image)clusterImageBarrier(command,moment.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto sampled=[&](VkImageView view,VkSampler sampler){return VkDescriptorImageInfo{sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};};
    std::array<VkDescriptorImageInfo,10> images{};
    images[0]=sampled(worldSurfaceDepth->view,t.nearest);images[1]=sampled(worldSurfaceTarget->planes[0].view,t.nearest);
    images[2]=sampled(worldSurfaceTarget->planes[1].view,t.nearest);images[3]=sampled(worldSurfaceTarget->planes[2].view,t.nearest);
    images[4]=sampled(rectLightLutTexture.view,t.linear);
    images[5]=worldShadowViews[currentFrame]?VkDescriptorImageInfo{t.nearest,worldShadowViews[currentFrame],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}:images[0];
    images[6]=sampled(moment.view?moment.view:worldSurfaceTarget->planes[0].view,t.nearest);
    images[7]=sampled(worldSurfaceTarget->planes[5].view,t.nearest);
    images[8]={VK_NULL_HANDLE,v.diffuse.texture.view,VK_IMAGE_LAYOUT_GENERAL};images[9]={VK_NULL_HANDLE,v.specular.texture.view,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo buffers[]{{v.work.buffer,0,grid.words*4ull},{v.args.buffer,0,24},{v.constants.buffer,0,sizeof(parameters)}};
    std::array<VkWriteDescriptorSet,13> writes{};
    for(unsigned i=0;i<13;++i) {
        auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=v.set;w.dstBinding=i;w.descriptorCount=1;
        w.descriptorType=i<8?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:i<10?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:i<12?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if(i<10)w.pImageInfo=&images[i];else w.pBufferInfo=&buffers[i-10];
    }
    vkUpdateDescriptorSets(device,13,writes.data(),0,nullptr);
    if(gi) {
        VkDescriptorBufferInfo fields[]{{worldIndirectBuffers[0],0,VK_WHOLE_SIZE},{worldIndirectBuffers[1],0,VK_WHOLE_SIZE}};
        VkDescriptorImageInfo receiver{VK_NULL_HANDLE,v.indirect.texture.view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet giWrites[3]{};
        for(unsigned i=0;i<3;++i) {
            auto& w=giWrites[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=v.set;w.dstBinding=13+i;w.descriptorCount=1;
            w.descriptorType=i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if(i==2)w.pImageInfo=&receiver;else w.pBufferInfo=&fields[i];
        }
        vkUpdateDescriptorSets(device,3,giWrites,0,nullptr);
    }
    vkCmdBindDescriptorSets(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&v.set,0,nullptr);
    const auto dispatch=[&](unsigned pass,unsigned x,unsigned y,unsigned z){vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[pass]);vkCmdDispatch(command,x,y,z);clusterWorkBarrier(command);};
    const auto beginPhase=[&](const char* name) {
        auto& frame=frames[currentFrame];if(!frame.timestampIssued)return -1;
        const int token=phaseProfile.begin(frame.phaseFrame,name);
        if(token>=0)vkCmdWriteTimestamp(worldRecordingCommand(command),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query);
        return token;
    };
    const auto endPhase=[&](int token) {
        auto& frame=frames[currentFrame];
        if(frame.timestampIssued&&phaseProfile.end(frame.phaseFrame,token))
            vkCmdWriteTimestamp(worldRecordingCommand(command),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);
    };
    auto token=beginPhase("cluster-cull");
    dispatch(0,(grid.coarseX+7)/8,(grid.coarseY+7)/8,1);dispatch(1,(grid.x+7)/8,(grid.y+7)/8,4);
    endPhase(token);token=beginPhase("cluster-classify");
    dispatch(2,grid.x,grid.y,1);dispatch(3,1,1,1);
    endPhase(token);token=beginPhase("cluster-shade");
    for(unsigned c=0;c<2;++c){vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[4+c]);vkCmdDispatchIndirect(command,v.args.buffer,c*12);clusterWorkBarrier(command);}
    endPhase(token);
    if(gi) {
        token=beginPhase("gi-receiver-prepare");
        ready(v.indirect,VK_IMAGE_LAYOUT_GENERAL);
        dispatch(6,(width+7)/8,(height+7)/8,1);
        ready(v.indirect,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        worldIndirectReceiver=v.indirect.texture.descriptorSet;
        endPhase(token);
    }
    ready(v.diffuse,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);ready(v.specular,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if(!worldSurfaceCompositionCapture)worldCompositionLighting=view;
    std::copy_n(worldSurfaceView.begin(),4,worldClusteredView.begin());worldClusteredView[4]={float(width),float(height),0,1};
    worldClusteredDescriptors={v.diffuse.texture.descriptorSet,v.specular.texture.descriptorSet,worldSurfaceDepth->descriptorSet};
    for(unsigned i=0;i<4;++i)worldStoredSurfaceDescriptors[i]=worldSurfaceTarget->planes[i].descriptorSet;
    worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
    resumeWorldSceneColor();return true;
}

bool VulkanRenderBackendImpl::captureWorldClusteredLighting() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!frameActive||!worldSceneColorPassActive||!worldSurfaceReady||!clustered||!clustered->selected||worldClusteredView[4][3]<.5f)return false;
    auto& t=*clustered;auto& v=*t.selected;
    const WorldClusteredLayout grid(v.width,v.height);
    if(t.readback.buffer)throw std::logic_error("Unread clustered diagnostic");
    t.readback=createBuffer(grid.words*4ull,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);
    VkBufferCopy copy{0,0,grid.words*4ull};vkCmdCopyBuffer(command,v.work.buffer,t.readback.buffer,1,&copy);
    b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
    t.diagnostic={v.width,v.height,{}};resumeWorldSceneColor();return true;
}
engine::render::WorldClusteredLightingDiagnostic VulkanRenderBackendImpl::readWorldClusteredLighting() {
    if(frameActive||!clustered||!clustered->readback.buffer)return {};
    auto& t=*clustered;clusterVk(vkDeviceWaitIdle(device),"Cluster readback wait");
    auto result=t.diagnostic;WorldClusteredLayout grid(result.width,result.height);result.words.resize(grid.words);
    std::memcpy(result.words.data(),t.readback.mapped,grid.words*4ull);destroyBuffer(t.readback);return result;
}
