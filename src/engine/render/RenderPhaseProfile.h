#pragma once
#include "engine/core/Environment.h"
#include "engine/render/IRenderBackendFrame.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>
#include <stdexcept>
#include <vector>

namespace engine::render {
class IRenderPhaseProfile {
public:
    virtual ~IRenderPhaseProfile() = default;
    virtual int beginRenderPhase(const char* name) = 0;
    virtual void endRenderPhase(int token) = 0;
};
class IRenderCpuPhaseProfile {
public:
    virtual ~IRenderCpuPhaseProfile() = default;
    virtual int beginRenderCpuPhase(const char* name) = 0;
    virtual void endRenderCpuPhase(int token) = 0;
};
class RenderCpuPhaseScope {
public:
    RenderCpuPhaseScope(IRenderBackendFrame& renderer,const char* name) {
        static const bool enabled=env::get("PHLOSION_RENDER_PROFILE_PATH").has_value();
        if(enabled) profiler_=dynamic_cast<IRenderCpuPhaseProfile*>(&renderer);
        if(profiler_) token_=profiler_->beginRenderCpuPhase(name);
    }
    ~RenderCpuPhaseScope(){if(profiler_ && token_>=0)profiler_->endRenderCpuPhase(token_);}
    RenderCpuPhaseScope(const RenderCpuPhaseScope&)=delete;
    RenderCpuPhaseScope& operator=(const RenderCpuPhaseScope&)=delete;
private:
    IRenderCpuPhaseProfile* profiler_=nullptr;
    int token_=-1;
};
class RenderPhaseScope {
public:
    RenderPhaseScope(IRenderBackendFrame& renderer,const char* name) {
        static const bool enabled=env::get("PHLOSION_RENDER_PROFILE_PATH").has_value();
        if(enabled) profiler_=dynamic_cast<IRenderPhaseProfile*>(&renderer);
        if(profiler_) token_=profiler_->beginRenderPhase(name);
    }
    ~RenderPhaseScope(){if(profiler_ && token_>=0)profiler_->endRenderPhase(token_);}
    RenderPhaseScope(const RenderPhaseScope&)=delete;
    RenderPhaseScope& operator=(const RenderPhaseScope&)=delete;
private:
    IRenderPhaseProfile* profiler_=nullptr;
    int token_=-1;
};

// Bounded, opt-in capture. GPU results are consumed only after the ordinary
// frame fence; writing is deferred to shutdown. Nested intervals are inclusive.
class RenderPhaseProfile {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr unsigned maxSpans=128,queryCapacity=2+2*maxSpans,maxFrames=2048;
    struct Span {
        std::string name;
        int parent=-1,query=-1;
        Clock::time_point start{};
        double cpuMs=0,gpuMs=0;
        bool ended=false;
    };
    struct Frame {
        std::uint64_t serial=0;
        unsigned count=0,gpuCount=0,dropped=0;
        int active=-1;
        Clock::time_point cpuStart{};
        double cpuMs=0,beginWaitMs=0,presentMs=0,gpuMs=0;
        bool collected=false;
        std::array<Span,maxSpans> spans{};
    };
    RenderPhaseProfile():path_(env::get("PHLOSION_RENDER_PROFILE_PATH").value_or("")) {
        if(enabled()) frames_.reserve(maxFrames);
    }
    bool enabled() const {return !path_.empty();}
    bool detailed() const {return enabled() && spansEnabled_;}
    static double elapsed(Clock::time_point begin) {
        return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
    }
    void start(Frame& f,std::uint64_t serial,Clock::time_point cpuStart) {
        if(!enabled())return;
        f.count=0;f.gpuCount=0;f.dropped=0;f.active=-1;f.serial=serial;f.collected=false;
        f.cpuStart=cpuStart;f.beginWaitMs=elapsed(cpuStart);f.cpuMs=0;f.presentMs=0;
    }
    int begin(Frame& f,const char* name,bool gpu=true) {
        if(!enabled() || !spansEnabled_ || !f.serial)return -1;
        if(f.count==maxSpans){++f.dropped;return -1;}
        const int token=int(f.count++);
        auto& s=f.spans[token];s.name=name;s.parent=f.active;s.start=Clock::now();
        s.query=gpu?int(2+2*f.gpuCount++):-1;
        s.cpuMs=0;s.gpuMs=0;s.ended=false;f.active=token;return token;
    }
    bool end(Frame& f,int token) {
        if(token<0 || unsigned(token)>=f.count)return false;
        auto& s=f.spans[token];
        if(s.ended)return false;
        s.cpuMs=elapsed(s.start);s.ended=true;f.active=s.parent;return true;
    }
    unsigned queryCount(const Frame& f) const {return enabled()?2+2*f.gpuCount:2;}
    void finishCpu(Frame& f,double presentMs) {
        if(enabled()){f.cpuMs=elapsed(f.cpuStart);f.presentMs=presentMs;}
    }
    void collect(Frame& f,const std::uint64_t* ticks,double nsPerTick) {
        if(!enabled() || !f.serial || f.collected)return;
        f.collected=true;
        if(ticks[1]<ticks[0]){++invalid_;return;}
        for(unsigned i=0;i<f.count;++i) {
            auto& s=f.spans[i];
            if(!s.ended){++invalid_;return;}
            if(s.query<0)continue;
            const auto q=unsigned(s.query);
            if(ticks[q+1]<ticks[q]){++invalid_;return;}
            s.gpuMs=double(ticks[q+1]-ticks[q])*nsPerTick/1000000.0;
        }
        f.gpuMs=double(ticks[1]-ticks[0])*nsPerTick/1000000.0;
        if(frames_.size()<maxFrames)frames_.push_back(f);else ++dropped_;
    }
    void flush(const char* api) {
        if(!enabled() || flushed_)return;
        flushed_=true;
        std::ofstream out(path_,std::ios::binary);
        if(!out)throw std::runtime_error("Cannot write render phase profile: "+path_);
        out<<std::setprecision(10);
        out<<"{\"schema\":\"phlosion-render-phases-v1\",\"api\":"<<std::quoted(api)
           <<",\"inclusive\":true,\"invalid_frames\":"<<invalid_<<",\"dropped_frames\":"<<dropped_
           <<",\"frames\":[";
        bool first=true;
        for(const auto& f:frames_) {
            if(!first)out<<',';first=false;
            out<<"{\"frame\":"<<f.serial<<",\"gpu_ms\":"<<f.gpuMs<<",\"cpu_frame_ms\":"<<f.cpuMs
               <<",\"begin_wait_ms\":"<<f.beginWaitMs<<",\"present_ms\":"<<f.presentMs
               <<",\"dropped_spans\":"<<f.dropped<<",\"spans\":[";
            for(unsigned i=0;i<f.count;++i) {
                if(i)out<<',';
                const auto& s=f.spans[i];
                out<<"{\"name\":"<<std::quoted(s.name)<<",\"parent\":"<<s.parent
                   <<",\"gpu_timed\":"<<(s.query>=0?"true":"false")<<",\"gpu_ms\":"<<s.gpuMs<<",\"cpu_ms\":"<<s.cpuMs<<'}';
            }
            out<<"]}";
        }
        out<<"]}\n";
        if(!out)throw std::runtime_error("Render phase profile write failed: "+path_);
    }
private:
    std::string path_;
    std::vector<Frame> frames_;
    unsigned invalid_=0,dropped_=0;
    bool flushed_=false;
    bool spansEnabled_=!env::flagEnabled("PHLOSION_RENDER_PROFILE_TOTALS_ONLY");
};
}
