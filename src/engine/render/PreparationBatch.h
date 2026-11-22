#pragma once
#include "engine/render/PreparationProgress.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>

namespace engine::render {
// Inputs/outputs belong to the caller and stay alive through the explicit join.
class PreparationBatch {
public:
    void add(std::function<void()> job) {
        if (jobs_.size() >= 4096) throw std::runtime_error("Preparation job limit exceeded");
        jobs_.push_back(std::move(job));
    }
    void run(unsigned maximumWorkers = 4) {
        if (jobs_.empty()) return;
        const auto count = std::min<unsigned>(static_cast<unsigned>(jobs_.size()),
            std::clamp(maximumWorkers, 1u, std::max(1u, std::thread::hardware_concurrency())));
        std::atomic_size_t next{0},completed{0};
        std::atomic_bool cancelled{false};
        std::exception_ptr failure;
        std::mutex failureMutex;
        {
            std::vector<std::jthread> workers;
            for (unsigned i=0; i<count; ++i) workers.emplace_back([&] {
                for (;;) {
                    if(cancelled)return;
                    const auto index=next.fetch_add(1);
                    if (index>=jobs_.size()) return;
                    try { jobs_[index](); }
                    catch (...) { std::lock_guard lock(failureMutex); if (!failure) failure=std::current_exception(); }
                    ++completed;
                }
            });
            try {
                while(preparationProgress && completed.load()<jobs_.size()) {
                    preparationProgress("Preparing GPU pipelines "+std::to_string(completed.load())+"/"+std::to_string(jobs_.size()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
            } catch (...) { cancelled=true;std::lock_guard lock(failureMutex);if(!failure)failure=std::current_exception(); }
        }
        jobs_.clear();
        if (failure) std::rethrow_exception(failure);
        if(preparationProgress)preparationProgress({});
    }
private:
    std::vector<std::function<void()>> jobs_;
};
}
