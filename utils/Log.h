// src/engine/utils/Log.h
#pragma once

#include <string>
#include <string_view>

namespace engine::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

// Set minimum level to print (default: Info).
void setMinLevel(Level lvl);
Level getMinLevel();

// Optional: route output to a file (append). Empty string disables file output.
bool setLogFile(const std::string& path);

// Log a message. Thread-safe.
void write(Level lvl, std::string_view tag, std::string_view msg);

// Backwards-compatible overloads.
inline void write(Level lvl, const std::string& msg) { write(lvl, "LOG", msg); }
inline void write(Level lvl, const char* msg) { write(lvl, "LOG", msg ? msg : ""); }

} // namespace engine::log

// Lightweight macros (avoid fmt dependency).
#define LOG_DEBUG(MSG)   ::engine::log::write(::engine::log::Level::Debug, "LOG", (MSG))
#define LOG_INFO(MSG)    ::engine::log::write(::engine::log::Level::Info,  "LOG", (MSG))
#define LOG_WARN(MSG)    ::engine::log::write(::engine::log::Level::Warn,  "LOG", (MSG))
#define LOG_ERROR(MSG)   ::engine::log::write(::engine::log::Level::Error, "LOG", (MSG))

#define LOG_DEBUG_T(TAG, MSG) ::engine::log::write(::engine::log::Level::Debug, (TAG), (MSG))
#define LOG_INFO_T(TAG, MSG)  ::engine::log::write(::engine::log::Level::Info,  (TAG), (MSG))
#define LOG_WARN_T(TAG, MSG)  ::engine::log::write(::engine::log::Level::Warn,  (TAG), (MSG))
#define LOG_ERROR_T(TAG, MSG) ::engine::log::write(::engine::log::Level::Error, (TAG), (MSG))
