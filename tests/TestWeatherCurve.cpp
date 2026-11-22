#include "engine/render/WeatherCurve.h"
#include "engine/render/WeatherTimeline.h"
#include <iostream>
#include <fstream>
#include <limits>
using namespace engine::render;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad,"Invalid curve accepted");}
int main(int argc,char** argv){try{
    if(argc==2) {
        std::ifstream input(argv[1]);std::size_t count=0,probes=0;input>>count;
        check(input.good()&&count>0&&count<=10000,"Invalid curve fixture count");
        for(std::size_t c=0;c<count;++c) {
            unsigned mode=0;std::size_t keys=0,samples=0;input>>mode>>keys>>samples;
            check(input.good()&&mode<=3&&keys>0&&keys<=4096&&samples>0&&samples<=16384,"Invalid curve fixture header");
            std::vector<WeatherKnot> knots(keys);
            for(auto& k:knots){input>>k.time;for(float& v:k.value)input>>v;}
            WeatherCurve curve(std::move(knots),WeatherInterpolation(mode));
            for(std::size_t i=0;i<samples;++i) {
                float time=0;std::array<double,4> expected{};
                input>>time;for(double& v:expected)input>>v;
                check(bool(input),"Truncated curve fixtures");
                const auto actual=curve.sample(time);
                for(unsigned j=0;j<4;++j) {
                    if(std::abs(actual[j]-expected[j])>5e-5*std::max(1.,std::abs(expected[j]))) {
                        std::cerr<<"curve "<<c<<" sample "<<i<<" channel "<<j<<" time "<<time<<" actual "<<actual[j]<<" expected "<<expected[j]<<'\n';
                        throw std::runtime_error("Independent curve fixture mismatch");
                    }
                }
                ++probes;
            }
        }
        std::cout<<count<<" independent authored curves / "<<probes<<" probes passed\n";
    } else check(argc==1,"Expected optional curve fixture path");

    WeatherCurve linear({{0,{1,2,3,4}},{24,{3,4,5,6}}});
    check(linear.sample(12)==std::array<float,4>{2,3,4,5},"Four-channel HDR interpolation");
    check(linear.sample(-2)==linear.sample(0)&&linear.sample(30)==linear.sample(24),"Endpoint clamp");
    WeatherCurve constant({{0,{1}},{1,{2}},{2,{3}}},WeatherInterpolation::Constant);
    check(constant.sample(.99f)[0]==1&&constant.sample(1)[0]==2&&constant.sample(2)[0]==3,"Constant boundary");
    WeatherCurve cubic({{0,{0}},{.1f,{2}},{.2f,{-1}},{1,{1}},{1.2f,{3}},{1.5f,{4}},{2,{5}}},WeatherInterpolation::Cubic);
    check(std::abs(cubic.sample(.5f)[0]-.5f)<1e-6f,"Grouped cubic polynomial");
    check(std::abs(cubic.sample(1.5f)[0]-3.375f)<1e-6f,"Second cubic segment");
    WeatherCurve quadratic({{0,{0}},{.1f,{2}},{1,{0}}},WeatherInterpolation::Quadratic);
    check(std::abs(quadratic.sample(.5f)[0]-1)<1e-6f,"Quadratic control point");
    rejects([]{WeatherCurve c({},WeatherInterpolation::Linear);});
    rejects([]{WeatherCurve c({{1,{}},{0,{}}});});
    rejects([]{WeatherCurve c({{1,{}},{1,{}}});});
    rejects([&]{linear.sample(std::numeric_limits<float>::infinity());});
    rejects([]{WeatherCurve c(std::vector<WeatherKnot>{{0,{std::numeric_limits<float>::quiet_NaN()}}});});
    WeatherTimeline timeline({16.3441257,5,7});
    timeline.advance(10);check(!timeline.active()&&timeline.seconds()==0,"Inactive effect advanced");
    timeline.start();const auto generation=timeline.generation();
    timeline.advance(6.5);check(timeline.seconds()==6.5,"Effect introduction");
    timeline.advance(3);check(timeline.seconds()==5.5,"Effect loop remainder");
    timeline.advance(100,false);check(timeline.seconds()==5.5,"Paused effect advanced");
    timeline.finish();timeline.advance(3);check(timeline.seconds()==8.5&&timeline.active(),"Effect tail discarded");
    timeline.advance(100);check(!timeline.active()&&timeline.seconds()==16.3441257,"Effect finish");
    timeline.start();check(timeline.generation()!=generation&&timeline.seconds()==0,"Effect restart identity");
    timeline.advance(1e300);check(std::isfinite(timeline.seconds())&&timeline.seconds()>=5&&timeline.seconds()<7,"Large loop update");
    timeline.reset();check(!timeline.active()&&timeline.seconds()==0,"Effect reset");
    rejects([]{WeatherTimeline invalid({4,5,7});});
    rejects([&]{timeline.advance(-1);});
    std::cout<<"Weather curve and timeline contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
