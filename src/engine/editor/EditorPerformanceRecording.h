#pragma once
#include "engine/editor/EditorPerformance.h"
#include <algorithm>
#include <vector>

namespace engine::editor {
struct PerformanceMetric {
    std::size_t samples = 0;
    double meanMs = 0, p95Ms = 0, maxMs = 0;
};
inline PerformanceMetric summarizePerformance(std::vector<double> values) {
    std::erase_if(values, [](double v) { return !std::isfinite(v) || v < 0; });
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double value : values) sum += value;
    const auto p95 = static_cast<std::size_t>(std::ceil(values.size() * .95)) - 1;
    return {values.size(), sum / values.size(), values[p95], values.back()};
}

// A finite capture of real host frames. Fixed simulation delta never controls
// its duration. Live recording has bounded memory even when a window stalls.
class EditorPerformanceRecording {
public:
    void start(double durationSeconds = 30, double warmupSeconds = 2) {
        samples_.clear();
        remainingMs_ = std::clamp(durationSeconds, .1, 300.0) * 1000;
        warmupMs_ = std::clamp(warmupSeconds, 0.0, 30.0) * 1000;
        active_ = true;
    }
    void cancel() { active_ = false; samples_.clear(); }
    bool add(const EditorPerformanceSample& sample) {
        if (!active_ || !std::isfinite(sample.frameMs) || sample.frameMs <= 0) return false;
        if (warmupMs_ > 0) { warmupMs_ -= sample.frameMs; return false; }
        samples_.push_back(sample);
        remainingMs_ -= sample.frameMs;
        if (remainingMs_ > 0 && samples_.size() < 100000) return false;
        active_ = false;
        return true;
    }
    bool active() const { return active_; }
    bool warmingUp() const { return warmupMs_ > 0; }
    double remainingSeconds() const { return std::max(0.0, remainingMs_ + std::max(0.0, warmupMs_)) / 1000; }
    const std::vector<EditorPerformanceSample>& samples() const { return samples_; }
private:
    std::vector<EditorPerformanceSample> samples_;
    double remainingMs_ = 0, warmupMs_ = 0;
    bool active_ = false;
};
} // namespace engine::editor
