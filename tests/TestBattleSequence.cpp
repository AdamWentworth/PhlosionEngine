#include "BattleSequence.h"

#include <iostream>
#include <stdexcept>

using namespace engine::exploration_fixture;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main() {
    BattleSequence sequence;
    CHECK(sequence.state().active && sequence.state().hudVisible);
    CHECK(sequence.advanceTo(29).empty());
    CHECK(sequence.advance() == std::vector<BattleCue>{BattleCue::CameraBlend});
    CHECK(sequence.advanceTo(59).empty());
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::CameraCut, BattleCue::Gesture}));
    CHECK(sequence.state().gestureClip == 8);
    CHECK(sequence.advanceTo(89).empty());
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::ImpactSound, BattleCue::ImpactEffect}));
    CHECK(sequence.state().effectVisible);
    CHECK(sequence.advanceTo(109) == std::vector<BattleCue>{BattleCue::EffectStop});
    CHECK((sequence.advance() == std::vector<BattleCue>{BattleCue::Damage, BattleCue::Settle}));
    CHECK(sequence.state().health[3] == .6f && sequence.state().gestureClip == 1);
    CHECK(sequence.advanceTo(149).empty());
    CHECK(sequence.advance() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(!sequence.state().active && !sequence.state().hudVisible);
    CHECK(sequence.advance().empty() && sequence.cancel().empty());

    sequence.reset();
    CHECK(sequence.advanceTo(94).size() == 5u);
    CHECK(sequence.cancel() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(sequence.state().cancelled && !sequence.state().effectVisible);
    CHECK(sequence.advance().empty());
    sequence.reset();
    CHECK(sequence.advanceTo(60).size() == 3u);
    CHECK(sequence.skip() == std::vector<BattleCue>{BattleCue::Cleanup});
    CHECK(sequence.state().skipped && sequence.state().health[3] == 1.0f);
    bool rejected = false;
    try { sequence.advanceTo(59); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    std::cout << "Battle sequence contracts PASS\n";
}
