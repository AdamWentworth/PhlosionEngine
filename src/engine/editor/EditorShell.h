#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "engine/editor/EditorRendererPreference.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <SDL2/SDL.h>

struct SDL_Window;
class IRenderBackend;

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

enum class LayoutGizmoOperation {
    Translate,
    Rotate,
    Scale,
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
    int layoutObjectIndex = -1;
    bool folder = false;
    bool expandedByDefault = false;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceLayoutObject {
    std::string stableId;
    std::string displayName;
    std::string typeName;
    std::string coordinateSystem;
    std::string reason;
    std::string targetKind;
    std::string categoryPath;
    std::string prefabAssetId;
    std::array<float, 3> sourceTranslation{};
    std::array<float, 3> sourceRotationDegrees{};
    std::array<float, 3> sourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translation{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::array<std::int32_t, 2> terrainGridOrigin{};
    std::array<std::uint32_t, 2> terrainGridExtent{};
    std::int32_t terrainElevationLevel = 0;
    bool terrainGridBound = false;
    std::array<std::int32_t, 2> northBenchTerrainGridOrigin{};
    std::array<std::int32_t, 2> southBenchTerrainGridOrigin{};
    std::uint32_t benchTerrainGridExtent = 0u;
    std::uint32_t benchGapCells = 0u;
    bool northBenchTerrainGridBound = false;
    bool southBenchTerrainGridBound = false;
    std::array<float, 3> boundsMinimum{};
    std::array<float, 3> boundsMaximum{};
    std::array<float, 2> viewportPosition{};
    std::array<float, 6> viewportAxisDirections{};
    std::array<float, 3> viewportSourceUnitsPerPixel{};
    bool viewportVisible = false;
    bool suppressed = false;
    bool hasOverride = false;
};

struct WorkspaceTerrainTile {
    EditorProjectTerrainTileCoordinate coordinate{};
    std::int32_t sourceElevationLevel = 0;
    std::int32_t elevationLevel = 0;
    std::string sourceSurface;
    std::string surface;
    std::string shape;
    std::array<float, 8> viewportCorners{};
    bool viewportVisible = false;
    bool sourceOccupied = false;
    bool authored = false;
};

struct WorkspaceTerrainSurface {
    std::string id;
    std::string displayName;
};

struct WorkspaceAsset {
    std::string id;
    std::string displayName;
    std::string typeName;
    std::string category;
    std::string path;
    bool previewable3d = false;
    bool sceneInstantiable = false;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceAssetAnimation {
    std::string name;
    float durationSeconds = 0.0f;
};

enum class WorkspaceAssetPreviewKind {
    Model,
    VisualEffect,
};

struct WorkspaceAssetPreview {
    WorkspaceAssetPreviewKind kind =
        WorkspaceAssetPreviewKind::Model;
    std::string assetId;
    std::string status;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t textureCount = 0u;
    std::uint32_t boneCount = 0u;
    std::uint32_t activeElementCount = 0u;
    int animationIndex = -1;
    float animationTimeSeconds = 0.0f;
    float animationDurationSeconds = 0.0f;
    float boundsRadius = 1.0f;
    float boundsCenterY = 0.0f;
    bool ready = false;
    const std::vector<WorkspaceAssetAnimation>*
        animations = nullptr;
};

struct WorkspaceScene {
    std::string id;
    std::string displayName;
    std::string category;
    std::string environmentAssetId;
    std::string environmentDisplayName;
    std::string environmentKind;
    std::string path;
    std::string authoredScenePath;
    std::string runtimePath;
    std::string status;
    bool startup = false;
    std::vector<WorkspaceProperty> properties;
};

struct WorkspaceGamePreview {
    std::string id;
    std::string displayName;
    std::string group;
    std::string description;
    std::string sceneId;
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
    std::string_view sceneId;
    std::string_view scenePath;
    std::string_view backendName;
    EditorRendererPreference rendererPreference =
        EditorRendererPreference::Auto;
    std::string_view status;
    EditorPlayState playState = EditorPlayState::Editing;
    float simulationSeconds = 0.0f;
    const std::vector<WorkspacePlayConfiguration>*
        playConfigurations = nullptr;
    const std::vector<WorkspaceHierarchyItem>*
        hierarchyItems = nullptr;
    const std::vector<WorkspaceLayoutObject>*
        layoutObjects = nullptr;
    bool layoutOverlayVisible = false;
    bool boardClearanceSupported = false;
    bool terrainTileEditingSupported = false;
    bool canUndoSceneEdit = false;
    bool canRedoSceneEdit = false;
    const std::vector<WorkspaceAsset>* assets = nullptr;
    const std::vector<WorkspaceTerrainTile>* terrainTiles = nullptr;
    const std::vector<WorkspaceTerrainSurface>* terrainSurfaces = nullptr;
    const WorkspaceAssetPreview* assetPreview = nullptr;
    const std::vector<WorkspaceScene>* scenes = nullptr;
    std::string_view activeSceneId;
    const std::vector<WorkspaceGamePreview>*
        gamePreviews = nullptr;
    std::string_view activeGamePreviewId;
    EditorViewportKind activeViewport =
        EditorViewportKind::Scene;
    bool focusActiveViewport = false;
    std::uint64_t sceneTextureId = 0u;
    std::uint64_t gameTextureId = 0u;
    std::uint64_t assetPreviewTextureId = 0u;
    bool flipRenderSurfaceTexturesVertically = false;
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
    std::string_view backendName;
    EditorRendererPreference rendererPreference =
        EditorRendererPreference::Auto;
};

struct EditorShellActions {
    bool openProject = false;
    bool closeProject = false;
    bool exit = false;
    bool rendererPreferenceChanged = false;
    EditorRendererPreference rendererPreference =
        EditorRendererPreference::Auto;
    bool togglePlay = false;
    bool togglePause = false;
    bool step = false;
    int recentProjectIndex = -1;
    int launchPlayConfigurationIndex = -1;
    int selectGamePreviewIndex = -1;
    int openSceneIndex = -1;
    int selectAssetIndex = -1;
    int instantiateAssetIndex = -1;
    int selectLayoutObjectIndex = -1;
    int editLayoutObjectIndex = -1;
    std::vector<int> editLayoutObjectIndices;
    bool layoutObjectEditRequested = false;
    bool layoutObjectPreviewRequested = false;
    bool layoutObjectCommitRequested = false;
    bool layoutObjectCancelRequested = false;
    bool layoutObjectResetRequested = false;
    bool layoutObjectDuplicateRequested = false;
    bool layoutObjectDeleteRequested = false;
    bool layoutObjectRenameRequested = false;
    bool layoutObjectReparentRequested = false;
    bool undoSceneEditRequested = false;
    bool redoSceneEditRequested = false;
    bool applyBoardClearanceRequested = false;
    bool resetSceneToSourceRequested = false;
    std::string layoutObjectText;
    bool layoutOverlayVisibilityChanged = false;
    bool layoutOverlayVisible = false;
    EditorProjectBoardClearanceRequest boardClearanceRequest{};
    bool terrainTileEditRequested = false;
    std::vector<EditorProjectTerrainTileCoordinate>
        terrainTileCoordinates;
    std::string terrainTileOperation;
    std::string terrainTileSurface;
    std::string terrainTileShape;
    std::array<float, 3> layoutTranslation{};
    std::array<float, 3> layoutRotationDegrees{};
    std::array<float, 3> layoutScale{1.0f, 1.0f, 1.0f};
    bool layoutSuppressed = false;
    LayoutGizmoOperation layoutGizmoOperation =
        LayoutGizmoOperation::Translate;
    int assetPreviewWidth = 400;
    int assetPreviewHeight = 300;
    float assetPreviewOrbitYaw = 0.0f;
    float assetPreviewOrbitPitch = 0.0f;
    float assetPreviewPanX = 0.0f;
    float assetPreviewPanY = 0.0f;
    float assetPreviewZoom = 0.0f;
    bool resetAssetPreviewCamera = false;
    bool assetPreviewOptionsChanged = false;
    bool assetPreviewSeekRequested = false;
    float assetPreviewSeekTimeSeconds = 0.0f;
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

struct EditorTextureDescriptor {
    std::uint64_t cpuHandle = 0u;
    std::uint64_t gpuHandle = 0u;

    bool valid() const noexcept {
        return cpuHandle != 0u && gpuHandle != 0u;
    }
};

class EditorShell {
public:
    EditorShell();
    ~EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    bool initialize(
        SDL_Window* window,
        IRenderBackend* renderer,
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
    bool allocateTextureDescriptor(
        EditorTextureDescriptor& outDescriptor);
    void selectAsset(int assetIndex);

    bool wantsMouseCapture() const;
    bool wantsKeyboardCapture() const;
    bool initialized() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace engine::editor
