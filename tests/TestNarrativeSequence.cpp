#include "engine/narrative/SequencePlayer.h"
#include <stdexcept>
#include <iostream>

using namespace engine::narrative;
void check(bool value) { if (!value) throw std::runtime_error("Narrative contract failed"); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}
int main() {
    try {
        SequencePlayer p({{"walk", 2}, {"talk", 0, {"Hi!", "Caf\xc3\xa9"}, 1}, {"leave", 2}});
        p.tick(false); check(p.stepTicks() == 1);
        p.tick(true); check(p.step()->id == "talk" && p.visibleText().empty());
        p.tick(true); check(p.visibleText() == "H"); // held timed-step input cannot confirm
        p.tick(false); p.tick(true); check(p.pageRevealed()); // reveal, not advance
        p.tick(true); check(p.pageIndex() == 0);
        p.tick(false); p.tick(true); check(p.pageIndex() == 1 && p.visibleText().empty());
        p.tick(false); p.tick(false); p.tick(false); p.tick(false);
        check(p.visibleText() == "Caf\xc3\xa9");
        const auto pausedTicks = p.stepTicks();
        p.tick(true, false); check(p.stepTicks() == pausedTicks);
        p.tick(true); check(p.step()->id == "talk"); // must release after lost focus
        p.tick(false); p.tick(true); check(p.step()->id == "leave");
        p.tick(false); p.tick(false); check(!p.active() && !p.step());
        p.restart(); check(p.step()->id == "walk"); p.skip(); check(!p.active());
        rejects([] { SequencePlayer p({}); });
        rejects([] { SequencePlayer p({{"empty"}}); });
        rejects([] { SequencePlayer p({{"both", 1, {"text"}}}); });
        rejects([] { SequencePlayer p({{"same", 1}, {"same", 2}}); });
        rejects([] { SequencePlayer p({{"text", 0, {""}}}); });
        rejects([] { SequencePlayer p({{"text", 0, {"\xc0\xaf"}}}); });
        rejects([] { SequencePlayer p({{"text", 0, {"\xed\xa0\x80"}}}); });
        std::cout << "Narrative sequence contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
