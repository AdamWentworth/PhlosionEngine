// Phlosion Editor project plugin for the public asset fixture. It loads the
// real plugin ABI through a generated project descriptor and presents the same
// shared fixture session that the standalone runtime host uses.

#include "FixturePresentation.h"

#include "engine/core/Environment.h"
#include "engine/editor/EditorProjectPlugin.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string envOrEmpty(const char* name) {
    const std::optional<std::string> value = engine::env::get(name);
    return value ? *value : std::string{};
}

class FixtureEditorRuntime final : public engine::editor::IEditorProjectRuntime {
public:
    bool open(
        const engine::editor::EditorProjectOpenContext& context,
        std::string* outError) override {
        (void)context;
        const std::string caseName = envOrEmpty("PHLOSION_FIXTURE_CASE");
        const std::optional<engine::fixture::FixtureCase> parsedCase =
            engine::fixture::parseFixtureCase(
                caseName.empty() ? "a-frame0" : caseName);
        if (!parsedCase) {
            if (outError) {
                *outError = "public fixture plugin does not know case: " + caseName;
            }
            return false;
        }
        fixtureCase_ = *parsedCase;
        session_ =
            std::make_unique<engine::fixture::FixtureSession>(fixtureCase_);
        handshakePath_ = envOrEmpty("PHLOSION_FIXTURE_HANDSHAKE");
        requestedBackend_ = envOrEmpty("PHLOSION_FIXTURE_API");
        if (requestedBackend_.empty()) {
            requestedBackend_ = "unknown";
        }
        const std::string frameText =
            envOrEmpty("PHLOSION_FIXTURE_LOGICAL_FRAME");
        if (!frameText.empty()) {
            try {
                logicalFrame_ = std::stoi(frameText);
            } catch (const std::exception&) {
                if (outError) {
                    *outError =
                        "public fixture logical frame is not an integer: " +
                        frameText;
                }
                return false;
            }
        } else {
            logicalFrame_ =
                engine::fixture::fixtureCaseLogicalFrame(fixtureCase_);
        }
        if (logicalFrame_ < 0) {
            if (outError) {
                *outError = "public fixture logical frame must be non-negative.";
            }
            return false;
        }
        if (!session_->prepare(outError)) {
            return false;
        }
        renderCount_ = 0;
        replacementAttempted_ = false;
        status_ = "Public asset fixture active: ";
        status_ += session_->activeContent().contentId;
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
        if (!engine::fixture::fixtureCaseReplaces(fixtureCase_)) {
            replacementAttempted_ = true;
        } else if (!replacementAttempted_ && renderCount_ >= 1) {
            replacementAttempted_ = true;
            std::string replacementError;
            if (!session_->attemptReplacement(&replacementError)) {
                throw std::runtime_error(
                    "public fixture replacement failed: " + replacementError);
            }
        }

        std::string renderError;
        if (!engine::fixture::renderFixture(
                session_->activeContent(),
                *context.renderer,
                logicalFrame_,
                context.surfaceWidth,
                context.surfaceHeight,
                &drawStats_,
                &renderError)) {
            throw std::runtime_error(
                "public fixture render failed: " + renderError);
        }
        ++renderCount_;
        if (!handshakePath_.empty()) {
            const engine::fixture::FixtureHandshake handshake =
                engine::fixture::buildFixtureHandshake(
                    "editor",
                    requestedBackend_,
                    *context.renderer,
                    *session_,
                    logicalFrame_,
                    context.surfaceWidth,
                    context.surfaceHeight);
            std::string handshakeError;
            if (!engine::fixture::writeFixtureHandshake(
                    handshakePath_,
                    handshake,
                    &handshakeError)) {
                throw std::runtime_error(
                    "public fixture handshake failed: " + handshakeError);
            }
        }
    }

    engine::editor::EditorProjectStats stats() const override {
        engine::editor::EditorProjectStats result;
        if (!session_) {
            return result;
        }
        result.sceneCount = 1u;
        result.drawClassCount = static_cast<std::uint32_t>(
            session_->activeContent().objects.size());
        result.visibleTriangleCount =
            static_cast<std::uint64_t>(drawStats_.triangles);
        result.shadowTriangleCount = 0u;
        result.archiveFileCount = session_->archiveFileCount();
        return result;
    }

    const char* status() const noexcept override {
        return status_.c_str();
    }

private:
    std::unique_ptr<engine::fixture::FixtureSession> session_;
    engine::fixture::FixtureCase fixtureCase_ =
        engine::fixture::FixtureCase::AFrame0;
    engine::fixture::FixtureDrawStats drawStats_{};
    std::string handshakePath_;
    std::string requestedBackend_;
    std::string status_ = "Public asset fixture idle.";
    int logicalFrame_ = 0;
    int renderCount_ = 0;
    bool replacementAttempted_ = false;
};

engine::editor::IEditorProjectRuntime* createRuntime() {
    return new FixtureEditorRuntime();
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
