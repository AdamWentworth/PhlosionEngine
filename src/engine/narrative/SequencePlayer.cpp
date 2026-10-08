#include "SequencePlayer.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace engine::narrative {
namespace {
std::size_t nextCodepoint(const std::string& text, std::size_t at) {
    if (at >= text.size()) return text.size();
    const auto first = static_cast<unsigned char>(text[at]);
    const std::size_t count = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
                              first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (count == 0 || count > text.size() - at) throw std::invalid_argument("Invalid dialogue UTF-8");
    for (std::size_t i = 1; i < count; ++i)
        if ((static_cast<unsigned char>(text[at + i]) & 0xc0) != 0x80)
            throw std::invalid_argument("Invalid dialogue UTF-8 continuation");
    if (count >= 3) {
        const auto second = static_cast<unsigned char>(text[at + 1]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
            throw std::invalid_argument("Invalid dialogue Unicode scalar");
    }
    return at + count;
}
}

SequencePlayer::SequencePlayer(std::vector<SequenceStep> steps) : steps_(std::move(steps)) {
    if (steps_.empty()) throw std::invalid_argument("Narrative sequence is empty");
    std::unordered_set<std::string> ids;
    for (const auto& entry : steps_) {
        if (entry.id.empty() || !ids.insert(entry.id).second)
            throw std::invalid_argument("Narrative step IDs must be nonempty and unique");
        if ((entry.durationTicks == 0) == entry.pages.empty() || entry.ticksPerCharacter == 0)
            throw std::invalid_argument("Narrative step needs either a duration or dialogue pages");
        for (const auto& text : entry.pages) {
            if (text.empty()) throw std::invalid_argument("Dialogue page is empty");
            for (std::size_t at = 0; at < text.size(); at = nextCodepoint(text, at)) {}
        }
    }
}

void SequencePlayer::restart() {
    index_ = page_ = visibleBytes_ = 0;
    stepTicks_ = pageTicks_ = 0;
    confirmWasDown_ = inputArmed_ = false;
}
void SequencePlayer::skip() { index_ = steps_.size(); }
const SequenceStep* SequencePlayer::step() const noexcept { return active() ? &steps_[index_] : nullptr; }
void SequencePlayer::nextStep() {
    ++index_;
    page_ = visibleBytes_ = 0;
    stepTicks_ = pageTicks_ = 0;
}
bool SequencePlayer::pageRevealed() const noexcept {
    return active() && !step()->pages.empty() && visibleBytes_ == step()->pages[page_].size();
}
std::string SequencePlayer::visibleText() const {
    return active() && !step()->pages.empty() ? step()->pages[page_].substr(0, visibleBytes_) : std::string{};
}
void SequencePlayer::tick(bool confirmDown, bool enabled) {
    if (!enabled) { inputArmed_ = false; confirmWasDown_ = confirmDown; return; }
    if (!active()) return;
    if (!confirmDown) inputArmed_ = true;
    const bool pressed = inputArmed_ && confirmDown && !confirmWasDown_;
    confirmWasDown_ = confirmDown;
    ++stepTicks_;
    const auto& current = *step();
    if (current.pages.empty()) {
        if (stepTicks_ >= current.durationTicks) nextStep();
        return;
    }
    const auto& text = current.pages[page_];
    if (pressed) {
        if (!pageRevealed()) visibleBytes_ = text.size();
        else if (++page_ == current.pages.size()) nextStep();
        else { pageTicks_ = 0; visibleBytes_ = 0; }
        return;
    }
    if (++pageTicks_ % current.ticksPerCharacter == 0 && visibleBytes_ < text.size())
        visibleBytes_ = nextCodepoint(text, visibleBytes_);
}
} // namespace engine::narrative
