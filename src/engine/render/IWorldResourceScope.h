#pragma once

#include <cstdint>
#include <string>

namespace engine::render {

using WorldResourceScopeId = std::uint64_t;

enum class WorldResourceScopeStatus {
    Ready,
    ReleasePending,
    BudgetExceeded,
    AllocationFailed,
    GpuWaitFailed
};

// Counts describe retained native objects. Payload excludes allocator padding,
// descriptors and driver metadata; API allocation bytes are separate.
struct WorldResourceCounts {
    std::uint64_t geometryBuffers = 0;
    std::uint64_t textures = 0;
    std::uint64_t materialDescriptors = 0;
    std::uint64_t textureDescriptors = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t apiAllocationBytes = 0;
    bool apiAllocationBytesEstimated = false;
};

struct WorldResourceScopeStats {
    WorldResourceScopeId id = 0;
    WorldResourceScopeStatus status = WorldResourceScopeStatus::Ready;
    std::uint64_t payloadByteLimit = 0;
    bool releasePending = false;
    WorldResourceCounts retained;
    std::string error;
};

struct WorldResourceStats {
    std::uint64_t liveScopes = 0;
    std::uint64_t pendingReleaseScopes = 0;
    std::uint64_t scopeLimit = 256;
    WorldResourceCounts retained;
    WorldResourceCounts pendingRelease;
    WorldResourceCounts allocated;
    WorldResourceCounts reclaimed;
    std::uint64_t allocationFailures = 0;
};

} // namespace engine::render

// Optional, owner-thread-only interface: dynamic_cast from IRenderBackend.
// Zero denotes legacy unowned resources and is never a valid managed scope.
// Release closes admission immediately. Collection at the next beginFrame may
// block for GPU completion; requesting release during a draw callback is safe.
class IWorldResourceScope {
public:
    virtual ~IWorldResourceScope() = default;
    virtual engine::render::WorldResourceScopeId createWorldResourceScope(
        std::uint64_t payloadByteLimit) = 0;
    virtual bool beginWorldResourceScope(engine::render::WorldResourceScopeId id) = 0;
    virtual bool endWorldResourceScope() = 0;
    virtual bool requestWorldResourceScopeRelease(engine::render::WorldResourceScopeId id) = 0;
    virtual bool getWorldResourceScopeStats(
        engine::render::WorldResourceScopeId id,
        engine::render::WorldResourceScopeStats& out) const = 0;
    virtual engine::render::WorldResourceStats getWorldResourceStats() const = 0;
};
