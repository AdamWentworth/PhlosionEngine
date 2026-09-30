#include "engine/editor/EditorGamePreviewClock.h"

#include <cmath>
#include <string>

bool test_editor_game_preview_clock_contract(std::string& outFail) {
    using engine::editor::EditorGamePreviewClock;
    const auto at = [](int milliseconds) {
        return EditorGamePreviewClock::Clock::time_point{} +
            std::chrono::milliseconds(milliseconds);
    };
    const auto near = [](double actual, double expected) {
        return std::abs(actual - expected) < 1e-9;
    };
    EditorGamePreviewClock clock;
    if (clock.beginFrame(at(0), false) != 0.0 ||
        clock.beginFrame(at(30000), true) != 0.0) {
        outFail = "Time spent editing/loading before Play must not advance gameplay.";
        return false;
    }
    // A slow first upload/present must not become 60 Hz catch-up debt.
    clock.presented(at(35000));
    if (!near(clock.beginFrame(at(35004), true), 0.004)) {
        outFail = "The first presented frame must exclude its five-second load.";
        return false;
    }
    clock.presented(at(35006));
    if (!near(clock.beginFrame(at(37504), true), 2.5)) {
        outFail = "Ordinary slow frames must retain all elapsed time, without a clamp.";
        return false;
    }
    clock.beginFrame(at(37508), false);
    if (clock.beginFrame(at(90000), true) != 0.0) {
        outFail = "Pause/resume must not import time from the paused epoch.";
        return false;
    }
    // No successful draw yet: remain at the same time until presentation.
    if (clock.beginFrame(at(94000), true) != 0.0) {
        outFail = "Playback must wait for a successful initial presentation.";
        return false;
    }
    clock.presented(at(96000));
    if (!near(clock.beginFrame(at(96010), true), 0.01) ||
        clock.beginFrame(at(99000), true, {}, true) != 0.0) {
        outFail = "Restart must discard the previous preview epoch.";
        return false;
    }
    clock.presented(at(104000));
    if (!near(clock.beginFrame(at(104010), true), 0.01)) {
        outFail = "Restart loading must also finish before real-time playback resumes.";
        return false;
    }
    clock.suspend();
    if (clock.beginFrame(at(200000), true) != 0.0) {
        outFail = "A minimized/replaced preview must start a fresh clock epoch.";
        return false;
    }
    clock.presented(at(200010));
    clock.beginFrame(at(200030), true);
    clock.restartDuringFrame();
    clock.presented(at(208030));
    if (!near(clock.beginFrame(at(208040), true), .01)) {
        outFail = "Loading an encounter inside a running fixed update must not become camera catch-up debt.";
        return false;
    }
    for (const float delta : {1.0f / 15.0f, 1.0f / 60.0f, 1.0f / 240.0f}) {
        EditorGamePreviewClock fixed;
        for (int frame = 0; frame < 6; ++frame) {
            if (fixed.beginFrame(at(frame * 10000), true, delta) != double(delta)) {
                outFail = "Synthetic captures must retain every step at 15/60/240 Hz.";
                return false;
            }
            fixed.presented(at(frame * 10000 + 9000));
        }
        if (fixed.beginFrame(at(70000), true, delta, true) != 0.0 ||
            fixed.beginFrame(at(80000), true, delta) != double(delta)) {
            outFail = "Explicit replay reset must still omit exactly its reset step.";
            return false;
        }
    }
    return true;
}
