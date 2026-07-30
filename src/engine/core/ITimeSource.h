// ITimeSource.h
#pragma once

namespace engine {

// Deterministic time source interface (injectable for tests).
class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    virtual double nowSeconds() const = 0;
};

} // namespace engine
