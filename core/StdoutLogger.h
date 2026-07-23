// src/engine/core/StdoutLogger.h

#pragma once
#include "engine/core/ILogger.h"
#include <mutex>
#include <string>

namespace engine {

// Minimal thread-safe stdout logger for transitional wiring.
// No global instance is provided; the composition root owns the object.
class StdoutLogger final : public ILogger {
public:
    void log(const LogMessage& msg) override;

private:
    std::mutex m_;
};

} // namespace engine
