#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace engine::render {
// Display-encoded RGB8 feedback, evaluated at 2x then box-downsampled with
// integer truncation. Rectangles and UVs use a top-left origin. Each update
// reads immutable previous history and starts over the current scene color.
struct ScreenFeedbackPass {
    std::array<float,4> rectangle{0,0,640,480};
    std::array<float,4> uv{0,0,1,1};
    // x: 0=history, 1=alpha-masked history, 2=black; y: opacity [0,255].
    std::array<float,4> options{0,255,0,0};
};
struct ScreenFeedbackUpdate {
    std::array<ScreenFeedbackPass,4> passes{};
    int count=0;
};
struct ScreenFeedbackFrame {
    std::uint64_t sequence=0;
    bool seed=false;
    // History resolution is fixed for this renderer's lifetime. The output
    // viewport can resize independently. One active sequence per renderer;
    // reseeding replaces it. Catch-up updates share the current scene input.
    int width=640,height=480;
    std::string maskKey;
    const unsigned char* maskRgba=nullptr;
    int maskWidth=0,maskHeight=0;
    std::span<const ScreenFeedbackUpdate> updates;
};
struct ScreenFeedbackUniform {
    // xy=history size, z=mode (0 seed / 1 feedback / 2 display), w=pass count.
    std::array<float,4> control{};
    std::array<ScreenFeedbackPass,4> passes{};
    std::array<float,4> scenePost{}; // x=bloom, y=scene exposure (zero is legacy)
};
static_assert(sizeof(ScreenFeedbackUniform)==224);
inline void validateScreenFeedback(const ScreenFeedbackFrame& frame) {
    if (!frame.sequence || frame.width<1 || frame.height<1 || frame.width>2048 || frame.height>2048 ||
        !frame.maskRgba || frame.maskKey.empty() || frame.maskWidth<1 || frame.maskHeight<1 ||
        frame.maskWidth>4096 || frame.maskHeight>4096 || frame.updates.size()>1024)
        throw std::runtime_error("Invalid screen feedback frame");
    for (const auto& update : frame.updates) {
        if (update.count<0 || update.count>4) throw std::runtime_error("Invalid screen pass count");
        for (int i=0;i<update.count;++i) {
            const auto& pass=update.passes[static_cast<std::size_t>(i)];
            for (float v : pass.rectangle) if (!std::isfinite(v)) throw std::runtime_error("Invalid screen rectangle");
            for (float v : pass.uv) if (!std::isfinite(v)) throw std::runtime_error("Invalid screen UV");
            if (!(pass.rectangle[2]>pass.rectangle[0] && pass.rectangle[3]>pass.rectangle[1]) ||
                (pass.options[0]!=0 && pass.options[0]!=1 && pass.options[0]!=2) ||
                !std::isfinite(pass.options[1]) || pass.options[1]<0 || pass.options[1]>255)
                throw std::runtime_error("Invalid screen feedback operation");
        }
    }
}
}
