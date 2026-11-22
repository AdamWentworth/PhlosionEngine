#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace engine::render {
// Fractional births are integrated once per accepted simulation time. Living
// particles belong to the consumer; changing emission never deletes them.
class ParticleEmission {
public:
    struct Birth {std::uint64_t id;double seconds;};
    std::vector<Birth> advance(double seconds,double rate,std::uint64_t generation,unsigned capacity) {
        if(!std::isfinite(seconds)||seconds<0||seconds>1e9||!std::isfinite(rate)||rate<0||rate>100000||!capacity||capacity>65536)
            throw std::invalid_argument("Particle emission inputs");
        if(generation&&generation_&&generation<generation_)return {};
        if(seconds_<0||generation!=generation_||(generation==0&&seconds<seconds_)){
            seconds_=seconds;generation_=generation;fraction_=0;return {};
        }
        if(seconds<=seconds_)return {};
        const double dt=seconds-seconds_,total=fraction_+dt*rate;
        const auto births=static_cast<std::uint64_t>(std::floor(total+1e-10));
        std::vector<Birth> result;result.reserve(std::min<std::uint64_t>(births,capacity));
        // Bound work after a delayed frame without changing the integrated ID.
        const auto first=births>capacity?births-capacity:0;
        for(auto i=first;i<births;++i)result.push_back({next_+i,seconds_+(double(i)+1-fraction_)/rate});
        next_+=births;fraction_=std::max(0.0,total-double(births));seconds_=seconds;return result;
    }
    double seconds() const {return seconds_;}
private:
    double seconds_=-1,fraction_=0;
    std::uint64_t generation_=0,next_=0;
};
}
