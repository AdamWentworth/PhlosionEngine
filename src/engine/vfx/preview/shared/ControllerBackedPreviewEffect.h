#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"

namespace vfx::preview::shared {

struct ControllerBackedPreviewEffectTraits {
    std::string name;
    engine::tools::vfx_preview::PreviewCasterAnimationRequest casterAnimation{};
    engine::tools::vfx_preview::PreviewActorSelection previewActors{};
    bool wantsExactClipMotionPreview = false;
    bool wantsTargetSurfaceImpactPoint = false;
    std::function<void(engine::tools::vfx_preview::PreviewSceneState&)> afterActivated;
    std::function<engine::tools::vfx_preview::PreviewEffectFocusFrame(
        const engine::tools::vfx_preview::PreviewSceneState&)>
        focusFrame;
    std::function<std::vector<std::string>(
        const engine::tools::vfx_preview::PreviewSceneState&)>
        overlayLines;
};

template <typename Controller>
class ControllerBackedPreviewEffect : public engine::tools::vfx_preview::IVfxPreviewEffect {
public:
    ControllerBackedPreviewEffect(std::string_view logPrefix,
                                  ControllerBackedPreviewEffectTraits traits)
        : controller_(logPrefix)
        , traits_(std::move(traits)) {}

    std::string_view name() const override {
        return traits_.name;
    }

    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) override {
        controller_.onActivated(scene);
        if (traits_.afterActivated) {
            traits_.afterActivated(scene);
        }
    }

    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene) override {
        controller_.replay(scene);
    }

    void reload(const engine::tools::vfx_preview::PreviewSceneState& scene) override {
        controller_.reload(scene);
    }

    void update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) override {
        controller_.update(dt, scene);
    }

    void stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) override {
        (void)scene;
        controller_.stepFrames(frames);
    }

    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame) override {
        controller_.render(frame);
    }

    void onResize(int width, int height) override {
        controller_.onResize(width, height);
    }

    std::uint32_t activeCount() const override {
        return controller_.activeCount();
    }

    engine::tools::vfx_preview::PreviewCasterAnimationRequest casterAnimationRequest() const override {
        return traits_.casterAnimation;
    }

    engine::tools::vfx_preview::PreviewActorSelection previewActors() const override {
        return traits_.previewActors;
    }

    bool wantsExactClipMotionPreview() const override {
        return traits_.wantsExactClipMotionPreview;
    }

    bool wantsTargetSurfaceImpactPoint() const override {
        return traits_.wantsTargetSurfaceImpactPoint;
    }

    engine::tools::vfx_preview::PreviewEffectFocusFrame previewFocusFrame(
        const engine::tools::vfx_preview::PreviewSceneState& scene) const override {
        if (!traits_.focusFrame) {
            return {};
        }
        return traits_.focusFrame(scene);
    }

    std::vector<std::string> overlayLines(
        const engine::tools::vfx_preview::PreviewSceneState& scene) const override {
        if (!traits_.overlayLines) {
            return {};
        }
        return traits_.overlayLines(scene);
    }

protected:
    Controller& controller() { return controller_; }
    const Controller& controller() const { return controller_; }

private:
    Controller controller_;
    ControllerBackedPreviewEffectTraits traits_;
};

} // namespace vfx::preview::shared
