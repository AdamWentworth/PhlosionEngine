#include "engine/utils/LogSink.h"

#include <ostream>

namespace engine::log {

Sink::Sink(std::string_view tag,
           std::ostream* infoOut,
           std::ostream* errOut)
    : tag_(tag.empty() ? "LOG" : std::string(tag))
    , infoOut_(infoOut)
    , errOut_(errOut) {}

void Sink::info(std::string_view msg) const {
    emit(Level::Info, msg);
}

void Sink::warn(std::string_view msg) const {
    emit(Level::Warn, msg);
}

void Sink::error(std::string_view msg) const {
    emit(Level::Error, msg);
}

void Sink::emit(Level lvl, std::string_view msg) const {
    if (msg.empty()) return;

    std::string_view normalized = msg;
    while (!normalized.empty() &&
           (normalized.back() == '\n' || normalized.back() == '\r')) {
        normalized.remove_suffix(1);
    }
    if (normalized.empty()) return;

    std::ostream* target = nullptr;
    if (lvl == Level::Warn || lvl == Level::Error) {
        target = errOut_ ? errOut_ : infoOut_;
    } else {
        target = infoOut_ ? infoOut_ : errOut_;
    }

    if (target) {
        (*target) << normalized << "\n";
        return;
    }

    engine::log::write(lvl, tag_, normalized);
}

} // namespace engine::log
