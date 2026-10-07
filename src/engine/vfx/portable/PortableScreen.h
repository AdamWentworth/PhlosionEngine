#pragma once
#include "engine/render/IRenderBackend.h"
#include "vfxlibrary/ScreenEffect.h"
#include <vector>

namespace vfx::portable {
// Adapter for the library's ordered rectangle/history screen operations.
// Scene selection and the clock remain host responsibilities.
class PortableScreen {
public:
    explicit PortableScreen(vfxlibrary::ScreenEffect effect);
    void reset();
    bool seeded() const noexcept { return seeded_; }
    void seed(IRenderBackend& renderer); // Completes the outgoing world pass.
    void compose(IRenderBackend& renderer, int ticks); // Completes the destination pass.
    int endTick() const { return effect_.updateTicks.back(); }
    int update() const { return player_.update(); }
private:
    engine::render::ScreenFeedbackFrame frame(bool seed) const;
    vfxlibrary::ScreenEffect effect_;
    vfxlibrary::ScreenPlayer player_;
    std::vector<unsigned char> mask_;
    std::string maskKey_;
    int maskWidth_=0,maskHeight_=0,lastTick_=-1;
    std::uint64_t sequence_=0;
    bool seeded_=false;
};
}
