#pragma once

#include "engine/render/RenderBackendTypes.h"
#include <cstring>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace engine::render {
struct WorldPreparedViewInput {
    std::array<std::byte, sizeof(WorldEnvironmentLighting)> environment{};
    std::array<float, 9> camera{};
    std::array<std::uint8_t, 4> flags{};
    explicit WorldPreparedViewInput(const backend::WorldTextureData* texture, bool composable = false) {
        if (!texture) return;
        std::memcpy(environment.data(), &texture->environment, environment.size());
        camera = {texture->cameraPosX, texture->cameraPosY, texture->cameraPosZ,
            texture->cameraForwardX, texture->cameraForwardY, texture->cameraForwardZ,
            texture->cameraTargetX, texture->cameraTargetY, texture->cameraTargetZ};
        flags = {1, texture->directionalShadowReceiver, texture->alphaMode, std::uint8_t(composable)};
    }
    bool operator==(const WorldPreparedViewInput& other) const noexcept {
        return environment == other.environment && flags == other.flags &&
            std::memcmp(camera.data(), other.camera.data(), sizeof(camera)) == 0;
    }
};

using WorldPreparedViewStats = backend::WorldPreparedViewStats;

// The CPU table is scoped to a native view/pass generation. Ordered batches
// within that view share records; changing a native view input invalidates them.
// Payloads refer to append-only frame allocations. Clearing CPU records never
// reclaims GPU memory; the native frame fence owns that lifetime.
template<class Payload> class WorldPreparedViews {
public:
    using Token = backend::WorldPreparedViewToken;
    static constexpr std::size_t capacity = 4096;
    WorldPreparedViews() = default;
    WorldPreparedViews(const WorldPreparedViews&) = delete;
    WorldPreparedViews& operator=(const WorldPreparedViews&) = delete;
    WorldPreparedViewStats stats{};
    void invalidate() {
        entries_.clear();
        generation_ = nextGeneration();
    }
    void beginFrame() { invalidate(); depth_ = 0; stats = {}; }
    void beginBatch() { ++depth_; }
    void endBatch() {
        if (!depth_) throw std::logic_error("Unbalanced prepared view batch");
        --depth_;
    }
    bool inBatch() const noexcept { return depth_ != 0; }
    template<class Builder> Token prepare(const WorldPreparedViewInput& input, Builder&& builder) {
        ++stats.requests;
        for (std::uint32_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].input == input) {
                ++stats.inputReuses;
                return {this, generation_, i};
            }
        if (entries_.size() == capacity) throw std::runtime_error("Prepared view batch capacity exceeded");
        Payload payload = std::forward<Builder>(builder)();
        entries_.push_back({input, std::move(payload)});
        ++stats.preparations;
        return {this, generation_, std::uint32_t(entries_.size() - 1)};
    }
    const Payload* get(Token token) {
        if (token.owner == this && token.generation == generation_ && token.index < entries_.size()) {
            ++stats.tokenReuses;
            return &entries_[token.index].payload;
        }
        if (token) ++stats.staleTokens;
        return nullptr;
    }
private:
    struct Entry { WorldPreparedViewInput input; Payload payload; };
    std::vector<Entry> entries_;
    static std::uint64_t nextGeneration() {
        static std::atomic<std::uint64_t> next{1};
        const auto value = next.fetch_add(1, std::memory_order_relaxed);
        if (!value) throw std::overflow_error("Prepared view generation exhausted");
        return value;
    }
    std::uint64_t generation_ = nextGeneration();
    unsigned depth_ = 0;
};
// Entry and exit invalidation covers temporary state used by capture/relighting
// helpers as well as persistent pass changes. GPU allocations remain frame-owned.
template<class Invalidate> class WorldPreparedViewBoundary {
    Invalidate invalidate_;
public:
    explicit WorldPreparedViewBoundary(Invalidate invalidate) : invalidate_(std::move(invalidate)) { invalidate_(); }
    ~WorldPreparedViewBoundary() { invalidate_(); }
    WorldPreparedViewBoundary(const WorldPreparedViewBoundary&) = delete;
    WorldPreparedViewBoundary& operator=(const WorldPreparedViewBoundary&) = delete;
};
} // namespace engine::render
