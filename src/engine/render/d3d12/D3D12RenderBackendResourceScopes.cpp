#include "engine/render/D3D12RenderBackend.h"

using namespace engine::render;

WorldResourceScopeId D3D12RenderBackend::createWorldResourceScope(std::uint64_t limit) {
    return worldScopes_.create(limit);
}
bool D3D12RenderBackend::beginWorldResourceScope(WorldResourceScopeId id) {
    if (!worldScopes_.begin(id)) return false;
#if defined(_WIN32)
    worldSceneMaterialBindingCache_.clear();
#endif
    return true;
}
bool D3D12RenderBackend::endWorldResourceScope() {
    const bool ended = worldScopes_.end();
#if defined(_WIN32)
    worldSceneMaterialBindingCache_.clear();
#endif
    return ended;
}
bool D3D12RenderBackend::requestWorldResourceScopeRelease(WorldResourceScopeId id) {
    return worldScopes_.release(id);
}
bool D3D12RenderBackend::getWorldResourceScopeStats(WorldResourceScopeId id, WorldResourceScopeStats& out) const {
    return worldScopes_.query(id, out);
}
WorldResourceStats D3D12RenderBackend::getWorldResourceStats() const {
    return worldScopes_.stats();
}

bool D3D12RenderBackend::collectWorldResourceScopes() {
    const auto pending = worldScopes_.pending();
    if (pending.empty()) return true;
#if defined(_WIN32)
    const auto signal = ++fenceValue_;
    if (!commandQueue_ || !fence_ ||
        FAILED(commandQueue_->Signal(fence_.Get(), signal)) ||
        !waitForFenceValue(signal) || fence_->GetCompletedValue() == UINT64_MAX ||
        fence_->GetCompletedValue() < signal) {
        for (const auto id : pending)
            worldScopes_.fail(id, "D3D12 queue completion failed", WorldResourceScopeStatus::GpuWaitFailed);
        return false;
    }
    worldSceneMaterialBindingCache_.clear();
    for (const auto id : pending) {
        for (auto it = worldMaterialDescriptorBlocks_.begin(); it != worldMaterialDescriptorBlocks_.end();) {
            if (it->first.scope != id) { ++it; continue; }
            freeWorldMaterialBlocks_.push_back(it->second);
            it = worldMaterialDescriptorBlocks_.erase(it);
        }
        for (auto it = cachedWorldMeshes_.begin(); it != cachedWorldMeshes_.end();) {
            if (it->second.scope != id) { ++it; continue; }
            it = cachedWorldMeshes_.erase(it);
        }
        for (auto it = worldTextures_.begin(); it != worldTextures_.end();) {
            if (it->second.scope != id) { ++it; continue; }
            freeWorldTextureDescriptors_.push_back(it->second.descriptorIndex);
            it = worldTextures_.erase(it);
        }
        worldScopes_.collect(id);
    }
    return true;
#else
    return false;
#endif
}
