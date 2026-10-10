#pragma once
#include <cstdint>
#include <optional>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <time.h>
#endif

namespace engine {
// Diagnostic CPU consumption of the calling thread, excluding waits. Resolution
// is platform-dependent; short spans may legitimately report zero CPU time.
inline std::optional<std::uint64_t> currentThreadCpuNanoseconds() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return std::nullopt;
    const auto ticks = [](const FILETIME& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) * 100;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) return std::nullopt;
    return static_cast<std::uint64_t>(time.tv_sec) * 1000000000 + static_cast<std::uint64_t>(time.tv_nsec);
#else
    return std::nullopt;
#endif
}
} // namespace engine
