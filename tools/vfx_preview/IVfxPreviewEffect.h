#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/tools/vfx_preview/VfxPreviewTypes.h"

namespace engine::tools::vfx_preview {

struct PreviewCasterAnimationRequest {
    std::string_view kind{};
    std::string_view move{};
    std::string_view phase{};

    constexpr bool valid() const {
        return !kind.empty() && !move.empty() && !phase.empty();
    }
};

struct PreviewPokemonSpeciesSelection {
    std::string_view attackerSpecies{"charmander"};
    std::string_view targetSpecies{"bulbasaur"};
};

class IVfxPreviewEffect {
public:
    virtual ~IVfxPreviewEffect() = default;

    virtual std::string_view name() const = 0;
    virtual void onActivated(PreviewSceneState& scene) = 0;
    virtual void replay(const PreviewSceneState& scene) = 0;
    virtual void reload(const PreviewSceneState& scene) { replay(scene); }
    virtual void update(float dt, const PreviewSceneState& scene) = 0;
    virtual void stepFrames(int frames, const PreviewSceneState& scene) = 0;
    virtual void render(const PreviewFrameContext& frame) = 0;
    virtual void onResize(int width, int height) { (void)width; (void)height; }
    virtual std::uint32_t activeCount() const = 0;
    virtual float loopCooldownSec() const { return 0.18f; }
    virtual PreviewCasterAnimationRequest casterAnimationRequest() const {
        return {};
    }
    virtual PreviewPokemonSpeciesSelection previewPokemonSpecies() const {
        return {};
    }
    virtual bool wantsExactClipMotionPreview() const {
        return false;
    }
    virtual bool wantsTargetSurfaceImpactPoint() const {
        return false;
    }
    virtual PreviewEffectFocusFrame previewFocusFrame(const PreviewSceneState& scene) const {
        (void)scene;
        return {};
    }
    virtual std::vector<std::string> overlayLines(const PreviewSceneState& scene) const {
        (void)scene;
        return {};
    }
};

} // namespace engine::tools::vfx_preview
