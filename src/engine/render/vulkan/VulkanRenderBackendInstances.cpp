#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace {

std::uint64_t hashBytes(const void* data, std::size_t size) {
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::byte*>(data);
    std::uint64_t hash = kOffset;
    for (std::size_t i = 0u; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

} // namespace

bool VulkanRenderBackendImpl::uploadWorldSkinPalette(
    const float* matrices,
    std::size_t floatCount,
    std::uint32_t& outBaseMatrixIndex) {
    outBaseMatrixIndex = 0u;
    if (!frameActive || !matrices || floatCount == 0u ||
        floatCount > static_cast<std::size_t>(
            kTransientBytesPerFrame / sizeof(float))) {
        return false;
    }

    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(floatCount * sizeof(float));
    const std::uint64_t hash = hashBytes(
        matrices,
        static_cast<std::size_t>(byteCount));
    const Buffer& transient = frames[currentFrame].transient;
    for (const CachedSkinPalette& cached : frameSkinPalettes) {
        if (cached.hash != hash || cached.size != byteCount ||
            cached.offset > transient.size ||
            cached.size > transient.size - cached.offset) {
            continue;
        }
        const auto* cachedData =
            static_cast<const std::byte*>(transient.mapped) + cached.offset;
        if (std::memcmp(
                cachedData,
                matrices,
                static_cast<std::size_t>(byteCount)) != 0) {
            continue;
        }
        outBaseMatrixIndex = static_cast<std::uint32_t>(
            cached.offset / (sizeof(float) * 16u));
        frameSkinPaletteReuseBytes += byteCount;
        ++frameSkinPaletteReuses;
        return true;
    }

    VkBuffer skinBuffer = VK_NULL_HANDLE;
    VkDeviceSize skinOffset = 0u;
    if (!writeTransient(
            matrices,
            byteCount,
            sizeof(float) * 16u,
            skinBuffer,
            skinOffset) ||
        skinBuffer != transient.buffer) {
        return false;
    }
    const VkDeviceSize baseMatrixIndex = skinOffset / (sizeof(float) * 16u);
    if (baseMatrixIndex >
        static_cast<VkDeviceSize>((std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }
    frameSkinPalettes.push_back({hash, skinOffset, byteCount});
    frameSkinPaletteUploadBytes += byteCount;
    ++frameSkinPaletteUploads;
    outBaseMatrixIndex = static_cast<std::uint32_t>(baseMatrixIndex);
    return true;
}

bool VulkanRenderBackendImpl::prepareWorldInstances(
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    std::uint32_t& outInstanceCount,
    std::uint32_t& outInstanceBaseWordIndex) {
    outInstanceCount = 0u;
    outInstanceBaseWordIndex = 0u;
    if (!frameActive || !instances || instanceCount == 0u ||
        instanceCount > static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }

    namespace vulkan = engine::render::vulkan_backend;
    static thread_local std::vector<vulkan::WorldInstanceState> instanceStates;

    instanceStates.clear();
    instanceStates.reserve(instanceCount);

    for (std::size_t i = 0u; i < instanceCount; ++i) {
        const std::size_t floatCount =
            vulkan::worldInstanceSkinMatrixFloatCount(instances[i]);
        std::uint32_t skinMatrixBaseIndex = 0u;
        if (floatCount > 0u &&
            !uploadWorldSkinPalette(
                instances[i].skinMatrices,
                floatCount,
                skinMatrixBaseIndex)) {
            return false;
        }
        instanceStates.push_back(
            vulkan::makeWorldInstanceState(instances[i], skinMatrixBaseIndex));
    }

    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize instanceOffset = 0u;
    if (!writeTransient(
            instanceStates.data(),
            static_cast<VkDeviceSize>(
                instanceStates.size() * sizeof(vulkan::WorldInstanceState)),
            sizeof(float) * 4u,
            instanceBuffer,
            instanceOffset) ||
        instanceBuffer != frames[currentFrame].transient.buffer) {
        return false;
    }

    const VkDeviceSize baseWordIndex = instanceOffset / (sizeof(float) * 4u);
    // A float in the transform UBO carries this index. The fixed 128 MiB
    // transient buffer keeps it within the exact integer range of float32.
    if (baseWordIndex > 16777216u) return false;
    outInstanceCount = static_cast<std::uint32_t>(instanceCount);
    outInstanceBaseWordIndex = static_cast<std::uint32_t>(baseWordIndex);
    return true;
}
