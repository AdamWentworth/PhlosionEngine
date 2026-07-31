#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <SDL2/SDL.h>

struct SDL_Window;

namespace engine::editor {

enum class EditorPlayState {
    Editing,
    Playing,
    Paused,
};

enum class EditorViewportKind {
    Scene,
    Game,
};

struct WorkspaceProperty {
    std::string name;
    std::string value;
};

struct WorkspaceHierarchyItem {
    std::string id;
    std::string displayName;
    std::string typeName;
    int depth = 0;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceAsset {
    std::string id;
    std::string displayName;
    std::string typeName;
    std::string category;
    std::string path;
    bool previewable3d = false;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceAssetAnimation {
    std::string name;
    float durationSeconds = 0.0f;
};

struct WorkspaceAssetPreview {
    std::string assetId;
    std::string status;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t textureCount = 0u;
    std::uint32_t boneCount = 0u;
    float boundsRadius = 1.0f;
    float boundsCenterY = 0.0f;
    bool ready = false;
    const std::vector<WorkspaceAssetAnimation>*
        animations = nullptr;
};

struct WorkspaceScene {
    std::string assetId;
    std::string displayName;
    std::string category;
    std::string kind;
    std::string path;
    std::string previewId;
    bool startup = false;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceGamePreview {
    std::string id;
    std::string displayName;
    std::string group;
    std::string description;
};

struct WorkspacePlayConfiguration {
    std::string id;
    std::string displayName;
    std::string group;
    std::string description;
    std::string executablePath;
    bool available = false;
};

struct WorkspaceView {
    std::string_view projectName;
    std::string_view projectId;
    std::string_view projectRoot;
    std::string_view sceneAssetId;
    std::string_view scenePath;
    std::string_view backendName;
    std::string_view status;
    EditorPlayState playState = EditorPlayState::Editing;
    float simulationSeconds = 0.0f;
    const std::vector<WorkspacePlayConfiguration>*
        playConfigurations = nullptr;
    const std::vector<WorkspaceHierarchyItem>*
        hierarchyItems = nullptr;
    const std::vector<WorkspaceAsset>* assets = nullptr;
    const WorkspaceAssetPreview* assetPreview = nullptr;
    const std::vector<WorkspaceScene>* scenes = nullptr;
    const std::vector<WorkspaceGamePreview>*
        gamePreviews = nullptr;
    std::string_view activeGamePreviewId;
    EditorViewportKind activeViewport =
        EditorViewportKind::Scene;
    bool focusActiveViewport = false;
    std::uint64_t sceneTextureId = 0u;
    std::uint64_t gameTextureId = 0u;
    std::uint64_t assetPreviewTextureId = 0u;
    std::uint32_t sceneCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t drawClassCount = 0u;
    std::uint32_t encounterGrassInstanceCount = 0u;
    std::uint32_t vegetationInstanceCount = 0u;
    std::uint64_t visibleTriangleCount = 0u;
    std::uint64_t shadowTriangleCount = 0u;
    std::size_t archiveFileCount = 0u;
};

struct ProjectBrowserView {
    std::string_view status;
    std::string_view error;
    const std::vector<std::string>* recentProjects = nullptr;
};

struct EditorShellActions {
    bool openProject = false;
    bool closeProject = false;
    bool exit = false;
    bool togglePlay = false;
    bool togglePause = false;
    bool step = false;
    int recentProjectIndex = -1;
    int launchPlayConfigurationIndex = -1;
    int selectGamePreviewIndex = -1;
    int openSceneIndex = -1;
    int selectAssetIndex = -1;
    int assetPreviewWidth = 400;
    int assetPreviewHeight = 300;
    float assetPreviewOrbitYaw = 0.0f;
    float assetPreviewOrbitPitch = 0.0f;
    float assetPreviewPanX = 0.0f;
    float assetPreviewPanY = 0.0f;
    float assetPreviewZoom = 0.0f;
    bool resetAssetPreviewCamera = false;
    bool assetPreviewOptionsChanged = false;
    int assetPreviewAnimationIndex = -1;
    float assetPreviewPlaybackSpeed = 1.0f;
    bool assetPreviewAnimationPlaying = true;
    bool assetPreviewShowMesh = true;
    bool assetPreviewShowMaterials = true;
    bool assetPreviewShowTextures = true;
    bool assetPreviewShowWireframe = false;
    bool assetPreviewShowSkeleton = false;
    EditorViewportKind activeViewport =
        EditorViewportKind::Scene;
    int viewportWidth = 1280;
    int viewportHeight = 720;
    float viewportScreenX = 0.0f;
    float viewportScreenY = 0.0f;
    bool viewportHovered = false;
    bool viewportFocused = false;
};

class EditorShell {
public:
    EditorShell();
    ~EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    bool initialize(
        SDL_Window* window,
        std::string* outError = nullptr,
        const char* settingsIniPath = nullptr);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame(float deltaSeconds);
    EditorShellActions drawProjectBrowser(
        const ProjectBrowserView& browser);
    EditorShellActions drawWorkspace(
        const WorkspaceView& workspace);
    void render();

    bool wantsMouseCapture() const;
    bool wantsKeyboardCapture() const;
    bool initialized() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace engine::editor
