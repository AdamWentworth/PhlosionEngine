#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include <cstring>
using namespace engine::render;
namespace {
void volCheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
void volBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout a,VkImageLayout b){
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};v.image=image;v.oldLayout=a;v.newLayout=b;
    v.srcQueueFamilyIndex=v.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    v.srcAccessMask=a==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    v.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&v);
}
}
struct VulkanRenderBackendImpl::PrecipitationState {
    struct Entry {
        WorldPrecipitationLightingView previous;
        std::array<unsigned,3> dimensions{};
        Texture light[2],scatter,depth;
        WorldResourceCounts counts;
        unsigned bank=0;std::uint64_t frame=~std::uint64_t(0),used=0;
        bool history=false;std::string key;
    };
    struct Dispatch {Buffer constants;std::array<VkDescriptorSet,3> sets{};};
    std::array<Entry,8> entries;
    std::array<std::array<Dispatch,8>,kFramesInFlight> dispatches;
    std::uint64_t frame=~std::uint64_t(0),clock=0;unsigned count=0;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;std::array<VkPipeline,3> pipelines{};
    VkSampler linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
    Entry* selected=nullptr;Buffer readback;
    WorldPrecipitationLightingDiagnostic diagnostic;
};
WorldPrecipitationLightingResult VulkanRenderBackendImpl::updateWorldPrecipitationLighting(const WorldPrecipitationLightingView& u){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive)throw std::runtime_error("Precipitation lighting pass admission");
    WorldResourceScopeStats owner;if(!worldScopes.query(u.owner,owner)||owner.releasePending)throw std::invalid_argument("Precipitation owner");
    const auto dims=precipitationDimensions(u);
    if(!precipitation)precipitation=std::make_shared<PrecipitationState>();auto& t=*precipitation;
    if(t.frame!=frameCounter){t.frame=frameCounter;t.count=0;}if(t.count>=8)throw std::runtime_error("Precipitation frame view budget");
    if(!t.layout){
        std::array<VkDescriptorSetLayoutBinding,12> bindings{};
        for(unsigned i=0;i<10;++i)bindings[i]={i,i==9?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i>=7?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:i>=5?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[10]={10,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[11]={11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo s{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};s.bindingCount=12;s.pBindings=bindings.data();
        volCheck(vkCreateDescriptorSetLayout(device,&s,nullptr,&t.setLayout),"Precipitation set layout");
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&t.setLayout;
        volCheck(vkCreatePipelineLayout(device,&l,nullptr,&t.layout),"Precipitation pipeline layout");
        constexpr unsigned count=8*3*kFramesInFlight;
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*7},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=4;pool.pPoolSizes=sizes;
        volCheck(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"Precipitation descriptor pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        volCheck(vkCreateSampler(device,&sampler,nullptr,&t.linear),"Precipitation sampler");
        sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;volCheck(vkCreateSampler(device,&sampler,nullptr,&t.nearest),"Precipitation depth sampler");
    }
    for(unsigned pass=0;pass<3;++pass)if(!t.pipelines[pass]){
        auto module=loadShaderModule(("precipitation_"+std::to_string(pass)+".comp.spv").c_str());
        VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=t.layout;
        p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=prepareComputePipelines(device,pipelineCache,1,&p,nullptr,&t.pipelines[pass]);vkDestroyShaderModule(device,module,nullptr);
        volCheck(result,"Precipitation pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& v){return v.previous.owner==u.owner&&v.previous.view.identity==u.view.identity&&v.previous.distant==u.distant;});
    if(found==t.entries.end())for(auto it=t.entries.begin();it!=t.entries.end();++it)
        if(it->frame!=frameCounter&&(found==t.entries.end()||it->used<found->used))found=it;
    if(found==t.entries.end()||found->frame==frameCounter)throw std::runtime_error("Precipitation view already recorded");
    auto& v=*found;const unsigned slot=unsigned(found-t.entries.begin());
    const bool history=v.used&&precipitationHistoryValid(u,v.previous)&&dims==v.dimensions;
    Texture* opaqueDepth=worldDepthSnapshot;
    if(u.usePreparedSurfaceDepth) {
        if(!worldSurfaceReady || !worldSurfaceTarget || worldSurfaceTarget->probeScratch || worldSurfacePlanar ||
           !worldProbeSurfaceKey.empty() || worldLocalReflection[0][3]<0 ||
           !surfaceContactViewMatches(worldSurfaceView,packWorldContactVisibility(u.view.viewProjection.data(),u.view.width,u.view.height,.8f,.015f)))
            throw std::runtime_error("Fog prepared opaque depth view mismatch");
        opaqueDepth=worldSurfaceDepth;
    } else {
        if(!captureWorldDepthSnapshot())throw std::runtime_error("Precipitation opaque depth");
        opaqueDepth=worldDepthSnapshot;
    }
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    if(!v.used||dims!=v.dimensions||v.previous.owner!=u.owner||
       u.view.width!=v.previous.view.width||u.view.height!=v.previous.view.height){
        volCheck(vkDeviceWaitIdle(device),"Precipitation view resize wait");
        if(v.counts.textures){worldScopes.reclaim(v.previous.owner,v.counts);v.counts={};}
        const auto bytes=std::uint64_t(dims[0])*dims[1]*dims[2]*24+std::uint64_t((u.view.width+15)/16)*((u.view.height+15)/16)*4;
        if(bytes>worldPrecipitationLightingByteLimit)throw std::runtime_error("Precipitation view memory budget");
        const bool scope=worldScopes.active()==0;if(scope&&!worldScopes.begin(u.owner))throw std::runtime_error("Precipitation resource admission");
        if(worldScopes.active()!=u.owner||!worldScopes.admit(bytes)){if(scope)worldScopes.end();throw std::runtime_error("Precipitation resource budget");}
        const auto create=[&](Texture& texture,unsigned w,unsigned h,VkFormat format){
            destroyTexture(texture);
            VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=format;i.extent={w,h,1};i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;
            i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            volCheck(vkCreateImage(device,&i,nullptr,&texture.image),"Precipitation image");
            VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,texture.image,&needs);
            VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=needs.size;m.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            volCheck(vkAllocateMemory(device,&m,nullptr,&texture.memory),"Precipitation image memory");
            volCheck(vkBindImageMemory(device,texture.image,texture.memory,0),"Precipitation image bind");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=texture.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            volCheck(vkCreateImageView(device,&view,nullptr,&texture.view),"Precipitation image view");
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            volCheck(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"Precipitation texture sampler");
            texture.width=w;texture.height=h;texture.format=format;texture.allocationBytes=needs.size;texture.payloadBytes=std::uint64_t(w)*h*(format==VK_FORMAT_R32_SFLOAT?4:8);
            volBarrier(cmd,texture.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue clear{};
            vkCmdClearColorImage(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&view.subresourceRange);
            volBarrier(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        try{
            for(auto* image:{&v.light[0],&v.light[1],&v.scatter})create(*image,dims[0]*8,dims[1]*((dims[2]+7)/8),VK_FORMAT_R16G16B16A16_SFLOAT);
            create(v.depth,(u.view.width+15)/16,(u.view.height+15)/16,VK_FORMAT_R32_SFLOAT);
            v.counts={};v.counts.textures=4;v.counts.textureDescriptors=8;v.counts.payloadBytes=bytes;
            for(auto* image:{&v.light[0],&v.light[1],&v.scatter,&v.depth})v.counts.apiAllocationBytes+=image->allocationBytes;
            worldScopes.commit(v.counts);if(scope)worldScopes.end();
        }catch(...){if(scope)worldScopes.end();throw;}
        for(auto& [key,material]:worldMaterials)if(material.textures[4]==&v.scatter){
            VkDescriptorImageInfo info{v.scatter.sampler,v.scatter.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=material.descriptorSet;w.dstBinding=4;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&info;
            vkUpdateDescriptorSets(device,1,&w,0,nullptr);
        }
        initializeIndexedWorldMaterialSets();v.dimensions=dims;v.bank=0;
    }
    auto parameters=packPrecipitationLighting(u,history?v.previous:u,history,worldDiffuseProbes,worldShadowReady?worldShadowView:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv());
    std::copy(worldSunPlan.uniforms.begin(),worldSunPlan.uniforms.end(),parameters.begin()+600);
    if(worldCloudOwner==u.owner){parameters[592]=worldClouds[0];parameters[593]=worldClouds[1];}
    auto& dispatch=t.dispatches[currentFrame][t.count++];
    if(!dispatch.constants.buffer)dispatch.constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(dispatch.constants.mapped,parameters.data(),sizeof(parameters));
    if(!worldIndirectFallbackBuffer.buffer){worldIndirectFallbackBuffer=createBuffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);std::memset(worldIndirectFallbackBuffer.mapped,0,16);}
    const unsigned next=1-v.bank;
    VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,nullptr,0,nullptr);
    const char* names[]{"rain-light-depth","rain-light-volume","rain-light-integrate"};
    for(unsigned pass=u.distant?1:0;pass<3;++pass){
        auto& output=pass==2?v.scatter:v.light[next];
        auto& set=dispatch.sets[pass];if(!set){VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=t.pool;a.descriptorSetCount=1;a.pSetLayouts=&t.setLayout;volCheck(vkAllocateDescriptorSets(device,&a,&set),"Precipitation dispatch set");}
        VkDescriptorImageInfo images[]{
            {t.nearest,opaqueDepth->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.linear,v.light[v.bank].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.nearest,v.light[next].view,pass==2?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL},
            {t.nearest,v.depth.view,pass==0?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {t.nearest,worldShadowViews[currentFrame]?worldShadowViews[currentFrame]:fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE,output.view,VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE,v.depth.view,VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo buffers[]{
            {worldIndirectBuffers[0]?worldIndirectBuffers[0]:worldIndirectFallbackBuffer.buffer,0,VK_WHOLE_SIZE},
            {worldIndirectBuffers[1]?worldIndirectBuffers[1]:worldIndirectFallbackBuffer.buffer,0,VK_WHOLE_SIZE},
            {dispatch.constants.buffer,0,sizeof(parameters)}};
        std::array<VkWriteDescriptorSet,10> writes{};
        for(unsigned i=0;i<10;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;
            if(i<5||i==7||i==8){w.descriptorType=i<5?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&images[i<5?i:i-2];}
            else{w.descriptorType=i==9?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i==9?2:i-5];}}
        vkUpdateDescriptorSets(device,10,writes.data(),0,nullptr);
        const auto* cloud=worldCloudOwner==u.owner&&worldCloudTextures[0]?worldCloudTextures[0]:&fallbackWorldLinearTexture;
        VkDescriptorImageInfo cloudInfo{t.linear,cloud->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet cloudWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};cloudWrite.dstSet=set;cloudWrite.dstBinding=10;
        cloudWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;cloudWrite.descriptorCount=1;cloudWrite.pImageInfo=&cloudInfo;
        vkUpdateDescriptorSets(device,1,&cloudWrite,0,nullptr);
    VkDescriptorImageInfo sunInfo{worldSunSampler,worldSunView?worldSunView:worldSunFallbackView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet sunWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};sunWrite.dstSet=set;sunWrite.dstBinding=11;
    sunWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;sunWrite.descriptorCount=1;sunWrite.pImageInfo=&sunInfo;
    vkUpdateDescriptorSets(device,1,&sunWrite,0,nullptr);

        volBarrier(cmd,output.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        if(pass==0)volBarrier(cmd,v.depth.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        auto& frame=frames[currentFrame];const int token=frame.timestampIssued?phaseProfile.begin(frame.phaseFrame,u.distant?(pass==1?"fog-distant-volume":"fog-distant-integrate"):names[pass]):-1;
        if(token>=0)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[pass]);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);
        if(pass==0)vkCmdDispatch(cmd,((u.view.width+15)/16+7)/8,((u.view.height+15)/16+7)/8,1);
        else if(pass==1)vkCmdDispatch(cmd,(dims[0]+3)/4,(dims[1]+3)/4,(dims[2]+3)/4);
        else vkCmdDispatch(cmd,(dims[0]+7)/8,(dims[1]+7)/8,1);
        if(token>=0&&phaseProfile.end(frame.phaseFrame,token))vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);
        volBarrier(cmd,output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if(pass==0)volBarrier(cmd,v.depth.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    v.bank=next;v.previous=u;v.frame=frameCounter;v.used=++t.clock;v.history=history;v.key="phlosion://precipitation/"+std::to_string(slot);t.selected=&v;
    resumeWorldSceneColor();
    return {v.key,{float(dims[0]),float(dims[1]),float(dims[2]),8},{u.range,1/u.depthExponent,u.environment.timeOfDayHours/24.f,1}};
}
void VulkanRenderBackendImpl::bindWorldFog(const WorldPrecipitationLightingView* view,const WorldPrecipitationLightingResult* nearVolume,const WorldPrecipitationLightingResult* distantVolume) {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    worldFog={};worldFogTextures={};
    if(view) {
        if(!nearVolume&&!distantVolume&&worldSceneColorPassActive) {
            worldFog=packWorldFog(*view,{},{});
            if(frameActive)worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
            return;
        }
        if(!nearVolume||!distantVolume||!precipitation||!worldSceneColorPassActive)throw std::invalid_argument("Fog view binding");
        for(unsigned i=0;i<2;++i) {
            const auto& r=i?*distantVolume:*nearVolume;
            auto found=std::find_if(precipitation->entries.begin(),precipitation->entries.end(),[&](const auto& v) {
                return v.key==r.texture&&fogBindingViewMatches(v.previous,*view)&&v.previous.distant==bool(i)&&v.frame==frameCounter;
            });
            if(found==precipitation->entries.end())throw std::invalid_argument("Stale fog view texture");
            worldFogTextures[i]=&found->scatter;
        }
        worldFog=packWorldFog(*view,*nearVolume,*distantVolume);
    }
    if(frameActive)worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
}
VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::findWorldPrecipitationDepth(
        const WorldParticleLightingView& u,std::array<float,4>& forwardRange) {
    if(!precipitation)return nullptr;
    for(auto& v:precipitation->entries) {
        const auto& p=v.previous;
        if(!v.used||v.frame!=frameCounter||p.distant||p.owner!=u.owner||
            p.view.identity!=u.view.identity||p.view.generation!=u.view.generation||p.view.frame!=u.view.frame||
            p.view.viewProjection!=u.view.viewProjection||p.view.cameraPosition!=u.view.cameraPosition||
            p.view.width!=u.view.width||p.view.height!=u.view.height)continue;
        const auto h=glm::inverse(glm::make_mat4(u.view.viewProjection.data()))*glm::vec4(0,0,1,1);
        const auto f=glm::normalize(glm::vec3(h)/h.w-glm::make_vec3(u.view.cameraPosition.data()));
        forwardRange={f.x,f.y,f.z,p.range};return &v.depth;
    }
    return nullptr;
}
VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::findWorldPrecipitationTexture(const char* key){
    if(!key||std::string_view(key).find("phlosion://precipitation/")!=0)return nullptr;
    if(precipitation)for(auto& v:precipitation->entries)if(v.key==key&&v.used)return &v.scatter;
    throw std::runtime_error("Precipitation lighting texture is not resident");
}
void VulkanRenderBackendImpl::releaseWorldPrecipitation(std::uint64_t owner){
    if(!precipitation)return;auto& t=*precipitation;
    worldFog={};worldFogTextures={};
    for(auto& v:t.entries)if(owner==~std::uint64_t{0}||v.previous.owner==owner){
        for(auto* i:{&v.light[0],&v.light[1],&v.scatter,&v.depth})destroyTexture(*i);
        v.used=0;v.previous={};v.counts={};v.key.clear();v.dimensions={};
    }
    if(owner!=~std::uint64_t{0})return;
    for(auto& frame:t.dispatches)for(auto& d:frame)destroyBuffer(d.constants);
    destroyBuffer(t.readback);for(auto p:t.pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
    if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
    if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);
    if(t.linear)vkDestroySampler(device,t.linear,nullptr);if(t.nearest)vkDestroySampler(device,t.nearest,nullptr);precipitation.reset();
}
bool VulkanRenderBackendImpl::captureWorldPrecipitationLighting(){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!precipitation||!precipitation->selected||precipitation->readback.buffer)return false;
    auto& t=*precipitation;auto& v=*t.selected;const auto bytes=std::uint64_t(v.scatter.width)*v.scatter.height*8;
    t.readback=createBuffer(bytes*2,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    unsigned index=0;for(auto* image:{&v.scatter,&v.light[v.bank]}){
        volBarrier(cmd,image->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.bufferOffset=index++*bytes;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={unsigned(image->width),unsigned(image->height),1};
        vkCmdCopyImageToBuffer(cmd,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.readback.buffer,1,&copy);
        volBarrier(cmd,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
    t.diagnostic={v.dimensions[0],v.dimensions[1],v.dimensions[2],v.counts.payloadBytes,v.previous.view.identity,v.previous.view.frame,v.history};
    resumeWorldSceneColor();return true;
}
WorldPrecipitationLightingDiagnostic VulkanRenderBackendImpl::readWorldPrecipitationLighting(){
    if(frameActive||!precipitation||!precipitation->readback.buffer)return {};
    volCheck(vkDeviceWaitIdle(device),"Precipitation readback wait");auto& t=*precipitation;auto result=t.diagnostic;
    const auto count=std::size_t(result.width)*result.height*result.depth;result.scattering.resize(count);result.lighting.resize(count);
    const auto atlasWidth=result.width*8,atlasHeight=result.height*((result.depth+7)/8);const auto offset=std::size_t(atlasWidth)*atlasHeight*8;
    for(unsigned z=0;z<result.depth;++z)for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x){
        const auto address=((std::size_t(z/8)*result.height+y)*atlasWidth+(z%8*result.width+x))*8;
        const auto index=(std::size_t(z)*result.height+y)*result.width+x;
        result.scattering[index]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(t.readback.mapped)+address));
        result.lighting[index]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(t.readback.mapped)+address+offset));
    }
    destroyBuffer(t.readback);return result;
}
