#pragma once

#include <cmath>
#include <cstdint>

namespace engine::editor {

// Wall-clock observations, independent of the editor's simulation clock.
struct EditorPerformanceSample {
    double frameMs = 0;
    double viewportMs = 0;
    double simulationMs = 0;
    double presentMs = 0;
    double gpuMs = 0;
    bool gpuValid = false;
    std::uint64_t drawCalls = 0;
    std::uint64_t triangles = 0;
};

struct EditorPerformanceSnapshot : EditorPerformanceSample {
    double fps = 0;
    std::uint32_t samples = 0;
};

class EditorPerformanceWindow {
public:
    void add(const EditorPerformanceSample& sample) {
        if (!std::isfinite(sample.frameMs) || sample.frameMs <= 0) return;
        sum_.frameMs += sample.frameMs;
        sum_.viewportMs += sample.viewportMs;
        sum_.simulationMs += sample.simulationMs;
        sum_.presentMs += sample.presentMs;
        if (sample.gpuValid && std::isfinite(sample.gpuMs) && sample.gpuMs >= 0) {
            sum_.gpuMs += sample.gpuMs;
            ++gpuSamples_;
        }
        ++samples_;
        if (sum_.frameMs < 500) return;
        latest_.frameMs = sum_.frameMs / samples_;
        latest_.fps = 1000.0 / latest_.frameMs;
        latest_.viewportMs = sum_.viewportMs / samples_;
        latest_.simulationMs = sum_.simulationMs / samples_;
        latest_.presentMs = sum_.presentMs / samples_;
        latest_.gpuValid = gpuSamples_ > 0;
        latest_.gpuMs = gpuSamples_ ? sum_.gpuMs / gpuSamples_ : 0;
        latest_.drawCalls = sample.drawCalls;
        latest_.triangles = sample.triangles;
        latest_.samples = samples_;
        sum_ = {};
        samples_ = gpuSamples_ = 0;
    }
    const EditorPerformanceSnapshot& latest() const { return latest_; }
private:
    EditorPerformanceSample sum_;
    EditorPerformanceSnapshot latest_;
    std::uint32_t samples_ = 0;
    std::uint32_t gpuSamples_ = 0;
};

} // namespace engine::editor
