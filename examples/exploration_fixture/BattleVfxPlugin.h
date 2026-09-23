#pragma once

#include "engine/render/IRenderBackend.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::exploration_fixture {

inline constexpr std::uint32_t battleVfxPluginAbi = 1;

struct BattleVfxFrame {
    IRenderBackend* renderer = nullptr;
    const float* viewProjection = nullptr;
    float cameraPosition[3]{};
    float cameraRight[3]{};
    float cameraUp[3]{};
    float cameraForward[3]{};
    int width = 0;
    int height = 0;
};

struct BattleVfxPluginApi {
    std::uint32_t abi = 0;
    std::uint32_t size = 0;
    void* context = nullptr;
    bool (*spawnImpact)(void*, float, float, float) noexcept = nullptr;
    bool (*step)(void*, float) noexcept = nullptr;
    bool (*render)(void*, const BattleVfxFrame*) noexcept = nullptr;
    void (*stopAll)(void*) noexcept = nullptr;
    std::size_t (*activeCount)(void*) noexcept = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
};

using CreateBattleVfxPlugin = bool (*)(std::uint32_t, BattleVfxPluginApi*) noexcept;

class BattleVfxPlugin {
public:
    explicit BattleVfxPlugin(const std::string& path);
    ~BattleVfxPlugin();
    BattleVfxPlugin(const BattleVfxPlugin&) = delete;
    BattleVfxPlugin& operator=(const BattleVfxPlugin&) = delete;

    bool available() const noexcept { return library_ != nullptr; }
    void spawnImpact(float x, float y, float z);
    void step(float dt);
    void render(const BattleVfxFrame& frame) const;
    void stopAll() noexcept;
    std::size_t activeCount() const noexcept;

private:
    void* library_ = nullptr;
    BattleVfxPluginApi api_{};
};

} // namespace engine::exploration_fixture
