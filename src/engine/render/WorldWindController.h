#pragma once
#include "engine/render/WorldWind.h"
#include <cstdint>

namespace engine::render {
struct WorldWindSettings {
    float speed=7, angle=.27094685f, gustStrength=.18f, directionVariation=.35f;
    std::uint32_t seed=417;
};
struct WorldWindSnapshot {
    std::array<float,2> velocity{};
    std::array<double,2> travel{};
    std::array<double,20> phaseTime{};
    std::array<std::array<double,2>,20> phaseDirection{};
    double seconds=0;
    float strength=0,gust=0;
    std::uint64_t generation=0;
};
// Game simulation owns this value. Rendering consumes immutable snapshots.
class WorldWindController {
public:
    explicit WorldWindController(WorldWindSettings settings={});
    void reset(WorldWindSettings settings={});
    // The seed identifies the simulation; reseeding is an explicit reset.
    void transition(WorldWindSettings settings,double seconds);
    void setVelocity(std::array<float,2> velocity);
    void advance(double seconds);
    WorldWindSnapshot snapshot() const;
    std::array<float,2> velocity{};
    std::array<double,2> offset{};
private:
    struct Flow { std::array<double,2> velocity{};double strength=0,gust=0; };
    Flow evaluate(double seconds) const;
    void integrate(WorldWindSnapshot&,double begin,double seconds) const;
    void refresh();
    WorldWindSettings from_{},to_{};
    WorldWindSnapshot integrated_{};
    std::array<float,2> fixedVelocity_{};
    double remainder_=0,transitionBegin_=0,transitionEnd_=0;
    bool fixed_=false;
};
}
