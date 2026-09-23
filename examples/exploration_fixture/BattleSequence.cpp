#include "BattleSequence.h"
#include "engine/render/AnimationBlend.h"

#include <stdexcept>
#include <string>

namespace engine::exploration_fixture {

std::vector<BattleCue> BattleSequence::advance() {
    if (!state_.active) return {};
    ++state_.tick;
    if (state_.tick > 60 && state_.tick < 110) {
        const std::vector<render::animation::TimedAnimationEvent> markers{{"impact", .5f}};
        std::vector<render::animation::FiredAnimationEvent> fired;
        std::string error;
        const double from = static_cast<double>(state_.tick - 61) / 60.0;
        const double to = static_cast<double>(state_.tick - 60) / 60.0;
        if (!render::animation::collectAnimationEvents(markers, 1.0f,
                                                       from, to, fired, &error,
                                                       1u, false, false))
            throw std::runtime_error(error);
        if (!fired.empty()) {
            state_.effectVisible = true;
            return {BattleCue::ImpactSound, BattleCue::ImpactEffect};
        }
    }
    switch (state_.tick) {
    case 30: return {BattleCue::CameraBlend};
    case 60:
        state_.gestureClip = 8;
        return {BattleCue::CameraCut, BattleCue::Gesture};
    case 101:
        state_.effectVisible = false;
        return {BattleCue::EffectStop};
    case 110:
        state_.health[3] = 0.6f;
        state_.gestureClip = 1;
        return {BattleCue::Damage, BattleCue::Settle};
    case 150:
        state_.active = false;
        state_.hudVisible = false;
        return {BattleCue::Cleanup};
    default: return {};
    }
}

std::vector<BattleCue> BattleSequence::advanceTo(int tick) {
    if (tick < state_.tick || tick > 3600)
        throw std::invalid_argument("Battle sequence ticks must advance monotonically and remain bounded");
    std::vector<BattleCue> cues;
    while (state_.tick < tick && state_.active) {
        auto next = advance();
        cues.insert(cues.end(), next.begin(), next.end());
    }
    return cues;
}

std::vector<BattleCue> BattleSequence::cancel() {
    if (!state_.active) return {};
    state_.active = false;
    state_.cancelled = true;
    state_.hudVisible = false;
    state_.effectVisible = false;
    state_.gestureClip = 1;
    return {BattleCue::Cleanup};
}

std::vector<BattleCue> BattleSequence::skip() {
    if (!state_.active) return {};
    state_.active = false;
    state_.skipped = true;
    state_.hudVisible = false;
    state_.effectVisible = false;
    state_.gestureClip = 1;
    return {BattleCue::Cleanup};
}

void BattleSequence::reset() { state_ = BattleSequenceState{}; }

} // namespace engine::exploration_fixture
