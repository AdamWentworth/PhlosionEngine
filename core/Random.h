// Random.h
#pragma once

#include <cstdint>
#include <algorithm>

#include "engine/core/IRandom.h"

namespace engine::random {

inline std::uint32_t nextU32(IRandom& rng) {
    return rng.nextU32();
}

inline float nextFloat01(IRandom& rng) {
    // Use upper 24 bits to build a float in [0,1).
    const std::uint32_t v = nextU32(rng) >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
}

inline int rangeInclusive(IRandom& rng, int minInclusive, int maxInclusive) {
    if (maxInclusive < minInclusive) std::swap(maxInclusive, minInclusive);
    const std::uint32_t span = static_cast<std::uint32_t>(maxInclusive - minInclusive + 1);
    if (span == 0) return minInclusive;
    return static_cast<int>(minInclusive + (nextU32(rng) % span));
}

} // namespace engine::random

namespace engine {

// Simple xorshift RNG (fast, deterministic). Not cryptographically secure.
class XorShift32 final : public IRandom {
public:
    explicit XorShift32(std::uint32_t seed = 0x12345678u) { reseed(seed); }

    void reseed(std::uint32_t seed) {
        state_ = seed ? seed : 0x6D2B79F5u;
    }

    std::uint32_t nextU32() override {
        std::uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

private:
    std::uint32_t state_ = 0x12345678u;
};

} // namespace engine
