#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <string>

namespace engine::editor {

bool validateEditorProjectPluginContract(
    const EditorProjectPluginContract* contract,
    const char* expectedBuildConfiguration,
    std::string& outError);

} // namespace engine::editor
