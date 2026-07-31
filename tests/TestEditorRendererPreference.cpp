#include "engine/editor/EditorRendererPreference.h"

#include <string>

bool test_editor_renderer_preference_contract(
    std::string& outFail) {
    using engine::editor::EditorHostPlatform;
    using engine::editor::EditorRendererPreference;
    using engine::editor::parseEditorRendererPreference;
    using engine::editor::resolveEditorRendererPreference;

    if (parseEditorRendererPreference("DX12") !=
        EditorRendererPreference::D3D12) {
        outFail = "DX12 alias did not resolve to D3D12";
        return false;
    }
    if (parseEditorRendererPreference("vk") !=
        EditorRendererPreference::Vulkan) {
        outFail = "vk alias did not resolve to Vulkan";
        return false;
    }
    if (parseEditorRendererPreference("GL") !=
        EditorRendererPreference::OpenGL) {
        outFail = "GL alias did not resolve to OpenGL";
        return false;
    }
    if (resolveEditorRendererPreference(
            EditorRendererPreference::Auto,
            EditorHostPlatform::Windows) !=
        EditorRendererPreference::D3D12) {
        outFail = "Windows Auto did not prefer D3D12";
        return false;
    }
    if (resolveEditorRendererPreference(
            EditorRendererPreference::Auto,
            EditorHostPlatform::Linux) !=
        EditorRendererPreference::Vulkan) {
        outFail = "Linux Auto did not prefer Vulkan";
        return false;
    }
    if (resolveEditorRendererPreference(
            EditorRendererPreference::Auto,
            EditorHostPlatform::MacOS) !=
        EditorRendererPreference::OpenGL) {
        outFail =
            "macOS Auto did not retain the compatibility backend";
        return false;
    }
    return true;
}
