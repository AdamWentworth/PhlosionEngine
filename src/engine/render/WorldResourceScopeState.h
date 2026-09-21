#pragma once

#include "engine/render/IWorldResourceScope.h"

#include <atomic>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render {

inline void addWorldResourceCounts(WorldResourceCounts& a, const WorldResourceCounts& b) {
    a.geometryBuffers += b.geometryBuffers;
    a.textures += b.textures;
    a.materialDescriptors += b.materialDescriptors;
    a.textureDescriptors += b.textureDescriptors;
    a.payloadBytes += b.payloadBytes;
    a.apiAllocationBytes += b.apiAllocationBytes;
    a.apiAllocationBytesEstimated |= b.apiAllocationBytesEstimated;
}

// Native backends commit only successfully created objects to this ledger.
// No GPU operations occur here; collect() is called only after native teardown.
class WorldResourceScopeState {
public:
    static constexpr std::uint64_t maxScopes = 256;
    WorldResourceScopeId create(std::uint64_t limit) {
        if (scopes_.size() >= maxScopes) { ++failures_; return 0; }
        auto id = nextId_.load(std::memory_order_relaxed);
        do {
            if (id == (std::numeric_limits<WorldResourceScopeId>::max)()) { ++failures_; return 0; }
        } while (!nextId_.compare_exchange_weak(id, id + 1, std::memory_order_relaxed));
        WorldResourceScopeStats value;
        value.id = id;
        value.payloadByteLimit = limit;
        try { scopes_.emplace(id, std::move(value)); }
        catch (...) { ++failures_; return 0; }
        return id;
    }
    bool begin(WorldResourceScopeId id) {
        const auto it = scopes_.find(id);
        if (active_ != 0 || it == scopes_.end() ||
            it->second.status != WorldResourceScopeStatus::Ready || it->second.releasePending) return false;
        active_ = id;
        return true;
    }
    bool end() {
        if (active_ == 0) return false;
        active_ = 0;
        return true;
    }
    bool release(WorldResourceScopeId id) {
        const auto it = scopes_.find(id);
        if (it == scopes_.end() || it->second.releasePending) return false;
        it->second.releasePending = true;
        if (it->second.status == WorldResourceScopeStatus::Ready)
            it->second.status = WorldResourceScopeStatus::ReleasePending;
        return true;
    }
    bool query(WorldResourceScopeId id, WorldResourceScopeStats& out) const {
        const auto it = scopes_.find(id);
        if (it == scopes_.end()) { out = {}; return false; }
        out = it->second;
        return true;
    }
    WorldResourceStats stats() const {
        WorldResourceStats out;
        out.liveScopes = scopes_.size();
        out.scopeLimit = maxScopes;
        out.allocated = allocated_;
        out.reclaimed = reclaimed_;
        out.allocationFailures = failures_;
        for (const auto& [id, value] : scopes_) {
            (void)id;
            addWorldResourceCounts(out.retained, value.retained);
            if (value.releasePending) {
                ++out.pendingReleaseScopes;
                addWorldResourceCounts(out.pendingRelease, value.retained);
            }
        }
        return out;
    }
    WorldResourceScopeId active() const { return active_; }
    bool admitted() const {
        if (active_ == 0) return true;
        const auto it = scopes_.find(active_);
        return it != scopes_.end() && !it->second.releasePending &&
            it->second.status == WorldResourceScopeStatus::Ready;
    }
    bool admit(std::uint64_t bytes) {
        if (!admitted()) return false;
        if (active_ == 0) return true;
        auto& value = scopes_.at(active_);
        if (bytes > value.payloadByteLimit - value.retained.payloadBytes) {
            fail("World resource scope payload budget exceeded", WorldResourceScopeStatus::BudgetExceeded);
            return false;
        }
        return true;
    }
    void fail(const char* message, WorldResourceScopeStatus status = WorldResourceScopeStatus::AllocationFailed) {
        fail(active_, message, status);
    }
    void fail(WorldResourceScopeId id, const char* message, WorldResourceScopeStatus status) {
        const auto it = scopes_.find(id);
        if (it == scopes_.end()) return;
        if (it->second.status == WorldResourceScopeStatus::Ready ||
            it->second.status == WorldResourceScopeStatus::ReleasePending) {
            it->second.status = status;
            it->second.error = message;
            ++failures_;
        }
    }
    void commit(const WorldResourceCounts& resources) {
        if (active_ == 0) return;
        addWorldResourceCounts(scopes_.at(active_).retained, resources);
        addWorldResourceCounts(allocated_, resources);
    }
    // A native cache can also invalidate an object while its scope stays live
    // (for example GL mesh VAOs during material pipeline replacement).
    void reclaim(WorldResourceScopeId id, const WorldResourceCounts& resources) {
        const auto it = scopes_.find(id);
        if (it == scopes_.end()) return;
        auto& retained = it->second.retained;
        retained.geometryBuffers -= resources.geometryBuffers;
        retained.textures -= resources.textures;
        retained.materialDescriptors -= resources.materialDescriptors;
        retained.textureDescriptors -= resources.textureDescriptors;
        retained.payloadBytes -= resources.payloadBytes;
        retained.apiAllocationBytes -= resources.apiAllocationBytes;
        addWorldResourceCounts(reclaimed_, resources);
    }
    std::vector<WorldResourceScopeId> pending() const {
        std::vector<WorldResourceScopeId> ids;
        for (const auto& [id, value] : scopes_)
            if (value.releasePending) ids.push_back(id);
        return ids;
    }
    void collect(WorldResourceScopeId id) {
        const auto it = scopes_.find(id);
        if (it == scopes_.end()) return;
        addWorldResourceCounts(reclaimed_, it->second.retained);
        scopes_.erase(it);
        // A forgotten end must not cause later allocations to become unowned.
        // The stale active ID continues to reject admission until end().
    }
    void reset() {
        for (const auto& [id, value] : scopes_) {
            (void)id;
            addWorldResourceCounts(reclaimed_, value.retained);
        }
        scopes_.clear();
        active_ = 0;
    }
    std::string key(std::string_view legacy) const {
        if (active_ == 0) return std::string(legacy);
        // C-string legacy identities cannot begin with this embedded NUL.
        return std::string(1, '\0') + std::to_string(active_) + ':' + std::string(legacy);
    }
    class SharedResources {
    public:
        explicit SharedResources(WorldResourceScopeState& state) : state_(state), saved_(state.active_) {
            state_.active_ = 0;
        }
        ~SharedResources() { state_.active_ = saved_; }
    private:
        WorldResourceScopeState& state_;
        WorldResourceScopeId saved_;
    };
    class AllocationAttempt {
    public:
        explicit AllocationAttempt(WorldResourceScopeState& state) : state_(state) {}
        ~AllocationAttempt() { if (!succeeded_) state_.fail("Native world resource allocation failed"); }
        void success() { succeeded_ = true; }
    private:
        WorldResourceScopeState& state_;
        bool succeeded_ = false;
    };
private:
    inline static std::atomic<WorldResourceScopeId> nextId_{1};
    WorldResourceScopeId active_ = 0;
    std::unordered_map<WorldResourceScopeId, WorldResourceScopeStats> scopes_;
    WorldResourceCounts allocated_;
    WorldResourceCounts reclaimed_;
    std::uint64_t failures_ = 0;
};

inline bool worldGeometryPayload(std::size_t vertices, std::size_t stride,
                                 std::size_t indices, std::uint64_t& out) {
    constexpr auto max = (std::numeric_limits<std::uint64_t>::max)();
    if (stride == 0 || vertices > max / stride || indices > max / 4) return false;
    const auto v = vertices * stride;
    const auto i = indices * 4;
    if (v > max - i) return false;
    out = v + i;
    return true;
}

inline bool worldTexturePayload(int width, int height, std::uint32_t levels,
                                std::uint64_t& out, std::uint32_t bytesPerPixel = 4) {
    out = 0;
    if (width <= 0 || height <= 0 || levels == 0 || levels > 32 || bytesPerPixel == 0) return false;
    auto w = static_cast<std::uint64_t>(width);
    auto h = static_cast<std::uint64_t>(height);
    constexpr auto max = (std::numeric_limits<std::uint64_t>::max)();
    for (std::uint32_t level = 0; level < levels; ++level) {
        if (w > max / h || w * h > max / bytesPerPixel || out > max - w * h * bytesPerPixel) return false;
        out += w * h * bytesPerPixel;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return true;
}

template<class Mip>
inline bool worldTextureMipPayload(int width, int height, const Mip* mips,
    std::uint32_t count, bool generate, std::uint64_t& out) {
    std::uint32_t levels = 1;
    if (mips && count > 0) {
        int w = width;
        int h = height;
        if (count > 32) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!mips[i].rgba || mips[i].width != w || mips[i].height != h) return false;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        levels = count;
    } else if (generate) {
        for (int d = width > height ? width : height; d > 1; d /= 2) ++levels;
    }
    return worldTexturePayload(width, height, levels, out);
}

} // namespace engine::render
