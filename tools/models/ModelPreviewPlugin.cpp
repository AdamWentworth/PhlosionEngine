#include "engine/editor/ModelPreviewRuntime.h"

namespace {
engine::editor::IEditorProjectRuntime* create() {
    return new engine::editor::ModelPreviewRuntime();
}
void destroy(engine::editor::IEditorProjectRuntime* runtime) { delete runtime; }
const engine::editor::EditorProjectPluginContract contract{
    engine::editor::kEditorProjectPluginAbiVersion,
    sizeof(engine::editor::EditorProjectPluginContract),
    engine::editor::kEditorProjectPluginLayoutFingerprint,
    PHLOSION_EDITOR_BUILD_CONFIGURATION,
    engine::editor::kEditorProjectPluginCompilerAbi, &create, &destroy};
}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorProjectPluginContract* phlosionEditorProjectPluginContract() {
    return &contract;
}
