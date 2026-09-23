#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace engine::exploration_fixture {

enum class BattleCue : std::uint8_t {
    CameraBlend,
    CameraCut,
    Gesture,
    ImpactSound,
    ImpactEffect,
    EffectStop,
    Damage,
    Settle,
    Cleanup,
};

struct BattleSequenceState {
    int tick = 0;
    bool active = true;
    bool cancelled = false;
    bool skipped = false;
    bool hudVisible = true;
    bool effectVisible = false;
    int gestureClip = 1;
    std::array<float, 4> health{1.0f, 1.0f, 1.0f, 1.0f};
};

// An authored, asset-independent encounter demonstration. It emits cues once
// on fixed ticks; the caller binds them to camera, animation, audio and VFX.
class BattleSequence {
public:
    std::vector<BattleCue> advance();
    std::vector<BattleCue> advanceTo(int tick);
    std::vector<BattleCue> cancel();
    std::vector<BattleCue> skip();
    void reset();
    const BattleSequenceState& state() const noexcept { return state_; }

private:
    BattleSequenceState state_;
};

} // namespace engine::exploration_fixture
