#pragma once

#include <string_view>

namespace engine::editor {

enum class EditorRendererPreference {
    Auto,
    D3D12,
    Vulkan,
    OpenGL,
};

enum class EditorHostPlatform {
    Windows,
    Linux,
    MacOS,
    Other,
};

EditorRendererPreference parseEditorRendererPreference(
    std::string_view value);
const char* editorRendererPreferenceName(
    EditorRendererPreference preference) noexcept;
EditorRendererPreference resolveEditorRendererPreference(
    EditorRendererPreference preference,
    EditorHostPlatform platform) noexcept;
EditorHostPlatform currentEditorHostPlatform() noexcept;

} // namespace engine::editor
