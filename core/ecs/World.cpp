// src/engine/core/ecs/World.cpp

#include "engine/core/ecs/World.h"
#include "engine/core/Services.h"

namespace engine::ecs {

World::World(engine::CoreServices* services)
    : services_(services) {}

Entity World::create() {
    std::uint32_t id = 0;
    if (!freeIds_.empty()) {
        id = freeIds_.back();
        freeIds_.pop_back();
    } else {
        id = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
    }
    return Entity{ id, generations_[id] };
}

bool World::alive(Entity e) const {
    if (e.id >= generations_.size()) return false;
    return generations_[e.id] == e.gen;
}

void World::removeAllComponentsFor(std::uint32_t id) {
    for (auto& kv : stores_) {
        kv.second->removeEntity(id);
    }
}

void World::destroyImmediate(Entity e) {
    if (!alive(e)) return;

    // Remove all components first (prevents leaks / stale component presence).
    removeAllComponentsFor(e.id);

    // Invalidate entity (bump generation).
    generations_[e.id] = static_cast<std::uint8_t>(generations_[e.id] + 1);
    freeIds_.push_back(e.id);
}

void World::destroy(Entity e) {
    if (iterationDepth_ > 0) {
        defer([this, e]() { destroyImmediate(e); });
        return;
    }
    destroyImmediate(e);
}

} // namespace engine::ecs
