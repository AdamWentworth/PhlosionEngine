#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace engine::render {
struct WeatherLoop {
    double duration=1,begin=0,end=1;
    void validate() const {
        if(!std::isfinite(duration)||!std::isfinite(begin)||!std::isfinite(end)||
            begin<0||end<=begin||duration<end||duration>86400)
            throw std::invalid_argument("Invalid weather effect loop");
    }
};
// A persistent effect owns this clock. Ending weather breaks its loop without
// discarding the authored tail or the already living particles.
class WeatherTimeline {
public:
    explicit WeatherTimeline(WeatherLoop loop={}) : loop_(loop) {loop.validate();}
    void start() {time_=0;active_=true;looping_=true;++generation_;}
    void resume() {
        if(!generation_){start();return;}
        if(time_>=loop_.end)time_=loop_.begin;
        active_=looping_=true;
    }
    void finish() {looping_=false;}
    void reset() {time_=0;active_=looping_=false;++generation_;}
    void advance(double seconds,bool playing=true) {
        if(!std::isfinite(seconds)||seconds<0)throw std::invalid_argument("Invalid effect delta");
        if(!active_||!playing||seconds==0)return;
        if(looping_&&seconds>=std::max(0.,loop_.end-time_)) {
            const double before=std::max(0.,loop_.end-time_);
            time_=loop_.begin+std::fmod(seconds-before,loop_.end-loop_.begin);
        } else {
            time_+=std::min(seconds,loop_.duration-time_);
            if(time_>=loop_.duration)active_=false;
        }
    }
    double seconds() const {return time_;}
    double progress() const {return time_/loop_.duration;}
    bool active() const {return active_;}
    bool looping() const {return active_&&looping_;}
    std::uint64_t generation() const {return generation_;}
private:
    WeatherLoop loop_;
    double time_=0;
    std::uint64_t generation_=0;
    bool active_=false,looping_=false;
};
}
