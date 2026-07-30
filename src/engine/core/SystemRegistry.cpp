// SystemRegistry.cpp

#include "SystemRegistry.h"

void SystemRegistry::registerSystem(std::shared_ptr<Updatable> system) {
    registerSystem(std::move(system), Phase::Update);
}

void SystemRegistry::registerSystem(std::shared_ptr<Updatable> system, Phase phase) {
    const auto idx = static_cast<size_t>(phase);
    if (idx >= systemsByPhase.size()) return;
    systemsByPhase[idx].push_back(std::move(system));
}

void SystemRegistry::updateAll(float deltaTime) {
    // Deterministic ordering across phases.
    for (size_t p = 0; p < systemsByPhase.size(); ++p) {
        updatePhase(static_cast<Phase>(p), deltaTime);
    }
}

void SystemRegistry::updatePhase(Phase phase, float deltaTime) {
    const auto idx = static_cast<size_t>(phase);
    if (idx >= systemsByPhase.size()) return;
    for (auto& system : systemsByPhase[idx]) {
        system->update(deltaTime);
    }
}

void SystemRegistry::clear() {
    for (auto& bucket : systemsByPhase) bucket.clear();
}
