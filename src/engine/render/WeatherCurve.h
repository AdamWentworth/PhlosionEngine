#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <stdexcept>
#include <vector>
namespace engine::render {
enum class WeatherInterpolation { Constant, Linear, Quadratic, Cubic };
struct WeatherKnot { float time=0;std::array<float,4> value{}; };
class WeatherCurve {
public:
    WeatherCurve()=default;
    WeatherCurve(std::vector<WeatherKnot> knots,WeatherInterpolation mode=WeatherInterpolation::Linear)
        : knots_(std::move(knots)),mode_(mode) {
        if(knots_.empty()||knots_.size()>4096||unsigned(mode)>unsigned(WeatherInterpolation::Cubic))
            throw std::invalid_argument("Invalid weather curve");
        for(std::size_t i=0;i<knots_.size();++i) {
            const auto& k=knots_[i];
            if(!std::isfinite(k.time)||(i&&k.time<=knots_[i-1].time))
                throw std::invalid_argument("Weather curve times must increase");
            for(float v:k.value)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite weather curve value");
        }
    }
    std::array<float,4> sample(float time) const {
        if(!std::isfinite(time)||knots_.empty())throw std::invalid_argument("Invalid weather curve sample");
        time=std::clamp(time,knots_.front().time,knots_.back().time);
        const auto upper=std::upper_bound(knots_.begin(),knots_.end(),time,
            [](float t,const WeatherKnot& k){return t<k.time;});
        std::size_t first=upper==knots_.begin()?0:std::size_t(upper-knots_.begin()-1);
        if(mode_==WeatherInterpolation::Constant)return knots_[first].value;
        const std::size_t degree=mode_==WeatherInterpolation::Quadratic?2:mode_==WeatherInterpolation::Cubic?3:1;
        first-=first%degree;
        std::array<std::array<float,4>,4> control{};
        for(std::size_t i=0;i<=degree;++i)control[i]=knots_[std::min(first+i,knots_.size()-1)].value;
        const float begin=knots_[first].time,end=knots_[std::min(first+degree,knots_.size()-1)].time;
        const float t=end>begin?(time-begin)/(end-begin):0;
        // Authored Bezier knots are grouped control points, not Catmull-Rom samples.
        for(std::size_t level=degree;level;--level)
            for(std::size_t i=0;i<level;++i)
                for(unsigned c=0;c<4;++c)control[i][c]=std::lerp(control[i][c],control[i+1][c],t);
        return control[0];
    }
    bool empty() const {return knots_.empty();}
private:
    std::vector<WeatherKnot> knots_;
    WeatherInterpolation mode_=WeatherInterpolation::Linear;
};
}
