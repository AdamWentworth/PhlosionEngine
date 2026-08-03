#pragma once

#include "engine/editor/ProjectDescriptor.h"

#include <cstdint>
#include <string>

namespace engine::editor {

struct WorkspaceView;
struct EditorShellActions;

inline constexpr std::uint32_t kEditorPackagePluginAbiVersion = 1u;
inline constexpr char kEditorPackagePluginAbiSymbol[] =
    "phlosionEditorPackagePluginAbiVersion";
inline constexpr char kCreateEditorPackageSymbol[] =
    "phlosionCreateEditorPackage";
inline constexpr char kDestroyEditorPackageSymbol[] =
    "phlosionDestroyEditorPackage";

enum class EditorPackageExtensionPoint : std::uint8_t {
    ViewportToolbar = 0u,
    SceneViewportOverlay = 1u,
    Inspector = 2u,
};

struct EditorPackageOpenContext {
    const ProjectDescriptor* descriptor = nullptr;
    const EditorPackageDependency* dependency = nullptr;
    const char* descriptorPath = nullptr;
    const char* projectRoot = nullptr;
};

// Screen coordinates are supplied only for SceneViewportOverlay. The ImGui
// context is opaque here so EditorCore remains independent of the UI library;
// an editor package may cast it to ImGuiContext after including imgui.h.
struct EditorPackageDrawContext {
    void* imguiContext = nullptr;
    float viewportMinimumX = 0.0f;
    float viewportMinimumY = 0.0f;
    float viewportMaximumX = 0.0f;
    float viewportMaximumY = 0.0f;
    bool viewportHovered = false;
    bool viewportFocused = false;
};

class IEditorPackage {
public:
    virtual ~IEditorPackage() = default;

    virtual const char* packageId() const noexcept = 0;
    virtual const char* packageVersion() const noexcept = 0;
    virtual bool open(
        const EditorPackageOpenContext& context,
        std::string* outError = nullptr) = 0;

    // Lets a package temporarily own Scene-view pointer input (for example,
    // while a tile brush is active) without teaching the Engine what that
    // package edits.
    virtual bool capturesSceneViewport(
        const WorkspaceView& workspace) const noexcept {
        (void)workspace;
        return false;
    }

    // Return true when this package rendered/handled the extension point.
    virtual bool draw(
        EditorPackageExtensionPoint point,
        const EditorPackageDrawContext& context,
        const WorkspaceView& workspace,
        EditorShellActions& actions) = 0;
};

using EditorPackagePluginAbiVersionFn = std::uint32_t (*)();
using CreateEditorPackageFn = IEditorPackage* (*)();
using DestroyEditorPackageFn = void (*)(IEditorPackage*);

} // namespace engine::editor

#if defined(_WIN32)
#define PHLOSION_EDITOR_PACKAGE_EXPORT \
    extern "C" __declspec(dllexport)
#else
#define PHLOSION_EDITOR_PACKAGE_EXPORT \
    extern "C" __attribute__((visibility("default")))
#endif
