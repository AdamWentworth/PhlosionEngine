// src/engine/core/ecs/Scheduler.h

#pragma once
#include "engine/core/ecs/ISystem.h"
#include <chrono>
#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace engine::ecs {

// Minimal phased scheduler: ordered list of systems per phase.
// Game owns which systems exist + ordering; engine provides the mechanism.
class Scheduler {
public:
    enum class Phase : int {
        PreUpdate = 0,
        Update    = 1,
        PostUpdate= 2,
        Count     = 3,
    };

    void add(std::unique_ptr<ISystem> sys) { add(std::move(sys), Phase::Update); }

    void add(std::unique_ptr<ISystem> sys, Phase phase) {
        systemsByPhase_[static_cast<std::size_t>(phase)].push_back(std::move(sys));
    }

    void tick(World& world, float dt) {
        tickPhase(Phase::PreUpdate, world, dt);
        tickPhase(Phase::Update, world, dt);
        tickPhase(Phase::PostUpdate, world, dt);
    }

    void tickPhase(Phase phase, World& world, float dt) {
        auto& bucket = systemsByPhase_[static_cast<std::size_t>(phase)];
        for (auto& s : bucket) s->update(world, dt);
    }

    template <typename Observer>
    void tickPhaseObserved(Phase phase, World& world, float dt, Observer&& observer) {
        using Clock = std::chrono::high_resolution_clock;
        auto& bucket = systemsByPhase_[static_cast<std::size_t>(phase)];
        for (auto& s : bucket) {
            const auto start = Clock::now();
            s->update(world, dt);
            const float elapsedMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            observer(*s, elapsedMs);
        }
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const auto& bucket : systemsByPhase_) total += bucket.size();
        return total;
    }

    void clear() {
        for (auto& bucket : systemsByPhase_) bucket.clear();
    }

private:
    static constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::Count);
    std::array<std::vector<std::unique_ptr<ISystem>>, kPhaseCount> systemsByPhase_;
};

} // namespace engine::ecs
