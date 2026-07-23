// TimeSources.h
#pragma once

#include <chrono>

#include "engine/core/ITimeSource.h"

namespace engine {

// Manual time source for tests or deterministic sims.
class ManualTimeSource final : public ITimeSource {
public:
    ManualTimeSource() = default;
    explicit ManualTimeSource(double startSeconds) : now_(startSeconds) {}

    void reset(double startSeconds = 0.0) { now_ = startSeconds; }
    void advance(double deltaSeconds) { now_ += deltaSeconds; }

    double nowSeconds() const override { return now_; }

private:
    double now_ = 0.0;
};

// Real-time source using steady_clock (non-deterministic).
class SteadyTimeSource final : public ITimeSource {
public:
    SteadyTimeSource() : start_(std::chrono::steady_clock::now()) {}

    double nowSeconds() const override {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

} // namespace engine
