// src/engine/core/ecs/ISystem.h

#pragma once
namespace engine::ecs {

class World;

class ISystem {
public:
    virtual ~ISystem() = default;
    virtual const char* debugName() const { return "system"; }
    virtual void update(World& world, float dt) = 0;
};

} // namespace engine::ecs
