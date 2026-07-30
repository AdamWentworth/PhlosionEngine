// IRandom.h
#pragma once

#include <cstdint>

namespace engine {

// Deterministic RNG interface (injectable for tests/repro).
class IRandom {
public:
    virtual ~IRandom() = default;
    virtual std::uint32_t nextU32() = 0;
};

} // namespace engine
