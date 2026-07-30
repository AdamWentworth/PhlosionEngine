#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanSpriteInstanceState.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

constexpr std::size_t kMaxSpriteQuads = 2048u;

struct SpriteDrawRun {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    std::uint32_t firstInstance = 0u;
    std::uint32_t instanceCount = 0u;
};

} // namespace

void VulkanRenderBackendImpl::drawDebugSprites(
    const IRenderBackend::DebugSprite* sprites,
    std::size_t spriteCount,
    int surfaceWidth,
    int surfaceHeight) {
    if (!frameActive || !sprites || spriteCount == 0u ||
        spritePipeline == VK_NULL_HANDLE) {
        return;
    }

    const std::size_t count = std::min(spriteCount, kMaxSpriteQuads);
    static thread_local std::vector<
        engine::render::vulkan_backend::SpriteInstanceState> instances;
    static thread_local std::vector<SpriteDrawRun> runs;
    instances.clear();
    runs.clear();
    instances.reserve(count);
    runs.reserve(count);

    for (std::size_t i = 0u; i < count; ++i) {
        const auto& sprite = sprites[i];
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) continue;
        Texture* texture = ensureSpriteTexture(sprite.texturePath);
        if (!texture || texture->descriptorSet == VK_NULL_HANDLE) continue;

        const std::uint32_t instanceIndex =
            static_cast<std::uint32_t>(instances.size());
        instances.push_back(
            engine::render::vulkan_backend::makeSpriteInstanceState(sprite));
        if (!runs.empty() &&
            runs.back().descriptorSet == texture->descriptorSet) {
            ++runs.back().instanceCount;
        } else {
            runs.push_back({texture->descriptorSet, instanceIndex, 1u});
        }
    }
    if (instances.empty() || runs.empty()) return;

    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize instanceOffset = 0u;
    if (!writeTransient(
            instances.data(),
            static_cast<VkDeviceSize>(instances.size()) *
                sizeof(engine::render::vulkan_backend::SpriteInstanceState),
            16u,
            instanceBuffer,
            instanceOffset)) {
        return;
    }

    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, surfaceWidth, surfaceHeight);
    bindGraphicsPipeline(commandBuffer, spritePipeline);
    DebugPushConstants push{};
    push.surfaceWidth = static_cast<float>(std::max(1, surfaceWidth));
    push.surfaceHeight = static_cast<float>(std::max(1, surfaceHeight));
    vkCmdPushConstants(
        commandBuffer,
        texturedPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0u,
        sizeof(push),
        &push);
    bindVertexBuffer(commandBuffer, instanceBuffer, instanceOffset);

    for (const SpriteDrawRun& run : runs) {
        bindTextureDescriptorSet(commandBuffer, run.descriptorSet);
        vkCmdDraw(
            commandBuffer,
            6u,
            run.instanceCount,
            0u,
            run.firstInstance);
    }

    frameStats.drawCalls += static_cast<std::uint32_t>(runs.size());
    frameStats.triangles +=
        static_cast<std::uint64_t>(instances.size()) * 2u;
    frameSpriteInstances += static_cast<std::uint32_t>(instances.size());
    frameSpriteDrawRuns += static_cast<std::uint32_t>(runs.size());
    frameSpriteDrawsSaved += static_cast<std::uint32_t>(
        instances.size() - runs.size());
    ++frameSpriteUploadBatches;
    frameSpriteUploadsSaved += static_cast<std::uint32_t>(
        instances.size() - 1u);
}
