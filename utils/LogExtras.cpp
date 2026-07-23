// src/engine/utils/LogExtras.cpp
#include "engine/utils/LogExtras.h"

#include <sstream>

namespace engine::log {

ScopedTimer::ScopedTimer(std::string_view tag, std::string_view name, Level lvl)
    : m_tag(tag.empty() ? "LOG" : tag)
    , m_name(name)
    , m_level(lvl)
    , m_start(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    const auto end = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
    std::ostringstream oss;
    oss << m_name << " took " << us << "us";
    engine::log::write(m_level, m_tag, oss.str());
}

RateLimiter::RateLimiter(std::chrono::milliseconds interval) : m_interval(interval) {}

bool RateLimiter::shouldLog() {
    using namespace std::chrono;
    const auto nowMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    auto nextAllowed = m_nextAllowedMs.load(std::memory_order_relaxed);
    if (nowMs < nextAllowed) return false;

    const auto newNext = nowMs + m_interval.count();
    return m_nextAllowedMs.compare_exchange_strong(
        nextAllowed, newNext,
        std::memory_order_relaxed, std::memory_order_relaxed
    );
}

} // namespace engine::log
