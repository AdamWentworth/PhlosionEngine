#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <stdexcept>
using namespace engine::render;
struct VulkanRenderBackendImpl::RainMapState {
    struct Entry {unsigned resolution=0;Texture pending;VkFramebuffer framebuffer=VK_NULL_HANDLE;std::string key;};
    std::unordered_map<std::uint64_t,Entry> entries;
    VkRenderPass loadPass=VK_NULL_HANDLE;
    std::uint64_t active=0,last=0;
    WorldDirectionalShadowView savedView;
    WorldLocalShadowAtlas savedLocal;
    int width=0,height=0;
    bool savedReady=false;
    Buffer readback;unsigned readResolution=0;
};
namespace {
void rainCheck(VkResult r,const char* message){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(message)+": "+std::to_string(r));}
void rainBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,
                 VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags read,VkAccessFlags write) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=from;b.newLayout=to;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    b.srcAccessMask=read;b.dstAccessMask=write;vkCmdPipelineBarrier(cmd,src,dst,0,0,nullptr,0,nullptr,1,&b);
}
constexpr VkPipelineStageFlags rainReadStages=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
}
void VulkanRenderBackendImpl::prewarmWorldRainMap(WorldResourceScopeId scope,unsigned res) {
    if(frameActive||!scope||res<128||res>2048||(res&(res-1))||!worldScopes.admitted()||worldScopes.active()!=scope)
        throw std::invalid_argument("Invalid rain-map preparation");
    if(!rainMaps)rainMaps=std::make_shared<RainMapState>();auto& state=*rainMaps;
    if(auto it=state.entries.find(scope);it!=state.entries.end()) {
        if(it->second.resolution!=res)throw std::invalid_argument("Rain-map resolution changed within its owner");
        return;
    }
    if(state.entries.size()>=4)throw std::runtime_error("Rain-map owner budget exhausted");
    const std::uint64_t bytes=std::uint64_t(res)*res*12;
    if(!worldScopes.admit(bytes))throw std::runtime_error("Rain-map scope budget exhausted");
    createWorldShadowRenderPass();
    if(!state.loadPass) {
        VkAttachmentDescription depth{};depth.format=VK_FORMAT_D32_SFLOAT;depth.samples=VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout=depth.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.pDepthStencilAttachment=&ref;
        std::array<VkSubpassDependency,2> deps{};
        deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        deps[0].dstStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;deps[0].dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=deps[0].dstStageMask;
        deps[1].dstStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;deps[1].srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=1;pass.pAttachments=&depth;
        pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=deps.data();
        rainCheck(vkCreateRenderPass(device,&pass,nullptr,&state.loadPass),"Rain-map load pass");
    }
    RainMapState::Entry entry;entry.resolution=res;entry.key="phlosion://rain-map/"+std::to_string(scope);
    Texture atlas;
    const auto create=[&](Texture& t,unsigned height,bool pending) {
        VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=VK_FORMAT_D32_SFLOAT;
        i.extent={res,height,1};i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;
        i.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(pending?VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:0);
        rainCheck(vkCreateImage(device,&i,nullptr,&t.image),"Rain-map image");
        VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,t.image,&needs);
        VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=needs.size;
        m.memoryTypeIndex=findMemoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        rainCheck(vkAllocateMemory(device,&m,nullptr,&t.memory),"Rain-map memory");
        rainCheck(vkBindImageMemory(device,t.image,t.memory,0),"Rain-map bind");
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=t.image;v.format=i.format;v.viewType=VK_IMAGE_VIEW_TYPE_2D;
        v.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        rainCheck(vkCreateImageView(device,&v,nullptr,&t.view),"Rain-map view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        rainCheck(vkCreateSampler(device,&sampler,nullptr,&t.sampler),"Rain-map sampler");
        t.width=int(res);t.height=int(height);t.scope=scope;t.format=i.format;t.allocationBytes=needs.size;
        auto cmd=beginOneTimeCommands();
        rainBarrier(cmd,t.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearDepthStencilValue clear{1,0};vkCmdClearDepthStencilImage(cmd,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&v.subresourceRange);
        rainBarrier(cmd,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,rainReadStages,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        endOneTimeCommands(cmd);
    };
    try {
        create(entry.pending,res,true);create(atlas,res*2,false);
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=state.loadPass;fb.attachmentCount=1;
        fb.pAttachments=&entry.pending.view;fb.width=fb.height=res;fb.layers=1;
        rainCheck(vkCreateFramebuffer(device,&fb,nullptr,&entry.framebuffer),"Rain-map framebuffer");
    } catch(...) {
        if(entry.framebuffer)vkDestroyFramebuffer(device,entry.framebuffer,nullptr);
        destroyTexture(entry.pending);destroyTexture(atlas);throw;
    }
    WorldResourceCounts counts;counts.textures=2;counts.textureDescriptors=1;counts.payloadBytes=bytes;
    counts.apiAllocationBytes=entry.pending.allocationBytes+atlas.allocationBytes;
    worldTextures.emplace(entry.key,atlas);state.entries.emplace(scope,std::move(entry));worldScopes.commit(counts);
}
bool VulkanRenderBackendImpl::beginWorldRainMap(WorldResourceScopeId scope,const WorldDirectionalShadowView& view,bool clear) {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!rainMaps||rainMaps->active||worldShadowActive||!frameActive||!worldSceneColorPassActive||
       !validWorldDirectionalShadowView(view))return false;
    auto it=rainMaps->entries.find(scope);if(it==rainMaps->entries.end()||it->second.resolution!=view.resolution)return false;
    WorldResourceScopeStats owner;if(!worldScopes.query(scope,owner)||owner.releasePending)return false;
    auto& s=*rainMaps;auto& entry=it->second;s.savedView=worldShadowView;s.savedLocal=worldLocalShadows_;s.savedReady=worldShadowReady;
    s.width=worldCommandState.boundViewportWidth;s.height=worldCommandState.boundViewportHeight;
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);
    // First batches discard old depth through the compatible clear pass.
    // Continuations retain pending static geometry through the load pass.
    VkClearValue clearValue{};clearValue.depthStencil={1,0};
    VkRenderPassBeginInfo p{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    p.renderPass=clear?worldShadowRenderPass:s.loadPass;p.framebuffer=entry.framebuffer;
    p.renderArea.extent={entry.resolution,entry.resolution};
    p.clearValueCount=clear?1u:0u;p.pClearValues=clear?&clearValue:nullptr;
    beginWorldRecordingRenderPass(cmd,&p,VK_SUBPASS_CONTENTS_INLINE);
    worldShadowView=view;worldLocalShadows_={};worldShadowActive=true;s.active=scope;
    worldCommandState={};setViewportAndScissor(cmd,view.resolution,view.resolution);return true;
}
bool VulkanRenderBackendImpl::endWorldRainMap() {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!rainMaps||!rainMaps->active)return false;
    auto& s=*rainMaps;endWorldRecordingRenderPass(frames[currentFrame].commandBuffer);
    worldShadowView=s.savedView;worldLocalShadows_=s.savedLocal;worldShadowReady=s.savedReady;worldShadowActive=false;
    s.last=s.active;s.active=0;resumeWorldSceneColor();worldCommandState={};
    setViewportAndScissor(frames[currentFrame].commandBuffer,s.width,s.height);return true;
}
bool VulkanRenderBackendImpl::publishWorldRainMap(WorldResourceScopeId scope,unsigned cascade) {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!rainMaps||rainMaps->active||rainMaps->last!=scope||cascade>1)return false;
    auto& e=rainMaps->entries.at(scope);auto& atlas=worldTextures.at(e.key);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);
    rainBarrier(cmd,e.pending.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rainReadStages|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    rainBarrier(cmd,atlas.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,rainReadStages,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;
    copy.dstOffset.y=int(cascade*e.resolution);copy.extent={e.resolution,e.resolution,1};
    vkCmdCopyImage(cmd,e.pending.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,atlas.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    rainBarrier(cmd,e.pending.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,rainReadStages,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    rainBarrier(cmd,atlas.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,rainReadStages,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    rainMaps->last=0;resumeWorldSceneColor();worldCommandState={};return true;
}
std::string VulkanRenderBackendImpl::worldRainMapTexture(WorldResourceScopeId scope) const {
    if(rainMaps)if(auto it=rainMaps->entries.find(scope);it!=rainMaps->entries.end())return it->second.key;
    return {};
}
VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::findWorldRainTexture(const char* key) {
    if(auto* particle=findWorldParticleLightingTexture(key))return particle;
    if(auto* volume=findWorldPrecipitationTexture(key))return volume;
    if(!key||std::string_view(key).find("phlosion://rain-map/")!=0)return nullptr;
    auto it=worldTextures.find(key);if(it==worldTextures.end())throw std::runtime_error("Rain-map texture is not resident");
    return &it->second;
}
void VulkanRenderBackendImpl::releaseWorldRainMaps(std::uint64_t owner) {
    worldWetHistory.release(owner);
    if(owner==~std::uint64_t{0}||worldWetOwner==owner){worldWet={};worldWetTextures={};worldWetOwner=0;}

    if(!rainMaps)return;
    if(rainMaps->active)throw std::runtime_error("Cannot retire an active rain-map pass");
    for(auto it=rainMaps->entries.begin();it!=rainMaps->entries.end();) {
        if(owner!=~std::uint64_t{0}&&it->first!=owner){++it;continue;}
        if(it->second.framebuffer)vkDestroyFramebuffer(device,it->second.framebuffer,nullptr);
        destroyTexture(it->second.pending);it=rainMaps->entries.erase(it);
    }
    if(owner==~std::uint64_t{0}) {
        destroyBuffer(rainMaps->readback);
        if(rainMaps->loadPass)vkDestroyRenderPass(device,rainMaps->loadPass,nullptr);rainMaps.reset();
    }
}

bool VulkanRenderBackendImpl::captureWorldRainMap(WorldResourceScopeId scope){
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!frameActive||!rainMaps||rainMaps->active||rainMaps->readback.buffer)return false;
    auto it=rainMaps->entries.find(scope);if(it==rainMaps->entries.end())return false;
    auto& s=*rainMaps;auto& image=worldTextures.at(it->second.key);s.readResolution=it->second.resolution;
    s.readback=createBuffer(std::uint64_t(s.readResolution)*s.readResolution*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto cmd=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(cmd);cmd=worldRecordingCommand(cmd);
    rainBarrier(cmd,image.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rainReadStages,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};c.imageExtent={s.readResolution,s.readResolution*2,1};
    vkCmdCopyImageToBuffer(cmd,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.readback.buffer,1,&c);
    rainBarrier(cmd,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,rainReadStages,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
    resumeWorldSceneColor();return true;
}
WorldRainMapDiagnostic VulkanRenderBackendImpl::readWorldRainMap(){
    if(frameActive||!rainMaps||!rainMaps->readback.buffer)return {};
    rainCheck(vkDeviceWaitIdle(device),"Rain-map readback wait");auto& s=*rainMaps;WorldRainMapDiagnostic r;
    r.resolution=s.readResolution;const auto* data=static_cast<const float*>(s.readback.mapped);r.depth.assign(data,data+std::size_t(r.resolution)*r.resolution*2);
    destroyBuffer(s.readback);return r;
}

void VulkanRenderBackendImpl::bindWorldWetSurfaces(const WorldWetSurfaceView* view) {
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    worldWet={};worldWetTextures={};worldWetOwner=0;
    if(view) {
        WorldResourceScopeStats owner;
        if(!frameActive||!worldSceneColorPassActive||worldShadowActive||worldScopes.active()!=view->owner||
           !worldScopes.query(view->owner,owner)||owner.releasePending||view->rainMap!=worldRainMapTexture(view->owner))
            throw std::invalid_argument("Wet-surface owner or render phase");
        const auto constants=packWorldWetSurfaces(*view);
        worldWetHistory.validate(*view,frameCounter);
        std::array<Texture*,5> textures{};
        for(unsigned i=0;i<4;++i) {
            const auto& t=view->textures[i];
            textures[i]=ensureWorldTextureRaw(t.key,t.cacheKey,t.rgba,t.width,t.height,t.wrapS,t.wrapT,false,
                nullptr,t.mipLevels,t.mipLevelCount,true);
            if(!textures[i]||textures[i]->scope!=worldScopes.assetOwner())throw std::runtime_error("Wet-surface texture admission");
        }
        textures[4]=findWorldRainTexture(view->rainMap.c_str());
        if(!textures[4]||textures[4]->scope!=view->owner)throw std::runtime_error("Wet-surface shelter admission");
        worldWet=constants;worldWetTextures=textures;worldWetOwner=view->owner;worldWetHistory.commit(*view,frameCounter);
    }
    if(frameActive)worldContactFilterSet=createContactFilterSet(worldContactFilterSources);
}
