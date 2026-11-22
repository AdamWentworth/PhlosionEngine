#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/WorldIndirectGpu.h"
#include <cstring>
using namespace engine::render;
namespace {
void giVk(VkResult result,const char* message){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(message)+": "+std::to_string(result));}
void giMemory(VkCommandBuffer cmd,VkAccessFlags source,VkAccessFlags destination,VkPipelineStageFlags stages) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=source;b.dstAccessMask=destination;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,stages,0,1,&b,0,nullptr,0,nullptr);
}
}
struct VulkanRenderBackendImpl::IndirectState {
    struct Entry {
        std::uint64_t id=0,revision=0,generation=0,used=0,lastRelight=0,frame=~std::uint64_t(0);
        std::shared_ptr<const IndirectTransport> source;
        std::uint64_t owner=0;
        std::array<unsigned,4> counts{};
        IndirectGpuData data;
        Buffer transport,upload,output[2];
        unsigned bank=0;
        std::array<IndirectRow,69> ambientKey{};
    };
    struct Dispatch {Buffer constants;VkDescriptorSet set=VK_NULL_HANDLE;};
    std::array<std::array<Dispatch,64>,kFramesInFlight> dispatches{};
    std::uint64_t frame=~std::uint64_t(0),scheduleFrame=~std::uint64_t(0);unsigned dispatchCount=0,scheduledCount=0;
    std::vector<std::uint64_t> scheduled;
    std::array<Entry,8> entries{};
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    std::array<VkPipeline,4> pipelines{};
    VkSampler linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
    Buffer readback;
    WorldIndirectLightingDiagnostic diagnostic;
    Entry* selected=nullptr;
    std::uint64_t clock=0;
};
void VulkanRenderBackendImpl::clearWorldIndirectLighting() {
    flushWorldDrawPackets();preparedWorldViews.invalidate();
    worldIndirectReceiver=VK_NULL_HANDLE;worldIndirectBuffers={};if(indirect)indirect->selected=nullptr;
    if(worldDiffuseProbes[0][3]>1.5f)worldDiffuseProbes={};
}
void VulkanRenderBackendImpl::destroyWorldIndirectLighting() {
    if(indirect) {
        auto& t=*indirect;
        for(auto& v:t.entries){destroyBuffer(v.transport);destroyBuffer(v.upload);for(auto& b:v.output)destroyBuffer(b);}
        for(auto& frame:t.dispatches)for(auto& d:frame)destroyBuffer(d.constants);
        destroyBuffer(t.readback);
        for(auto p:t.pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
        if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);
        if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
        if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);
        if(t.linear)vkDestroySampler(device,t.linear,nullptr);
        if(t.nearest)vkDestroySampler(device,t.nearest,nullptr);
    }
    indirect.reset();worldIndirectReceiver=VK_NULL_HANDLE;worldIndirectBuffers={};
}
bool VulkanRenderBackendImpl::updateWorldIndirectLighting(const WorldIndirectLightingUpdate& update) {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive||placedProbes.active())return false;
    if(!update.transport||!update.environment.enabled){clearWorldIndirectLighting();return true;}
    if(update.resourceScope) {
        WorldResourceScopeStats owner;
        if(!worldScopes.query(update.resourceScope,owner)||owner.releasePending||owner.status!=WorldResourceScopeStatus::Ready)
            throw std::invalid_argument("Indirect owner is not live");
    }
    if(!indirect)indirect=std::make_shared<IndirectState>();auto& t=*indirect;
    if(t.frame!=frameCounter){t.frame=frameCounter;t.dispatchCount=0;}
    if(t.dispatchCount>=64)throw std::runtime_error("Indirect frame dispatch budget");
    std::vector<std::uint64_t> ids;for(auto id:update.environmentProbes)if(id)ids.push_back(id);
    const auto selection=placedProbes.select(ids);
    auto parameters=packIndirectUpdate(update,selection,worldShadowReady?worldShadowView:WorldDirectionalShadowView{},worldLocalShadows_.directionalUv(),false);
    std::copy(worldSunPlan.uniforms.begin(),worldSunPlan.uniforms.end(),parameters.begin()+162);
    if(worldCloudOwner==update.resourceScope){parameters[160]=worldClouds[0];parameters[161]=worldClouds[1];}
    if(!t.layout) {
        std::array<VkDescriptorSetLayoutBinding,8> bindings{};
        for(unsigned i=0;i<6;++i)bindings[i]={i,i==5?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:(i==2||i==3)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[6]={6,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        bindings[7]={7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=8;set.pBindings=bindings.data();
        giVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&t.setLayout),"Indirect descriptor layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.setLayout;
        giVk(vkCreatePipelineLayout(device,&layout,nullptr,&t.layout),"Indirect layout");
        constexpr unsigned count=64*kFramesInFlight;
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*3},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
        giVk(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"Indirect pool");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        giVk(vkCreateSampler(device,&sampler,nullptr,&t.linear),"Indirect ambient sampler");
        sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;giVk(vkCreateSampler(device,&sampler,nullptr,&t.nearest),"Indirect shadow sampler");
    }
    for(unsigned pass=0;pass<4;++pass)if(!t.pipelines[pass]) {
        auto module=loadShaderModule(("indirect_"+std::to_string(pass)+".comp.spv").c_str());
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.layout;
        info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.pipelines[pass]);
        vkDestroyShaderModule(device,module,nullptr);giVk(result,"Indirect pipeline");
    }
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& e){
        return e.id==update.identity&&e.revision==update.revision&&e.owner==update.resourceScope&&e.source==update.transport;});
    if(found==t.entries.end()) {
        for(auto it=t.entries.begin();it!=t.entries.end();++it)
            if(it->frame!=frameCounter&&(found==t.entries.end()||it->used<found->used))found=it;
    }
    if(found==t.entries.end())throw std::runtime_error("Indirect resident entries pinned by this frame");
    auto& v=*found;v.used=++t.clock;v.frame=frameCounter;
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    if(v.id!=update.identity||v.revision!=update.revision||v.owner!=update.resourceScope||v.source!=update.transport) {
        if(v.transport.buffer)giVk(vkDeviceWaitIdle(device),"Indirect replacement wait");
        auto prepared=update.preparedTransport ? update.preparedTransport->dataFor(update.transport)
            : packIndirectTransport(*update.transport);
        if(prepared.rows.size()*16ull>64u*1024u*1024u||prepared.outputRows*32ull>64u*1024u*1024u)throw std::runtime_error("Indirect resident budget");
        v.data=std::move(prepared);
        destroyBuffer(v.transport);destroyBuffer(v.upload);for(auto& b:v.output)destroyBuffer(b);
        v.transport=createBuffer(v.data.rows.size()*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        v.upload=createBuffer(v.data.rows.size()*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(v.upload.mapped,v.data.rows.data(),v.data.rows.size()*16);
        VkBufferCopy copy{0,0,v.data.rows.size()*16};vkCmdCopyBuffer(cmd,v.upload.buffer,v.transport.buffer,1,&copy);
        for(auto& b:v.output){b=createBuffer(v.data.outputRows*16ull,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);vkCmdFillBuffer(cmd,b.buffer,0,VK_WHOLE_SIZE,0);}
        giMemory(cmd,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        v.id=update.identity;v.revision=update.revision;v.source=update.transport;v.owner=update.resourceScope;v.counts={unsigned(update.transport->bricks.size()),unsigned(update.transport->probes.size()),unsigned(update.transport->surfels.size()),update.transport->terrain.empty()?3u:4u};v.generation=0;v.bank=0;
    }
    if(update.reset)v.generation=0;
    parameters[0][0]=v.generation?1.f:0.f;
    auto& dispatch=t.dispatches[currentFrame][t.dispatchCount++];
    std::array<IndirectRow,69> ambientKey{};ambientKey[0]=parameters[3];
    std::copy_n(parameters.begin()+84,68,ambientKey.begin()+1);
    parameters[5][3]=v.generation&&ambientKey==v.ambientKey?0.f:1.f;
    auto& constants=dispatch.constants;
    if(!constants.buffer)constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(constants.mapped,parameters.data(),sizeof(parameters));
    auto& set=dispatch.set;
    if(!set){VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=t.pool;info.descriptorSetCount=1;info.pSetLayouts=&t.setLayout;giVk(vkAllocateDescriptorSets(device,&info,&set),"Indirect dispatch descriptor");}
    Texture* ambient=&fallbackWorldLinearTexture;
    for(auto& item:worldTextures)if(item.second.descriptorSet==placedProbeDescriptor)ambient=&item.second;
    const unsigned next=1-v.bank;
    VkDescriptorBufferInfo buffers[]{{v.transport.buffer,0,VK_WHOLE_SIZE},{v.output[v.bank].buffer,0,VK_WHOLE_SIZE},{v.output[next].buffer,0,VK_WHOLE_SIZE},{constants.buffer,0,sizeof(parameters)}};
    VkDescriptorImageInfo images[]{{t.linear,ambient->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {t.nearest,worldShadowViews[currentFrame]?worldShadowViews[currentFrame]:fallbackWorldLinearTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    std::array<VkWriteDescriptorSet,6> writes{};
    for(unsigned i=0;i<6;++i) {
        auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;
        if(i==2||i==3){w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[i-2];}
        else {w.descriptorType=i==5?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i<2?i:i-2];}
    }
    vkUpdateDescriptorSets(device,6,writes.data(),0,nullptr);
    const auto* cloud=worldCloudOwner==update.resourceScope&&worldCloudTextures[0]?worldCloudTextures[0]:&fallbackWorldLinearTexture;
    VkDescriptorImageInfo cloudInfo{t.linear,cloud->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet cloudWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};cloudWrite.dstSet=set;cloudWrite.dstBinding=6;
    cloudWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;cloudWrite.descriptorCount=1;cloudWrite.pImageInfo=&cloudInfo;
    vkUpdateDescriptorSets(device,1,&cloudWrite,0,nullptr);
    VkDescriptorImageInfo sunInfo{worldSunSampler,worldSunView?worldSunView:worldSunFallbackView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet sunWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};sunWrite.dstSet=set;sunWrite.dstBinding=7;
    sunWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;sunWrite.descriptorCount=1;sunWrite.pImageInfo=&sunInfo;
    vkUpdateDescriptorSets(device,1,&sunWrite,0,nullptr);

    giMemory(cmd,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);
    const unsigned groups[]{30,unsigned(update.transport->bricks.size()),unsigned(update.transport->probes.size()*6),unsigned((update.transport->terrain.size()+63)/64)};
    const char* names[]{"gi-environment-ambient","gi-brick-relight","gi-probe-gather","gi-distant-relight"};
    for(unsigned pass=0;pass<4;++pass)if(groups[pass]) {
        auto& frame=frames[currentFrame];const int token=frame.timestampIssued?phaseProfile.begin(frame.phaseFrame,names[pass]):-1;
        if(token>=0)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[pass]);vkCmdDispatch(cmd,groups[pass],1,1);
        giMemory(cmd,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if(token>=0&&phaseProfile.end(frame.phaseFrame,token))vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestampQueries,frame.phaseFrame.spans[token].query+1);
    }
    giMemory(cmd,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    v.bank=next;++v.generation;v.lastRelight=++t.clock;v.ambientKey=ambientKey;t.selected=&v;worldIndirectReceiver=VK_NULL_HANDLE;worldIndirectBuffers={v.transport.buffer,v.output[v.bank].buffer};
    worldDiffuseProbes={};worldDiffuseProbes[0]={update.camera[0],update.camera[1],update.camera[2],2};
    worldDiffuseProbes[1]=parameters[6];worldDiffuseProbes[2]={update.controls.threshold,float(update.volume),1,update.controls.reflectionDimming};
    worldDiffuseProbes[3]=update.controls.distantRange;worldDiffuseProbes[4]=update.controls.distantControls;
    worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
    resumeWorldSceneColor();return true;
}
bool VulkanRenderBackendImpl::captureWorldIndirectLighting() {
    if(!frameActive||!indirect||!indirect->selected||indirect->readback.buffer)return false;
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    auto& t=*indirect;auto& v=*t.selected;
    t.readback=createBuffer(v.data.outputRows*16ull,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    giMemory(cmd,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferCopy copy{0,0,v.data.outputRows*16ull};vkCmdCopyBuffer(cmd,v.output[v.bank].buffer,t.readback.buffer,1,&copy);
    giMemory(cmd,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT);
    t.diagnostic={v.id,v.revision,v.generation,v.data.rows.size()*16,v.data.outputRows*32,
        v.counts[0],v.counts[1],v.counts[2],v.counts[3],{}};
    t.diagnostic.output.resize(v.data.outputRows);resumeWorldSceneColor();return true;
}
WorldIndirectLightingDiagnostic VulkanRenderBackendImpl::readWorldIndirectLighting() {
    if(frameActive||!indirect||!indirect->readback.buffer)return {};
    giVk(vkDeviceWaitIdle(device),"Indirect readback wait");auto& t=*indirect;auto result=t.diagnostic;
    std::memcpy(result.output.data(),t.readback.mapped,result.output.size()*16);destroyBuffer(t.readback);return result;
}

bool VulkanRenderBackendImpl::bindWorldIndirectLighting(const engine::render::WorldIndirectLightingUpdate& update) {
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive||placedProbes.active())return false;
    if(!indirect)return false;
    validateIndirectControls(update.controls);
    auto& t=*indirect;
    auto found=std::find_if(t.entries.begin(),t.entries.end(),[&](const auto& e){return e.id==update.identity&&e.revision==update.revision&&e.owner==update.resourceScope&&e.source==update.transport&&e.generation;});
    if(found==t.entries.end())return false;
    flushWorldDrawPackets();preparedWorldViews.invalidate();
    auto& v=*found;v.frame=frameCounter;v.used=++t.clock;t.selected=&v;worldIndirectReceiver=VK_NULL_HANDLE;worldIndirectBuffers={v.transport.buffer,v.output[v.bank].buffer};
    worldDiffuseProbes={};worldDiffuseProbes[0]={update.camera[0],update.camera[1],update.camera[2],2};
    worldDiffuseProbes[1]=IndirectRow{update.controls.shadowDistanceWeight,update.controls.normalWeight,update.controls.probeDistanceWeight,update.controls.visibilityBias};worldDiffuseProbes[2]={update.controls.threshold,float(update.volume),1,update.controls.reflectionDimming};
    worldDiffuseProbes[3]=update.controls.distantRange;worldDiffuseProbes[4]=update.controls.distantControls;
    worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
    return true;
}
bool VulkanRenderBackendImpl::prepareWorldIndirectLighting(std::span<const engine::render::WorldIndirectLightingUpdate> updates,std::uint64_t consumer) {
    if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldSurfaceActive||placedProbes.active())return false;
    auto selected=std::find_if(updates.begin(),updates.end(),[&](const auto& u){return u.identity==consumer;});
    if(selected==updates.end()||selected->quality>3){clearWorldIndirectLighting();return false;}
    if(!selected->environment.enabled){clearWorldIndirectLighting();return true;}
    try {
        if(indirect&&indirect->scheduleFrame!=frameCounter) {
            indirect->scheduleFrame=frameCounter;indirect->scheduledCount=0;indirect->scheduled.clear();
        }
        std::vector<IndirectScheduleCandidate> candidates;
        for(const auto& u:updates) {
            validateIndirectControls(u.controls);
            if(!u.environment.enabled)continue;
            IndirectScheduleCandidate c{&u,0,0};
            if(indirect)for(const auto& e:indirect->entries)
                if(e.id==u.identity&&e.revision==u.revision&&e.owner==u.resourceScope&&e.source==u.transport){c.generation=e.generation;c.lastRelight=e.lastRelight;break;}
            candidates.push_back(c);
        }
        // Validate all requests before recording; an invalid later sector must
        // not publish a different sector as the requested consumer.
        scheduleIndirectLighting(candidates,8);
        const bool coldConsumer=std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){
            return c.update->identity==consumer&&c.generation==0;
        });
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& c){
            return indirect&&std::find(indirect->scheduled.begin(),indirect->scheduled.end(),c.update->identity)!=indirect->scheduled.end();
        }),candidates.end());
        constexpr unsigned budgets[]{1,2,4,8};
        const unsigned spent=indirect?indirect->scheduledCount:0;
        if(spent<budgets[selected->quality])
            for(unsigned i:scheduleIndirectLighting(candidates,budgets[selected->quality]-spent)) {
                if(!updateWorldIndirectLighting(*candidates[i].update)){clearWorldIndirectLighting();return false;}
                indirect->scheduleFrame=frameCounter;++indirect->scheduledCount;indirect->scheduled.push_back(candidates[i].update->identity);
            }
        if(bindWorldIndirectLighting(*selected))return true;
        // Refresh budgets may be spent by the outgoing scene. A new consumer
        // still needs its first publication before it can render.
        if(coldConsumer&&(!indirect||std::find(indirect->scheduled.begin(),indirect->scheduled.end(),consumer)==indirect->scheduled.end())) {
            if(!updateWorldIndirectLighting(*selected)){clearWorldIndirectLighting();return false;}
            indirect->scheduleFrame=frameCounter;++indirect->scheduledCount;indirect->scheduled.push_back(consumer);
            if(bindWorldIndirectLighting(*selected))return true;
        }
        clearWorldIndirectLighting();return false;
    } catch(...) {clearWorldIndirectLighting();throw;}
}

void VulkanRenderBackendImpl::releaseWorldIndirectScope(std::uint64_t owner) {
    if(!indirect||!owner)return;
    for(auto& entry:indirect->entries)if(entry.owner==owner) {
        if(indirect->selected==&entry)clearWorldIndirectLighting();
        // CPU control blocks may execute plugin code: release them before unload.
        // GPU buffers remain fenced by the ordinary cache replacement path.
        entry.source.reset();entry.id=entry.revision=entry.generation=entry.owner=0;
    }
}
