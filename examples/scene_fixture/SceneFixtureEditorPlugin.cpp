// Phlosion Editor project plugin for the glTF scene fixture. It loads through
// the real plugin ABI and drives the same shared scene session as the
// standalone runtime host.

#include "SceneFixtureCase.h"
#include "SceneFixturePresentation.h"

#include "engine/core/Environment.h"
#include "engine/editor/EditorProjectPlugin.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::string envOrEmpty(const char* name) {
    const std::optional<std::string> value = engine::env::get(name);
    return value ? *value : std::string{};
}

class SceneFixtureEditorRuntime final
    : public engine::editor::IEditorProjectRuntime {
public:
    bool open(const engine::editor::EditorProjectOpenContext& context,
              std::string* outError) override {
        (void)context;
        const std::string caseName = envOrEmpty("PHLOSION_SCENE_FIXTURE_CASE");
        const std::optional<engine::scene_fixture::FixtureCase> parsedCase =
            engine::scene_fixture::parseFixtureCase(
                caseName.empty() ? "a-bind" : caseName);
        if (!parsedCase) {
            if (outError != nullptr) {
                *outError = "scene fixture plugin does not know case: " +
                            caseName;
            }
            return false;
        }
        requestedBackend_ = envOrEmpty("PHLOSION_SCENE_FIXTURE_API");
        if (requestedBackend_.empty()) {
            requestedBackend_ = "unknown";
        }
        handshakePath_ = envOrEmpty("PHLOSION_SCENE_FIXTURE_HANDSHAKE");
        std::string sessionError;
        if (!engine::scene_fixture::prepareFixtureSession(
                *parsedCase, session_, &sessionError)) {
            if (outError != nullptr) {
                *outError = "scene fixture session failed: " + sessionError;
            }
            return false;
        }
        renderCount_ = 0;
        stats_ = engine::scene_fixture::FixtureDrawStats{};
        status_ = std::string("Scene fixture active: ") +
                  engine::scene_fixture::fixtureCaseName(*parsedCase);
        if (outError != nullptr) {
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
        if (context.renderer == nullptr || context.surfaceWidth <= 0 ||
            context.surfaceHeight <= 0) {
            return;
        }
        // The authored lifecycle uses the same frame numbering as the
        // standalone host, so the transition precedes the requested capture
        // frame in both hosts.
        std::string error;
        if (!engine::scene_fixture::advanceFixtureSession(
                session_, renderCount_ + 1, &error)) {
            throw std::runtime_error("scene fixture lifecycle failed: " + error);
        }
        if (!engine::scene_fixture::drawFixtureSession(*context.renderer, session_,
                                                       context.surfaceWidth,
                                                       context.surfaceHeight,
                                                       &stats_, &error)) {
            throw std::runtime_error("scene fixture render failed: " + error);
        }
        ++renderCount_;
        const int captureFrame = std::stoi(engine::env::get(
            "PHLOSION_BACKEND_SCREENSHOT_FRAME").value_or("6"));
        if (!handshakePath_.empty() && renderCount_ == captureFrame) {
            const engine::scene_fixture::FixtureHandshake handshake =
                engine::scene_fixture::buildFixtureHandshake(
                    "editor", requestedBackend_, session_, stats_,
                    context.surfaceWidth, context.surfaceHeight,
                    *context.renderer);
            std::string handshakeError;
            if (!engine::scene_fixture::writeFixtureHandshake(
                    handshakePath_, handshake, &handshakeError)) {
                throw std::runtime_error("scene fixture handshake failed: " +
                                         handshakeError);
            }
        }
    }

    engine::editor::EditorProjectStats stats() const override {
        engine::editor::EditorProjectStats result;
        result.sceneCount = 1u;
        result.drawClassCount = 2u;
        result.visibleTriangleCount =
            static_cast<std::uint64_t>(stats_.triangles);
        result.shadowTriangleCount = 0u;
        result.archiveFileCount = 0u;
        return result;
    }

    const char* status() const noexcept override {
        return status_.c_str();
    }

private:
    engine::scene_fixture::FixtureSession session_;
    engine::scene_fixture::FixtureDrawStats stats_{};
    std::string handshakePath_;
    std::string requestedBackend_;
    std::string status_ = "Scene fixture idle.";
    int renderCount_ = 0;
};

engine::editor::IEditorProjectRuntime* createRuntime() {
    return new SceneFixtureEditorRuntime();
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

}  // namespace

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
