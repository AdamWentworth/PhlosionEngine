#pragma once
#include "engine/render/ShaderPreparation.h"
#include <vulkan/vulkan.h>
#include <vector>
namespace engine::render {
inline VkResult prepareGraphicsPipelines(VkDevice device, VkPipelineCache cache,
    uint32_t count, const VkGraphicsPipelineCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipeline* out) {
    ShaderPreparationScope trace("vulkan-pso", "graphics");
    return vkCreateGraphicsPipelines(device, cache, count, info, allocator, out);
}
inline VkResult prepareComputePipelines(VkDevice device, VkPipelineCache cache,
    uint32_t count, const VkComputePipelineCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipeline* out) {
    ShaderPreparationScope trace("vulkan-pso", "compute");
    return vkCreateComputePipelines(device, cache, count, info, allocator, out);
}
inline VkResult prepareGraphicsPipelineWithFeedback(VkDevice device, VkPipelineCache cache,
    const VkGraphicsPipelineCreateInfo& info, bool feedbackEnabled, std::string_view name,
    VkPipeline* out) {
    ShaderPreparationScope trace("vulkan-pso", name);
    auto request = info;
    VkPipelineCreationFeedback feedback{};
    std::vector<VkPipelineCreationFeedback> stages(info.stageCount);
    VkPipelineCreationFeedbackCreateInfo details{VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO};
    details.pNext = request.pNext;
    details.pPipelineCreationFeedback = &feedback;
    details.pipelineStageCreationFeedbackCount = info.stageCount;
    details.pPipelineStageCreationFeedbacks = stages.data();
    if (feedbackEnabled) request.pNext = &details;
    const auto result = vkCreateGraphicsPipelines(device, cache, 1, &request, nullptr, out);
    trace.cacheHit((feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) != 0);
    if (engine::env::flagEnabled("PHLOSION_SHADER_PREPARATION_TRACE")) {
        std::osyncstream log(std::clog);
        log << "[Vulkan][PipelineFeedback] name=" << name << " result=" << result
            << " valid=" << bool(feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
            << " cache_hit=" << bool(feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT)
            << " driver_ns=" << feedback.duration;
        for (std::size_t i = 0; i < stages.size(); ++i)
            log << " stage" << info.pStages[i].stage << "_valid="
                << bool(stages[i].flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
                << " stage" << info.pStages[i].stage << "_ns=" << stages[i].duration;
        log << '\n';
    }
    return result;
}

}
