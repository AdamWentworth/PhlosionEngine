#pragma once

#include "engine/utils/Log.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace engine::log {

class Sink {
public:
    explicit Sink(std::string_view tag,
                  std::ostream* infoOut = nullptr,
                  std::ostream* errOut = nullptr);

    void info(std::string_view msg) const;
    void warn(std::string_view msg) const;
    void error(std::string_view msg) const;

    const std::string& tag() const { return tag_; }

private:
    void emit(Level lvl, std::string_view msg) const;

private:
    std::string tag_;
    std::ostream* infoOut_ = nullptr;
    std::ostream* errOut_ = nullptr;
};

} // namespace engine::log
