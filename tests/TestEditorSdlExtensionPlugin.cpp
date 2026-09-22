#include "engine/editor/EditorSdlGameInput.h"
#include "engine/editor/EditorProjectPlugin.h"
namespace {
using namespace engine::editor;
IEditorProjectRuntime* create(){return nullptr;}
void destroy(IEditorProjectRuntime*){}
const EditorProjectPluginContract contract{kEditorProjectPluginAbiVersion,sizeof(EditorProjectPluginContract),kEditorProjectPluginLayoutFingerprint,PHLOSION_EDITOR_BUILD_CONFIGURATION,kEditorProjectPluginCompilerAbi,&create,&destroy};
struct Header {std::uint32_t version=1,size=16;std::uint64_t fingerprint=kEditorSdlGameInputFingerprint;} malformed;
}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorProjectPluginContract* phlosionEditorProjectPluginContract(){return &contract;}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorSdlGameInputExtension* phlosionEditorSdlGameInput(){return reinterpret_cast<const engine::editor::EditorSdlGameInputExtension*>(&malformed);}
