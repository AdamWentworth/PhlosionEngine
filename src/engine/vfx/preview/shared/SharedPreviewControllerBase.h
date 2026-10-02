#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <glm/glm.hpp>

#include "engine/render/Camera3D.h"
#include "engine/tools/vfx_preview/VfxPreviewTypes.h"
#include "engine/utils/LogSink.h"
#include "engine/vfx/preview/shared/SharedAuthoredVfxRenderer.h"

namespace vfx::preview::shared {

inline constexpr float kFixedPreviewStepSec = 1.0f / 60.0f;

inline glm::vec3 safeForwardXZ(const glm::vec3& value) {
    glm::vec3 forward(value.x, 0.0f, value.z);
    const float lenSq = glm::dot(forward, forward);
    if (lenSq <= 0.000001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return forward / std::sqrt(lenSq);
}

template <typename Derived>
class SharedPreviewControllerBase {
public:
    SharedPreviewControllerBase(std::filesystem::path manifestPath,
                                std::string_view logChannel,
                                std::string_view effectName,
                                std::string_view logPrefix)
        : manifestPath_(std::move(manifestPath))
        , logChannel_(logChannel)
        , effectName_(effectName)
        , logPrefix_(logPrefix) {}

    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
        applyDefaultScene(scene);
        captureScene(scene);
        ensureConfigured();
    }

    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
        captureScene(scene);
        ensureConfigured();
        resetSimulationState();
        self().emitScene(scene);
    }

    void reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
        captureScene(scene);
        refreshManifestWriteTime();
        self().configureEffect();
        resetSimulationState();
        self().emitScene(scene);
        log(std::string("Reloaded ") + effectName_ + " preview");
    }

    void update(float dt,
                const engine::tools::vfx_preview::PreviewSceneState& scene) {
        captureScene(scene);
        pollManifestHotReload(scene);

        dt = std::max(0.0f, dt);
        accumulator_ += dt;
        while (accumulator_ >= kFixedPreviewStepSec) {
            self().advanceEffect(kFixedPreviewStepSec);
            accumulator_ -= kFixedPreviewStepSec;
            ++frameCursor_;
        }
    }

    void stepFrames(int frames) {
        if (frames == 0 || !hasCapturedScene_) return;
        accumulator_ = 0.0f;
        if (frames > 0) {
            for (int i = 0; i < frames; ++i) {
                self().advanceEffect(kFixedPreviewStepSec);
                ++frameCursor_;
            }
            return;
        }

        const int targetFrame = std::max(0, frameCursor_ + frames);
        resetToCapturedScene();
        for (int i = 0; i < targetFrame; ++i) {
            self().advanceEffect(kFixedPreviewStepSec);
            ++frameCursor_;
        }
    }

    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
        if (!renderer_) {
            renderer_ = std::make_unique<vfx::preview::authored::SharedAuthoredVfxRenderer>();
        }
        renderer_->setRenderBackend(frame.renderer);
        lastViewMatrix_ = frame.camera.getViewMatrix();
        hasLastViewMatrix_ = true;
        self().renderPreview(*renderer_, frame);
    }

    void onResize(int width, int height) {
        if (!renderer_) {
            renderer_ = std::make_unique<vfx::preview::authored::SharedAuthoredVfxRenderer>();
        }
        renderer_->onResize(width, height);
    }

    std::uint32_t activeCount() const {
        return selfConst().activeEffectCount();
    }

protected:
    const glm::mat4* lastViewMatrix() const {
        return hasLastViewMatrix_ ? &lastViewMatrix_ : nullptr;
    }

    void ensureConfigured() {
        self().configureEffect();
        if (!manifestWriteTime_.has_value()) {
            refreshManifestWriteTime();
        }
    }

private:
    Derived& self() {
        return static_cast<Derived&>(*this);
    }

    const Derived& selfConst() const {
        return static_cast<const Derived&>(*this);
    }

    void applyDefaultScene(engine::tools::vfx_preview::PreviewSceneState& scene) {
        if constexpr (requires(Derived& d, engine::tools::vfx_preview::PreviewSceneState& s) {
                          d.applySceneDefaults(s);
                      }) {
            self().applySceneDefaults(scene);
            return;
        }

        scene.emitter.y = 0.42f;
        scene.target.y = 0.35f;
    }

    void captureScene(const engine::tools::vfx_preview::PreviewSceneState& scene) {
        capturedScene_ = scene;
        hasCapturedScene_ = true;
    }

    void resetSimulationState() {
        accumulator_ = 0.0f;
        frameCursor_ = 0;
    }

    void resetToCapturedScene() {
        if (!hasCapturedScene_) return;
        self().configureEffect();
        resetSimulationState();
        self().emitScene(capturedScene_);
    }

    void refreshManifestWriteTime() {
        std::error_code ec;
        if (!std::filesystem::exists(manifestPath_, ec) || ec) {
            manifestWriteTime_.reset();
            return;
        }
        manifestWriteTime_ = std::filesystem::last_write_time(manifestPath_, ec);
        if (ec) manifestWriteTime_.reset();
    }

    void pollManifestHotReload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
        std::error_code ec;
        if (!manifestWriteTime_.has_value()) {
            refreshManifestWriteTime();
        }
        if (!std::filesystem::exists(manifestPath_, ec) || ec || !manifestWriteTime_.has_value()) {
            return;
        }

        const auto latest = std::filesystem::last_write_time(manifestPath_, ec);
        if (ec || latest == *manifestWriteTime_) return;

        manifestWriteTime_ = latest;
        self().configureEffect();
        resetSimulationState();
        self().emitScene(scene);
        log(std::string("Detected ") + effectName_ + " manifest change, hot reloaded preview");
    }

    void log(std::string_view message) const {
        if (message.empty()) return;
        engine::log::Sink log(logChannel_, &std::cout, &std::cerr);
        if (logPrefix_.empty()) {
            log.info(message);
            return;
        }
        log.info(logPrefix_ + " " + std::string(message));
    }

private:
    std::filesystem::path manifestPath_;
    std::string logChannel_;
    std::string effectName_;
    std::string logPrefix_;
    std::optional<std::filesystem::file_time_type> manifestWriteTime_;
    std::unique_ptr<vfx::preview::authored::SharedAuthoredVfxRenderer> renderer_;
    float accumulator_ = 0.0f;
    int frameCursor_ = 0;
    glm::mat4 lastViewMatrix_{1.0f};
    bool hasLastViewMatrix_ = false;
    bool hasCapturedScene_ = false;
    engine::tools::vfx_preview::PreviewSceneState capturedScene_{};
};

} // namespace vfx::preview::shared
