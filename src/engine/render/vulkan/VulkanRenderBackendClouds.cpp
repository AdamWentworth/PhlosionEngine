
#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include <cstring>
using namespace engine::render;
namespace {
void cloudCheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
void cloudBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout a,VkImageLayout b){
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};v.image=image;v.oldLayout=a;v.newLayout=b;
    v.srcQueueFamilyIndex=v.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    v.srcAccessMask=a==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    v.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&v);
}
void cloudMemory(VkCommandBuffer cmd){
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
}
}
struct VulkanRenderBackendImpl::CloudState {
    struct Entry{
        WorldCloudView view;WorldCloudRows parameters{};WorldResourceCounts counts;
        Texture shadow,integrated,depth,resolved,skyModel;Buffer tiles;
        std::uint64_t frame=~0ull,used=0;bool resolvedThisFrame=false;
    };
    struct Dispatch{Buffer constants;std::array<VkDescriptorSet,5> sets{};};
    std::array<Entry,8> entries;std::array<std::array<Dispatch,16>,kFramesInFlight> dispatches;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;std::array<VkPipeline,5> pipelines{};
    VkSampler wrap=VK_NULL_HANDLE,linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
    std::uint64_t frame=~0ull,clock=0;unsigned count=0;
    Entry* selected=nullptr;Buffer readback;std::array<std::uint64_t,5> offsets{};
    WorldCloudDiagnostic diagnostic;
};
void VulkanRenderBackendImpl::recordWorldClouds(const WorldCloudView& u,bool resolve){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive)throw std::runtime_error("Cloud pass admission");
    WorldResourceScopeStats owner;if(!worldScopes.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Cloud owner");
    auto parameters=packWorldClouds(u);
    if(u.quality==0&&resolve){worldClouds=cloudConsumerUniforms(u,false);worldClouds[8][3]=worldSkyModelTexture?1.f:0.f;return;}
    if(!clouds)clouds=std::make_shared<CloudState>();auto& t=*clouds;
    if(t.frame!=frameCounter){t.frame=frameCounter;t.count=0;}if(t.count>=16)throw std::runtime_error("Cloud frame view budget");
    if(!t.layout){
        std::array<VkDescriptorSetLayoutBinding,14> bindings{};
        for(unsigned i=0;i<11;++i)bindings[i]={i,i==10?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==6||i==9?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i>=7?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        for(unsigned i=11;i<14;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo s{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};s.bindingCount=14;s.pBindings=bindings.data();
        cloudCheck(vkCreateDescriptorSetLayout(device,&s,nullptr,&t.setLayout),"Cloud set layout");
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&t.setLayout;
        cloudCheck(vkCreatePipelineLayout(device,&l,nullptr,&t.layout),"Cloud pipeline layout");
        constexpr unsigned count=16*5*kFramesInFlight;
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*9},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=4;pool.pPoolSizes=sizes;
        cloudCheck(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"Cloud pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
        cloudCheck(vkCreateSampler(device,&sampler,nullptr,&t.wrap),"Cloud wrap sampler");
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cloudCheck(vkCreateSampler(device,&sampler,nullptr,&t.linear),"Cloud linear sampler");
        sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
        cloudCheck(vkCreateSampler(device,&sampler,nullptr,&t.nearest),"Cloud nearest sampler");
    }
    for(unsigned pass=0;pass<5;++pass)if(!t.pipelines[pass]){
        auto module=loadShaderModule(("clouds_"+std::to_string(pass)+".comp.spv").c_str());
        VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=t.layout;
        p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=prepareComputePipelines(device,pipelineCache,1,&p,nullptr,&t.pipelines[pass]);vkDestroyShaderModule(device,module,nullptr);
        cloudCheck(result,"Cloud pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.used&&v.view.owner==u.owner&&v.view.view.identity==u.view.identity;});
    if(resolve){
        if(found==t.entries.end()||found->frame!=frameCounter||found->parameters!=parameters||found->resolvedThisFrame)
            throw std::invalid_argument("Cloud resolve has no matching current producer");
    }else{
        if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
            if(it->frame!=frameCounter&&(found==t.entries.end()||it->used<found->used))found=it;
        if(found==t.entries.end()||found->frame==frameCounter)throw std::runtime_error("Cloud view already prepared");
    }
    auto& v=*found;const bool enabled=u.quality!=0;
    const unsigned lowW=enabled?unsigned(parameters[0][2]):1,lowH=enabled?unsigned(parameters[0][3]):1,shadowRes=enabled?unsigned(parameters[1][0]):1;
    const unsigned fullW=enabled?unsigned(u.view.width):1,fullH=enabled?unsigned(u.view.height):1;
    Texture* opaque=&fallbackWorldLinearTexture;
    if(resolve){
        if(!worldSurfaceReady||!worldSurfaceTarget||worldSurfaceTarget->probeScratch||worldSurfacePlanar||
           !surfaceContactViewMatches(worldSurfaceView,packWorldContactVisibility(u.view.viewProjection.data(),u.view.width,u.view.height,.8f,.015f)))
            throw std::runtime_error("Cloud primary depth mismatch");
        opaque=worldSurfaceDepth;
    }
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    if(!v.used||v.view.owner!=u.owner||v.parameters[0]!=parameters[0]||v.parameters[1]!=parameters[1]||(v.view.quality!=0)!=enabled){
        cloudCheck(vkDeviceWaitIdle(device),"Cloud resize wait");
        if(v.counts.textures)worldScopes.reclaim(v.view.owner,v.counts);v.counts={};
        const auto tileBytes=(enabled?std::uint64_t(parameters[23][0])+3:4)*4;
        const auto bytes=std::uint64_t(shadowRes)*shadowRes*8+std::uint64_t(lowW)*lowH*12+std::uint64_t(fullW)*fullH*8+tileBytes+128*64*8;
        if(bytes>256ull*1024*1024)throw std::runtime_error("Cloud view memory limit");
        const bool own=worldScopes.active()==0;if(own&&!worldScopes.begin(u.owner))throw std::runtime_error("Cloud scope");
        if(worldScopes.active()!=u.owner||!worldScopes.admit(bytes)){if(own)worldScopes.end();throw std::runtime_error("Cloud allocation budget");}
        const auto image=[&](Texture& target,unsigned w,unsigned h,VkFormat format){
            destroyTexture(target);
            VkImageCreateInfo d{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};d.imageType=VK_IMAGE_TYPE_2D;d.format=format;d.extent={w,h,1};
            d.mipLevels=d.arrayLayers=1;d.samples=VK_SAMPLE_COUNT_1_BIT;d.tiling=VK_IMAGE_TILING_OPTIMAL;
            d.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            cloudCheck(vkCreateImage(device,&d,nullptr,&target.image),"Cloud image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,target.image,&needs);
            VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=needs.size;m.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            cloudCheck(vkAllocateMemory(device,&m,nullptr,&target.memory),"Cloud memory");
            cloudCheck(vkBindImageMemory(device,target.image,target.memory,0),"Cloud memory bind");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=target.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            cloudCheck(vkCreateImageView(device,&view,nullptr,&target.view),"Cloud view");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            cloudCheck(vkCreateSampler(device,&sampler,nullptr,&target.sampler),"Cloud sampler");
            target.width=w;target.height=h;target.format=format;target.allocationBytes=needs.size;target.payloadBytes=std::uint64_t(w)*h*(format==VK_FORMAT_R32_SFLOAT?4:8);
            cloudBarrier(cmd,target.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue clear{};clear.float32[3]=1;
            vkCmdClearColorImage(cmd,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&view.subresourceRange);
            cloudBarrier(cmd,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        try{
            image(v.shadow,shadowRes,shadowRes,VK_FORMAT_R16G16B16A16_SFLOAT);
            image(v.integrated,lowW,lowH,VK_FORMAT_R16G16B16A16_SFLOAT);
            image(v.depth,lowW,lowH,VK_FORMAT_R32_SFLOAT);
            image(v.resolved,fullW,fullH,VK_FORMAT_R16G16B16A16_SFLOAT);
            image(v.skyModel,128,64,VK_FORMAT_R16G16B16A16_SFLOAT);
            destroyBuffer(v.tiles);v.tiles=createBuffer(tileBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            v.counts.textures=5;v.counts.geometryBuffers=1;v.counts.textureDescriptors=12;v.counts.payloadBytes=bytes;v.counts.apiAllocationBytes=tileBytes;
            for(auto* i:{&v.shadow,&v.integrated,&v.depth,&v.resolved,&v.skyModel})v.counts.apiAllocationBytes+=i->allocationBytes;
            worldScopes.commit(v.counts);if(own)worldScopes.end();
        }catch(...){if(own)worldScopes.end();throw;}
    }
    const bool own=worldScopes.active()==0;if(own&&!worldScopes.begin(u.owner))throw std::runtime_error("Cloud texture scope");
    const auto input=[&](const IRenderBackend::WorldTextureData& t){
        return ensureWorldTextureRaw(t.key,t.cacheKey,t.rgba,t.width,t.height,t.wrapS,t.wrapT,false,nullptr,t.mipLevels,t.mipLevelCount,true);
    };
    auto* weather=input(u.weather);auto* noise=input(u.noise);if(own)worldScopes.end();
    if(!weather||!noise)throw std::runtime_error("Cloud texture inputs");
    if(resolve)std::copy(worldFog.begin(),worldFog.end(),parameters.begin()+32);
    auto& dispatch=t.dispatches[currentFrame][t.count++];
    if(!dispatch.constants.buffer)dispatch.constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(dispatch.constants.mapped,parameters.data(),sizeof(parameters));
    if(resolve){
        vkCmdFillBuffer(cmd,v.tiles.buffer,0,VK_WHOLE_SIZE,0);
        for(auto* i:{&v.integrated,&v.depth}){
            cloudBarrier(cmd,i->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue clear{};clear.float32[3]=1;if(i==&v.depth)clear.float32[0]=1e7f;
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdClearColorImage(cmd,i->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
            cloudBarrier(cmd,i->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    cloudMemory(cmd);
    const char* names[]{"cloud-shadow","cloud-visible","cloud-integrate","cloud-reconstruct","sky-model-filter"};
    for(unsigned iteration=0;iteration<(resolve?3u:enabled?2u:1u);++iteration){
        const unsigned pass=resolve?iteration+1:iteration==0?4:0;
        auto& out=pass==4?v.skyModel:pass==0?v.shadow:pass==3?v.resolved:v.integrated;
        cloudBarrier(cmd,out.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        cloudBarrier(cmd,v.depth.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        auto& set=dispatch.sets[pass];if(!set){
            VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=t.pool;a.descriptorSetCount=1;a.pSetLayouts=&t.setLayout;
            cloudCheck(vkAllocateDescriptorSets(device,&a,&set),"Cloud set");
        }
        VkDescriptorImageInfo images[]{
            {t.nearest,opaque->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{t.wrap,weather->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.wrap,noise->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{t.linear,v.shadow.view,pass==0?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.nearest,v.integrated.view,pass==1||pass==2?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.nearest,v.depth.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,out.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,v.depth.view,VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo buffers[]{{v.tiles.buffer,0,VK_WHOLE_SIZE},{dispatch.constants.buffer,0,sizeof(parameters)}};
        std::array<VkWriteDescriptorSet,11> writes{};
        for(unsigned i=0;i<11;++i){
            auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;
            if(i==6||i==9||i==10){w.descriptorType=i==10?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i==10?1:0];}
            else{w.descriptorType=i<6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i<6?i:i-1];}
        }
        vkUpdateDescriptorSets(device,11,writes.data(),0,nullptr);
        VkDescriptorImageInfo fogInfo[2]{};VkWriteDescriptorSet fogWrite[2]{};
        for(unsigned i=0;i<2;++i){
            const auto* fog=resolve&&worldFogTextures[i]?worldFogTextures[i]:weather;
            fogInfo[i]={t.linear,fog->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto& w=fogWrite[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=11+i;
            w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.descriptorCount=1;w.pImageInfo=&fogInfo[i];
        }
        vkUpdateDescriptorSets(device,2,fogWrite,0,nullptr);
        const auto* sky=resolve?&v.skyModel:weather;
        VkDescriptorImageInfo skyInfo{t.linear,sky->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet skyWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        skyWrite.dstSet=set;skyWrite.dstBinding=13;skyWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyWrite.descriptorCount=1;skyWrite.pImageInfo=&skyInfo;
        vkUpdateDescriptorSets(device,1,&skyWrite,0,nullptr);
        auto& frame=frames[currentFrame];const int token=frame.timestampIssued?phaseProfile.begin(frame.phaseFrame,names[pass]):-1;
        if(token>=0)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[pass]);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);
        if(pass==4)vkCmdDispatch(cmd,16,8,1);
        if(pass==0)vkCmdDispatch(cmd,(shadowRes+7)/8,(shadowRes+7)/8,1);
        if(pass==1)vkCmdDispatch(cmd,(unsigned(parameters[1][2])+7)/8,(unsigned(parameters[1][3])+7)/8,1);
        if(pass==2)vkCmdDispatchIndirect(cmd,v.tiles.buffer,0);
        if(pass==3)vkCmdDispatch(cmd,(unsigned(u.view.width)+7)/8,(unsigned(u.view.height)+7)/8,1);
        if(token>=0){phaseProfile.end(frame.phaseFrame,token);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);}
        cloudBarrier(cmd,out.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cloudBarrier(cmd,v.depth.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cloudMemory(cmd);
    }
    v.view=u;v.parameters=packWorldClouds(u);v.frame=frameCounter;v.used=++t.clock;v.resolvedThisFrame=resolve;t.selected=enabled?&v:nullptr;
    worldSkyCloud=parameters;worldClouds=cloudConsumerUniforms(u,resolve);worldCloudTextures={&v.shadow,&v.resolved};worldCloudOwner=u.owner;worldSkyModelTexture=&v.skyModel;worldClouds[8][3]=1;
    worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
    resumeWorldSceneColor();
}
void VulkanRenderBackendImpl::prepareWorldClouds(const WorldCloudView& u){recordWorldClouds(u,false);}
void VulkanRenderBackendImpl::resolveWorldClouds(const WorldCloudView& u){recordWorldClouds(u,true);}
void VulkanRenderBackendImpl::clearWorldClouds(){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    worldSkyModelTexture=nullptr;worldSkyCloud={};worldClouds={};worldCloudTextures={};worldCloudOwner=0;
    if(frameActive)worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
}
void VulkanRenderBackendImpl::releaseWorldClouds(std::uint64_t owner){
    if(!clouds)return;auto& t=*clouds;
    if(owner==~0ull||owner==worldCloudOwner){worldSkyModelTexture=nullptr;worldSkyCloud={};worldClouds={};worldCloudTextures={};worldCloudOwner=0;}
    for(auto& v:t.entries)if(owner==~0ull||v.view.owner==owner){
        for(auto* i:{&v.shadow,&v.integrated,&v.depth,&v.resolved,&v.skyModel})destroyTexture(*i);
        destroyBuffer(v.tiles);v.used=0;v.view={};v.parameters={};v.counts={};if(t.selected==&v)t.selected=nullptr;
    }
    if(owner!=~0ull)return;
    for(auto& f:t.dispatches)for(auto& d:f)destroyBuffer(d.constants);destroyBuffer(t.readback);
    for(auto p:t.pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
    if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
    if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);
    for(auto s:{t.wrap,t.linear,t.nearest})if(s)vkDestroySampler(device,s,nullptr);
    clouds.reset();
}
bool VulkanRenderBackendImpl::captureWorldClouds(){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!clouds||!clouds->selected||clouds->readback.buffer||!clouds->selected->resolvedThisFrame)return false;
    auto& t=*clouds;auto& v=*t.selected;std::uint64_t total=0;unsigned index=0;
    for(auto* i:{&v.shadow,&v.integrated,&v.resolved,&v.skyModel}){t.offsets[index++]=total;total+=std::uint64_t(i->width)*i->height*8;}
    t.offsets[4]=total;t.readback=createBuffer(total+16,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);index=0;
    for(auto* i:{&v.shadow,&v.integrated,&v.resolved,&v.skyModel}){
        cloudBarrier(cmd,i->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.bufferOffset=t.offsets[index++];copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={unsigned(i->width),unsigned(i->height),1};
        vkCmdCopyImageToBuffer(cmd,i->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.readback.buffer,1,&copy);
        cloudBarrier(cmd,i->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    cloudMemory(cmd);VkBufferCopy copy{0,total,4};vkCmdCopyBuffer(cmd,v.tiles.buffer,t.readback.buffer,1,&copy);
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
    t.diagnostic={unsigned(v.view.view.width),unsigned(v.view.view.height),unsigned(v.parameters[1][0]),0,v.counts.payloadBytes,v.view.owner,v.view.view.identity,v.view.view.frame};
    resumeWorldSceneColor();return true;
}
WorldCloudDiagnostic VulkanRenderBackendImpl::readWorldClouds(){
    if(frameActive||!clouds||!clouds->readback.buffer)return {};
    cloudCheck(vkDeviceWaitIdle(device),"Cloud readback wait");auto& t=*clouds;auto out=t.diagnostic;unsigned index=0;
    for(auto* output:{&out.shadows,&out.integrated,&out.resolved,&out.skyModel}){
        const auto count=(t.offsets[index+1]-t.offsets[index])/8;output->resize(count);
        for(std::size_t i=0;i<count;++i)(*output)[i]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(t.readback.mapped)+t.offsets[index]+i*8));
        ++index;
    }
    std::memcpy(&out.visibleTiles,static_cast<const char*>(t.readback.mapped)+t.offsets[4],4);destroyBuffer(t.readback);return out;
}
