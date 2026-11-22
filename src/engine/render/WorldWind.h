#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace engine::render {
// Simulation ownership keeps integration independent of rendering/view count.
struct WorldWindState {
    std::array<float,2> velocity{18,5};
    std::array<double,2> offset{};
    void setVelocity(std::array<float,2> value) {
        if(!std::isfinite(value[0]) || !std::isfinite(value[1]) ||
           std::hypot(value[0],value[1])>150.f)
            throw std::invalid_argument("Invalid world wind velocity");
        velocity=value;
    }
    void advance(double seconds) {
        if(!std::isfinite(seconds)||seconds<0)
            throw std::invalid_argument("Invalid world wind duration");
        auto next=offset;
        for(unsigned i=0;i<2;++i) {
            // Preserve normal history; bound enormous authoring/offline time
            // steps before multiplication can overflow. 1.4 trillion metres
            // is a common period of the current rain consumers.
            constexpr double period=1400000000000.;
            const double speed=double(velocity[i]);
            const double delta=speed==0?0:std::fmod(seconds,period/std::abs(speed))*speed;
            next[i]=std::fmod(next[i]+delta,period);
        }
        offset=next;
    }
};
}
