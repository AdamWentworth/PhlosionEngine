#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <cstring>
using namespace engine::render;
namespace {void layerCheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}}
struct VulkanRenderBackendImpl::LayeredState {
    struct Entry {
        std::uint64_t owner=0;
        const WorldLayerMaterialSet* source=nullptr;
        std::string identity;
        Buffer parameters,upload;
        std::vector<Texture*> textures;
    };
    struct Binding {std::uint64_t owner=0,windOwner=0,ledgerOwner=0;VkBuffer wind=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;};
    std::array<Entry,8> entries;
    Entry* selected=nullptr;
    Buffer fallback;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    std::vector<Binding> bindings;
};
VkDescriptorSet VulkanRenderBackendImpl::worldLayeredDescriptorSet(const Buffer& wind,std::uint64_t windOwner){
    if(!layeredMaterials)layeredMaterials=std::make_shared<LayeredState>();auto& state=*layeredMaterials;
    const auto owner=state.selected?state.selected->owner:0;
    for(const auto& b:state.bindings)if(b.owner==owner&&b.windOwner==windOwner&&b.wind==wind.buffer)return b.set;
    if(state.bindings.size()>=32)throw std::runtime_error("Layered scene binding budget");
    if(!state.fallback.buffer){
        state.fallback=createBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memset(state.fallback.mapped,0,256);
    }
    if(!state.pool){
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,64},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,32*kWorldLayerTextureLimit}};
        VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};p.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        p.maxSets=32;p.poolSizeCount=2;p.pPoolSizes=sizes;layerCheck(vkCreateDescriptorPool(device,&p,nullptr,&state.pool),"Layered descriptor pool");
    }
    LayeredState::Binding binding{owner,windOwner,owner?owner:windOwner,wind.buffer};
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool=state.pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&windDrawSetLayout;
    layerCheck(vkAllocateDescriptorSets(device,&allocation,&binding.set),"Layered scene descriptor");
    const auto& data=state.selected?state.selected->parameters:state.fallback;
    VkDescriptorBufferInfo buffers[]{{wind.buffer,0,wind.size},{data.buffer,0,data.size}};
    std::array<VkDescriptorImageInfo,kWorldLayerTextureLimit> images;
    for(unsigned i=0;i<images.size();++i){
        auto* t=state.selected?state.selected->textures[i<state.selected->textures.size()?i:0]:&fallbackWorldLinearTexture;
        images[i]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    std::array<VkWriteDescriptorSet,3> writes{};
    for(unsigned i=0;i<3;++i){
        auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=binding.set;w.dstBinding=i;
        w.descriptorType=i<2?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount=i<2?1u:kWorldLayerTextureLimit;if(i<2)w.pBufferInfo=&buffers[i];else w.pImageInfo=images.data();
    }
    vkUpdateDescriptorSets(device,unsigned(writes.size()),writes.data(),0,nullptr);
    WorldResourceCounts counts;counts.materialDescriptors=2;counts.textureDescriptors=kWorldLayerTextureLimit;
    worldScopes.commit(binding.ledgerOwner,counts);
    state.bindings.push_back(binding);return binding.set;
}
void VulkanRenderBackendImpl::bindWorldLayeredMaterials(std::uint64_t owner,std::shared_ptr<const WorldLayerMaterialSet> set){
    if(!layeredMaterials)layeredMaterials=std::make_shared<LayeredState>();auto& state=*layeredMaterials;
    if(set && (!owner || worldScopes.active()!=owner || !worldScopes.admitted()))
        throw std::invalid_argument("Layered material owner or phase");
    engine::render::WorldResourceScopeState::AssetResources assets(worldScopes);
    if(set)owner=worldScopes.active();
    if(!set&&!state.selected)return;
    if(set&&state.selected&&state.selected->owner==owner&&state.selected->source==set.get()&&state.selected->identity==set->identity)return;
    WorldPreparedViewBoundary boundary([&]{flushWorldDrawPackets();preparedWorldViews.invalidate();});
    if(!set){state.selected=nullptr;return;}
    WorldResourceScopeStats scope;
    if(!owner||!worldScopes.query(owner,scope)||scope.releasePending||worldScopes.active()!=owner)
        throw std::invalid_argument("Layered material owner or phase");
    for(auto& entry:state.entries)if(entry.owner==owner){
        if(entry.source!=set.get()||entry.identity!=set->identity)throw std::invalid_argument("Immutable layered material scope changed");
        state.selected=&entry;return;
    }
    auto it=std::find_if(state.entries.begin(),state.entries.end(),[](const auto& v){return !v.owner;});
    if(it==state.entries.end())throw std::runtime_error("Layered material scope budget");
    const auto packed=packWorldLayerMaterialSet(*set);const VkDeviceSize bytes=packed.words.size()*4;
    if(!worldScopes.admit(bytes*2+packed.textureBytes))throw std::runtime_error("Layered material memory budget");
    LayeredState::Entry entry;entry.owner=owner;entry.source=set.get();entry.identity=set->identity;
    for(const auto& t:set->textures){
        const auto index=std::uint32_t(&t-set->textures.data());
        const bool materialImage=std::none_of(set->materials.begin(),set->materials.end(),[&](const auto& m){
            return m.maskTexture==index||m.tilingMap==index;
        });
        std::vector<IRenderBackend::WorldTextureMipLevel> levels;
        for(const auto& mip:t.mips)levels.push_back({mip.rgba.data(),int(mip.width),int(mip.height)});
        const auto& mip=t.mips.front();
        auto* texture=ensureWorldTextureRaw(t.identity.c_str(),nullptr,mip.rgba.data(),int(mip.width),int(mip.height),
            10497,10497,t.srgb,nullptr,levels.data(),static_cast<std::uint32_t>(levels.size()),false, materialImage);
        if(!texture||texture->scope!=owner)throw std::runtime_error("Layered texture admission");
        entry.textures.push_back(texture);
    }
    try{
        entry.parameters=createBuffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        entry.upload=createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(entry.upload.mapped,packed.words.data(),bytes);
    }catch(...){destroyBuffer(entry.parameters);destroyBuffer(entry.upload);throw;}
    // Immutable first-use upload follows texture preparation: a separate
    // completed command leaves any active surface/rain-map/shadow pass intact.
    // Resuming the ordinary color pass here would invalidate its depth draws.
    const auto cmd=beginOneTimeCommands();
    VkBufferCopy copy{0,0,bytes};vkCmdCopyBuffer(cmd,entry.upload.buffer,entry.parameters.buffer,1,&copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=entry.parameters.buffer;barrier.size=bytes;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    endOneTimeCommands(cmd);
    WorldResourceCounts counts;counts.geometryBuffers=2;counts.payloadBytes=bytes*2;
    counts.apiAllocationBytes=entry.parameters.allocationBytes+entry.upload.allocationBytes;
    worldScopes.commit(counts);*it=std::move(entry);state.selected=&*it;
}
void VulkanRenderBackendImpl::releaseWorldLayeredMaterials(std::uint64_t owner){
    if(!layeredMaterials)return;auto& state=*layeredMaterials;
    for(auto it=state.bindings.begin();it!=state.bindings.end();){
        if(owner!=~0ull&&it->owner!=owner&&it->windOwner!=owner){++it;continue;}
        layerCheck(vkFreeDescriptorSets(device,state.pool,1,&it->set),"Layered descriptor retirement");
        WorldResourceCounts counts;counts.materialDescriptors=2;counts.textureDescriptors=kWorldLayerTextureLimit;
        worldScopes.reclaim(it->ledgerOwner,counts);
        it=state.bindings.erase(it);
    }
    for(auto& entry:state.entries)if(owner==~0ull||entry.owner==owner){
        if(state.selected==&entry)state.selected=nullptr;
        destroyBuffer(entry.parameters);destroyBuffer(entry.upload);entry={};
    }
    if(owner==~0ull){destroyBuffer(state.fallback);if(state.pool)vkDestroyDescriptorPool(device,state.pool,nullptr);layeredMaterials.reset();}
}
