// Phlosion Editor project plugin for the animation fixture. It loads through
// the real plugin ABI and presents the same shared animation fixture that the
// standalone runtime host uses.

#include "AnimationFixturePresentation.h"

#include "engine/core/Environment.h"
#include "engine/editor/EditorProjectPlugin.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

constexpr int kBindFrames = 2;

std::string envOrEmpty(const char* name) {
    const std::optional<std::string> value = engine::env::get(name);
    return value ? *value : std::string{};
}

class AnimationFixtureEditorRuntime final
    : public engine::editor::IEditorProjectRuntime {
public:
    bool open(const engine::editor::EditorProjectOpenContext& context,
              std::string* outError) override {
        (void)context;
        const std::string caseName =
            envOrEmpty("PHLOSION_ANIMATION_FIXTURE_CASE");
        const std::optional<engine::animation_fixture::FixtureCase> parsedCase =
            engine::animation_fixture::parseFixtureCase(
                caseName.empty() ? "bind" : caseName);
        if (!parsedCase) {
            if (outError) {
                *outError =
                    "animation fixture plugin does not know case: " + caseName;
            }
            return false;
        }
        fixtureCase_ = *parsedCase;
        handshakePath_ = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_HANDSHAKE");
        requestedBackend_ = envOrEmpty("PHLOSION_ANIMATION_FIXTURE_API");
        if (requestedBackend_.empty()) {
            requestedBackend_ = "unknown";
        }
        const std::string frameText =
            envOrEmpty("PHLOSION_ANIMATION_FIXTURE_LOGICAL_FRAME");
        if (!frameText.empty()) {
            try {
                logicalFrame_ = std::stoi(frameText);
            } catch (const std::exception&) {
                if (outError) {
                    *outError =
                        "animation fixture logical frame is not an integer: " +
                        frameText;
                }
                return false;
            }
        } else {
            logicalFrame_ =
                engine::animation_fixture::fixtureCaseLogicalFrame(fixtureCase_);
        }
        if (logicalFrame_ < 0) {
            if (outError) {
                *outError =
                    "animation fixture logical frame must be non-negative.";
            }
            return false;
        }
        std::string poseError;
        if (!engine::animation_fixture::buildFixturePose(
                engine::animation_fixture::FixtureCase::Bind,
                bindPose_,
                &poseError) ||
            !engine::animation_fixture::buildFixturePose(
                fixtureCase_, requestedPose_, &poseError)) {
            if (outError) {
                *outError = "animation fixture pose failed: " + poseError;
            }
            return false;
        }
        renderCount_ = 0;
        poseFrames_ = 0;
        const std::size_t jointCount =
            engine::animation_fixture::fixtureRig()
                .skins[engine::animation_fixture::fixtureRig().skinIndex]
                .joints.size();
        palette_ = engine::animation_fixture::FixturePaletteStorage(jointCount);
        palette_.setFrom(bindPose_);
        bindPalette_ = palette_.snapshot();
        transition_ = engine::animation_fixture::FixturePaletteTransition{};
        bindDrawRecorded_ = false;
        poseDrawRecorded_ = false;
        status_ = "Animation fixture active: ";
        status_ += engine::animation_fixture::fixtureCaseName(fixtureCase_);
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void prewarm(
        IRenderBackend& renderer,
        const engine::editor::EditorProjectCameraContext& camera) override {
        (void)camera;
        renderer.prewarmWorldRenderAssets();
    }

    void update(float simulationSeconds) override {
        (void)simulationSeconds;
    }

    void render(const engine::editor::EditorProjectRenderContext& context) override {
        if (!context.renderer || context.surfaceWidth <= 0 ||
            context.surfaceHeight <= 0) {
            return;
        }
        const bool presentPose = renderCount_ >= kBindFrames;
        if (presentPose) {
            palette_.setFrom(requestedPose_);
            if (!poseDrawRecorded_) {
                transition_.recordPoseDraw(palette_, bindPalette_);
                poseDrawRecorded_ = true;
            }
            ++poseFrames_;
        } else {
            palette_.setFrom(bindPose_);
            if (!bindDrawRecorded_) {
                transition_.recordBindDraw(palette_);
                bindDrawRecorded_ = true;
            }
        }
        std::string renderError;
        if (!engine::animation_fixture::renderAnimationFixture(
                engine::animation_fixture::fixtureRig(),
                palette_,
                *context.renderer,
                context.surfaceWidth,
                context.surfaceHeight,
                &drawStats_,
                &renderError)) {
            throw std::runtime_error(
                "animation fixture render failed: " + renderError);
        }
        ++renderCount_;
        if (!handshakePath_.empty()) {
            const engine::animation_fixture::FixtureHandshake handshake =
                engine::animation_fixture::buildFixtureHandshake(
                    "editor",
                    requestedBackend_,
                    *context.renderer,
                    requestedPose_,
                    bindPose_,
                    transition_,
                    context.surfaceWidth,
                    context.surfaceHeight,
                    kBindFrames,
                    poseFrames_);
            std::string handshakeError;
            if (!engine::animation_fixture::writeFixtureHandshake(
                    handshakePath_, handshake, &handshakeError)) {
                throw std::runtime_error(
                    "animation fixture handshake failed: " + handshakeError);
            }
        }
    }

    engine::editor::EditorProjectStats stats() const override {
        engine::editor::EditorProjectStats result;
        result.sceneCount = 1u;
        result.drawClassCount = 2u;
        result.visibleTriangleCount =
            static_cast<std::uint64_t>(drawStats_.triangles);
        result.shadowTriangleCount = 0u;
        result.archiveFileCount = 0u;
        return result;
    }

    const char* status() const noexcept override {
        return status_.c_str();
    }

private:
    engine::animation_fixture::FixtureCase fixtureCase_ =
        engine::animation_fixture::FixtureCase::Bind;
    engine::animation_fixture::FixturePose bindPose_;
    engine::animation_fixture::FixturePose requestedPose_;
    engine::animation_fixture::FixturePaletteStorage palette_;
    engine::animation_fixture::FixturePaletteTransition transition_;
    std::vector<float> bindPalette_;
    engine::animation_fixture::FixtureDrawStats drawStats_{};
    std::string handshakePath_;
    std::string requestedBackend_;
    std::string status_ = "Animation fixture idle.";
    int logicalFrame_ = 0;
    int renderCount_ = 0;
    int poseFrames_ = 0;
    bool bindDrawRecorded_ = false;
    bool poseDrawRecorded_ = false;
};

engine::editor::IEditorProjectRuntime* createRuntime() {
    return new AnimationFixtureEditorRuntime();
}

void destroyRuntime(engine::editor::IEditorProjectRuntime* runtime) {
    delete runtime;
}

const engine::editor::EditorProjectPluginContract kContract{
    engine::editor::kEditorProjectPluginAbiVersion,
    sizeof(engine::editor::EditorProjectPluginContract),
    engine::editor::kEditorProjectPluginLayoutFingerprint,
    PHLOSION_EDITOR_BUILD_CONFIGURATION,
    engine::editor::kEditorProjectPluginCompilerAbi,
    &createRuntime,
    &destroyRuntime};

} // namespace

PHLOSION_EDITOR_PROJECT_EXPORT std::uint32_t
phlosionEditorProjectPluginAbiVersion() {
    return engine::editor::kEditorProjectPluginAbiVersion;
}

PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorProjectPluginContract*
phlosionEditorProjectPluginContract() {
    return &kContract;
}

PHLOSION_EDITOR_PROJECT_EXPORT engine::editor::IEditorProjectRuntime*
phlosionCreateEditorProjectRuntime() {
    return createRuntime();
}

PHLOSION_EDITOR_PROJECT_EXPORT void
phlosionDestroyEditorProjectRuntime(
    engine::editor::IEditorProjectRuntime* runtime) {
    destroyRuntime(runtime);
}
