#pragma once

#include <compare>
#include <cstdint>

namespace engine::collision {

class ColliderId {
public:
    constexpr ColliderId() = default;
    constexpr explicit operator bool() const { return value_ != 0; }
    auto operator<=>(const ColliderId&) const = default;
private:
    explicit constexpr ColliderId(std::uint64_t value) : value_(value) {}
    std::uint64_t value_ = 0;
    friend class CollisionWorld;
};

struct ColliderInfo {
    ColliderId id;
    std::uint64_t userData = 0;
    std::uint32_t layer = 0;
    bool sensor = false;
};

} // namespace engine::collision
