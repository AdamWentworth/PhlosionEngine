#include "engine/editor/EditorPerformance.h"
#include "engine/editor/EditorPerformanceRecording.h"
#include "engine/editor/EditorBuildProfile.h"
#include <cmath>
#include <limits>
#include <string>

bool test_editor_performance_contract(std::string& error) {
    using namespace engine::editor;
    EditorPerformanceWindow stats;
    // A paused or fixed-60-Hz simulation must still report the actual 100-Hz host.
    for (int i = 0; i < 50; ++i)
        stats.add({.frameMs=10,.viewportMs=2,.simulationMs=0,.gpuMs=3,.gpuValid=i%2==0});
    const auto first = stats.latest();
    if (std::abs(first.fps-100) > .001 || first.viewportMs != 2 || first.simulationMs != 0 ||
        !first.gpuValid || first.gpuMs != 3 || first.samples != 50) {
        error = "Editor stats must use wall time and only valid GPU samples.";
        return false;
    }
    stats.add({.frameMs=std::numeric_limits<double>::quiet_NaN()});
    stats.add({.frameMs=0});
    for (int i = 0; i < 25; ++i) stats.add({.frameMs=20,.viewportMs=4,.simulationMs=1});
    const auto second = stats.latest();
    if (second.fps != 50 || second.gpuValid || second.gpuMs != 0 || second.samples != 25 ||
        second.viewportMs != 4 || second.simulationMs != 1) {
        error = "A new stats window must discard old GPU readings and invalid frame intervals.";
        return false;
    }
    EditorPerformanceRecording recording;
    recording.start(.1, .05);
    recording.add({.frameMs=50}); // Warmup is excluded even if it contains a spike.
    for (int i = 0; i < 9; ++i) {
        if (recording.add({.frameMs=10})) { error="Recording ended early."; return false; }
    }
    if (!recording.add({.frameMs=10}) || recording.active() || recording.samples().size() != 10) {
        error="A recording must end on real elapsed frame time with warmup excluded."; return false;
    }
    const auto metric = summarizePerformance({1, 2, 3, 4, 90, -1, std::numeric_limits<double>::quiet_NaN()});
    if (metric.samples != 5 || metric.meanMs != 20 || metric.p95Ms != 90 || metric.maxMs != 90) {
        error="Recordings must retain frame spikes and reject invalid observations."; return false;
    }
    recording.start(.1, 0);
    recording.add({.frameMs=10});
    recording.cancel();
    if (recording.add({.frameMs=100}) || !recording.samples().empty() ||
        buildProfileName("RelWithDebInfo") != "Development" || buildProfileName("Debug") != "Debug") {
        error="Cancellation must discard partial data; build profiles must identify the optimized development build."; return false;
    }
    return true;
}
