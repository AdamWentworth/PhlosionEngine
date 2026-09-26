#include "engine/editor/EditorSdlGameInput.h"
#include "engine/editor/EditorProjectPlugin.h"
namespace {
using namespace engine::editor;
IEditorProjectRuntime* create(){return nullptr;}
void destroy(IEditorProjectRuntime*){}
const EditorProjectPluginContract contract{kEditorProjectPluginAbiVersion,sizeof(EditorProjectPluginContract),kEditorProjectPluginLayoutFingerprint,PHLOSION_EDITOR_BUILD_CONFIGURATION,kEditorProjectPluginCompilerAbi,&create,&destroy};
struct Header {std::uint32_t version=1,size=16;std::uint64_t fingerprint=kEditorSdlGameInputFingerprint;} malformed;
void begin(IEditorProjectRuntime*, const EditorSdlGameInputContext*) {}
void event(IEditorProjectRuntime*, const EditorSdlGameInputContext*, const SDL_Event*) {}
void end(IEditorProjectRuntime*, const EditorSdlGameInputContext*) {}
const EditorSdlGameInputExtension valid{1,sizeof(EditorSdlGameInputExtension),
    kEditorSdlGameInputFingerprint,&begin,&event,&end};
}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorProjectPluginContract* phlosionEditorProjectPluginContract(){return &contract;}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorSdlGameInputExtension* phlosionEditorSdlGameInput(){
#if defined(PHLOSION_TEST_VALID_SDL_EXTENSION)
    return &valid;
#else
    return reinterpret_cast<const engine::editor::EditorSdlGameInputExtension*>(&malformed);
#endif
}
