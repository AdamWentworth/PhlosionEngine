#pragma once
#include "engine/core/Environment.h"
#include <chrono>
#include <iostream>
#include <syncstream>
#include <string>
#include <string_view>

namespace engine::render {
class ShaderPreparationScope {
public:
    ShaderPreparationScope(std::string_view kind, std::string_view name)
        : kind_(kind), name_(name), start_(std::chrono::steady_clock::now()),
          enabled_(engine::env::flagEnabled("PHLOSION_SHADER_PREPARATION_TRACE")) {}
    void cacheHit(bool hit) { hit_ = hit; }
    ~ShaderPreparationScope() {
        if (!enabled_) return;
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        std::osyncstream(std::clog) << "[ShaderPreparation] kind=" << kind_
            << " name=" << name_ << " hit=" << hit_ << " ms=" << ms << '\n';
    }
private:
    std::string kind_, name_;
    std::chrono::steady_clock::time_point start_;
    bool enabled_, hit_ = false;
};
}
