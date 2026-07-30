// src/engine/core/StdoutLogger.cpp

#include "engine/core/StdoutLogger.h"
#include <iostream>

namespace engine {

static const char* toString(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "LOG";
    }
}

void StdoutLogger::log(const LogMessage& msg) {
    std::lock_guard<std::mutex> lock(m_);
    std::cout << "[" << toString(msg.level) << "]";
    if (!msg.category.empty()) std::cout << "[" << msg.category << "]";
    std::cout << " " << msg.text << "\n";
}

} // namespace engine
