// SystemRegistry.h
#pragma once

#include "Updatable.h"
#include <array>
#include <memory>
#include <vector>

class SystemRegistry {
public:
    enum class Phase : int {
        PreUpdate = 0,
        Update    = 1,
        PostUpdate= 2,
        Count     = 3,
    };

    SystemRegistry() = default;

    // Backwards-compatible: existing call sites register into Update phase.
    void registerSystem(std::shared_ptr<Updatable> system);

    // New: explicit phase registration.
    void registerSystem(std::shared_ptr<Updatable> system, Phase phase);

    // Runs phases in deterministic order: PreUpdate -> Update -> PostUpdate.
    void updateAll(float deltaTime);
    void updatePhase(Phase phase, float deltaTime);

    void clear();

private:
    static constexpr size_t kPhaseCount = static_cast<size_t>(Phase::Count);
    std::array<std::vector<std::shared_ptr<Updatable>>, kPhaseCount> systemsByPhase;
};
