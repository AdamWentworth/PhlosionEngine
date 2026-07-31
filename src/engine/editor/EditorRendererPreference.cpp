#include "engine/editor/EditorRendererPreference.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace engine::editor {

EditorRendererPreference parseEditorRendererPreference(
    std::string_view value) {
    std::string token(value);
    std::transform(
        token.begin(),
        token.end(),
        token.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (token == "d3d12" || token == "direct3d12" ||
        token == "direct3d-12" || token == "dx12") {
        return EditorRendererPreference::D3D12;
    }
    if (token == "vulkan" || token == "vk") {
        return EditorRendererPreference::Vulkan;
    }
    if (token == "opengl" || token == "gl" ||
        token == "opengl3") {
        return EditorRendererPreference::OpenGL;
    }
    return EditorRendererPreference::Auto;
}

const char* editorRendererPreferenceName(
    EditorRendererPreference preference) noexcept {
    switch (preference) {
        case EditorRendererPreference::D3D12:
            return "d3d12";
        case EditorRendererPreference::Vulkan:
            return "vulkan";
        case EditorRendererPreference::OpenGL:
            return "opengl";
        case EditorRendererPreference::Auto:
        default:
            return "auto";
    }
}

EditorRendererPreference resolveEditorRendererPreference(
    EditorRendererPreference preference,
    EditorHostPlatform platform) noexcept {
    if (preference != EditorRendererPreference::Auto) {
        return preference;
    }
    switch (platform) {
        case EditorHostPlatform::Windows:
            return EditorRendererPreference::D3D12;
        case EditorHostPlatform::Linux:
            return EditorRendererPreference::Vulkan;
        case EditorHostPlatform::MacOS:
        case EditorHostPlatform::Other:
        default:
            return EditorRendererPreference::OpenGL;
    }
}

EditorHostPlatform currentEditorHostPlatform() noexcept {
#if defined(_WIN32)
    return EditorHostPlatform::Windows;
#elif defined(__APPLE__)
    return EditorHostPlatform::MacOS;
#elif defined(__linux__)
    return EditorHostPlatform::Linux;
#else
    return EditorHostPlatform::Other;
#endif
}

} // namespace engine::editor
