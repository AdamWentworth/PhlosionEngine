#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/WorldDirectionalShadowPolicy.h"
#include "engine/render/IWorldLocalHaze.h"
#include "engine/render/WorldSurfaceComposition.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>
#include <stdexcept>

#include "engine/render/DebugGeometry.h"

namespace {

constexpr std::size_t kMaxDebugPrimitives = 4096u;
constexpr std::size_t kMaxWorldVertices = 540000u;
constexpr std::size_t kMaxWorldIndices = 900000u;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace

engine::render::backend::WorldPreparedViewToken VulkanRenderBackendImpl::prepareWorldViewData(
    const IRenderBackend::WorldTextureData* texture) {
    const bool composable = worldSurfaceActive && worldMaterialDebugView == 0 && worldSurfaceCompositionCapture &&
        engine::render::worldSurfaceCompositionMaterial(texture,worldCompositionLighting,texture && texture->materialMode<256 && worldMaterialProfile.modes[texture->materialMode].foliageComposition);
    return preparedWorldViews.prepare(engine::render::WorldPreparedViewInput(texture, composable), [&] {
    auto viewState =
        engine::render::vulkan_backend::makeWorldViewState(texture, &worldEnvironmentPackets);
    if (worldShadowReady && !worldShadowActive && texture && texture->environment.enabled && texture->directionalShadowReceiver)
        viewState.directionalShadow = engine::render::packWorldDirectionalShadow(worldShadowView);
    viewState.shadowAtlasUv = worldLocalShadows_.directionalUv();
    viewState.cameraPosition[3] = worldSceneColorPassActive && worldSceneRadianceExposure > 0 ? 1.f : 0.f;
    if (!worldShadowActive && texture && texture->environment.enabled && texture->directionalShadowReceiver)
        viewState.localShadows = worldLocalShadows_.uniforms;
    if (!worldShadowActive && texture && texture->environment.enabled) viewState.diffuseProbes = worldDiffuseProbes;
    if (!worldShadowActive) {viewState.planar=worldPlanarReflection;viewState.contact=worldContact;viewState.screenReflection=worldScreenReflection;viewState.localReflection=worldLocalReflection;viewState.fog=worldFog;viewState.wet=worldWet;viewState.clouds=worldClouds;viewState.sun=worldSunPlan.uniforms;viewState.skyCloud=worldSkyCloud;viewState.fog[2][3]=texture&&(texture->blendMode==1||texture->blendMode==3)?1.f:0.f;}
    if(!worldShadowActive && !worldSurfaceActive && texture && texture->environment.enabled && texture->directionalShadowReceiver && texture->alphaMode<2) viewState.clustered=worldClusteredView;
    if (composable) viewState.clustered[4][2]=1;
        PreparedWorldView result;
        const VkDeviceSize alignment = std::max<VkDeviceSize>(
            16u, physicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
        if (!writeCachedWorldViewState(viewState, alignment, result.buffer, result.offset))
            throw std::runtime_error("Cannot upload prepared world view");
        return result;
    });
}

bool VulkanRenderBackendImpl::prepareWorldDescriptorSets(
    engine::render::vulkan_backend::WorldDescriptorBindings& bindings,
    VkDescriptorSet materialDescriptorSet,
    const IRenderBackend::WorldTextureData* texture,
    bool instancingEnabled,
    std::uint32_t instanceBaseWordIndex) {
    const auto* prepared = texture ? preparedWorldViews.get(texture->preparedView) : nullptr;
    if (!prepared) prepared = preparedWorldViews.get(prepareWorldViewData(texture));
    auto specializedMaterialState =
        engine::render::vulkan_backend::makeWorldSpecializedMaterialState(texture);
    if (worldMaterialDebugView > 0 && texture &&
        texture->materialMode >= 2u) {
        specializedMaterialState.flipbook1[3] =
            -100.0f - static_cast<float>(worldMaterialDebugView);
        specializedMaterialState.timingFlagsAtlas[1] =
            static_cast<float>(
                engine::render::vulkan_backend::
                    worldMaterialTexturePresenceFlags(texture));
    }
    const std::size_t skinFloatCount =
        engine::render::vulkan_backend::worldSkinMatrixFloatCount(texture);
    std::uint32_t skinMatrixBaseIndex = 0u;
    if (skinFloatCount > 0u &&
        !uploadWorldSkinPalette(
            texture->skinMatrices,
            skinFloatCount,
            skinMatrixBaseIndex,texture->deformation)) {
        return false;
    }
    auto transformState =
        engine::render::vulkan_backend::makeWorldTransformState(
            texture,
            skinMatrixBaseIndex,
            instancingEnabled,
            instanceBaseWordIndex);
    if(worldSurfaceActive) {
        if(!worldMotionUploaded) {
            VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;
            if(!writeTransient(worldMotionWords.data(),worldMotionWords.size()*16,16,buffer,offset) ||
                buffer!=frames[currentFrame].transient.buffer)return false;
            worldMotionBase=static_cast<std::uint32_t>(offset/16);worldMotionUploaded=true;
        }
        transformState.instanceParams[2]=float(worldMotionBase);
        transformState.instanceParams[3]=1;
    }
    VkBuffer viewBuffer = VK_NULL_HANDLE;
    VkDeviceSize viewOffset = 0u;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        16u,
        physicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
    VkBuffer specializedMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceSize specializedMaterialOffset = 0u;
    VkBuffer transformBuffer = VK_NULL_HANDLE;
    VkDeviceSize transformOffset = 0u;
    viewBuffer = prepared->buffer;
    viewOffset = prepared->offset;
    if (!writeCachedWorldSpecializedMaterialState(
            specializedMaterialState,
            alignment,
            specializedMaterialBuffer,
            specializedMaterialOffset) ||
        !writeTransient(
            &transformState,
            sizeof(transformState),
            alignment,
            transformBuffer,
            transformOffset) ||
        viewBuffer != frames[currentFrame].transient.buffer ||
        specializedMaterialBuffer != frames[currentFrame].transient.buffer ||
        transformBuffer != frames[currentFrame].transient.buffer ||
        viewOffset > std::numeric_limits<std::uint32_t>::max() ||
        specializedMaterialOffset > std::numeric_limits<std::uint32_t>::max() ||
        transformOffset > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const std::array<std::uint32_t, 4> dynamicOffsets{
        static_cast<std::uint32_t>(viewOffset),
        static_cast<std::uint32_t>(specializedMaterialOffset),
        static_cast<std::uint32_t>(transformOffset),
        0u,
    };
    bindings = prepareWorldStateBindings(materialDescriptorSet, dynamicOffsets, texture && (texture->materialMode == 246 || texture->materialMode == 248));
    return true;
}
bool VulkanRenderBackendImpl::bindWorldDescriptorSets(VkCommandBuffer commandBuffer,
    VkDescriptorSet materialDescriptorSet, const IRenderBackend::WorldTextureData* texture,
    bool instancingEnabled, std::uint32_t instanceBaseWordIndex) {
    engine::render::vulkan_backend::WorldDescriptorBindings bindings;
    if(!prepareWorldDescriptorSets(bindings,materialDescriptorSet,texture,instancingEnabled,instanceBaseWordIndex))return false;
    engine::render::vulkan_backend::WorldCommandRecorder<> recorder{{},worldRecordingCommand(commandBuffer),texturedPipelineLayout,worldCommandState,worldCommandCounters};
    recorder.bindWorldDescriptors(bindings);
    return true;
}

void VulkanRenderBackendImpl::drawDebugVertices(const DebugVertex* vertices,
                                                std::size_t vertexCount,
                                                int surfaceWidth,
                                                int surfaceHeight) {
    if (!frameActive || !vertices || vertexCount < 3u || debugPipeline == VK_NULL_HANDLE) return;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(vertexCount) * sizeof(DebugVertex);
    if (!writeTransient(vertices, byteCount, 16u, vertexBuffer, vertexOffset)) return;

    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, surfaceWidth, surfaceHeight);
    bindGraphicsPipeline(commandBuffer, worldSceneColorPassActive ? sceneDebugPipeline : debugPipeline);
    bindVertexBuffer(commandBuffer, vertexBuffer, vertexOffset);
    DebugPushConstants push{};
    push.surfaceWidth = static_cast<float>(std::max(1, surfaceWidth));
    push.surfaceHeight = static_cast<float>(std::max(1, surfaceHeight));
    vkCmdPushConstants(worldRecordingCommand(commandBuffer),
                       debugPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDraw(worldRecordingCommand(commandBuffer), static_cast<std::uint32_t>(vertexCount), 1u, 0u, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += vertexCount / 3u;
}

void VulkanRenderBackendImpl::drawDebugQuads(const IRenderBackend::DebugQuad* quads,
                                             std::size_t quadCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!quads || quadCount == 0u) return;
    const std::size_t count = std::min(quadCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 6u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& q = quads[i];
        const DebugVertex a{q.x, q.y, q.r, q.g, q.b, q.a};
        const DebugVertex b{q.x + q.w, q.y, q.r, q.g, q.b, q.a};
        const DebugVertex c{q.x + q.w, q.y + q.h, q.r, q.g, q.b, q.a};
        const DebugVertex d{q.x, q.y + q.h, q.r, q.g, q.b, q.a};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawDebugLines(const IRenderBackend::DebugLine* lines,
                                             std::size_t lineCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!lines || lineCount == 0u) return;
    const std::size_t count = std::min(lineCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 6u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& line = lines[i];
        if (line.thickness <= 0.0f) continue;
        const float dx = line.x2 - line.x1;
        const float dy = line.y2 - line.y1;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) continue;
        const float scale = line.thickness * 0.5f / length;
        const float nx = -dy * scale;
        const float ny = dx * scale;
        const DebugVertex a{line.x1 + nx, line.y1 + ny, line.r, line.g, line.b, line.a};
        const DebugVertex b{line.x2 + nx, line.y2 + ny, line.r, line.g, line.b, line.a};
        const DebugVertex c{line.x2 - nx, line.y2 - ny, line.r, line.g, line.b, line.a};
        const DebugVertex d{line.x1 - nx, line.y1 - ny, line.r, line.g, line.b, line.a};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawDebugTriangles(
    const IRenderBackend::DebugTriangle* triangles,
    std::size_t triangleCount,
    int surfaceWidth,
    int surfaceHeight) {
    if (!triangles || triangleCount == 0u) return;
    const std::size_t count = std::min(triangleCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 3u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& t = triangles[i];
        vertices.push_back({t.x1, t.y1, t.r, t.g, t.b, t.a});
        vertices.push_back({t.x2, t.y2, t.r, t.g, t.b, t.a});
        vertices.push_back({t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawWorldTriangles(
    const IRenderBackend::WorldTriangle* triangles,
    std::size_t triangleCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (!frameActive || !triangles || triangleCount == 0u || !viewProjectionMatrix4x4) return;
    const std::size_t count = std::min(triangleCount, kMaxWorldIndices / 3u);
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    vertices.reserve(count * 3u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& triangle = triangles[i];
        const Vec3 p0{triangle.x1, triangle.y1, triangle.z1};
        const Vec3 p1{triangle.x2, triangle.y2, triangle.z2};
        const Vec3 p2{triangle.x3, triangle.y3, triangle.z3};
        const auto color = [&](float r, float g, float b, float a) {
            return std::array<float, 4>{
                r >= 0.0f ? r : triangle.r,
                g >= 0.0f ? g : triangle.g,
                b >= 0.0f ? b : triangle.b,
                a >= 0.0f ? a : triangle.a,
            };
        };
        const auto c0 = color(triangle.r1, triangle.g1, triangle.b1, triangle.a1);
        const auto c1 = color(triangle.r2, triangle.g2, triangle.b2, triangle.a2);
        const auto c2 = color(triangle.r3, triangle.g3, triangle.b3, triangle.a3);
        vertices.push_back({p0.x, p0.y, p0.z, 0.0f, 0.0f,
                            c0[0], c0[1], c0[2], c0[3], 0.0f, 0.0f, 0.0f});
        vertices.push_back({p1.x, p1.y, p1.z, 0.0f, 0.0f,
                            c1[0], c1[1], c1[2], c1[3], 0.0f, 0.0f, 0.0f});
        vertices.push_back({p2.x, p2.y, p2.z, 0.0f, 0.0f,
                            c2[0], c2[1], c2[2], c2[3], 0.0f, 0.0f, 0.0f});
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    if (!writeTransient(vertices.data(),
                        static_cast<VkDeviceSize>(vertices.size()) *
                            sizeof(IRenderBackend::WorldMeshVertex),
                        16u,
                        vertexBuffer,
                        vertexOffset)) {
        return;
    }
    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, surfaceWidth, surfaceHeight);
    const VkPipeline pipeline = worldSurfaceComposing ? worldSurfaceCompositionPipeline : worldScreenReflection[8][3]==2.f ? (worldScreenReflection[10][3]==8.f ? worldProbeRelightPipeline : worldSurfaceReflectionPipeline) : worldSurfaceActive ? worldSurfacePipeline : (worldSceneColorPassActive ? sceneWorldPipelines : worldPipelines)[0u];
    bindGraphicsPipeline(commandBuffer, pipeline);
    if (!bindWorldDescriptorSets(
            commandBuffer,
            fallbackWorldMaterial.descriptorSet,
            nullptr,
            false,
            0u)) {
        return;
    }
    bindVertexBuffer(commandBuffer, vertexBuffer, vertexOffset);
    WorldPushConstants push = engine::render::vulkan_backend::makeWorldPushConstants(nullptr);
    push.reserved0 = worldSceneColorPassActive ? 1.0f : 0.0f;
    std::memcpy(push.viewProjection.data(), viewProjectionMatrix4x4, sizeof(float) * 16u);
    vkCmdPushConstants(worldRecordingCommand(commandBuffer),
                       texturedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDraw(worldRecordingCommand(commandBuffer), static_cast<std::uint32_t>(vertices.size()), 1u, 0u, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += vertices.size() / 3u;
}

void VulkanRenderBackendImpl::drawWorldIndexedMesh(
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const IRenderBackend::WorldTextureData* textureData,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    drawWorldIndexedMeshInstanced(
        vertices,
        vertexCount,
        indices,
        indexCount,
        textureData,
        nullptr,
        0u,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void VulkanRenderBackendImpl::drawWorldIndexedMeshInstanced(
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const IRenderBackend::WorldTextureData* textureData,
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (!frameActive || !vertices || !indices || !viewProjectionMatrix4x4 ||
        vertexCount == 0u || indexCount < 3u) {
        return;
    }
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    for (std::size_t i = 0u; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return;
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    if (!writeTransient(vertices,
                        static_cast<VkDeviceSize>(safeVertexCount) *
                            sizeof(IRenderBackend::WorldMeshVertex),
                        16u,
                        vertexBuffer,
                        vertexOffset)) {
        return;
    }
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0u;
    if (!writeTransient(indices,
                        static_cast<VkDeviceSize>(safeIndexCount) * sizeof(std::uint32_t),
                        4u,
                        indexBuffer,
                        indexOffset)) {
        return;
    }

    drawWorldIndexedMeshBuffers(
        vertexBuffer,
        vertexOffset,
        safeVertexCount,
        indexBuffer,
        indexOffset,
        safeIndexCount,
        0u,
        0,
        VK_NULL_HANDLE,
        textureData,
        instances,
        instanceCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void VulkanRenderBackendImpl::drawWorldIndexedMeshBuffers(
    VkBuffer vertexBuffer,
    VkDeviceSize vertexOffset,
    std::size_t vertexCount,
    VkBuffer indexBuffer,
    VkDeviceSize indexOffset,
    std::size_t indexCount,
    std::uint32_t firstIndex,
    std::int32_t baseVertex,
    VkDescriptorSet preparedMaterialDescriptorSet,
    const IRenderBackend::WorldTextureData* textureData,
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if(worldSurfaceComposed && worldMaterialDebugView==0 && worldSurfaceCompositionCapture && engine::render::worldSurfaceCompositionMaterial(textureData,worldCompositionLighting,textureData && textureData->materialMode<256 && worldMaterialProfile.modes[textureData->materialMode].foliageComposition))return;
    IRenderBackend::WorldTextureData shadowTexture;
    if (worldPlanarReflection[4][3]<0 && !worldShadowActive && !worldSurfaceComposing && !engine::render::isWorldLocalHazePass(textureData)) {
        viewProjectionMatrix4x4=worldPlanarReflectionView.viewProjection.data();
        surfaceWidth=worldPlanarReflectionView.width; surfaceHeight=worldPlanarReflectionView.heightPixels;
    }
    if (worldShadowActive) {
        if (!engine::render::worldDirectionalShadowCaster(textureData)) return;
        viewProjectionMatrix4x4 = worldShadowView.viewProjection.data();
        surfaceWidth = surfaceHeight = static_cast<int>(worldShadowView.resolution);
        if (textureData) { shadowTexture = *textureData; shadowTexture.clipSpaceDepthBias = 0; textureData = &shadowTexture; }
    }
    if (!frameActive ||
        vertexBuffer == VK_NULL_HANDLE ||
        indexBuffer == VK_NULL_HANDLE ||
        !viewProjectionMatrix4x4 ||
        vertexCount == 0u ||
        indexCount < 3u) {
        return;
    }
    VkDescriptorSet materialDescriptorSet = preparedMaterialDescriptorSet;
    if (materialDescriptorSet == VK_NULL_HANDLE) {
        WorldMaterial* material = ensureWorldMaterial(textureData);
        if (!material || material->descriptorSet == VK_NULL_HANDLE) return;
        materialDescriptorSet = material->descriptorSet;
    }

    std::uint32_t drawInstanceCount = 1u;
    std::uint32_t instanceBaseWordIndex = 0u;
    const bool instancingEnabled = instances && instanceCount > 0u;
    if (instancingEnabled &&
        !prepareWorldInstances(
            instances,
            instanceCount,
            drawInstanceCount,
            instanceBaseWordIndex)) {
        return;
    }

    engine::render::vulkan_backend::WorldIndexedDrawPacket packet;
    packet.viewport = prepareWorldViewport(surfaceWidth,surfaceHeight);
    const std::size_t pipelineIndex =
        engine::render::vulkan_backend::worldPipelineIndex(
            textureData, dualSourceBlendSupported);
    if (!prepareWorldDescriptorSets(
            packet.bindings,
            materialDescriptorSet,
            textureData,
            instancingEnabled,
            instanceBaseWordIndex)) {
        return;
    }
    packet.vertices=vertexBuffer; packet.vertexOffset=vertexOffset;
    packet.indices=indexBuffer; packet.indexOffset=indexOffset;
    packet.indexCount=static_cast<std::uint32_t>(indexCount);
    packet.instanceCount=drawInstanceCount; packet.firstIndex=firstIndex; packet.baseVertex=baseVertex;
    WorldPushConstants push = engine::render::vulkan_backend::makeWorldPushConstants(textureData, &worldMaterialProfile);
    push.reserved0 = worldSceneColorPassActive ? 1.0f : 0.0f;
    std::memcpy(push.viewProjection.data(), viewProjectionMatrix4x4, sizeof(float) * 16u);
    const bool drawCharacterOutline =
        textureData &&
        !worldShadowActive && !worldSurfaceActive && textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u;
    // Keep the textured surface authoritative even when its blend state does
    // not write depth: emit the inverted hull before the surface replay.
    if (drawCharacterOutline) {
        const bool outlineDepthEnabled = textureData->depthTestEnabled != 0u;
        const std::size_t outlinePipelineIndex = outlineDepthEnabled ? 2u : 3u;
        packet.outlinePipeline = (worldSceneColorPassActive ? sceneWorldPipelines : worldPipelines)[outlinePipelineIndex];
        WorldPushConstants outlinePush = push;
        outlinePush.materialMode = 3.0f;
        outlinePush.outlineExtrude =
            engine::render::vulkan_backend::kWorldCharacterOutlineExtrude;
        packet.outline=outlinePush;
        ++frameStats.drawCalls;
        frameStats.triangles += static_cast<std::uint64_t>(indexCount / 3u) *
                                drawInstanceCount;
    }

    const bool clusteredComposition=(!worldSurfaceCompositionFinished || worldSurfaceComposed) && worldSceneColorPassActive && worldSurfaceReady &&
        worldClusteredView[4][3]>.5f && pipelineIndex==0 && textureData &&
        (textureData->materialMode==2 || (textureData->materialMode<256 && worldMaterialProfile.modes[textureData->materialMode].sharedLocalLighting && textureData->environment.localHazeDeferred && worldDiffuseProbes[0][3]>1.5f)) && textureData->environment.enabled &&
        textureData->directionalShadowReceiver && textureData->alphaMode<2 &&
        (worldPlanarReflection[4][3]>=0 || worldSurfacePlanar) && worldLocalReflection[0][3]>=0 && worldScreenReflection[8][3]>=0;
    const bool volume = textureData && textureData->materialMode == 249 &&
        pipelineIndex == engine::render::vulkan_backend::kFirstStandardBlendPipeline;
    const auto family = textureData ? worldMaterialProfile.transparentFamilyIndex(textureData->materialMode) : -1;
    if (family >= 0 && (pipelineIndex < 2 || worldSurfaceActive || worldShadowActive ||
            worldSurfaceComposing || worldScreenReflection[8][3] == 2.f || worldContact[8][3] > 1.5f ||
            (textureData->characterInkingEnabled || textureData->environment.skyDraw)))
        throw std::runtime_error("Transparent material family requires a blended color pass.");
    auto ordinaryPipeline = family >= 0 ?
        (worldSceneColorPassActive ? transparentPipelines.at(family).scene : transparentPipelines.at(family).display).at(pipelineIndex) :
        (worldSceneColorPassActive ? sceneWorldPipelines : worldPipelines)[pipelineIndex];
    if (family >= 0 && !ordinaryPipeline)
        throw std::runtime_error("Transparent material family pipeline is unavailable.");
    if(textureData) {
        const auto key=static_cast<std::uint32_t>(textureData->materialMode)*32+
            static_cast<std::uint32_t>(pipelineIndex)+(worldSceneColorPassActive?8192u:0u);
        if(auto it=worldMaterialPipelines.find(key);it!=worldMaterialPipelines.end())ordinaryPipeline=it->second;
    }
    auto clusteredPipeline=worldClusteredCompositionPipeline;
    if(clusteredComposition && textureData) {
        const auto key=static_cast<std::uint32_t>(textureData->materialMode)*32+8192u+16384u;
        if(auto it=worldMaterialPipelines.find(key);it!=worldMaterialPipelines.end())clusteredPipeline=it->second;
    }
    packet.surfacePipeline = worldSurfaceComposing ? worldSurfaceCompositionPipeline : worldScreenReflection[8][3]==2.f ? (worldScreenReflection[10][3]==8.f ? worldProbeRelightPipeline : worldSurfaceReflectionPipeline) : worldSurfaceActive ? worldSurfacePipeline : worldShadowActive ? worldShadowPipeline :
        textureData && textureData->materialMode==239 ? worldFogComposePipeline :
        worldContact[8][3]>1.5f ? worldContactFilterPipeline :
        worldHazeStencilActive && textureData && textureData->materialMode==248 ? worldHazeStencilComposePipeline :
        worldHazeStencilActive && textureData && textureData->materialMode==246 ? worldHazeStencilEdgePipeline :
        worldHazeActive && worldHazePipeline && textureData && textureData->materialMode==247 ? worldHazePipeline :
        worldSceneColorPassActive && worldTemporalHazePipeline && textureData && textureData->materialMode==248 ? worldTemporalHazePipeline :
        worldSceneColorPassActive && worldHazeEdgePipeline && textureData && textureData->materialMode==246 ? worldHazeEdgePipeline :
        clusteredComposition ? clusteredPipeline :
        volume ? (worldSceneColorPassActive ? sceneWorldVolumePipeline : worldVolumePipeline) : ordinaryPipeline;
    packet.surface=push;
    submitWorldDrawPacket(packet);
    ++frameStats.drawCalls;
    frameStats.triangles += static_cast<std::uint64_t>(indexCount / 3u) *
                            drawInstanceCount;
}

namespace {
void requireRecordingVk(VkResult result,const char* message) {
    if(result!=VK_SUCCESS)throw std::runtime_error(message);
}
}
VkCommandBuffer VulkanRenderBackendImpl::allocateWorldRecordingCommand(unsigned laneIndex) {
    auto& lane=worldRecordingFrames[currentFrame][laneIndex];
    if(!lane.pool) {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex=graphicsQueueFamily;
        requireRecordingVk(vkCreateCommandPool(device,&info,nullptr,&lane.pool),"Create recording command pool failed");
    }
    if(lane.used==lane.lists.size()) {
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool=lane.pool;info.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;info.commandBufferCount=1;
        VkCommandBuffer list=VK_NULL_HANDLE;
        requireRecordingVk(vkAllocateCommandBuffers(device,&info,&list),"Allocate recording command buffer failed");
        lane.lists.push_back(list);
    }
    return lane.lists[lane.used++];
}
VkCommandBuffer VulkanRenderBackendImpl::worldRecordingCommand(VkCommandBuffer command,bool flush) {
    if(command!=frames[currentFrame].commandBuffer)return command;
    if(flush)flushWorldDrawPackets();
    if(!worldRecordingPass.renderPass)return command;
    if(!worldRecordingInline) {
        worldRecordingInline=allocateWorldRecordingCommand(2);
        VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        inherit.renderPass=worldRecordingPass.renderPass;inherit.framebuffer=worldRecordingPass.framebuffer;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        begin.pInheritanceInfo=&inherit;
        requireRecordingVk(vkBeginCommandBuffer(worldRecordingInline,&begin),"Begin inline secondary failed");
        worldCommandState={};
    }
    return worldRecordingInline;
}
void VulkanRenderBackendImpl::finishWorldRecordingInline() {
    if(!worldRecordingInline)return;
    requireRecordingVk(vkEndCommandBuffer(worldRecordingInline),"End inline secondary failed");
    vkCmdExecuteCommands(frames[currentFrame].commandBuffer,1,&worldRecordingInline);
    worldRecordingInline=VK_NULL_HANDLE;worldCommandState={};
}
void VulkanRenderBackendImpl::beginWorldRecordingRenderPass(VkCommandBuffer command,
    const VkRenderPassBeginInfo* info,VkSubpassContents) {
    flushWorldDrawPackets();
    if(worldRecordingPass.renderPass)throw std::logic_error("Nested native render pass");
    vkCmdBeginRenderPass(command,info,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    worldRecordingPass=*info;
    worldRecordingPass.pClearValues=nullptr;worldRecordingPass.clearValueCount=0;
    worldCommandState={};
}
void VulkanRenderBackendImpl::endWorldRecordingRenderPass(VkCommandBuffer command) {
    flushWorldDrawPackets();
    finishWorldRecordingInline();
    vkCmdEndRenderPass(command);
    worldRecordingPass.renderPass=VK_NULL_HANDLE;
    worldCommandState={};
}
void VulkanRenderBackendImpl::resetWorldRecordingFrame() {
    if(!worldDrawPackets.empty())throw std::logic_error("Unsubmitted Vulkan world batch");
    worldRecordingStats={};
    worldRecordingPass.renderPass=VK_NULL_HANDLE;
    worldRecordingInline=VK_NULL_HANDLE;
    for(auto& lane:worldRecordingFrames[currentFrame]) {
        if(lane.pool)requireRecordingVk(vkResetCommandPool(device,lane.pool,0),"Reset world command pool failed");
        lane.used=0;
    }
}
void VulkanRenderBackendImpl::destroyWorldRecordingFrames() {
    worldDrawPackets.clear();worldRecordingWorkers.reset();
    for(auto& frame:worldRecordingFrames)for(auto& lane:frame) {
        if(lane.pool)vkDestroyCommandPool(device,lane.pool,nullptr);
        lane={};
    }
}
void VulkanRenderBackendImpl::submitWorldDrawPacket(
    const engine::render::vulkan_backend::WorldIndexedDrawPacket& packet) {
    if(!preparedWorldViews.inBatch()) {
        flushWorldDrawPackets();
        engine::render::vulkan_backend::WorldCommandRecorder<> recorder{
            {},worldRecordingCommand(frames[currentFrame].commandBuffer,false),texturedPipelineLayout,worldCommandState,worldCommandCounters};
        recorder.record(packet);++worldRecordingStats.serialPackets;
        return;
    }
    worldDrawPackets.push_back(packet);
    if(worldDrawPackets.size()==engine::render::worldRecordingMaxPackets)flushWorldDrawPackets();
}
void VulkanRenderBackendImpl::flushWorldDrawPackets() {
    using namespace engine::render;
    using namespace engine::render::vulkan_backend;
    if(worldDrawPackets.empty())return;
    const auto setupStart=WorldRecordingWorkers::Clock::now();
    auto& lanes=worldRecordingFrames[currentFrame];
    const auto primary=frames[currentFrame].commandBuffer;
    if(worldDrawPackets.size()<worldRecordingMinPackets || !worldRecordingPass.renderPass ||
       lanes[0].used==worldRecordingListsPerLane) {
        WorldCommandRecorder<> recorder{{},worldRecordingCommand(primary,false),texturedPipelineLayout,worldCommandState,worldCommandCounters};
        for(const auto& packet:worldDrawPackets)recorder.record(packet);
        worldRecordingStats.bindingWrites+=recorder.descriptorWrites;
        worldRecordingStats.bindingReuses+=recorder.descriptorReuses;
        worldRecordingStats.serialPackets+=worldDrawPackets.size();
        ++worldRecordingStats.serialBatches;
        worldRecordingStats.serialMs+=std::chrono::duration<double,std::milli>(WorldRecordingWorkers::Clock::now()-setupStart).count();
        worldDrawPackets.clear();return;
    }
    if(!worldRecordingWorkers)worldRecordingWorkers=std::make_unique<WorldRecordingWorkers>();
    std::array<VkCommandBuffer,2> lists{allocateWorldRecordingCommand(0),allocateWorldRecordingCommand(1)};
    VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inheritance.renderPass=worldRecordingPass.renderPass;inheritance.framebuffer=worldRecordingPass.framebuffer;
    std::array<WorldCommandCounters,2> counters{};
    std::array<std::array<std::uint64_t,2>,2> bindings{};
    const auto runStart=WorldRecordingWorkers::Clock::now();
    WorldRecordingWorkers::RunStats run;
    const auto times=worldRecordingWorkers->run([&](unsigned lane) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        begin.pInheritanceInfo=&inheritance;
        requireRecordingVk(vkBeginCommandBuffer(lists[lane],&begin),"Begin worker command buffer failed");
        WorldCommandState state;
        WorldCommandRecorder<> recorder{{},lists[lane],texturedPipelineLayout,state,counters[lane]};
        for(auto i=worldDrawPackets.size()*lane/2,end=worldDrawPackets.size()*(lane+1)/2;i<end;++i)
            recorder.record(worldDrawPackets[i]);
        bindings[lane]={recorder.descriptorWrites,recorder.descriptorReuses};
        requireRecordingVk(vkEndCommandBuffer(lists[lane]),"End worker command buffer failed");
    },&run);
    const auto executeStart=WorldRecordingWorkers::Clock::now();
    // Preserve preceding inline work, then execute ranges in authored order.
    // The render pass stays open: no new attachment stores/loads or barriers.
    finishWorldRecordingInline();
    vkCmdExecuteCommands(primary,2,lists.data());
    worldCommandState={};
    for(const auto& c:counters) {
        worldCommandCounters.framePipelineBindCalls+=c.framePipelineBindCalls;
        worldCommandCounters.framePipelineBindSkips+=c.framePipelineBindSkips;
        worldCommandCounters.frameDescriptorBindCalls+=c.frameDescriptorBindCalls;
        worldCommandCounters.frameDescriptorBindSkips+=c.frameDescriptorBindSkips;
        worldCommandCounters.frameVertexBufferBindCalls+=c.frameVertexBufferBindCalls;
        worldCommandCounters.frameVertexBufferBindSkips+=c.frameVertexBufferBindSkips;
        worldCommandCounters.frameIndexBufferBindCalls+=c.frameIndexBufferBindCalls;
        worldCommandCounters.frameIndexBufferBindSkips+=c.frameIndexBufferBindSkips;
        worldCommandCounters.frameViewportUpdates+=c.frameViewportUpdates;
        worldCommandCounters.frameViewportSkips+=c.frameViewportSkips;
    }
    for(const auto& count:bindings) {
        worldRecordingStats.bindingWrites+=count[0];
        worldRecordingStats.bindingReuses+=count[1];
    }
    worldRecordingStats.add(worldDrawPackets.size(),times);
    worldRecordingStats.addRun(run,
        std::chrono::duration<double,std::milli>(runStart-setupStart).count(),
        std::chrono::duration<double,std::milli>(WorldRecordingWorkers::Clock::now()-executeStart).count());
    worldDrawPackets.clear();
}
