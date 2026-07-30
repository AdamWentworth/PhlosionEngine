// src/engine/core/Log.h
#pragma once
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <string>

#include "engine/core/Environment.h"

namespace engine::log {

// Debug enabled if ENGINE_LOG_DEBUG=1
inline bool debug_enabled() {
    return engine::env::equals("ENGINE_LOG_DEBUG", "1");
}

inline void vprint(FILE* out, const char* level, const char* fmt, std::va_list args) {
    std::fprintf(out, "[%s] ", level);
    std::vfprintf(out, fmt, args);
    std::fprintf(out, "\n");
}

inline void info(const char* fmt, ...) {
    std::va_list args; va_start(args, fmt);
    vprint(stdout, "INFO", fmt, args);
    va_end(args);
}
inline void warn(const char* fmt, ...) {
    std::va_list args; va_start(args, fmt);
    vprint(stdout, "WARN", fmt, args);
    va_end(args);
}
inline void error(const char* fmt, ...) {
    std::va_list args; va_start(args, fmt);
    vprint(stderr, "ERROR", fmt, args);
    va_end(args);
}
inline void debug(const char* fmt, ...) {
    if (!debug_enabled()) return;
    std::va_list args; va_start(args, fmt);
    vprint(stdout, "DEBUG", fmt, args);
    va_end(args);
}

// Convenience overloads for std::string (no formatting).
inline void info(const std::string& msg) { info("%s", msg.c_str()); }
inline void warn(const std::string& msg) { warn("%s", msg.c_str()); }
inline void error(const std::string& msg) { error("%s", msg.c_str()); }
inline void debug(const std::string& msg) { debug("%s", msg.c_str()); }

// Hex formatting helper (lowercase, no 0x prefix).
inline std::string to_hex(unsigned value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

} // namespace engine::log
