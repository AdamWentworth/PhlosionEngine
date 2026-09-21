#include "engine/render/VulkanRenderBackend.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>

using namespace engine::render;

WorldResourceScopeId VulkanRenderBackend::createWorldResourceScope(std::uint64_t limit) {
    return impl_->worldScopes.create(limit);
}
bool VulkanRenderBackend::beginWorldResourceScope(WorldResourceScopeId id) {
    if (!impl_->worldScopes.begin(id)) return false;
    impl_->worldSceneMaterialDescriptorSets.clear();
    impl_->worldSceneMaterials.clear();
    return true;
}
bool VulkanRenderBackend::endWorldResourceScope() {
    const bool ended = impl_->worldScopes.end();
    impl_->worldSceneMaterialDescriptorSets.clear();
    impl_->worldSceneMaterials.clear();
    return ended;
}
bool VulkanRenderBackend::requestWorldResourceScopeRelease(WorldResourceScopeId id) {
    return impl_->worldScopes.release(id);
}
bool VulkanRenderBackend::getWorldResourceScopeStats(WorldResourceScopeId id, WorldResourceScopeStats& out) const {
    return impl_->worldScopes.query(id, out);
}
WorldResourceStats VulkanRenderBackend::getWorldResourceStats() const {
    return impl_->worldScopes.stats();
}

bool VulkanRenderBackendImpl::collectWorldResourceScopes() {
    const auto pending = worldScopes.pending();
    if (pending.empty()) return true;
    if (device == VK_NULL_HANDLE || vkDeviceWaitIdle(device) != VK_SUCCESS) {
        for (const auto id : pending)
            worldScopes.fail(id, "Vulkan device completion failed", WorldResourceScopeStatus::GpuWaitFailed);
        return false;
    }
    worldSceneMaterialDescriptorSets.clear();
    worldSceneMaterials.clear();
    resetWorldFrameStateCache();
    std::vector<VkDescriptorSet> retiredSets;
    for (const auto& [key, material] : worldMaterials) {
        (void)key;
        if (std::find(pending.begin(), pending.end(), material.scope) != pending.end())
            retiredSets.push_back(material.descriptorSet);
    }
    if (!retiredSets.empty() && vkFreeDescriptorSets(device, descriptorPool,
            static_cast<std::uint32_t>(retiredSets.size()), retiredSets.data()) != VK_SUCCESS) {
        for (const auto id : pending)
            worldScopes.fail(id, "Vulkan material descriptor release failed", WorldResourceScopeStatus::AllocationFailed);
        return false;
    }
    for (const auto id : pending) {
        // Remove every pointer before destroying the pointed-to material/texture.
        for (auto it = worldMaterials.begin(); it != worldMaterials.end();) {
            auto& material = it->second;
            if (material.scope != id) { ++it; continue; }
            if (material.indexedTableSlot != UINT32_MAX)
                indexedWorldMaterials[material.indexedTableSlot] = nullptr;
            it = worldMaterials.erase(it);
        }
    }
    std::erase(indexedWorldMaterials, nullptr);
    for (std::size_t i = 0; i < indexedWorldMaterials.size(); ++i)
        indexedWorldMaterials[i]->indexedTableSlot = static_cast<std::uint32_t>(i);
    initializeIndexedWorldMaterialSets();
    for (const auto id : pending) {
        for (auto it = cachedWorldMeshes.begin(); it != cachedWorldMeshes.end();) {
            if (it->second.scope != id) { ++it; continue; }
            destroyBuffer(it->second.ownedGeometry);
            it = cachedWorldMeshes.erase(it);
        }
        for (auto it = worldTextures.begin(); it != worldTextures.end();) {
            if (it->second.scope != id) { ++it; continue; }
            destroyTexture(it->second);
            it = worldTextures.erase(it);
        }
        worldScopes.collect(id);
    }
    return true;
}
