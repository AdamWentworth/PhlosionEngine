#pragma once

#include "CharacterController.h"
#include "CharacterTriggers.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine::collision {

struct CollisionWorldOptions {
    std::uint32_t maxColliders = 4096;
    std::uint32_t maxMeshVertices = 1'000'000;
    std::uint32_t maxMeshTriangles = 1'000'000;
    std::uint32_t maxCharacters = 64;
};

struct StaticColliderDesc {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t layer = 0;
    bool sensor = false;
    std::uint64_t userData = 0;
};

enum class SensorPolicy { Exclude, Include, Only };

struct QueryFilter {
    std::uint32_t layerMask = 0xffffffffu;
    SensorPolicy sensors = SensorPolicy::Exclude;
    ColliderId ignore;
};

struct Capsule {
    glm::vec3 center{0.0f};
    float radius = 0.5f;
    float cylinderHalfHeight = 0.5f;
};

struct Hit {
    ColliderInfo collider;
    float fraction = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool initialOverlap = false; // Capsule sweeps only; positive initial penetration.
};

// Owner-thread API, including destruction. See docs/COLLISION_WORLD.md for
// numeric limits, contact conventions and ownership of the process Jolt runtime.
class CollisionWorld {
public:
    explicit CollisionWorld(const CollisionWorldOptions& options = {});
    ~CollisionWorld();
    CollisionWorld(const CollisionWorld&) = delete;
    CollisionWorld& operator=(const CollisionWorld&) = delete;
    CollisionWorld(CollisionWorld&&) = delete;
    CollisionWorld& operator=(CollisionWorld&&) = delete;

    bool addBox(const StaticColliderDesc& desc, const glm::vec3& halfExtents,
                ColliderId& out, std::string* error = nullptr);
    bool addTriangleMesh(const StaticColliderDesc& desc,
                         std::span<const glm::vec3> vertices,
                         std::span<const std::uint32_t> indices,
                         ColliderId& out, std::string* error = nullptr);
    bool remove(ColliderId id);
    void clear();
    bool contains(ColliderId id) const;
    std::size_t colliderCount() const;

    bool createCharacter(const CharacterSettings& settings, const glm::vec3& center,
                         CharacterId& out, std::string* error = nullptr);
    bool removeCharacter(CharacterId id);
    std::size_t characterCount() const;
    bool characterState(CharacterId id, CharacterState& out, std::string* error = nullptr) const;
    bool stepCharacter(CharacterId id, const CharacterInput& input, CharacterState& out,
                       std::string* error = nullptr);
    bool teleportCharacter(CharacterId id, const glm::vec3& center, std::string* error = nullptr);

    // Discrete geometric endpoint membership, not swept crossing events. False preserves out/history.
    bool sampleCharacterTriggers(CharacterId id, CharacterTriggerSample& out,
                                 const TriggerFilter& filter = {}, std::string* error = nullptr);

    // False preserves out and sets error. True publishes a hit/miss and clears error.
    bool rayCast(const glm::vec3& origin, const glm::vec3& displacement,
                 std::optional<Hit>& out, const QueryFilter& filter = {},
                 std::string* error = nullptr) const;
    bool sweepCapsule(const Capsule& capsule, const glm::vec3& displacement,
                      std::optional<Hit>& out, const QueryFilter& filter = {},
                      std::string* error = nullptr) const;
    bool overlapCapsule(const Capsule& capsule, std::vector<ColliderInfo>& out,
                        const QueryFilter& filter = {}, std::string* error = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::collision
