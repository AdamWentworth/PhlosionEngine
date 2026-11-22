#include "engine/render/RenderPhaseProfile.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
using namespace engine::render;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void configure(const std::string& path) {
#if defined(_WIN32)
    _putenv_s("PHLOSION_RENDER_PROFILE_PATH",path.c_str());
#else
    if(path.empty())unsetenv("PHLOSION_RENDER_PROFILE_PATH");
    else setenv("PHLOSION_RENDER_PROFILE_PATH",path.c_str(),1);
#endif
}
void configureTotals(bool enabled) {
#if defined(_WIN32)
    _putenv_s("PHLOSION_RENDER_PROFILE_TOTALS_ONLY",enabled?"1":"0");
#else
    setenv("PHLOSION_RENDER_PROFILE_TOTALS_ONLY",enabled?"1":"0",1);
#endif
}
int main() {
 try {
    configure("");configureTotals(false);
    RenderPhaseProfile disabled;RenderPhaseProfile::Frame frame;
    check(disabled.begin(frame,"ignored")==-1 && disabled.queryCount(frame)==2,"Disabled capture changed query count");
    const auto path=std::filesystem::temp_directory_path()/("phlosion-phase-"+std::to_string(RenderPhaseProfile::Clock::now().time_since_epoch().count())+".json");
    configure(path.string());
    RenderPhaseProfile profile;
    check(profile.detailed() && !disabled.detailed(),"Detailed diagnostic eligibility differs from capture mode");
    profile.start(frame,42,RenderPhaseProfile::Clock::now());
    const auto outer=profile.begin(frame,"scene"),inner=profile.begin(frame,"rain");
    check(profile.end(frame,inner) && !profile.end(frame,inner),"Duplicate phase end accepted");
    check(profile.end(frame,outer),"Parent phase did not close");
    check(profile.queryCount(frame)==6,"Query range does not match issued spans");
    std::array<std::uint64_t,RenderPhaseProfile::queryCapacity> ticks{};
    ticks[0]=100;ticks[1]=1100;ticks[2]=200;ticks[3]=1000;ticks[4]=400;ticks[5]=600;
    profile.finishCpu(frame,2.5);
    profile.collect(frame,ticks.data(),1000);
    profile.collect(frame,ticks.data(),1000);
    profile.start(frame,45,RenderPhaseProfile::Clock::now());
    const auto cpu=profile.begin(frame,"prepare",false);
    check(frame.spans[cpu].query==-1 && profile.queryCount(frame)==2,"CPU scope allocated GPU queries");
    const auto gpu=profile.begin(frame,"submit");
    const auto nestedCpu=profile.begin(frame,"record",false);
    check(profile.end(frame,nestedCpu) && profile.end(frame,gpu) && profile.end(frame,cpu),"Mixed phase closure failed");
    check(profile.queryCount(frame)==4 && frame.spans[gpu].query==2,"GPU queries have CPU holes");
    profile.collect(frame,ticks.data(),1000);
    profile.start(frame,43,RenderPhaseProfile::Clock::now());
    for(unsigned i=0;i<RenderPhaseProfile::maxSpans;++i) {auto id=profile.begin(frame,"bounded");check(id>=0,"Premature overflow");profile.end(frame,id);}
    check(profile.begin(frame,"overflow")==-1 && frame.dropped==1,"Overflow did not fail closed");
    check(profile.queryCount(frame)==RenderPhaseProfile::queryCapacity,"Overflow escaped query pool");
    // Invalid GPU data must not become a misleading negative/huge duration.
    ticks[1]=99;profile.collect(frame,ticks.data(),1000);
    profile.start(frame,44,RenderPhaseProfile::Clock::now());
    check(frame.count==0 && frame.dropped==0 && frame.active==-1,"Stale metadata survived slot reuse");
    profile.flush("test");
    std::ifstream in(path);const auto data=nlohmann::json::parse(in);
    check(data["invalid_frames"]==1 && data["frames"].size()==2,"Invalid or duplicated frame published");
    const auto& record=data["frames"][0];
    check(record["frame"]==42 && record["gpu_ms"]==1.0,"Timestamp frequency conversion failed");
    check(record["spans"][1]["parent"]==0 && record["spans"][1]["gpu_ms"]==0.2,"Nested timing lost identity");
    check(record["present_ms"]==2.5,"CPU presentation duration lost");
    const auto& mixed=data["frames"][1]["spans"];
    check(!mixed[0]["gpu_timed"].get<bool>() && mixed[0]["gpu_ms"]==0 &&
          mixed[1]["gpu_timed"].get<bool>() && mixed[1]["gpu_ms"]==0.8 &&
          mixed[2]["parent"]==1 && !mixed[2]["gpu_timed"].get<bool>(),"Mixed CPU/GPU profiling fabricated GPU intervals");
    in.close();std::filesystem::remove(path);
    configureTotals(true);
    RenderPhaseProfile totals;
    check(totals.enabled() && !totals.detailed(),"Totals mode enabled detailed synchronous diagnostics");
    totals.start(frame,46,RenderPhaseProfile::Clock::now());
    check(totals.begin(frame,"ignored")==-1 && totals.queryCount(frame)==2,"Totals mode allocated detailed queries");
    ticks[1]=1100;totals.finishCpu(frame,1.25);totals.collect(frame,ticks.data(),1000);totals.flush("test");
    std::ifstream totalInput(path);const auto totalData=nlohmann::json::parse(totalInput);
    const auto& total=totalData["frames"][0];
    check(totalData["frames"].size()==1 && total["gpu_ms"]==1.0 && total["present_ms"]==1.25 &&
          total["spans"].empty() && total["cpu_frame_ms"].get<double>()>=0,"Totals mode lost complete CPU/GPU frame timing");
    totalInput.close();std::filesystem::remove(path);configure("");configureTotals(false);
    std::cout<<"Render phase profile contracts passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
