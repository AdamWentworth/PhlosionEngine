#include "BattleVfxPlugin.h"

#include <SDL_loadso.h>

#include <stdexcept>

namespace engine::exploration_fixture {

BattleVfxPlugin::BattleVfxPlugin(const std::string& path) {
    if (path.empty()) return;
    library_ = SDL_LoadObject(path.c_str());
    if (!library_) throw std::runtime_error("Battle VFX plugin load failed: " + path + ": " + SDL_GetError());
    auto* symbol = SDL_LoadFunction(library_, "phlosionBattleVfxPluginCreate");
    if (!symbol) {
        SDL_UnloadObject(library_);
        library_ = nullptr;
        throw std::runtime_error("Battle VFX plugin lacks phlosionBattleVfxPluginCreate: " + path);
    }
    const auto create = reinterpret_cast<CreateBattleVfxPlugin>(symbol);
    if (!create(battleVfxPluginAbi, &api_) || api_.abi != battleVfxPluginAbi ||
        api_.size != sizeof(BattleVfxPluginApi) || !api_.context ||
        !api_.spawnImpact || !api_.step || !api_.render || !api_.stopAll ||
        !api_.activeCount || !api_.destroy) {
        if (api_.context && api_.destroy) api_.destroy(api_.context);
        SDL_UnloadObject(library_);
        library_ = nullptr;
        throw std::runtime_error("Battle VFX plugin ABI mismatch: " + path);
    }
}

BattleVfxPlugin::~BattleVfxPlugin() {
    if (!library_) return;
    stopAll();
    api_.destroy(api_.context);
    SDL_UnloadObject(library_);
}

void BattleVfxPlugin::spawnImpact(float x, float y, float z) {
    if (library_ && !api_.spawnImpact(api_.context, x, y, z))
        throw std::runtime_error("Battle VFX spawn failed");
}

void BattleVfxPlugin::step(float dt) {
    if (library_ && !api_.step(api_.context, dt))
        throw std::runtime_error("Battle VFX update failed");
}

void BattleVfxPlugin::render(const BattleVfxFrame& frame) const {
    if (library_ && !api_.render(api_.context, &frame))
        throw std::runtime_error("Battle VFX render failed");
}

void BattleVfxPlugin::stopAll() noexcept {
    if (library_) api_.stopAll(api_.context);
}

std::size_t BattleVfxPlugin::activeCount() const noexcept {
    return library_ ? api_.activeCount(api_.context) : 0u;
}

} // namespace engine::exploration_fixture
