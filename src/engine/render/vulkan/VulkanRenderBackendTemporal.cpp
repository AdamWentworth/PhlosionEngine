#include "engine/render/WorldDisplayPass.h"
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include "engine/render/WorldContactCompute.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/WorldTemporalScene.h"
#include <cstring>
#include "engine/render/WorldReflectionBlueNoise.h"
using namespace engine::render;
namespace {
void temporalVk(VkResult r,const char* what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": "+std::to_string(r));}
void temporalBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout before,VkImageLayout after,
                     VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=before;b.newLayout=after;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={aspect,0,1,0,1};
    b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
}
struct VulkanRenderBackendImpl::TemporalState {
    struct Image {Texture texture;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;};
    struct View {
        WorldTemporalHistory history;
        WorldDisplayResources<Image> display;
        std::array<std::vector<VkDescriptorSet>,kFramesInFlight> displaySets;
        WorldTemporalHistory contactHistory;
        WorldContactUniforms contactView{};
        std::array<std::array<float,4>,32> contactParameters{};
        std::array<Image,2> contactDepth,contactVelocity,contactCone;
        Image contactNormals,contactRaw,contactSpatial,contactFull,contactDummyDepth,contactDummyVelocity,contactDummyNormal;
        int contactSlot=0;
        std::array<Buffer,kFramesInFlight> contactConstants;
        std::array<std::array<VkDescriptorSet,5>,kFramesInFlight> contactSets{};

        bool retired=false;std::uint64_t retiredFrame=0;
        std::chrono::steady_clock::time_point renderedAt{};
        std::array<Image,2> meta,id,color;
        Image rawMask,mask,dummyId,traceDepth,reflection,confidence;
        std::array<Buffer,kFramesInFlight> reflectionConstants;
        std::array<std::array<VkDescriptorSet,2>,kFramesInFlight> reflectionSets{};
        std::array<std::vector<Image>,2> feedback;
        std::array<std::vector<VkDescriptorSet>,kFramesInFlight> sets;
        int slot=0,width=0,height=0;
    };
    std::unordered_map<std::uint64_t,View> views;
    VkDescriptorSetLayout contactSetLayout=VK_NULL_HANDLE;
    VkPipelineLayout contactLayout=VK_NULL_HANDLE;
    VkDescriptorPool contactPool=VK_NULL_HANDLE;
    VkSampler contactLinear=VK_NULL_HANDLE,contactNearest=VK_NULL_HANDLE;
    std::array<VkPipeline,7> contactPipelines{};
    WorldTemporalView pending{};
    std::chrono::steady_clock::time_point beganAt{};
    bool active=false,valid=false,prepared=false,reflected=false;
    VkDescriptorSetLayout reflectionSetLayout=VK_NULL_HANDLE;
    VkPipelineLayout reflectionLayout=VK_NULL_HANDLE;
    VkDescriptorPool reflectionPool=VK_NULL_HANDLE;
    std::array<VkPipeline,5> reflectionPipelines{};
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    std::array<VkPipeline,3> pipelines{};
    std::array<VkPipeline,10> displayPipelines{};
    bool heatApplied=false;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkSampler linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
    Buffer readback;
    std::array<VkDeviceSize,11> offsets{};
    unsigned displayBase=0;
    Buffer contactReadback;
    std::array<VkDeviceSize,8> contactOffsets{};
    WorldContactImage contactImage;
    int contactReadbackDepthWidth=0;
    WorldTemporalImage image;
};
bool VulkanRenderBackendImpl::beginWorldTemporalScene(const WorldTemporalView& view) {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!worldSceneColorPassActive||!worldSurfaceReady||worldSurfaceActive)return false;
    if(!temporal)temporal=std::make_shared<TemporalState>();
    collectWorldTemporalViews();
    auto& t=*temporal;
    if(t.active)throw std::logic_error("Nested temporal scene");
    if(t.views.size()>=WorldMotionHistory::maxViews&&!t.views.contains(view.identity))throw std::runtime_error("Temporal view budget exhausted");
    auto& v=t.views[view.identity];v.history.validate(view);
    if(view.width!=int(worldSurfaceView[8][0])||view.height!=int(worldSurfaceView[8][1]))return false;
    t.pending=view;t.beganAt=std::chrono::steady_clock::now();
    if(view.deltaSeconds<0)t.pending.deltaSeconds=std::chrono::duration<float>(t.beganAt-v.renderedAt).count();
    t.reflected=false;t.prepared=false;t.heatApplied=false;t.active=true;t.valid=v.history.accepts(view);worldTemporalOutput=VK_NULL_HANDLE;worldDisplayReady=false;worldTemporalOutputView=VK_NULL_HANDLE;return true;
}
WorldTemporalFeedback VulkanRenderBackendImpl::worldTemporalFeedback() const {
    if(!temporal||!temporal->active||!temporal->valid)return {};
    const auto& v=temporal->views.at(temporal->pending.identity);
    return {v.history.previous.identity,v.history.previous.frame,std::max(1,v.width/2),std::max(1,v.height/2),
        worldTemporalMipCount(v.width,v.height),v.history.previous.preExposure,true};
}
bool VulkanRenderBackendImpl::resolveWorldTemporalScene() {
    if(worldSurfaceTarget && worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
return processWorldTemporalScene(true);}
bool VulkanRenderBackendImpl::resolveWorldHeatHaze() {
    if(worldDisplaySettings.heatHaze.strength==0)return true;
    return processWorldTemporalScene(false,true);
}
bool VulkanRenderBackendImpl::processWorldTemporalScene(bool finish,bool distort) {
    if(!temporal||!temporal->active||!worldSceneColorPassActive||!worldSurfaceReady)return false;
    auto& t=*temporal;auto& v=t.views.at(t.pending.identity);
    if((t.prepared&&!finish&&!distort)||(distort&&t.heatApplied))return true;

    if(!t.pipelines[0]) {
        VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(physicalDevice,&features);
        if(!features.shaderStorageImageExtendedFormats)throw std::runtime_error("Compact temporal history storage unavailable");
        std::array<VkDescriptorSetLayoutBinding,12> bindings{};
        for(unsigned i=0;i<12;++i)bindings[i]={i,i<10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount=12;set.pBindings=bindings.data();temporalVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&t.setLayout),"temporal set layout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,128};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.setLayout;
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
        temporalVk(vkCreatePipelineLayout(device,&layout,nullptr,&t.layout),"temporal pipeline layout");
        constexpr const char* shaders[]{"temporal_scene.comp.spv","temporal_scene_half.comp.spv","temporal_scene_feedback.comp.spv"};
        for(unsigned i=0;i<t.pipelines.size();++i) {
            auto module=loadShaderModule(shaders[i]);
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.layout;
            info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
            auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.pipelines[i]);vkDestroyShaderModule(device,module,nullptr);
            temporalVk(result,"temporal pipeline");
        }
        constexpr unsigned sets=WorldMotionHistory::maxViews*kFramesInFlight*52;
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,sets*10},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,sets*2}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pool.maxSets=sets;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
        temporalVk(vkCreateDescriptorPool(device,&pool,nullptr,&t.pool),"temporal descriptor pool");
        for(bool linear:{false,true}) {
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            temporalVk(vkCreateSampler(device,&sampler,nullptr,linear?&t.linear:&t.nearest),"temporal sampler");
        }
    }
    auto command=frames[currentFrame].commandBuffer;
    const auto allocate=[&](TemporalState::Image& image,int w,int h,VkFormat format) {
        auto& texture=image.texture;
        if(texture.image&&texture.width==w&&texture.height==h&&texture.format==format)return;
        VkFormatProperties support{};vkGetPhysicalDeviceFormatProperties(physicalDevice,format,&support);
        if(!(support.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            throw std::runtime_error("Temporal history storage format unsupported");
        if(texture.image) {
            temporalVk(vkDeviceWaitIdle(device),"resize temporal image");
            vkFreeDescriptorSets(device,descriptorPool,1,&texture.descriptorSet);destroyTexture(texture);
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
        info.format=format;info.extent={unsigned(w),unsigned(h),1};
        info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        temporalVk(vkCreateImage(device,&info,nullptr,&texture.image),"temporal image");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,texture.image,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;
        memory.memoryTypeIndex=findMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        temporalVk(vkAllocateMemory(device,&memory,nullptr,&texture.memory),"temporal memory");
        temporalVk(vkBindImageMemory(device,texture.image,texture.memory,0),"temporal image binding");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=texture.image;view.format=info.format;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};temporalVk(vkCreateImageView(device,&view,nullptr,&texture.view),"temporal view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=format==VK_FORMAT_R32G32_UINT?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        temporalVk(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"temporal image sampler");
        texture.width=w;texture.height=h;texture.format=info.format;texture.allocationBytes=requirements.size;
        texture.descriptorSet=createPlanarDescriptor(texture);image.layout=VK_IMAGE_LAYOUT_UNDEFINED;
    };
    const int width=t.pending.width,height=t.pending.height,hw=std::max(1,width/2),hh=std::max(1,height/2);
    for(int i=0;i<2;++i) {
        // Preserve full depth precision for history rejection and exact object IDs.
        allocate(v.meta[i],hw,hh,VK_FORMAT_R32G32B32A32_SFLOAT);allocate(v.id[i],hw,hh,VK_FORMAT_R32G32_UINT);
        allocate(v.color[i],width,height,VK_FORMAT_R16G16B16A16_SFLOAT);
        int w=hw,h=hh;std::size_t level=0;
        do {
            if(v.feedback[i].size()==level)v.feedback[i].emplace_back();
            allocate(v.feedback[i][level++],w,h,VK_FORMAT_B10G11R11_UFLOAT_PACK32);if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);
        } while(true);
    }
    allocate(v.rawMask,hw,hh,VK_FORMAT_R16G16B16A16_SFLOAT);allocate(v.mask,hw,hh,VK_FORMAT_R16G16B16A16_SFLOAT);
    allocate(v.dummyId,1,1,VK_FORMAT_R32G32_UINT);
    v.width=width;v.height=height;const int previous=v.slot,current=1-previous;
    auto scene=worldSceneColorImages[currentFrame];auto sceneView=worldSceneColorViews[currentFrame];
    if(activeEditorSurface){auto& e=editorSurfaces.at(activeEditorSurface).frames[currentFrame];scene=e.linearColor;sceneView=e.linearColorView;}
    endWorldRecordingRenderPass(command);
    temporalBarrier(command,scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for(auto& plane:worldSurfaceTarget->planes)
        temporalBarrier(command,plane.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    temporalBarrier(command,worldSurfaceDepth->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT);
    const auto ready=[&](TemporalState::Image& image,VkImageLayout layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        temporalBarrier(command,image.texture.image,image.layout,layout);image.layout=layout;
    };
    for(auto& image:v.meta)ready(image);for(auto& image:v.id)ready(image);for(auto& image:v.color)ready(image);
    ready(v.rawMask);ready(v.mask);ready(v.dummyId,VK_IMAGE_LAYOUT_GENERAL);
    const auto sampled=[&](VkImageView view,bool nearest=false,VkImageLayout layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return VkDescriptorImageInfo{nearest?t.nearest:t.linear,view,layout};
    };
    std::array<VkDescriptorImageInfo,12> images{};
    images.fill(sampled(sceneView));
    images[1]=sampled(worldSurfaceTarget->planes[4].view,true);
    images[2]=sampled(worldSurfaceDepth->view,true,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    images[3]=sampled(t.valid?v.color[previous].texture.view:sceneView);
    images[5]=sampled(t.valid?v.meta[previous].texture.view:v.meta[current].texture.view,true);
    images[8]=sampled(worldSurfaceTarget->planes[5].view,true);
    images[9]=sampled(t.valid?v.id[previous].texture.view:worldSurfaceTarget->planes[5].view,true);
    auto& sets=v.sets[currentFrame];unsigned pass=t.prepared?3:0;
    const auto camera=worldTemporalCameraConstants(t.pending,v.history.previous,t.valid);
    const auto dispatch=[&](int stage,TemporalState::Image& output,int iw,int ih) {
        vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.pipelines[stage==0?0:stage<4?1:2]);
        if(pass==sets.size()) {
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool=t.pool;info.descriptorSetCount=1;info.pSetLayouts=&t.setLayout;
            VkDescriptorSet set;temporalVk(vkAllocateDescriptorSets(device,&info,&set),"temporal dispatch set");sets.push_back(set);
        }
        auto set=sets[pass++];ready(output,VK_IMAGE_LAYOUT_GENERAL);
        auto& id=stage==0?v.id[current]:v.dummyId;if(stage==0)ready(id,VK_IMAGE_LAYOUT_GENERAL);
        images[10]={VK_NULL_HANDLE,output.texture.view,VK_IMAGE_LAYOUT_GENERAL};images[11]={VK_NULL_HANDLE,id.texture.view,VK_IMAGE_LAYOUT_GENERAL};
        std::array<VkWriteDescriptorSet,12> writes{};
        for(unsigned i=0;i<12;++i){auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet=set;write.dstBinding=i;write.descriptorCount=1;write.descriptorType=i<10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo=&images[i];}
        vkUpdateDescriptorSets(device,12,writes.data(),0,nullptr);
        vkCmdBindDescriptorSets(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);
        float p[32]{float(output.texture.width),float(output.texture.height),float(iw),float(ih),float(t.valid),
            worldTemporalCurrentWeight(t.pending.deltaSeconds),t.valid?t.pending.preExposure/v.history.previous.preExposure:1.f,float(stage),
            };
        std::copy(camera.begin(),camera.end(),p+8);
        vkCmdPushConstants(worldRecordingCommand(command),t.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),p);
        vkCmdDispatch(command,(output.texture.width+7)/8,(output.texture.height+7)/8,1);
        ready(output);if(stage==0)ready(id);
    };
    if(distort) {
        const auto constants=worldHeatHazeParameters(worldDisplaySettings.heatHaze,t.pending,worldHeatHazeNoise);
        if(!t.displayPipelines[9]) {
            auto module=loadShaderModule("display_9.comp.spv");
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.layout;
            info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
            const auto result=prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.displayPipelines[9]);
            vkDestroyShaderModule(device,module,nullptr);temporalVk(result,"heat haze pipeline");
        }
        const auto& data=worldHeatHazeNoise;
        auto* noise=ensureWorldTextureRaw(data.key.c_str(),data.key.c_str(),data.rgba.data(),
            data.width,data.height*data.layers,33071,33071,false,nullptr,nullptr,0,true);
        if(!noise)throw std::runtime_error("Heat haze noise upload");
        allocate(v.display.heat,width,height,VK_FORMAT_R16G16B16A16_SFLOAT);
        auto& heat=v.display.heat;ready(heat,VK_IMAGE_LAYOUT_GENERAL);
        auto& heatSets=v.displaySets[currentFrame];
        if(heatSets.empty()){
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool=t.pool;info.descriptorSetCount=1;info.pSetLayouts=&t.setLayout;
            VkDescriptorSet set;temporalVk(vkAllocateDescriptorSets(device,&info,&set),"heat haze descriptor");heatSets.push_back(set);
        }
        std::array<VkDescriptorImageInfo,9> heatImages{};std::array<VkWriteDescriptorSet,9> writes{};
        for(unsigned i=0;i<9;++i){
            heatImages[i]=i<8?sampled(i==1?worldSurfaceDepth->view:i==2?noise->view:sceneView):
                VkDescriptorImageInfo{VK_NULL_HANDLE,heat.texture.view,VK_IMAGE_LAYOUT_GENERAL};
            auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=heatSets[0];w.dstBinding=i<8?i:10;
            w.descriptorCount=1;w.descriptorType=i<8?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w.pImageInfo=&heatImages[i];
        }
        vkUpdateDescriptorSets(device,unsigned(writes.size()),writes.data(),0,nullptr);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,t.displayPipelines[9]);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&heatSets[0],0,nullptr);
        vkCmdPushConstants(command,t.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,constants.data());
        vkCmdDispatch(command,(width+7)/8,(height+7)/8,1);
        ready(heat,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        temporalBarrier(command,scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.extent={unsigned(width),unsigned(height),1};
        vkCmdCopyImage(command,heat.texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,scene,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        temporalBarrier(command,scene,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);ready(heat);
        t.heatApplied=true;resumeWorldSceneColor();return true;
    }
    if(!t.prepared) {
    // First pass must not bind its own metadata/identity output as sampled input.
    images[5]=sampled(t.valid?v.meta[previous].texture.view:sceneView,true);
    dispatch(0,v.meta[current],width,height);
    images[4]=sampled(v.meta[current].texture.view,true);
    images[5]=sampled(t.valid?v.meta[previous].texture.view:v.meta[current].texture.view,true);
    images[8]=sampled(v.id[current].texture.view,true);images[9]=sampled(t.valid?v.id[previous].texture.view:v.id[current].texture.view,true);
    dispatch(1,v.rawMask,hw,hh);images[6]=sampled(v.rawMask.texture.view);
    dispatch(2,v.mask,hw,hh);images[6]=sampled(v.mask.texture.view);
        t.prepared=true;
    }
    if(!finish){resumeWorldSceneColor();return true;}
    images[4]=sampled(v.meta[current].texture.view,true);
    images[5]=sampled(t.valid?v.meta[previous].texture.view:v.meta[current].texture.view,true);
    images[6]=sampled(v.mask.texture.view);
    images[8]=sampled(worldSurfaceTarget->planes[5].view,true);
    dispatch(3,v.color[current],width,height);
    {
        images[0]=sampled(v.color[current].texture.view);int w=width,h=height;
        int level=0;
        for(auto& mip:v.feedback[current]) {
            dispatch((level++%4)==0?4:5,mip,w,h);images[0]=sampled(mip.texture.view);w=mip.texture.width;h=mip.texture.height;if(w==1&&h==1)break;
        }
    }

    if(worldSceneRadianceExposure>0) {
        auto& timingFrame=frames[currentFrame];
        const int displayToken=timingFrame.timestampIssued?phaseProfile.begin(timingFrame.phaseFrame,"display"):-1;
        if(displayToken>=0)vkCmdWriteTimestamp(worldRecordingCommand(command),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            timingFrame.timestampQueries,timingFrame.phaseFrame.spans[displayToken].query);
        if(!t.displayPipelines[0])for(int i=0;i<9;++i) {
            auto module=loadShaderModule(("display_"+std::to_string(i)+".comp.spv").c_str());
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.layout;
            info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
            auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.displayPipelines[i]);
            vkDestroyShaderModule(device,module,nullptr);temporalVk(result,"display pipeline");
        }
        TemporalState::Image emissionImage,depthImage;
        emissionImage.texture.view=worldSurfaceTarget->planes[3].view;
        depthImage.texture.view=worldSurfaceDepth->view;
        emissionImage.layout=depthImage.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        unsigned displayPass=t.heatApplied?1:0;auto& displaySets=v.displaySets[currentFrame];
        const auto displayAllocate=[&](auto& image,int w,int h,int format){
            allocate(image,w,h,format==0?VK_FORMAT_R16G16B16A16_SFLOAT:format==1?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        };
        const auto displayDispatch=[&](int stage,auto& output,const auto& sources,const auto& constants){
            if(displayPass==displaySets.size()){
                VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                info.descriptorPool=t.pool;info.descriptorSetCount=1;info.pSetLayouts=&t.setLayout;
                VkDescriptorSet set;temporalVk(vkAllocateDescriptorSets(device,&info,&set),"display descriptor");displaySets.push_back(set);
            }
            auto set=displaySets[displayPass++];ready(output,VK_IMAGE_LAYOUT_GENERAL);
            std::array<VkDescriptorImageInfo,9> displayImages{};
            std::array<VkWriteDescriptorSet,9> writes{};
            for(unsigned i=0;i<9;++i){
                displayImages[i]=i<8?sampled(sources[i]->texture.view):
                    VkDescriptorImageInfo{VK_NULL_HANDLE,output.texture.view,VK_IMAGE_LAYOUT_GENERAL};
                auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;
                w.dstBinding=i<8?i:10;w.descriptorCount=1;w.descriptorType=i<8?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w.pImageInfo=&displayImages[i];
            }
            vkUpdateDescriptorSets(device,unsigned(writes.size()),writes.data(),0,nullptr);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,t.displayPipelines[stage]);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&set,0,nullptr);
            vkCmdPushConstants(command,t.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,constants.data());
            vkCmdDispatch(command,stage==1?64:(output.texture.width+7)/8,stage==1?1:(output.texture.height+7)/8,1);
            ready(output);
        };
        runWorldDisplay(v.display,worldDisplaySettings,t.pending,v.history.previous,t.valid,
            worldSceneRadianceExposure,worldBloomStrength,v.color[current],emissionImage,depthImage,displayAllocate,displayDispatch);
        worldDisplayReady=true;
        if(timingFrame.timestampIssued&&phaseProfile.end(timingFrame.phaseFrame,displayToken))
            vkCmdWriteTimestamp(worldRecordingCommand(command),VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                timingFrame.timestampQueries,timingFrame.phaseFrame.spans[displayToken].query+1);
    }
    auto& presented=worldDisplayReady?v.display.output:v.color[current];
    worldTemporalOutput=presented.texture.descriptorSet;worldTemporalOutputView=presented.texture.view;
    v.slot=current;v.history.commit(t.pending);v.renderedAt=t.beganAt;t.active=false;
    resumeWorldSceneColor();return true;
}
bool VulkanRenderBackendImpl::captureWorldTemporalScene() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!temporal||!worldTemporalOutput||!worldSceneColorPassActive)return false;
    auto& t=*temporal;auto& v=t.views.at(t.pending.identity);
    std::vector<TemporalState::Image*> images{&v.color[v.slot],&v.mask,&v.feedback[v.slot][0]};
    if(t.reflected)images.insert(images.end(),{&v.reflection,&v.confidence,&v.traceDepth});
    t.displayBase=0;
    if(worldDisplayReady){
        t.displayBase=unsigned(images.size());
        images.insert(images.end(),{&v.display.output,&v.display.meter,&v.display.exposure[v.display.slot],&v.display.reduced,&v.display.lut});
    }
    const unsigned count=unsigned(images.size());
    VkDeviceSize bytes=0;
    for(unsigned i=0;i<count;++i){t.offsets[i]=bytes;bytes+=VkDeviceSize(images[i]->texture.width)*images[i]->texture.height*(t.displayBase&&i>=t.displayBase?((i-t.displayBase==2||i-t.displayBase==3)?16:8):(i<2?8:i<4?4:i==4?1:2));bytes=(bytes+3)&~VkDeviceSize(3);}
    destroyBuffer(t.readback);t.readback=createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    for(unsigned i=0;i<count;++i) {
        auto& image=*images[i];temporalBarrier(command,image.texture.image,image.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.bufferOffset=t.offsets[i];copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={unsigned(image.texture.width),unsigned(image.texture.height),1};
        vkCmdCopyImageToBuffer(command,image.texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.readback.buffer,1,&copy);
        temporalBarrier(command,image.texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image.layout);
    }
    t.image={v.width,v.height,v.mask.texture.width,v.mask.texture.height,v.feedback[v.slot][0].texture.width,v.feedback[v.slot][0].texture.height};
    const auto payload=[](const TemporalState::Image& image) {
        const auto& d=image.texture;
        if(!d.image)return VkDeviceSize(0);
        const auto bpp=d.format==VK_FORMAT_R32G32B32A32_SFLOAT?16:(d.format==VK_FORMAT_R16G16B16A16_SFLOAT||d.format==VK_FORMAT_R32G32_UINT)?8:4;
        return VkDeviceSize(d.width)*d.height*bpp;
    };
    for(auto& image:v.meta)t.image.historyTexelBytes+=payload(image);
    for(auto& image:v.id)t.image.historyTexelBytes+=payload(image);
    for(auto& image:v.color)t.image.historyTexelBytes+=payload(image);
    for(auto& levels:v.feedback)for(auto& image:levels)t.image.historyTexelBytes+=payload(image);
    t.image.historyTexelBytes+=payload(v.rawMask)+payload(v.mask)+payload(v.dummyId);
    if(t.reflected){t.image.reflectionDepthWidth=v.traceDepth.texture.width;t.image.reflectionDepthHeight=v.traceDepth.texture.height;}
    resumeWorldSceneColor();return true;
}
WorldTemporalImage VulkanRenderBackendImpl::readWorldTemporalScene() {
    if(!temporal||!temporal->readback.buffer||frameActive)return {};
    presentPendingFrame();temporalVk(vkDeviceWaitIdle(device),"temporal readback wait");auto& t=*temporal;
    void* mapped=t.readback.mapped;
    t.image.color.resize(t.image.width*t.image.height);t.image.mask.resize(t.image.maskWidth*t.image.maskHeight);
    t.image.feedback.resize(t.image.feedbackWidth*t.image.feedbackHeight);
    std::array<std::vector<std::array<float,4>>*,3> outputs{&t.image.color,&t.image.mask,&t.image.feedback};
    for(unsigned i=0;i<3;++i) {
        const auto* data=static_cast<const char*>(mapped)+t.offsets[i];
        for(std::size_t j=0;j<outputs[i]->size();++j)
            (*outputs[i])[j]=i==2?unpackWorldReflection(reinterpret_cast<const std::uint32_t*>(data)[j],255):
                unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(data)+j*4);
    }
    if(t.image.reflectionDepthWidth) {
        const auto* rgb=reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped)+t.offsets[3]);
        const auto* fade=reinterpret_cast<const unsigned char*>(static_cast<const char*>(mapped)+t.offsets[4]);
        const auto* depth=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(mapped)+t.offsets[5]);
        t.image.reflection.resize(t.image.width*t.image.height);
        for(std::size_t i=0;i<t.image.reflection.size();++i)t.image.reflection[i]=unpackWorldReflection(rgb[i],fade[i]);
        t.image.reflectionDepth.resize(t.image.reflectionDepthWidth*t.image.reflectionDepthHeight);
        for(std::size_t i=0;i<t.image.reflectionDepth.size();++i)t.image.reflectionDepth[i]=depth[i]/65535.f;
    }
    if(t.displayBase) {
        std::array<std::vector<std::array<float,4>>*,5> out{&t.image.display,&t.image.meter,&t.image.exposure,&t.image.histogram,&t.image.displayLut};
        const int widths[]{t.image.width,256,1,512,2304},heights[]{t.image.height,256,1,1,48};
        for(unsigned n=0;n<5;++n){
            out[n]->resize(std::size_t(widths[n])*heights[n]);
            for(int y=0;y<heights[n];++y)for(int x=0;x<widths[n];++x){
                const auto* row=static_cast<const char*>(mapped)+t.offsets[t.displayBase+n]+std::size_t(y)*widths[n]*((n==2||n==3)?16:8);
                if(n==2||n==3)std::memcpy((*out[n])[y*widths[n]+x].data(),row+x*16,16);
                else (*out[n])[y*widths[n]+x]=unpackWorldTemporalHalf(reinterpret_cast<const std::uint16_t*>(row)+x*4);
            }
        }
    }
    destroyBuffer(t.readback);return std::exchange(t.image,{});
}
void VulkanRenderBackendImpl::clearWorldTemporalScene() {
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });

    if(temporal)temporal->active=false;worldTemporalOutput=VK_NULL_HANDLE;worldDisplayReady=false;worldTemporalOutputView=VK_NULL_HANDLE;
}
void VulkanRenderBackendImpl::releaseWorldTemporalScene() {
    destroyWorldIndirectLighting();
    destroyWorldClusteredLighting();
    if(!temporal)return;auto& t=*temporal;
    const auto destroy=[&](TemporalState::Image& image){if(image.texture.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&image.texture.descriptorSet);destroyTexture(image.texture);};
    for(auto& [id,v]:t.views) {
        v.display.each(destroy);
        for(auto& i:v.meta)destroy(i);for(auto& i:v.id)destroy(i);for(auto& i:v.color)destroy(i);
        destroy(v.rawMask);destroy(v.mask);destroy(v.dummyId);
        destroy(v.traceDepth);destroy(v.reflection);destroy(v.confidence);
        for(auto& i:v.contactDepth)destroy(i);for(auto& i:v.contactVelocity)destroy(i);for(auto& i:v.contactCone)destroy(i);
        destroy(v.contactNormals);destroy(v.contactRaw);destroy(v.contactSpatial);destroy(v.contactFull);
        destroy(v.contactDummyDepth);destroy(v.contactDummyVelocity);destroy(v.contactDummyNormal);
        for(auto& b:v.contactConstants)destroyBuffer(b);
        for(auto& sets:v.contactSets)for(auto set:sets)if(set)vkFreeDescriptorSets(device,t.contactPool,1,&set);

        for(auto& buffer:v.reflectionConstants)destroyBuffer(buffer);
        for(auto& sets:v.reflectionSets)for(auto set:sets)if(set)vkFreeDescriptorSets(device,t.reflectionPool,1,&set);for(auto& levels:v.feedback)for(auto& i:levels)destroy(i);
    }
    destroyBuffer(t.readback);destroyBuffer(t.contactReadback);
    for(auto p:t.contactPipelines)if(p)vkDestroyPipeline(device,p,nullptr);
    if(t.contactLayout)vkDestroyPipelineLayout(device,t.contactLayout,nullptr);
    if(t.contactPool)vkDestroyDescriptorPool(device,t.contactPool,nullptr);
    if(t.contactSetLayout)vkDestroyDescriptorSetLayout(device,t.contactSetLayout,nullptr);
    if(t.contactLinear)vkDestroySampler(device,t.contactLinear,nullptr);
    if(t.contactNearest)vkDestroySampler(device,t.contactNearest,nullptr);
    for(auto pipeline:t.reflectionPipelines)if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
    if(t.reflectionLayout)vkDestroyPipelineLayout(device,t.reflectionLayout,nullptr);
    if(t.reflectionPool)vkDestroyDescriptorPool(device,t.reflectionPool,nullptr);
    if(t.reflectionSetLayout)vkDestroyDescriptorSetLayout(device,t.reflectionSetLayout,nullptr);
    for(auto pipeline:t.displayPipelines)if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
    for(auto pipeline:t.pipelines)if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
    if(t.layout)vkDestroyPipelineLayout(device,t.layout,nullptr);
    if(t.pool)vkDestroyDescriptorPool(device,t.pool,nullptr);
    if(t.setLayout)vkDestroyDescriptorSetLayout(device,t.setLayout,nullptr);
    if(t.linear)vkDestroySampler(device,t.linear,nullptr);
    if(t.nearest)vkDestroySampler(device,t.nearest,nullptr);
    temporal.reset();worldTemporalOutput=VK_NULL_HANDLE;worldDisplayReady=false;worldTemporalOutputView=VK_NULL_HANDLE;
}

void VulkanRenderBackendImpl::releaseWorldTemporalView(std::uint64_t id) {
    if(!temporal)return;
    if(auto it=temporal->views.find(id);it!=temporal->views.end()) {
        it->second.retired=true;it->second.retiredFrame=frameCounter;
        if(temporal->active&&temporal->pending.identity==id)temporal->active=false;
    }
}
void VulkanRenderBackendImpl::collectWorldTemporalViews() {
    if(!temporal)return;auto& t=*temporal;bool waited=false;
    for(auto it=t.views.begin();it!=t.views.end();) {
        auto& v=it->second;
        if(!v.retired||v.retiredFrame>=frameCounter){++it;continue;}
        if(!waited){temporalVk(vkDeviceWaitIdle(device),"retire temporal view");waited=true;}
        const auto destroy=[&](TemporalState::Image& image) {
            if(image.texture.descriptorSet)vkFreeDescriptorSets(device,descriptorPool,1,&image.texture.descriptorSet);
            destroyTexture(image.texture);
        };
        v.display.each(destroy);
        for(auto& i:v.meta)destroy(i);for(auto& i:v.id)destroy(i);for(auto& i:v.color)destroy(i);
        destroy(v.rawMask);destroy(v.mask);destroy(v.dummyId);
        destroy(v.traceDepth);destroy(v.reflection);destroy(v.confidence);
        for(auto& i:v.contactDepth)destroy(i);for(auto& i:v.contactVelocity)destroy(i);for(auto& i:v.contactCone)destroy(i);
        destroy(v.contactNormals);destroy(v.contactRaw);destroy(v.contactSpatial);destroy(v.contactFull);
        destroy(v.contactDummyDepth);destroy(v.contactDummyVelocity);destroy(v.contactDummyNormal);
        for(auto& b:v.contactConstants)destroyBuffer(b);
        for(auto& sets:v.contactSets)for(auto set:sets)if(set)vkFreeDescriptorSets(device,t.contactPool,1,&set);

        for(auto& buffer:v.reflectionConstants)destroyBuffer(buffer);
        for(auto& sets:v.reflectionSets)for(auto set:sets)if(set)vkFreeDescriptorSets(device,t.reflectionPool,1,&set);
        for(auto& mips:v.feedback)for(auto& i:mips)destroy(i);
        for(auto& sets:v.sets)if(!sets.empty())vkFreeDescriptorSets(device,t.pool,unsigned(sets.size()),sets.data());
        for(auto& sets:v.displaySets)if(!sets.empty())vkFreeDescriptorSets(device,t.pool,unsigned(sets.size()),sets.data());
        it=t.views.erase(it);
    }
}

bool VulkanRenderBackendImpl::resolveWorldTemporalReflection(const WorldTemporalReflectionView& camera) {
    if(worldSurfaceTarget && worldSurfaceTarget->probeScratch)return false;
    engine::render::WorldPreparedViewBoundary preparedViewBoundary([&] { flushWorldDrawPackets(); preparedWorldViews.invalidate(); });
    if(!temporal||!temporal->active||temporal->reflected)return false;
    if(!processWorldTemporalScene(false))return false;
    auto& t=*temporal;auto& v=t.views.at(t.pending.identity);
    const auto parameters=packWorldTemporalReflection(worldSurfaceView,camera,t.pending,worldTemporalFeedback());
    const auto budget=worldTemporalReflectionBudget(camera.quality);
    if(!t.reflectionLayout) {
        VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(physicalDevice,&features);
        if(!features.shaderStorageImageExtendedFormats)throw std::runtime_error("Compact reflection storage formats unavailable");
        std::array<VkDescriptorSetLayoutBinding,16> bindings{};
        for(unsigned i=0;i<16;++i)bindings[i]={i,i==15?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<12?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=16;set.pBindings=bindings.data();
        temporalVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&t.reflectionSetLayout),"reflection set layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.reflectionSetLayout;
        temporalVk(vkCreatePipelineLayout(device,&layout,nullptr,&t.reflectionLayout),"reflection pipeline layout");
        constexpr unsigned count=WorldMotionHistory::maxViews*kFramesInFlight*2;
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*12},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*3},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets=count;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
        temporalVk(vkCreateDescriptorPool(device,&pool,nullptr,&t.reflectionPool),"reflection descriptor pool");
    }
    const unsigned quality=unsigned(camera.quality);
    for(unsigned i:{4u,quality})if(!t.reflectionPipelines[i]) {
        auto name="temporal_reflection_"+std::to_string(i)+".comp.spv";
        auto shader=loadShaderModule(name.c_str());
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.reflectionLayout;
        info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};
        auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.reflectionPipelines[i]);
        vkDestroyShaderModule(device,shader,nullptr);temporalVk(result,"reflection compute pipeline");
    }
    const auto allocate=[&](TemporalState::Image& image,int width,int height,VkFormat format) {
        auto& texture=image.texture;if(texture.image&&texture.width==width&&texture.height==height)return;
        VkFormatProperties support{};vkGetPhysicalDeviceFormatProperties(physicalDevice,format,&support);
        if(!(support.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))throw std::runtime_error("Unsupported reflection image format");
        if(texture.image) {
            temporalVk(vkDeviceWaitIdle(device),"resize reflection image");
            vkFreeDescriptorSets(device,descriptorPool,1,&texture.descriptorSet);destroyTexture(texture);
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={unsigned(width),unsigned(height),1};
        info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        temporalVk(vkCreateImage(device,&info,nullptr,&texture.image),"reflection image");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,texture.image,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;
        memory.memoryTypeIndex=findMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        temporalVk(vkAllocateMemory(device,&memory,nullptr,&texture.memory),"reflection memory");
        temporalVk(vkBindImageMemory(device,texture.image,texture.memory,0),"reflection memory binding");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=texture.image;view.format=format;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        temporalVk(vkCreateImageView(device,&view,nullptr,&texture.view),"reflection image view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        temporalVk(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"reflection sampler");
        texture.width=width;texture.height=height;texture.format=format;texture.allocationBytes=requirements.size;
        texture.descriptorSet=createPlanarDescriptor(texture);image.layout=VK_IMAGE_LAYOUT_UNDEFINED;
    };
    const int width=t.pending.width,height=t.pending.height;
    allocate(v.traceDepth,std::max(1,width/4),std::max(1,height/4),VK_FORMAT_R16_UNORM);
    allocate(v.reflection,width,height,VK_FORMAT_B10G11R11_UFLOAT_PACK32);
    allocate(v.confidence,width,height,VK_FORMAT_R8_UNORM);
    auto& constants=v.reflectionConstants[currentFrame];
    if(!constants.buffer)constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(constants.mapped,parameters.data(),sizeof(parameters));
    auto* noiseTexture = [&] {
        engine::render::WorldResourceScopeState::SharedResources shared(worldScopes);
        static const auto noise=worldReflectionBlueNoise();
        return ensureWorldTextureRaw("__temporal_reflection_noise_u16_v2",nullptr,noise.data(),64,4096,1,1,false);
    }();
    if(!noiseTexture)throw std::runtime_error("Reflection blue noise upload failed");
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    const auto ready=[&](TemporalState::Image& image,VkImageLayout layout) {
        temporalBarrier(command,image.texture.image,image.layout,layout);image.layout=layout;
    };
    temporalBarrier(command,worldSurfaceDepth->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT);
    for(unsigned plane:{1u,2u,4u})temporalBarrier(command,worldSurfaceTarget->planes[plane].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ready(v.reflection,VK_IMAGE_LAYOUT_GENERAL);ready(v.confidence,VK_IMAGE_LAYOUT_GENERAL);ready(v.traceDepth,VK_IMAGE_LAYOUT_GENERAL);
    const auto sampled=[&](VkImageView view){return VkDescriptorImageInfo{t.linear,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};};
    std::array<VkDescriptorImageInfo,15> images{};
    images[0]=sampled(worldSurfaceDepth->view);images[1]=sampled(worldSurfaceTarget->planes[1].view);
    images[2]=sampled(worldSurfaceTarget->planes[4].view);images[3]=sampled(v.mask.texture.view);images[4]=images[0];
    for(int i=0;i<5;++i)images[5+i]=sampled(t.valid?v.feedback[v.slot][std::min(i,worldTemporalMipCount(width,height)-1)].texture.view:worldSurfaceTarget->planes[1].view);
    images[10]=sampled(noiseTexture->view);images[11]=sampled(worldSurfaceTarget->planes[2].view);
    images[12]={VK_NULL_HANDLE,v.reflection.texture.view,VK_IMAGE_LAYOUT_GENERAL};
    images[13]={VK_NULL_HANDLE,v.confidence.texture.view,VK_IMAGE_LAYOUT_GENERAL};
    images[14]={VK_NULL_HANDLE,v.traceDepth.texture.view,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo buffer{constants.buffer,0,sizeof(parameters)};
    const auto dispatch=[&](unsigned pass,unsigned pipeline,int w,int h) {
        auto& set=v.reflectionSets[currentFrame][pass];
        if(!set) {
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=t.reflectionPool;
            info.descriptorSetCount=1;info.pSetLayouts=&t.reflectionSetLayout;
            temporalVk(vkAllocateDescriptorSets(device,&info,&set),"reflection dispatch set");
        }
        std::array<VkWriteDescriptorSet,16> writes{};
        for(unsigned i=0;i<16;++i) {
            auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.dstBinding=i;write.descriptorCount=1;
            write.descriptorType=i==15?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<12?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            if(i<15)write.pImageInfo=&images[i];else write.pBufferInfo=&buffer;
        }
        vkUpdateDescriptorSets(device,16,writes.data(),0,nullptr);
        vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.reflectionPipelines[pipeline]);
        vkCmdBindDescriptorSets(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.reflectionLayout,0,1,&set,0,nullptr);
        vkCmdDispatch(command,(w+7)/8,(h+7)/8,1);
    };
    dispatch(0,4,v.traceDepth.texture.width,v.traceDepth.texture.height);
    ready(v.traceDepth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);images[4]=sampled(v.traceDepth.texture.view);
    dispatch(1,quality,width,height);
    ready(v.reflection,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);ready(v.confidence,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    worldScreenReflection={};std::copy_n(worldSurfaceView.begin(),8,worldScreenReflection.begin());
    worldScreenReflection[8]={float(width),float(height),0,4};worldScreenReflection[10]={1,1,float(budget.steps),budget.probeRoughness};
    worldScreenReflectionDescriptor=v.reflection.texture.descriptorSet;worldDepthSnapshot=worldSurfaceDepth;
    worldContactDescriptor=worldSurfaceDepth->descriptorSet;
    worldContactFilterSet=createContactFilterSet({worldContactPreviousVisibility?worldContactPreviousVisibility:worldPlanarFallback,
        v.reflection.texture.descriptorSet,v.confidence.texture.descriptorSet});
    t.reflected=true;resumeWorldSceneColor();return true;
}


bool VulkanRenderBackendImpl::processWorldContactVisibility(){
    if(!temporal||!temporal->active||worldContact[9][3]<.5f)return false;
    auto& t=*temporal;auto& v=t.views.at(t.pending.identity);
    v.contactHistory.validate(t.pending);
    const bool valid=v.contactHistory.accepts(t.pending)&&v.contactView[10][3]==worldContact[10][3]&&
        v.contactView[8][2]==worldContact[8][2]&&!engine::env::flagEnabled("PHLOSION_DISABLE_CONTACT_HISTORY");
    const unsigned quality=unsigned(worldContact[10][3]);
    if(quality>2)throw std::invalid_argument("Contact quality");
    const auto parameters=worldContactComputeParameters(worldContact,valid?v.contactView:worldContact,t.pending.frame,valid);
    v.contactParameters=parameters;
    if(!t.contactLayout) {
        VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(physicalDevice,&features);
        if(!features.shaderStorageImageExtendedFormats)throw std::runtime_error("Compact contact storage formats unavailable");
        std::array<VkDescriptorSetLayoutBinding,16> bindings{};
        for(unsigned i=0;i<16;++i)bindings[i]={i,i==15?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<11?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=16;set.pBindings=bindings.data();
        temporalVk(vkCreateDescriptorSetLayout(device,&set,nullptr,&t.contactSetLayout),"contact set layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.contactSetLayout;
        temporalVk(vkCreatePipelineLayout(device,&layout,nullptr,&t.contactLayout),"contact pipeline layout");
        constexpr unsigned count=WorldMotionHistory::maxViews*kFramesInFlight*5;
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count*11},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count*4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets=count;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
        temporalVk(vkCreateDescriptorPool(device,&pool,nullptr,&t.contactPool),"contact descriptor pool");
    }
    for(unsigned i:{0u,1u+quality,4u,5u,6u})if(!t.contactPipelines[i]){
        const int stage=i==0?0:i<=3?1:int(i)-2, q=i>=1&&i<=3?int(i)-1:2;
        auto name="contact_compute_"+std::to_string(stage)+"_"+std::to_string(q)+".comp.spv";
        auto module=loadShaderModule(name.c_str());
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=t.contactLayout;
        info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        auto result=engine::render::prepareComputePipelines(device,pipelineCache,1,&info,nullptr,&t.contactPipelines[i]);
        vkDestroyShaderModule(device,module,nullptr);temporalVk(result,"contact pipeline");
    }
    if(!t.contactLinear){
        for(bool linear:{false,true}){
            VkSamplerCreateInfo desc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};desc.minFilter=desc.magFilter=linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
            desc.addressModeU=desc.addressModeV=desc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            desc.borderColor=linear?VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
            temporalVk(vkCreateSampler(device,&desc,nullptr,linear?&t.contactLinear:&t.contactNearest),"contact sampler");
        }
    }
    const auto allocate=[&](TemporalState::Image& image,int width,int height,VkFormat format) {
        auto& texture=image.texture;if(texture.image&&texture.width==width&&texture.height==height)return;
        VkFormatProperties support{};vkGetPhysicalDeviceFormatProperties(physicalDevice,format,&support);
        if(!(support.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))throw std::runtime_error("Unsupported contact image format");
        if(texture.image) {
            temporalVk(vkDeviceWaitIdle(device),"resize contact image");
            vkFreeDescriptorSets(device,descriptorPool,1,&texture.descriptorSet);destroyTexture(texture);
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={unsigned(width),unsigned(height),1};
        info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        temporalVk(vkCreateImage(device,&info,nullptr,&texture.image),"contact image");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,texture.image,&requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;
        memory.memoryTypeIndex=findMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        temporalVk(vkAllocateMemory(device,&memory,nullptr,&texture.memory),"contact memory");
        temporalVk(vkBindImageMemory(device,texture.image,texture.memory,0),"contact memory binding");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=texture.image;view.format=format;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        temporalVk(vkCreateImageView(device,&view,nullptr,&texture.view),"contact image view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
        sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        temporalVk(vkCreateSampler(device,&sampler,nullptr,&texture.sampler),"contact sampler");
        texture.width=width;texture.height=height;texture.format=format;texture.allocationBytes=requirements.size;
        texture.descriptorSet=createPlanarDescriptor(texture);image.layout=VK_IMAGE_LAYOUT_UNDEFINED;
    };

    const int width=t.pending.width,height=t.pending.height,hw=(width+1)/2,hh=(height+1)/2;
    for(int i=0;i<2;++i){
        allocate(v.contactDepth[i],hw,hh,VK_FORMAT_R32_SFLOAT);
        allocate(v.contactVelocity[i],hw,hh,VK_FORMAT_R16G16_SFLOAT);
        allocate(v.contactCone[i],hw,hh,VK_FORMAT_R8G8B8A8_UNORM);
    }
    allocate(v.contactNormals,hw,hh,VK_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactRaw,hw,hh,VK_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactSpatial,hw,hh,VK_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactFull,width,height,VK_FORMAT_R8G8B8A8_UNORM);
    allocate(v.contactDummyDepth,1,1,VK_FORMAT_R32_SFLOAT);
    allocate(v.contactDummyVelocity,1,1,VK_FORMAT_R16G16_SFLOAT);
    allocate(v.contactDummyNormal,1,1,VK_FORMAT_R8G8B8A8_UNORM);
    auto& constants=v.contactConstants[currentFrame];
    if(!constants.buffer)constants=createBuffer(sizeof(parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(constants.mapped,parameters.data(),sizeof(parameters));
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    const auto ready=[&](TemporalState::Image& image,VkImageLayout layout){
        temporalBarrier(command,image.texture.image,image.layout,layout);image.layout=layout;};
    temporalBarrier(command,worldSurfaceDepth->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT);
    for(unsigned plane:{1u,4u,5u})temporalBarrier(command,worldSurfaceTarget->planes[plane].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const int previous=v.contactSlot,current=1-previous;
    for(auto* image:{&v.contactDepth[previous],&v.contactVelocity[previous],&v.contactCone[previous]})
        ready(*image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto sampled=[&](VkImageView view,bool integer=false){
        return VkDescriptorImageInfo{integer?t.contactNearest:t.contactLinear,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};};
    std::array<VkDescriptorImageInfo,15> images{};
    images[0]=sampled(worldSurfaceDepth->view);images[1]=sampled(worldSurfaceTarget->planes[1].view);
    images[2]=sampled(worldSurfaceTarget->planes[4].view);images[3]=sampled(worldSurfaceTarget->planes[5].view,true);
    images[4]=images[0];images[5]=images[1];images[6]=images[2];images[7]=images[1];
    images[8]=valid?sampled(v.contactDepth[previous].texture.view):images[0];
    images[9]=valid?sampled(v.contactVelocity[previous].texture.view):images[2];
    images[10]=valid?sampled(v.contactCone[previous].texture.view):images[1];
    VkDescriptorBufferInfo buffer{constants.buffer,0,sizeof(parameters)};
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
    const char* names[]{"contact-prepare","contact-horizon","contact-spatial","contact-temporal","contact-upsample"};
    const auto dispatch=[&](unsigned stage,unsigned pipeline,TemporalState::Image& cone){
        std::array<TemporalState::Image*,4> out{&cone,stage==0?&v.contactDepth[current]:&v.contactDummyDepth,
            stage==0?&v.contactVelocity[current]:&v.contactDummyVelocity,stage==0?&v.contactNormals:&v.contactDummyNormal};
        for(unsigned i=0;i<4;++i){ready(*out[i],VK_IMAGE_LAYOUT_GENERAL);images[11+i]={VK_NULL_HANDLE,out[i]->texture.view,VK_IMAGE_LAYOUT_GENERAL};}
        auto& set=v.contactSets[currentFrame][stage];
        if(!set){
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=t.contactPool;
            info.descriptorSetCount=1;info.pSetLayouts=&t.contactSetLayout;
            temporalVk(vkAllocateDescriptorSets(device,&info,&set),"contact dispatch set");
        }
        std::array<VkWriteDescriptorSet,16> writes{};
        for(unsigned i=0;i<16;++i){
            auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;
            w.descriptorType=i==15?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<11?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            if(i<15)w.pImageInfo=&images[i];else w.pBufferInfo=&buffer;
        }
        vkUpdateDescriptorSets(device,16,writes.data(),0,nullptr);
        vkCmdBindPipeline(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.contactPipelines[pipeline]);
        vkCmdBindDescriptorSets(worldRecordingCommand(command),VK_PIPELINE_BIND_POINT_COMPUTE,t.contactLayout,0,1,&set,0,nullptr);
        const int token=beginPhase(names[stage]);
        vkCmdDispatch(command,(cone.texture.width+7)/8,(cone.texture.height+7)/8,1);endPhase(token);
        for(auto* image:out)ready(*image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    dispatch(0,0,v.contactRaw);
    images[4]=sampled(v.contactDepth[current].texture.view);images[5]=sampled(v.contactNormals.texture.view);images[6]=sampled(v.contactVelocity[current].texture.view);
    dispatch(1,1+quality,v.contactRaw);images[7]=sampled(v.contactRaw.texture.view);
    dispatch(2,4,v.contactSpatial);images[7]=sampled(v.contactSpatial.texture.view);
    dispatch(3,5,v.contactCone[current]);images[7]=sampled(v.contactCone[current].texture.view);
    dispatch(4,6,v.contactFull);
    v.contactSlot=current;v.contactHistory.commit(t.pending);v.contactView=worldContact;
    worldContact[8][3]=1;worldContact[10][3]=-1;worldContact[9][3]=1;
    worldContactPreviousVisibility=v.contactFull.texture.descriptorSet;
    worldContactDescriptor=worldSurfaceDepth->descriptorSet;worldDepthSnapshot=worldSurfaceDepth;
    worldContactFilterSet=createContactFilterSet({worldContactPreviousVisibility,worldContactPreviousVisibility,worldContactPreviousVisibility});
    resumeWorldSceneColor();return true;
}

bool VulkanRenderBackendImpl::captureWorldContactVisibility(){
    if(!temporal||!worldSceneColorPassActive)return false;
    auto& t=*temporal;auto& v=t.views.at(t.pending.identity);
    if(!v.contactFull.texture.image||!worldSurfaceDepth)return false;
    std::array<TemporalState::Image*,7> images{&v.contactRaw,&v.contactSpatial,&v.contactCone[v.contactSlot],&v.contactFull,
        &v.contactDepth[v.contactSlot],&v.contactVelocity[v.contactSlot],&v.contactNormals};
    VkDeviceSize bytes=0;
    for(unsigned i=0;i<8;++i){
        t.contactOffsets[i]=bytes;
        const auto& texture=i==7?*worldSurfaceDepth:images[i]->texture;
        bytes+=VkDeviceSize(texture.width)*texture.height*4;
    }
    destroyBuffer(t.contactReadback);
    t.contactReadback=createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto command=frames[currentFrame].commandBuffer;endWorldRecordingRenderPass(command);
    for(unsigned i=0;i<8;++i){
        const auto& texture=i==7?*worldSurfaceDepth:images[i]->texture;
        VkImageAspectFlags aspect=i==7?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
        auto layout=i==7?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:images[i]->layout;
        temporalBarrier(command,texture.image,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,aspect);
        VkBufferImageCopy copy{};copy.bufferOffset=t.contactOffsets[i];copy.imageSubresource={aspect,0,0,1};
        copy.imageExtent={unsigned(texture.width),unsigned(texture.height),1};
        vkCmdCopyImageToBuffer(command,texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.contactReadback.buffer,1,&copy);
        temporalBarrier(command,texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout,aspect);
    }
    t.contactReadbackDepthWidth=int(worldSurfaceDepth->width);
    t.contactImage={int(v.contactFull.texture.width),int(v.contactFull.texture.height),
        int(v.contactRaw.texture.width),int(v.contactRaw.texture.height),v.contactParameters};
    t.contactImage.texelBytes=std::uint64_t(v.contactRaw.texture.width)*v.contactRaw.texture.height*36+
        std::uint64_t(v.contactFull.texture.width)*v.contactFull.texture.height*4+12;
    resumeWorldSceneColor();return true;
}
WorldContactImage VulkanRenderBackendImpl::readWorldContactVisibility(){
    if(!temporal||!temporal->contactReadback.buffer||frameActive)return {};
    presentPendingFrame();temporalVk(vkDeviceWaitIdle(device),"contact readback wait");auto& t=*temporal;
    for(unsigned i=0;i<8;++i){
        auto* bytes=static_cast<const unsigned char*>(t.contactReadback.mapped)+t.contactOffsets[i];
        auto& data=t.contactImage.planes[i];
        data.resize((i==3||i==7)?t.contactImage.width*t.contactImage.height:t.contactImage.halfWidth*t.contactImage.halfHeight);
        for(std::size_t p=0;p<data.size();++p){
            if(i==4)data[p][0]=reinterpret_cast<const float*>(bytes)[p];
            else if(i==7)data[p][0]=reinterpret_cast<const float*>(bytes)[(p/t.contactImage.width)*t.contactReadbackDepthWidth+p%t.contactImage.width];
            else if(i==5){auto* half=reinterpret_cast<const std::uint16_t*>(bytes)+p*2;data[p]={decodeMotionHalf(half[0]),decodeMotionHalf(half[1]),0,0};}
            else for(unsigned c=0;c<4;++c)data[p][c]=bytes[p*4+c]/255.f;
        }
    }
    destroyBuffer(t.contactReadback);return std::exchange(t.contactImage,{});
}
