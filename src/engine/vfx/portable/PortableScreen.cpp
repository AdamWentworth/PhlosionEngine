#include "PortableScreen.h"
#include <stb_image.h>
#include <atomic>
#include <stdexcept>

namespace vfx::portable {
namespace { std::atomic<std::uint64_t> nextSequence{1}; }
PortableScreen::PortableScreen(vfxlibrary::ScreenEffect effect)
    : effect_(std::move(effect)),player_(effect_) {
    if(effect_.operation!=vfxlibrary::ScreenOperation::MaskFeedback)
        throw std::runtime_error("This screen adapter supports alpha-masked rectangle feedback only");
    maskKey_=effect_.ingredient.generic_string();
    int channels=0;
    auto* data=stbi_load(maskKey_.c_str(),&maskWidth_,&maskHeight_,&channels,4);
    if(!data)throw std::runtime_error("Could not load screen mask: "+maskKey_);
    mask_.assign(data,data+static_cast<std::size_t>(maskWidth_)*maskHeight_*4);
    stbi_image_free(data);reset();
}
void PortableScreen::reset() {
    player_.reset();sequence_=nextSequence.fetch_add(1);seeded_=false;lastTick_=-1;
}
engine::render::ScreenFeedbackFrame PortableScreen::frame(bool seed) const {
    engine::render::ScreenFeedbackFrame out;
    out.sequence=sequence_;out.seed=seed;out.maskKey=maskKey_;out.maskRgba=mask_.data();
    out.maskWidth=maskWidth_;out.maskHeight=maskHeight_;return out;
}
void PortableScreen::seed(IRenderBackend& renderer) {
    if(seeded_)throw std::runtime_error("Screen feedback was already seeded");
    renderer.endWorldSceneColorPass(frame(true));seeded_=true;
}
void PortableScreen::compose(IRenderBackend& renderer,int ticks) {
    if(!seeded_ || ticks<lastTick_)throw std::runtime_error("Screen playback requires a seed and a monotonic clock");
    lastTick_=ticks;
    std::vector<engine::render::ScreenFeedbackUpdate> updates;
    const int target=effect_.updateAtTicks(ticks);
    while(player_.update()<target) {
        const auto sample=player_.advance();
        engine::render::ScreenFeedbackUpdate update;
        bool feedback=false;
        for(const auto& pass : sample.passes) {
            using S=vfxlibrary::ScreenShader;
            if(pass.shader==S::FeedbackCopy){feedback=true;continue;}
            if(feedback || update.count>=4 || pass.rotationDegrees!=0 ||
               pass.input!=vfxlibrary::ScreenInput::History ||
               (pass.shader!=S::SceneRgb && pass.shader!=S::MaskedSceneRgb))
                throw std::runtime_error("Unsupported ordered screen pass");
            auto& output=update.passes[static_cast<std::size_t>(update.count++)];
            output.rectangle=pass.rectangle;output.uv=pass.uv;
            output.options={pass.shader==S::MaskedSceneRgb?1.0f:0.0f,float(pass.opacity),0,0};
        }
        if(!feedback)throw std::runtime_error("Missing screen feedback copy");
        updates.push_back(update);
    }
    auto output=frame(false);output.updates=updates;renderer.endWorldSceneColorPass(output);
}
}
