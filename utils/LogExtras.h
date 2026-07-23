// src/engine/utils/LogExtras.h
#pragma once

#include "engine/utils/Log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::log {

class ScopedTimer {
public:
    ScopedTimer(std::string_view tag, std::string_view name, Level lvl = Level::Debug);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string_view m_tag;
    std::string m_name;
    Level m_level;
    std::chrono::steady_clock::time_point m_start;
};

class RateLimiter {
public:
    explicit RateLimiter(std::chrono::milliseconds interval);
    bool shouldLog();

private:
    const std::chrono::milliseconds m_interval;
    std::atomic<int64_t> m_nextAllowedMs{0};
};

} // namespace engine::log

#define LOG_SCOPED_TIMER(TAG, NAME) ::engine::log::ScopedTimer _log_scoped_timer_##__LINE__((TAG), (NAME))
#define LOG_SCOPED_TIMER_LVL(TAG, NAME, LVL) ::engine::log::ScopedTimer _log_scoped_timer_##__LINE__((TAG), (NAME), (LVL))
