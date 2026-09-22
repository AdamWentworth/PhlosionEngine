#pragma once

#include "CollisionTypes.h"
#include <glm/glm.hpp>
#include <optional>

namespace engine::collision {

class CharacterId {
public:
    constexpr CharacterId() = default;
    constexpr explicit operator bool() const { return value_ != 0; }
    auto operator<=>(const CharacterId&) const = default;
private:
    explicit constexpr CharacterId(std::uint64_t value) : value_(value) {}
    std::uint64_t value_ = 0;
    friend class CollisionWorld;
};

// Numeric ranges and contact conventions: docs/CHARACTER_CONTROLLER.md.
struct CharacterSettings {
    float radius = 0.35f;
    float cylinderHalfHeight = 0.55f;
    float walkSpeed = 2.0f;
    float runSpeed = 4.5f;
    float gravity = 9.81f;
    float terminalFallSpeed = 50.0f;
    float maxSlopeDegrees = 45.0f;
    float stepHeight = 0.3f;
    float floorSnapDistance = 0.15f;
    float fixedDeltaSeconds = 1.0f / 60.0f;
    std::uint32_t layerMask = 0xffffffffu;
    ColliderId ignore;
};

struct CharacterInput {
    glm::vec2 move{0.0f}; // World X/Z; magnitude <= 1 preserves analog intent.
    bool run = false;
};

enum class CharacterGroundState { Grounded, Steep, Unsupported, InAir };

struct CharacterState {
    glm::vec3 center{0.0f}; // Geometric capsule center, matching Capsule.
    glm::vec3 velocity{0.0f}; // Actual last-tick displacement / dt, including step/snap/recovery.
    CharacterGroundState groundState = CharacterGroundState::InAir;
    std::optional<ColliderInfo> support; // Copied contact metadata, also on steep/unsupported contacts.
    glm::vec3 groundPoint{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
};

} // namespace engine::collision
