#pragma once
#include "engine/render/vulkan/VulkanPipelinePreparation.h"
#include <array>
#include <bit>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>

namespace engine::render {
// Owned by one backend; preparation joins before clear/device teardown.
class VulkanGraphicsPipelineLibrary {
public:
    bool enabled = false;
    bool feedbackEnabled = false;
    bool linkTimeOptimization = false;

    void registerShader(VkShaderModule shader, const char* name) {
        std::lock_guard lock(mutex_);
        shaderIds_[shader] = {++nextShaderId_, name};
    }
    void clear(VkDevice device) {
        std::lock_guard lock(mutex_);
        for (const auto& [_, entry] : entries_)
            if (entry->pipeline) vkDestroyPipeline(device, entry->pipeline, nullptr);
        entries_.clear();
        shaderIds_.clear();
    }
    VkResult prepare(VkDevice device, VkPipelineCache cache,
                     const VkGraphicsPipelineCreateInfo& info, std::string_view name,
                     VkPipeline* out) {
        if (engine::env::flagEnabled("PHLOSION_SHADER_PREPARATION_TRACE")) {
            std::lock_guard lock(mutex_);
            std::osyncstream log(std::clog);
            log << "[Vulkan][PipelineProgram] name=" << name;
            for(std::uint32_t i=0;i<info.stageCount;++i) {
                const auto found=shaderIds_.find(info.pStages[i].module);
                if(found!=shaderIds_.end())log << " stage" << info.pStages[i].stage << '=' << found->second.name;
            }
            log << '\n';
        }
        if (!enabled || !supportedState(info))
            return prepareGraphicsPipelineWithFeedback(device, cache, info, feedbackEnabled, name, out);
        constexpr std::array<VkGraphicsPipelineLibraryFlagBitsEXT,4> parts{
            VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT};
        std::array<VkPipeline,4> libraries{};
        for (std::size_t i=0; i<parts.size(); ++i) {
            const auto result = preparePart(device, cache, info, parts[i], name, &libraries[i]);
            if (result != VK_SUCCESS) return result;
        }
        VkPipelineLibraryCreateInfoKHR links{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
        links.libraryCount = static_cast<std::uint32_t>(libraries.size());
        links.pLibraries = libraries.data();
        VkGraphicsPipelineCreateInfo executable{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        executable.pNext = &links;
        executable.layout = info.layout;
        executable.renderPass = info.renderPass;
        executable.subpass = info.subpass;
        executable.flags = linkTimeOptimization ? VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT : 0;
        return prepareGraphicsPipelineWithFeedback(device, cache, executable, feedbackEnabled,
            std::string(name) + "-link", out);
    }
private:
    struct Entry { std::mutex mutex; VkPipeline pipeline{}; VkResult result=VK_NOT_READY; };
    std::mutex mutex_;
    std::map<std::string,std::shared_ptr<Entry>> entries_;
    struct ShaderIdentity { std::uint64_t id; std::string name; };
    std::map<VkShaderModule,ShaderIdentity> shaderIds_;
    std::uint64_t nextShaderId_{};

    // The shared key describes only this backend's fixed triangle-list state.
    // New state/extensions must be represented before they can share libraries.
    static bool supportedState(const VkGraphicsPipelineCreateInfo& c) {
        if(c.pNext || c.pTessellationState || c.stageCount!=2 || !c.pStages ||
           c.pStages[0].stage!=VK_SHADER_STAGE_VERTEX_BIT || c.pStages[1].stage!=VK_SHADER_STAGE_FRAGMENT_BIT ||
           !c.pVertexInputState || !c.pInputAssemblyState || !c.pViewportState ||
           !c.pRasterizationState || !c.pMultisampleState || !c.pDepthStencilState ||
           !c.pColorBlendState || !c.pDynamicState || c.basePipelineHandle ||
           (c.flags & (VK_PIPELINE_CREATE_DERIVATIVE_BIT | VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)))return false;
        if(c.pVertexInputState->pNext || c.pInputAssemblyState->pNext || c.pViewportState->pNext ||
           c.pRasterizationState->pNext || c.pMultisampleState->pNext || c.pDepthStencilState->pNext ||
           c.pColorBlendState->pNext || c.pDynamicState->pNext ||
           c.pStages[0].pNext || c.pStages[1].pNext)return false;
        return c.pDynamicState->dynamicStateCount==2 &&
            c.pDynamicState->pDynamicStates[0]==VK_DYNAMIC_STATE_VIEWPORT &&
            c.pDynamicState->pDynamicStates[1]==VK_DYNAMIC_STATE_SCISSOR;
    }
    struct Key {
        std::string bytes;
        template<class T> void add(T value) {
            std::uint64_t word{};
            if constexpr (std::is_pointer_v<T>) word=reinterpret_cast<std::uintptr_t>(value);
            else if constexpr (std::is_floating_point_v<T>) word=std::bit_cast<std::uint32_t>(value);
            else word=static_cast<std::uint64_t>(value);
            bytes.append(reinterpret_cast<const char*>(&word),sizeof(word));
        }
        void text(const char* value) { auto n=std::char_traits<char>::length(value);add(n);bytes.append(value,n); }
    };
    std::string key(const VkGraphicsPipelineCreateInfo& c, VkGraphicsPipelineLibraryFlagBitsEXT part) {
        Key k;k.add(part);k.add(c.flags);
        if(part != VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
            k.add(c.renderPass);k.add(c.subpass);
        }
        if(c.pDynamicState) {
            k.add(c.pDynamicState->flags);k.add(c.pDynamicState->dynamicStateCount);
            for(std::uint32_t i=0;i<c.pDynamicState->dynamicStateCount;++i)k.add(c.pDynamicState->pDynamicStates[i]);
        } else k.add(0);
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
            const auto& v=*c.pVertexInputState;k.add(v.flags);k.add(v.vertexBindingDescriptionCount);
            for(std::uint32_t i=0;i<v.vertexBindingDescriptionCount;++i) {
                const auto& b=v.pVertexBindingDescriptions[i];k.add(b.binding);k.add(b.stride);k.add(b.inputRate);
            }
            k.add(v.vertexAttributeDescriptionCount);
            for(std::uint32_t i=0;i<v.vertexAttributeDescriptionCount;++i) {
                const auto& a=v.pVertexAttributeDescriptions[i];k.add(a.location);k.add(a.binding);k.add(a.format);k.add(a.offset);
            }
            k.add(c.pInputAssemblyState->flags);k.add(c.pInputAssemblyState->topology);k.add(c.pInputAssemblyState->primitiveRestartEnable);
        }
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT ||
           part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
            k.add(c.layout);
            for(std::uint32_t i=0;i<c.stageCount;++i) {
                const auto& s=c.pStages[i];
                if((s.stage==VK_SHADER_STAGE_FRAGMENT_BIT)!=(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))continue;
                k.add(s.stage);k.add(s.flags);k.text(s.pName);
                { std::lock_guard lock(mutex_);k.add(shaderIds_.at(s.module).id); }
                if(s.pSpecializationInfo) {
                    const auto& spec=*s.pSpecializationInfo;k.add(spec.mapEntryCount);
                    for(std::uint32_t j=0;j<spec.mapEntryCount;++j) {
                        const auto& e=spec.pMapEntries[j];k.add(e.constantID);k.add(e.size);
                        k.bytes.append(static_cast<const char*>(spec.pData)+e.offset,e.size);
                    }
                } else k.add(0);
            }
        }
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
            const auto& r=*c.pRasterizationState;
            k.add(r.flags);k.add(r.depthClampEnable);k.add(r.rasterizerDiscardEnable);k.add(r.polygonMode);k.add(r.cullMode);
            k.add(r.frontFace);k.add(r.depthBiasEnable);k.add(r.depthBiasConstantFactor);k.add(r.depthBiasClamp);k.add(r.depthBiasSlopeFactor);k.add(r.lineWidth);
            // This helper only accepts the backend's dynamic viewport/scissor pipelines.
            k.add(c.pViewportState->flags);k.add(c.pViewportState->viewportCount);k.add(c.pViewportState->scissorCount);
        }
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
            const auto& d=*c.pDepthStencilState;
            k.add(d.flags);k.add(d.depthTestEnable);k.add(d.depthWriteEnable);k.add(d.depthCompareOp);
            k.add(d.depthBoundsTestEnable);k.add(d.stencilTestEnable);k.add(d.minDepthBounds);k.add(d.maxDepthBounds);
            for(const auto& s:{d.front,d.back}) {
                k.add(s.failOp);k.add(s.passOp);k.add(s.depthFailOp);k.add(s.compareOp);k.add(s.compareMask);k.add(s.writeMask);k.add(s.reference);
            }
        }
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT ||
           part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
            const auto& m=*c.pMultisampleState;k.add(m.flags);k.add(m.rasterizationSamples);k.add(m.sampleShadingEnable);
            k.add(m.minSampleShading);k.add(m.alphaToCoverageEnable);k.add(m.alphaToOneEnable);
            for(std::uint32_t i=0;i<(static_cast<std::uint32_t>(m.rasterizationSamples)+31)/32;++i)
                k.add(m.pSampleMask ? m.pSampleMask[i] : ~0u);
        }
        if(part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
            const auto& b=*c.pColorBlendState;k.add(b.flags);k.add(b.logicOpEnable);k.add(b.logicOp);k.add(b.attachmentCount);
            for(std::uint32_t i=0;i<b.attachmentCount;++i) {
                const auto& a=b.pAttachments[i];k.add(a.blendEnable);k.add(a.srcColorBlendFactor);k.add(a.dstColorBlendFactor);
                k.add(a.colorBlendOp);k.add(a.srcAlphaBlendFactor);k.add(a.dstAlphaBlendFactor);k.add(a.alphaBlendOp);k.add(a.colorWriteMask);
            }
            for(float f:b.blendConstants)k.add(f);
        }
        return std::move(k.bytes);
    }
    VkResult preparePart(VkDevice device, VkPipelineCache cache, const VkGraphicsPipelineCreateInfo& info,
                         VkGraphicsPipelineLibraryFlagBitsEXT part, std::string_view name, VkPipeline* out) {
        const auto identity=key(info,part);
        std::shared_ptr<Entry> entry;
        { std::lock_guard lock(mutex_);auto& slot=entries_[identity];if(!slot)slot=std::make_shared<Entry>();entry=slot; }
        std::lock_guard lock(entry->mutex);
        if(entry->result==VK_NOT_READY) {
            VkGraphicsPipelineLibraryCreateInfoEXT subset{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT};
            subset.flags=part;
            auto c=info;c.pNext=&subset;
            // Each library owns only its subset. Do not expose another subset's
            // blend state while compiling a fragment program shared across blends.
            const bool vertexInput=part==VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;
            const bool preRaster=part==VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;
            const bool fragment=part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
            const bool output=part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
            if(!vertexInput){c.pVertexInputState=nullptr;c.pInputAssemblyState=nullptr;}
            if(!preRaster){c.pViewportState=nullptr;c.pRasterizationState=nullptr;c.pDynamicState=nullptr;}
            if(!fragment)c.pDepthStencilState=nullptr;
            if(!fragment && !output)c.pMultisampleState=nullptr;
            if(!output)c.pColorBlendState=nullptr;
            if(vertexInput || output)c.layout=VK_NULL_HANDLE;
            if(vertexInput){c.renderPass=VK_NULL_HANDLE;c.subpass=0;}
            c.flags |= VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};std::uint32_t count=0;
            for(std::uint32_t i=0;i<info.stageCount;++i) {
                const auto& s=info.pStages[i];
                if((part==VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT && s.stage!=VK_SHADER_STAGE_FRAGMENT_BIT) ||
                   (part==VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT && s.stage==VK_SHADER_STAGE_FRAGMENT_BIT))stages[count++]=s;
            }
            c.stageCount=count;c.pStages=stages.data();
            entry->result=prepareGraphicsPipelineWithFeedback(device,cache,c,feedbackEnabled,
                std::string(name)+"-part"+std::to_string(part),&entry->pipeline);
        } else if(engine::env::flagEnabled("PHLOSION_SHADER_PREPARATION_TRACE")) {
            std::osyncstream(std::clog) << "[Vulkan][PipelineLibrary] name=" << name << " part=" << part << " reuse=1\n";
        }
        *out=entry->pipeline;return entry->result;
    }
};
}
