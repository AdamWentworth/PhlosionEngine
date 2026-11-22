#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <algorithm>
#include "engine/render/vulkan/VulkanWorldMaterialState.h"

namespace engine::render::vulkan_backend {

// A packet borrows native resources from its owning frame/pass, but no CPU input
// pointers. The recorder has no access to uploads, descriptors or renderer policy.
struct WorldViewport {
    VkViewport viewport{};
    VkRect2D scissor{};
};
struct WorldDescriptorBindings {
    std::array<VkDescriptorSet,8> sets{};
    std::array<std::uint32_t,4> offsets{};
};
struct WorldIndexedDrawPacket {
    WorldViewport viewport{};
    WorldDescriptorBindings bindings{};
    VkBuffer vertices = VK_NULL_HANDLE, indices = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0, indexOffset = 0;
    VkPipeline surfacePipeline = VK_NULL_HANDLE, outlinePipeline = VK_NULL_HANDLE;
    WorldPushConstants surface{}, outline{};
    std::uint32_t indexCount = 0, instanceCount = 0, firstIndex = 0;
    std::int32_t baseVertex = 0;
};
struct WorldCommandState {
    VkPipeline boundGraphicsPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundTexturedDescriptorSet = VK_NULL_HANDLE;
    VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize boundVertexOffset = 0u;
    VkBuffer boundIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize boundIndexOffset = 0u;
    VkIndexType boundIndexType = VK_INDEX_TYPE_MAX_ENUM;
    std::uint32_t boundViewportWidth = 0u;
    std::uint32_t boundViewportHeight = 0u;
    bool boundViewportValid = false;
    WorldViewport viewport{};
};
struct WorldCommandCounters {
    std::uint32_t framePipelineBindCalls = 0u;
    std::uint32_t framePipelineBindSkips = 0u;
    std::uint32_t frameDescriptorBindCalls = 0u;
    std::uint32_t frameDescriptorBindSkips = 0u;
    std::uint32_t frameVertexBufferBindCalls = 0u;
    std::uint32_t frameVertexBufferBindSkips = 0u;
    std::uint32_t frameIndexBufferBindCalls = 0u;
    std::uint32_t frameIndexBufferBindSkips = 0u;
    std::uint32_t frameViewportUpdates = 0u;
    std::uint32_t frameViewportSkips = 0u;
};
struct NativeWorldCommands {
    static constexpr auto vkCmdBindPipeline = ::vkCmdBindPipeline;
    static constexpr auto vkCmdBindVertexBuffers = ::vkCmdBindVertexBuffers;
    static constexpr auto vkCmdBindIndexBuffer = ::vkCmdBindIndexBuffer;
    static constexpr auto vkCmdBindDescriptorSets = ::vkCmdBindDescriptorSets;
    static constexpr auto vkCmdSetViewport = ::vkCmdSetViewport;
    static constexpr auto vkCmdSetScissor = ::vkCmdSetScissor;
    static constexpr auto vkCmdPushConstants = ::vkCmdPushConstants;
    static constexpr auto vkCmdDrawIndexed = ::vkCmdDrawIndexed;
};
template<class Commands = NativeWorldCommands>
struct WorldCommandRecorder {
    Commands commands;
    VkCommandBuffer commandBuffer;
    VkPipelineLayout layout;
    WorldCommandState& state;
    WorldCommandCounters& counters;
    // Descriptor reuse is private to this recorder's uninterrupted range.
    // Other renderer paths can bind sets without participating in this cache.
    WorldDescriptorBindings previousBindings{};
    bool haveBindings=false;
    std::uint64_t descriptorWrites=0,descriptorReuses=0;
    void bindGraphicsPipeline(
        VkPipeline pipeline) {
        if (state.boundGraphicsPipeline == pipeline) {
            ++counters.framePipelineBindSkips;
            return;
        }
        commands.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        state.boundGraphicsPipeline = pipeline;
        ++counters.framePipelineBindCalls;
    }

    void bindVertexBuffer(
        VkBuffer buffer,
        VkDeviceSize offset) {
        if (state.boundVertexBuffer == buffer && state.boundVertexOffset == offset) {
            ++counters.frameVertexBufferBindSkips;
            return;
        }
        commands.vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &buffer, &offset);
        state.boundVertexBuffer = buffer;
        state.boundVertexOffset = offset;
        ++counters.frameVertexBufferBindCalls;
    }

    void bindIndexBuffer(
        VkBuffer buffer,
        VkDeviceSize offset,
        VkIndexType indexType) {
        if (state.boundIndexBuffer == buffer && state.boundIndexOffset == offset &&
            state.boundIndexType == indexType) {
            ++counters.frameIndexBufferBindSkips;
            return;
        }
        commands.vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
        state.boundIndexBuffer = buffer;
        state.boundIndexOffset = offset;
        state.boundIndexType = indexType;
        ++counters.frameIndexBufferBindCalls;
    }

    void bindTextureDescriptorSet(
        VkDescriptorSet descriptorSet) {
        if (state.boundTexturedDescriptorSet == descriptorSet) {
            ++counters.frameDescriptorBindSkips;
            return;
        }
        commands.vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout,
            0u,
            1u,
            &descriptorSet,
            0u,
            nullptr);
        state.boundTexturedDescriptorSet = descriptorSet;
        ++counters.frameDescriptorBindCalls;
    }


    void bindWorldDescriptors(const WorldDescriptorBindings& bindings) {
        if(haveBindings && state.boundTexturedDescriptorSet!=previousBindings.sets[0])
            haveBindings=false;
        const unsigned count=static_cast<unsigned>(bindings.sets.size());
        unsigned first=count,last=0;
        for(unsigned i=0;i<count;++i) {
            const bool same=haveBindings
                ? previousBindings.sets[i]==bindings.sets[i] &&
                    (i!=1 || previousBindings.offsets==bindings.offsets)
                : i==0 && state.boundTexturedDescriptorSet==bindings.sets[0];
            if(same)continue;
            first=std::min(first,i);last=i;
        }
        if(first<count) {
            const bool dynamic=first<=1 && last>=1;
            commands.vkCmdBindDescriptorSets(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout,first,last-first+1,bindings.sets.data()+first,
                dynamic?4u:0u,dynamic?bindings.offsets.data():nullptr);
            descriptorWrites+=last-first+1;
            ++counters.frameDescriptorBindCalls;
        }
        descriptorReuses+=first<count ? count-(last-first+1) : count;
        if(first!=0)++counters.frameDescriptorBindSkips;
        state.boundTexturedDescriptorSet=bindings.sets[0];
        previousBindings=bindings;haveBindings=true;
    }
    void setViewport(const WorldViewport& value) {
        const auto& a=state.viewport.viewport; const auto& b=value.viewport;
        const auto& x=state.viewport.scissor; const auto& y=value.scissor;
        if(state.boundViewportValid && a.x==b.x && a.y==b.y &&
            a.width==b.width && a.height==b.height && a.minDepth==b.minDepth && a.maxDepth==b.maxDepth &&
            x.offset.x==y.offset.x && x.offset.y==y.offset.y &&
            x.extent.width==y.extent.width && x.extent.height==y.extent.height) {
            ++counters.frameViewportSkips;
            return;
        }
        commands.vkCmdSetViewport(commandBuffer,0,1,&value.viewport);
        commands.vkCmdSetScissor(commandBuffer,0,1,&value.scissor);
        state.viewport=value;
        state.boundViewportWidth=static_cast<std::uint32_t>(b.width);
        state.boundViewportHeight=static_cast<std::uint32_t>(b.height);
        state.boundViewportValid=true;
        ++counters.frameViewportUpdates;
    }
    void record(const WorldIndexedDrawPacket& packet) {
        setViewport(packet.viewport);
        bindWorldDescriptors(packet.bindings);
        bindVertexBuffer(packet.vertices,packet.vertexOffset);
        bindIndexBuffer(packet.indices,packet.indexOffset,VK_INDEX_TYPE_UINT32);
        if(packet.outlinePipeline) {
            bindGraphicsPipeline(packet.outlinePipeline);
            commands.vkCmdPushConstants(commandBuffer,layout,
                VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet.outline),&packet.outline);
            commands.vkCmdDrawIndexed(commandBuffer,packet.indexCount,packet.instanceCount,packet.firstIndex,packet.baseVertex,0);
        }
        bindGraphicsPipeline(packet.surfacePipeline);
        commands.vkCmdPushConstants(commandBuffer,layout,
            VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet.surface),&packet.surface);
        commands.vkCmdDrawIndexed(commandBuffer,packet.indexCount,packet.instanceCount,packet.firstIndex,packet.baseVertex,0);
    }
};
} // namespace engine::render::vulkan_backend
