#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <ostream>
#include <sstream>
#include <thread>
#include <utility>
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace engine::render {
// Two native recording lanes. The caller participates and may claim an unstarted
// helper range. Short jobs never have to wait for a sleeping worker to be scheduled.
class WorldRecordingWorkers {
public:
    static constexpr unsigned count = 2;
    using Clock = std::chrono::steady_clock;
    struct Interval { Clock::time_point begin{}, end{}; };
    struct RunStats {
        double dispatchMs=0, runMs=0, joinWaitMs=0, helperStartDelayMs=0;
        bool ownerClaimed=false;
    };
    WorldRecordingWorkers() : helper_([this]{worker();}) {}
    ~WorldRecordingWorkers() {
        stopping_.store(true,std::memory_order_release);
        work_.fetch_add(4,std::memory_order_release);
        work_.notify_all();
        helper_.join();
    }
    WorldRecordingWorkers(const WorldRecordingWorkers&) = delete;
    WorldRecordingWorkers& operator=(const WorldRecordingWorkers&) = delete;
    std::array<Interval,count> run(std::function<void(unsigned)> job,RunStats* stats=nullptr) {
        const auto start=Clock::now();
        job_=std::move(job);errors_={};
        const auto ticket=(ticket_+=4);
        const auto submitted=Clock::now();
        work_.store(ticket|queued,std::memory_order_release);work_.notify_one();
        execute(0);
        const auto ownerFinished=Clock::now();
        bool ownerClaimed=false;
        auto expected=ticket|queued;
        if(work_.compare_exchange_strong(expected,ticket|running,std::memory_order_acquire)) {
            ownerClaimed=true;
            execute(1);
            work_.store(ticket|complete,std::memory_order_release);work_.notify_one();
        } else {
            while((expected=work_.load(std::memory_order_acquire))!=(ticket|complete))
                waitForChange(expected);
        }
        const auto joined=Clock::now();
        job_={};
        if(stats) {
            const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
            stats->dispatchMs=ms(start,intervals_[0].begin);
            stats->runMs=ms(start,joined);
            stats->joinWaitMs=ownerClaimed?0:ms(ownerFinished,joined);
            stats->helperStartDelayMs=ownerClaimed?0:ms(submitted,intervals_[1].begin);
            stats->ownerClaimed=ownerClaimed;
        }
        for(const auto& error:errors_)if(error)std::rethrow_exception(error);
        return intervals_;
    }
private:
    static constexpr std::uint64_t queued=1,running=2,complete=3;
    void execute(unsigned lane) noexcept {
        intervals_[lane].begin=Clock::now();
        try { job_(lane); } catch(...) { errors_[lane]=std::current_exception(); }
        intervals_[lane].end=Clock::now();
    }
    void waitForChange(std::uint64_t expected) {
        const auto until=Clock::now()+std::chrono::microseconds(100);
        while(work_.load(std::memory_order_acquire)==expected) {
            if(stopping_.load(std::memory_order_acquire))return;
            if(Clock::now()>=until) { work_.wait(expected,std::memory_order_acquire);return; }
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        }
    }
    void worker() {
        for (;;) {
            if(stopping_.load(std::memory_order_acquire))return;
            auto expected=work_.load(std::memory_order_acquire);
            if((expected&3)==queued) {
                const auto ticket=expected&~std::uint64_t(3);
                if(work_.compare_exchange_strong(expected,ticket|running,std::memory_order_acquire)) {
                    execute(1);
                    work_.store(ticket|complete,std::memory_order_release);work_.notify_one();
                }
            } else waitForChange(expected);
        }
    }
    std::array<Interval,count> intervals_{};
    std::array<std::exception_ptr,count> errors_{};
    std::function<void(unsigned)> job_;
    alignas(64) std::atomic<std::uint64_t> work_{0};
    std::atomic<bool> stopping_{false};
    std::uint64_t ticket_=0;
    // Construct last: the helper can immediately read all preceding members.
    std::thread helper_;
};
struct WorldRecordingStats {
    std::uint64_t batches=0, packets=0, serialPackets=0, overlappingBatches=0;
    double recordingMs=0, overlapMs=0;
    std::uint64_t ownerClaimedBatches=0,serialBatches=0,bindingWrites=0,bindingReuses=0;
    double setupMs=0,dispatchMs=0,runMs=0,joinWaitMs=0,helperStartDelayMs=0,executeMs=0,serialMs=0;
    void addRun(const WorldRecordingWorkers::RunStats& run,double setup,double execute) {
        ownerClaimedBatches+=run.ownerClaimed;
        setupMs+=setup;dispatchMs+=run.dispatchMs;runMs+=run.runMs;
        joinWaitMs+=run.joinWaitMs;helperStartDelayMs+=run.helperStartDelayMs;executeMs+=execute;
    }
    void add(std::size_t size,const std::array<WorldRecordingWorkers::Interval,2>& times) {
        ++batches; packets+=size;
        for(const auto& time:times)
            recordingMs+=std::chrono::duration<double,std::milli>(time.end-time.begin).count();
        const auto overlap=std::min(times[0].end,times[1].end)-std::max(times[0].begin,times[1].begin);
        if(overlap>WorldRecordingWorkers::Clock::duration::zero()) {
            ++overlappingBatches;overlapMs+=std::chrono::duration<double,std::milli>(overlap).count();
        }
    }
    void print(std::ostream& out,const char* api,std::uint64_t frame) const {
        std::ostringstream line;
        line<<"[WorldRecording] api="<<api<<" frame="<<frame<<" batches="<<batches
           <<" jobs="<<batches*2<<" packets="<<packets<<" serial="<<serialPackets
           <<" overlapping_batches="<<overlappingBatches<<" recording_ms="<<recordingMs
           <<" overlap_ms="<<overlapMs<<" owner_claimed_batches="<<ownerClaimedBatches
           <<" setup_ms="<<setupMs<<" dispatch_ms="<<dispatchMs<<" run_ms="<<runMs
           <<" join_wait_ms="<<joinWaitMs<<" helper_start_delay_ms="<<helperStartDelayMs
           <<" execute_ms="<<executeMs<<" serial_batches="<<serialBatches<<" serial_ms="<<serialMs<<" binding_writes="<<bindingWrites<<" binding_reuses="<<bindingReuses<<'\n';
        // One write prevents redirected diagnostic pipes from stalling each field.
        out<<line.str();
    }
};
inline constexpr std::size_t worldRecordingMinPackets=8, worldRecordingMaxPackets=256,
    worldRecordingListsPerLane=512;
} // namespace engine::render
