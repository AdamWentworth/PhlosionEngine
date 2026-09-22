#pragma once

#include "CollisionTypes.h"
#include <vector>

namespace engine::collision {

// Independent of the character's movement filter. Only sensors participate.
struct TriggerFilter {
    std::uint32_t layerMask = 0xffffffffu;
    ColliderId ignore;
};

enum class TriggerEventKind { Enter, Stay, Exit };

struct TriggerEvent {
    TriggerEventKind kind = TriggerEventKind::Enter;
    ColliderInfo collider;
};

struct CharacterTriggerSample {
    std::vector<ColliderInfo> overlaps;
    std::vector<TriggerEvent> events;
};

} // namespace engine::collision
