// src/engine/core/ecs/Entity.h

#pragma once
#include <cstdint>

namespace engine::ecs {

// 32-bit entity id with 8-bit generation.
struct Entity {
    std::uint32_t id = 0;
    std::uint8_t  gen = 0;

    friend bool operator==(const Entity& a, const Entity& b) {
        return a.id == b.id && a.gen == b.gen;
    }
    friend bool operator!=(const Entity& a, const Entity& b) { return !(a==b); }
};

} // namespace engine::ecs
