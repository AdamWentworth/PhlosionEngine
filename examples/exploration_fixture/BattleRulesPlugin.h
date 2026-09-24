#pragma once

#include <cstdint>

namespace engine::exploration_fixture {

inline constexpr std::uint32_t battleRulesPluginAbi = 1;

struct BattleRulesPluginApi {
    std::uint32_t abi = 0;
    std::uint32_t size = 0;
    void* context = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    void (*reset)(void*) noexcept = nullptr;
    void (*navigate)(void*, int) noexcept = nullptr;
    void (*confirm)(void*) noexcept = nullptr;
    bool (*back)(void*) noexcept = nullptr;
    void (*select)(void*, int) noexcept = nullptr;
    void (*step)(void*) noexcept = nullptr;
    void (*abort)(void*) noexcept = nullptr;
    bool (*complete)(void*) noexcept = nullptr;
    const char* (*stateJson)(void*) noexcept = nullptr;
    const char* (*lastError)(void*) noexcept = nullptr;
};

using CreateBattleRulesPlugin = bool (*)(std::uint32_t, const char*,
                                           BattleRulesPluginApi*) noexcept;

} // namespace engine::exploration_fixture
