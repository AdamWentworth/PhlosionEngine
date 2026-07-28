#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include "engine/core/Environment.h"
#include "engine/render/vulkan/VulkanWorldIndirectBatch.h"
#include "engine/render/vulkan/VulkanWorldSceneData.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

bool indirectWorldSceneEnabled() {
    static const bool enabled = []() {
        const auto value = engine::env::get("PAC_VULKAN_INDIRECT_WORLD_SCENE");
        if (!value.has_value()) return true;
        return engine::env::flagEnabled("PAC_VULKAN_INDIRECT_WORLD_SCENE");
    }();
    return enabled;
}

std::uint64_t bufferKey(VkBuffer buffer) {
    std::uint64_t key = 0u;
    static_assert(sizeof(buffer) <= sizeof(key));
    std::memcpy(&key, &buffer, sizeof(buffer));
    return key;
}

void appendInstances(
    const IRenderBackend::WorldSceneDrawClass& drawClass,
    std::vector<IRenderBackend::WorldMeshInstance>& instances) {
    instances.clear();
    instances.reserve(drawClass.instances.size());
    for (const IRenderBackend::WorldSceneInstance& sceneInstance :
         drawClass.instances) {
        IRenderBackend::WorldMeshInstance instance{};
        instance.modelMatrix = sceneInstance.modelMatrix;
        instance.vertexColorMulR = sceneInstance.vertexColorMulR;
        instance.vertexColorMulG = sceneInstance.vertexColorMulG;
        instance.vertexColorMulB = sceneInstance.vertexColorMulB;
        instance.vertexColorMulA = sceneInstance.vertexColorMulA;
        instance.gpuSkinning = sceneInstance.gpuSkinning;
        instance.gpuSkinningMode = sceneInstance.gpuSkinningMode;
        instance.skinMatrixCount = sceneInstance.skinMatrixCount;
        instance.skinMatrices = sceneInstance.skinMatrices;
        instances.push_back(std::move(instance));
    }
}

} // namespace

bool VulkanRenderBackendImpl::submitWorldSceneIndirect(
    const IRenderBackend::WorldSceneFrame& frame,
    const IRenderBackend::WorldSceneView& view) {
    namespace vulkan = engine::render::vulkan_backend;
    if (!indirectWorldSceneEnabled() || !indirectWorldBatchingSupported ||
        indirectWorldPipelineLayout == VK_NULL_HANDLE) {
        return false;
    }
    static thread_local std::vector<IRenderBackend::WorldMeshInstance> instances;
    static thread_local std::vector<vulkan::WorldIndirectDrawState> drawStates;
    static thread_local std::vector<VkDrawIndexedIndirectCommand> commands;
    static thread_local std::vector<vulkan::WorldIndirectDrawKey> drawKeys;
    static thread_local std::vector<vulkan::WorldIndirectDrawState> outlineStates;
    static thread_local std::vector<VkDrawIndexedIndirectCommand> outlineCommands;
    static thread_local std::vector<vulkan::WorldIndirectDrawKey> outlineKeys;
    drawStates.clear();
    commands.clear();
    drawKeys.clear();
    outlineStates.clear();
    outlineCommands.clear();
    outlineKeys.clear();
    const std::size_t drawClassCount = frame.drawClasses.size();
    drawStates.reserve(drawClassCount * 2u);
    commands.reserve(drawClassCount * 2u);
    drawKeys.reserve(drawClassCount * 2u);
    outlineStates.reserve(drawClassCount);
    outlineCommands.reserve(drawClassCount);
    outlineKeys.reserve(drawClassCount);

    IRenderBackend::WorldTextureData viewTexture{};
    bool haveViewTexture = false;
    std::uint64_t triangleCount = 0u;
    std::uint32_t logicalInstances = 0u;
    std::uint32_t previousGeometryId = 0u;
    std::uint32_t previousMaterialId = 0u;
    std::uint32_t geometrySwitches = 0u;
    std::uint32_t materialSwitches = 0u;
    bool havePreviousDraw = false;

    for (const IRenderBackend::WorldSceneDrawClass& drawClass :
         frame.drawClasses) {
        if (!drawClass.objectHandle || drawClass.instances.empty()) continue;
        const std::size_t objectIndex =
            static_cast<std::size_t>(drawClass.objectHandle.id - 1u);
        if (objectIndex >= view.renderObjects->size()) continue;
        const IRenderBackend::WorldSceneRenderObject& object =
            (*view.renderObjects)[objectIndex];
        if (!object.geometryHandle || !object.materialHandle) continue;
        const std::size_t geometryIndex =
            static_cast<std::size_t>(object.geometryHandle.id - 1u);
        const std::size_t materialIndex =
            static_cast<std::size_t>(object.materialHandle.id - 1u);
        if (geometryIndex >= view.geometries->size() ||
            materialIndex >= view.materials->size()) {
            continue;
        }

        const IRenderBackend::WorldSceneGeometry& geometry =
            (*view.geometries)[geometryIndex];
        const IRenderBackend::WorldSceneMaterial& material =
            (*view.materials)[materialIndex];
        if (!geometry.vertices || !geometry.indices ||
            geometry.vertexCount == 0u || geometry.indexCount < 3u ||
            !IRenderBackend::worldSceneGeometrySourceSemanticsValid(geometry)) {
            continue;
        }
        IRenderBackend::WorldTextureData texture =
            vulkan::makeWorldSceneTextureData(material, view);
        if (worldSceneMaterials.size() <= materialIndex) {
            worldSceneMaterials.resize(materialIndex + 1u);
        }
        WorldMaterial*& worldMaterial = worldSceneMaterials[materialIndex];
        if (!worldMaterial) {
            ++framePreparedMaterialCacheMisses;
            worldMaterial = ensureWorldMaterial(&texture);
        } else {
            ++framePreparedMaterialCacheHits;
        }
        if (!worldMaterial ||
            worldMaterial->indexedTableSlot == UINT32_MAX) {
            return false;
        }
        CachedWorldMesh* mesh = ensureCachedWorldMesh(
            geometry.geometryCacheKey.empty()
                ? nullptr
                : geometry.geometryCacheKey.c_str(),
            geometry.vertices,
            geometry.vertexCount,
            geometry.indices,
            geometry.indexCount);
        if (!mesh || mesh->geometryBuffer == VK_NULL_HANDLE) return false;

        appendInstances(drawClass, instances);
        std::uint32_t instanceCount = 0u;
        std::uint32_t instanceBaseWordIndex = 0u;
        if (!prepareWorldInstances(
                instances.data(),
                instances.size(),
                instanceCount,
                instanceBaseWordIndex)) {
            return false;
        }

        const std::size_t pipelineIndex = vulkan::worldPipelineIndex(
            &texture, dualSourceBlendSupported);
        if (pipelineIndex >= indirectWorldPipelines.size() ||
            indirectWorldPipelines[pipelineIndex] == VK_NULL_HANDLE) {
            return false;
        }
        drawStates.push_back(vulkan::makeWorldIndirectDrawState(
            &texture,
            worldMaterial->indexedTableSlot,
            instanceBaseWordIndex));
        const VkDrawIndexedIndirectCommand command{
            static_cast<std::uint32_t>(mesh->indexCount),
            instanceCount,
            mesh->firstIndex,
            mesh->baseVertex,
            0u};
        commands.push_back(command);
        const vulkan::WorldIndirectDrawKey drawKey{
            bufferKey(mesh->geometryBuffer),
            static_cast<std::uint32_t>(pipelineIndex)};
        drawKeys.push_back(drawKey);
        const bool drawCharacterOutline =
            material.characterInkingEnabled != 0u &&
            material.materialMode >= 2u;
        if (drawCharacterOutline) {
            if (indirectWorldPipelines[2u] == VK_NULL_HANDLE) return false;
            outlineStates.push_back(
                vulkan::makeWorldIndirectOutlineDrawState(drawStates.back()));
            outlineCommands.push_back(command);
            outlineKeys.push_back({drawKey.geometryBufferKey, 2u});
        }
        if (!haveViewTexture) {
            viewTexture = texture;
            haveViewTexture = true;
        }

        logicalInstances += instanceCount;
        triangleCount += static_cast<std::uint64_t>(mesh->indexCount / 3u) *
                         instanceCount;
        if (drawCharacterOutline) {
            triangleCount += static_cast<std::uint64_t>(mesh->indexCount / 3u) *
                             instanceCount;
        }
        if (!havePreviousDraw ||
            previousGeometryId != object.geometryHandle.id) {
            ++geometrySwitches;
        }
        if (!havePreviousDraw ||
            previousMaterialId != object.materialHandle.id) {
            ++materialSwitches;
        }
        previousGeometryId = object.geometryHandle.id;
        previousMaterialId = object.materialHandle.id;
        havePreviousDraw = true;
    }
    if (commands.empty()) return true;
    const std::uint32_t surfaceCommandCount =
        static_cast<std::uint32_t>(commands.size());
    const std::uint32_t outlineCommandCount =
        static_cast<std::uint32_t>(outlineCommands.size());
    // Alpha-blended character surfaces may not populate depth. Submit their
    // outlines first so the later textured surface pass cannot be blacked out.
    outlineStates.insert(
        outlineStates.end(), drawStates.begin(), drawStates.end());
    outlineCommands.insert(
        outlineCommands.end(), commands.begin(), commands.end());
    outlineKeys.insert(outlineKeys.end(), drawKeys.begin(), drawKeys.end());
    drawStates.swap(outlineStates);
    commands.swap(outlineCommands);
    drawKeys.swap(outlineKeys);
    if (!syncIndexedWorldMaterialSet()) return false;

    VkBuffer drawStateBuffer = VK_NULL_HANDLE;
    VkDeviceSize drawStateOffset = 0u;
    const VkDeviceSize storageAlignment = std::max<VkDeviceSize>(
        16u,
        physicalDeviceProperties.limits.minStorageBufferOffsetAlignment);
    if (!writeTransient(
            drawStates.data(),
            drawStates.size() * sizeof(vulkan::WorldIndirectDrawState),
            storageAlignment,
            drawStateBuffer,
            drawStateOffset) ||
        drawStateBuffer != frames[currentFrame].transient.buffer ||
        drawStateOffset > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceSize indirectOffset = 0u;
    if (!writeTransient(
            commands.data(),
            commands.size() * sizeof(VkDrawIndexedIndirectCommand),
            alignof(VkDrawIndexedIndirectCommand),
            indirectBuffer,
            indirectOffset) ||
        indirectBuffer != frames[currentFrame].transient.buffer) {
        return false;
    }

    const vulkan::WorldViewState viewState =
        vulkan::makeWorldViewState(&viewTexture);
    VkBuffer viewBuffer = VK_NULL_HANDLE;
    VkDeviceSize viewOffset = 0u;
    const VkDeviceSize uniformAlignment = std::max<VkDeviceSize>(
        16u,
        physicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
    if (!writeCachedWorldViewState(
            viewState,
            uniformAlignment,
            viewBuffer,
            viewOffset) ||
        viewBuffer != frames[currentFrame].transient.buffer ||
        viewOffset > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }

    FrameResources& frameResources = frames[currentFrame];
    const std::array<VkDescriptorSet, 2> descriptorSets{
        frameResources.indexedWorldMaterialDescriptorSet,
        frameResources.worldStateDescriptorSet,
    };
    const std::array<std::uint32_t, 4> dynamicOffsets{
        static_cast<std::uint32_t>(viewOffset),
        0u,
        0u,
        static_cast<std::uint32_t>(drawStateOffset),
    };
    VkCommandBuffer commandBuffer = frameResources.commandBuffer;
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        indirectWorldPipelineLayout,
        0u,
        static_cast<std::uint32_t>(descriptorSets.size()),
        descriptorSets.data(),
        static_cast<std::uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
    frameResources.indexedWorldMaterialSetBound = true;
    boundTexturedDescriptorSet = VK_NULL_HANDLE;
    ++frameDescriptorBindCalls;
    setViewportAndScissor(commandBuffer, view.surfaceWidth, view.surfaceHeight);

    const std::vector<vulkan::WorldIndirectRun> runs =
        vulkan::buildWorldIndirectRuns(drawKeys);
    std::uint32_t apiCalls = 0u;
    for (const vulkan::WorldIndirectRun& run : runs) {
        bindGraphicsPipeline(
            commandBuffer,
            indirectWorldPipelines[run.key.pipelineIndex]);
        VkBuffer geometryBuffer = VK_NULL_HANDLE;
        static_assert(sizeof(geometryBuffer) <= sizeof(run.key.geometryBufferKey));
        std::memcpy(
            &geometryBuffer,
            &run.key.geometryBufferKey,
            sizeof(geometryBuffer));
        bindVertexBuffer(commandBuffer, geometryBuffer, 0u);
        bindIndexBuffer(
            commandBuffer, geometryBuffer, 0u, VK_INDEX_TYPE_UINT32);

        std::uint32_t submitted = 0u;
        while (submitted < run.drawCount) {
            const std::uint32_t chunkCount = std::min(
                run.drawCount - submitted,
                physicalDeviceProperties.limits.maxDrawIndirectCount);
            vulkan::WorldIndirectPushConstants push{};
            std::memcpy(
                push.viewProjection.data(),
                view.viewProjectionMatrix4x4,
                sizeof(float) * 16u);
            push.batchParams[0] = static_cast<std::uint32_t>(
                run.firstDraw + submitted);
            vkCmdPushConstants(
                commandBuffer,
                indirectWorldPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0u,
                sizeof(push),
                &push);
            vkCmdDrawIndexedIndirect(
                commandBuffer,
                indirectBuffer,
                indirectOffset +
                    (run.firstDraw + submitted) *
                        sizeof(VkDrawIndexedIndirectCommand),
                chunkCount,
                sizeof(VkDrawIndexedIndirectCommand));
            submitted += chunkCount;
            ++apiCalls;
        }
    }

    frameStats.drawCalls += apiCalls;
    frameStats.triangles += triangleCount;
    frameStats.fastSceneInstances += logicalInstances;
    ++frameStats.fastSceneMaterialTableBinds;
    frameStats.fastSceneIndirectCommands +=
        static_cast<std::uint32_t>(commands.size());
    frameStats.indexedOpaqueDraws += surfaceCommandCount;
    frameStats.indexedCachedDraws += surfaceCommandCount;
    frameStats.indexedInstancedDraws += surfaceCommandCount;
    frameStats.indexedOutlineBatches += outlineCommandCount;
    frameStats.indexedGeometrySwitches += geometrySwitches;
    frameStats.indexedMaterialSwitches += materialSwitches;
    frameStats.indexedTextureSwitches += materialSwitches;
    frameIndirectWorldApiCalls += apiCalls;
    frameIndirectWorldCommands +=
        static_cast<std::uint32_t>(commands.size());
    return true;
}
