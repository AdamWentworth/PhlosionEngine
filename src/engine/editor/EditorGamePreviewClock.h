#pragma once

#include <chrono>
#include <optional>

namespace engine::editor {

// Wall-clock playback starts after its first frame has loaded and presented.
// Explicit fixed-delta captures retain their synthetic frame cadence.
class EditorGamePreviewClock {
public:
    using Clock = std::chrono::steady_clock;

    double beginFrame(Clock::time_point now, bool playing,
                      std::optional<float> fixedDelta = {}, bool reset = false) {
        if (!playing_ || reset) awaitingFirstPresent_ = true;
        playing_ = playing;
        const double elapsed = std::chrono::duration<double>(now - previous_).count();
        previous_ = now;
        if (!playing || reset) return 0.0;
        if (fixedDelta) return static_cast<double>(*fixedDelta);
        return awaitingFirstPresent_ ? 0.0 : elapsed;
    }

    void presented(Clock::time_point now) {
        if (playing_ && awaitingFirstPresent_) {
            previous_ = now;
            awaitingFirstPresent_ = false;
        }
    }

    void suspend() { playing_ = false; }

private:
    Clock::time_point previous_{};
    bool playing_ = false;
    bool awaitingFirstPresent_ = true;
};

} // namespace engine::editor
