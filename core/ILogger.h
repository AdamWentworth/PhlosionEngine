// src/engine/core/ILogger.h

#pragma once
#include <string_view>
#include <cstdint>

namespace engine {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

struct LogMessage {
    LogLevel level{};
    std::string_view category{};
    std::string_view text{};
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const LogMessage& msg) = 0;
};

} // namespace engine
