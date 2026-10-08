#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::narrative {

// Content bindings (actors, cameras, audio, persistence) belong to the host.
// A step is either a fixed number of host ticks or confirmed dialogue pages.
struct SequenceStep {
    std::string id;
    std::uint32_t durationTicks = 0;
    std::vector<std::string> pages;
    std::uint32_t ticksPerCharacter = 2;
};

class SequencePlayer {
public:
    explicit SequencePlayer(std::vector<SequenceStep> steps);
    void restart();
    void skip();
    // One fixed host tick. Disabled playback pauses and requires neutral input
    // on resumption. Confirmation is edge-triggered, including between pages.
    void tick(bool confirmDown, bool enabled = true);
    bool active() const noexcept { return index_ < steps_.size(); }
    const SequenceStep* step() const noexcept;
    std::size_t stepIndex() const noexcept { return index_; }
    std::uint64_t stepTicks() const noexcept { return stepTicks_; }
    std::size_t pageIndex() const noexcept { return page_; }
    std::string visibleText() const;
    bool pageRevealed() const noexcept;

private:
    void nextStep();
    std::vector<SequenceStep> steps_;
    std::size_t index_ = 0, page_ = 0, visibleBytes_ = 0;
    std::uint64_t stepTicks_ = 0, pageTicks_ = 0;
    bool confirmWasDown_ = false, inputArmed_ = false;
};

} // namespace engine::narrative
