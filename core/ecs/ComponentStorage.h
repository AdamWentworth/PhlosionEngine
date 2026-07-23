// src/engine/core/ecs/ComponentStorage.h

#pragma once
#include "engine/core/ecs/Entity.h"
#include <unordered_map>
#include <utility>
#include <cstdint>

namespace engine::ecs {

// Simple per-component storage keyed by entity id.
// This is not the final high-performance ECS; it's a safe starter that keeps engine_core headless.
template <class T>
class ComponentStorage {
public:
    template <class... Args>
    T& emplace(Entity e, Args&&... args) {
        return data_.try_emplace(e.id, T{std::forward<Args>(args)...}).first->second;
    }

    bool has(Entity e) const { return data_.find(e.id) != data_.end(); }

    T* get(Entity e) {
        auto it = data_.find(e.id);
        return it == data_.end() ? nullptr : &it->second;
    }

    const T* get(Entity e) const {
        auto it = data_.find(e.id);
        return it == data_.end() ? nullptr : &it->second;
    }

    void remove(Entity e) { data_.erase(e.id); }

    // Used by World::destroy() to remove components without requiring a generation.
    void removeById(std::uint32_t id) { data_.erase(id); }

    // Iteration for simple systems/tests.
    auto& raw() { return data_; }
    const auto& raw() const { return data_; }

private:
    std::unordered_map<std::uint32_t, T> data_;
};

} // namespace engine::ecs
