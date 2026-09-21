// Phlosion Editor project plugin for the material fixture. It loads through the
// real plugin ABI and presents the same shared material fixture that the
// standalone runtime host uses.

#include "MaterialFixtureAssets.h"
#include "MaterialFixturePresentation.h"

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

class MaterialFixtureEditorRuntime final
    : public engine::editor::IEditorProjectRuntime {
public:
    bool open(const engine::editor::EditorProjectOpenContext& context,
              std::string* outError) override {
        (void)context;
        const std::string caseName = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_CASE");
        const std::optional<engine::material_fixture::FixtureCase> parsedCase =
            engine::material_fixture::parseFixtureCase(
                caseName.empty() ? "authored-maps" : caseName);
        if (!parsedCase) {
            if (outError) {
                *outError = "material fixture plugin does not know case: " +
                    caseName;
            }
            return false;
        }
        requestedBackend_ = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_API");
        if (requestedBackend_.empty()) {
            requestedBackend_ = "unknown";
        }
        handshakePath_ = envOrEmpty("PHLOSION_MATERIAL_FIXTURE_HANDSHAKE");
        std::string decodeError;
        if (!engine::material_fixture::decodeFixtureTextures(
                *parsedCase, textures_, &decodeError)) {
            if (outError) {
                *outError = "material fixture inputs failed: " + decodeError;
            }
            return false;
        }
        fixtureCase_ = *parsedCase;
        renderCount_ = 0;
        stats_ = engine::material_fixture::FixtureDrawStats{};
        status_ = std::string("Material fixture active: ") +
            engine::material_fixture::fixtureCaseName(fixtureCase_);
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
        std::string renderError;
        if (!engine::material_fixture::renderMaterialFixture(
                textures_,
                *context.renderer,
                context.surfaceWidth,
                context.surfaceHeight,
                &stats_,
                &renderError)) {
            throw std::runtime_error("material fixture render failed: " +
                                     renderError);
        }
        ++renderCount_;
        if (!handshakePath_.empty()) {
            const engine::material_fixture::FixtureHandshake handshake =
                engine::material_fixture::buildFixtureHandshake(
                    "editor",
                    requestedBackend_,
                    textures_,
                    *context.renderer,
                    context.surfaceWidth,
                    context.surfaceHeight,
                    stats_);
            std::string handshakeError;
            if (!engine::material_fixture::writeFixtureHandshake(
                    handshakePath_, handshake, &handshakeError)) {
                throw std::runtime_error("material fixture handshake failed: " +
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
    engine::material_fixture::FixtureCase fixtureCase_ =
        engine::material_fixture::FixtureCase::AuthoredMaps;
    engine::material_fixture::FixtureTextureSet textures_;
    engine::material_fixture::FixtureDrawStats stats_{};
    std::string handshakePath_;
    std::string requestedBackend_;
    std::string status_ = "Material fixture idle.";
    int renderCount_ = 0;
};

engine::editor::IEditorProjectRuntime* createRuntime() {
    return new MaterialFixtureEditorRuntime();
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
