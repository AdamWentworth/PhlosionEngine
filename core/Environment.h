#pragma once

#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace engine::env {

inline std::optional<std::string> get(const char* name) {
    if (name == nullptr || *name == '\0') return std::nullopt;

#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> holder(raw, &std::free);
    std::string value(holder.get());
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) return std::nullopt;
    std::string value(raw);
#endif

    if (value.empty()) return std::nullopt;
    return value;
}

inline bool equals(const char* name, std::string_view expected) {
    const auto value = get(name);
    return value.has_value() && std::string_view(*value) == expected;
}

inline bool truthyNonZero(const char* name) {
    const auto value = get(name);
    return value.has_value() && *value != "0";
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ac = static_cast<unsigned char>(a[i]);
        const unsigned char bc = static_cast<unsigned char>(b[i]);
        if (std::tolower(ac) != std::tolower(bc)) return false;
    }
    return true;
}

inline bool flagEnabled(const char* name) {
    const auto value = get(name);
    if (!value.has_value()) return false;
    const std::string_view s(*value);
    return s == "1" || iequals(s, "true") || iequals(s, "yes") || iequals(s, "on");
}

} // namespace engine::env
