#pragma once

#include "engine/editor/EditorPackagePlugin.h"
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
    std::string inspectorTitle;
    std::string inspectorSummary;
    std::string translationLabel;
    std::string viewportHint;
    std::string resetLabel;
    std::string scaleReadOnlyLabel;
    std::string scaleReadOnlyDescription;
    std::uint32_t capabilities =
        kEditorProjectLayoutDefaultCapabilities;
    std::uint8_t viewportMask =
        EditorProjectLayoutViewportScene;
    std::array<float, 3> translationSnap{};
    std::array<float, 3> fineTranslationSnap{};
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
    bool useGridTranslationEditor = false;
    std::array<EditorProjectGridRegion, 4> terrainRegions{};
    std::size_t terrainRegionCount = 0u;
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
    std::string sourceShape;
    std::string surface;
    std::string shape;
    std::string visualVariant;
    EditorProjectTerrainTileCoordinate sourceReference{};
    std::array<float, 8> viewportCorners{};
    std::array<float, 8> viewportFlatCorners{};
    std::array<float, 8> viewportLevelStep{};
    bool viewportVisible = false;
    bool sourceOccupied = false;
    bool authored = false;
    bool hasSourceReference = false;
    bool receivesProjectedShadow = true;
    bool normalizeSourceTint = false;
    bool suppressOverlappingVegetation = false;
};

struct WorkspaceTerrainSurface {
    std::string id;
    std::string displayName;
};

struct WorkspaceTerrainPrefab {
    std::string id;
    std::string displayName;
    std::string category;
    std::string surface;
    std::string shape;
    std::string visualVariant;
    std::uint32_t previewTopRgba = 0x6f7a82ffu;
    std::uint32_t previewSideRgba = 0x343b40ffu;
    std::uint32_t previewAccentRgba = 0xaeb7bdffu;
    std::uint32_t previewConnectionMask = 0xffffffffu;
    EditorProjectTerrainPrefabKind kind =
        EditorProjectTerrainPrefabKind::Ground;
    std::int32_t elevationDelta = 0;
};

struct WorkspaceTerrainTileStamp {
    std::int32_t offsetGridX = 0;
    std::int32_t offsetGridZ = 0;
    std::int32_t relativeElevationLevel = 0;
    std::int32_t absoluteElevationLevel = 0;
    std::string surface;
    std::string shape;
    std::string visualVariant;
    EditorProjectTerrainTileCoordinate sourceReference{};
    bool hasSourceReference = false;
    bool receivesProjectedShadow = true;
    bool normalizeSourceTint = false;
    bool suppressOverlappingVegetation = false;
};

struct WorkspaceProjectCommandField {
    std::string id;
    std::string displayName;
    std::string description;
    EditorProjectCommandFieldKind kind =
        EditorProjectCommandFieldKind::Boolean;
    bool booleanValue = false;
    float floatValue = 0.0f;
    float minimumFloat = 0.0f;
    float maximumFloat = 1.0f;
    float stepFloat = 0.05f;
};

struct WorkspaceProjectCommand {
    std::string id;
    std::string displayName;
    std::string category;
    std::string description;
    std::string buttonLabel;
    std::string confirmationText;
    std::vector<WorkspaceProjectCommandField> fields;
    bool confirmationRequired = false;
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
    int graphicsQuality = 3;
    int materialDebugView = 0;
    int lightingProfile = 1;
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
    std::string_view gameplayReloadStatus;
    std::string_view gameplayBuildLog;
    bool gameplayReloadAvailable = false;
    bool gameplayAutoReload = false;
    bool gameplayBuilding = false;
    EditorPlayState playState = EditorPlayState::Editing;
    float simulationSeconds = 0.0f;
    const std::vector<WorkspacePlayConfiguration>*
        playConfigurations = nullptr;
    const std::vector<WorkspaceHierarchyItem>*
        hierarchyItems = nullptr;
    const std::vector<WorkspaceLayoutObject>*
        layoutObjects = nullptr;
    bool layoutOverlayVisible = false;
    bool terrainTileEditingSupported = false;
    bool canUndoSceneEdit = false;
    bool canRedoSceneEdit = false;
    const std::vector<WorkspaceAsset>* assets = nullptr;
    const std::vector<WorkspaceTerrainTile>* terrainTiles = nullptr;
    const std::vector<WorkspaceTerrainSurface>* terrainSurfaces = nullptr;
    const std::vector<WorkspaceTerrainPrefab>* terrainPrefabs = nullptr;
    std::vector<WorkspaceProjectCommand>* projectCommands = nullptr;
    const WorkspaceAssetPreview* assetPreview = nullptr;
    const std::vector<WorkspaceScene>* scenes = nullptr;
    std::string_view activeSceneId;
    const std::vector<WorkspaceGamePreview>*
        gamePreviews = nullptr;
    // Optional project-declared editor packages. The shell knows only the
    // extension contract; package-owned tools decide whether to draw or
    // capture viewport input for this workspace.
    const std::vector<IEditorPackage*>* editorPackages = nullptr;
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
    bool rebuildGameplay = false;
    bool toggleGameplayAutoReload = false;
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
    int executeProjectCommandIndex = -1;
    std::string layoutObjectText;
    bool layoutOverlayVisibilityChanged = false;
    bool layoutOverlayVisible = false;
    bool terrainTileEditRequested = false;
    std::vector<EditorProjectTerrainTileCoordinate>
        terrainTileCoordinates;
    std::string terrainTileOperation;
    std::string terrainTileSurface;
    std::string terrainTileShape;
    std::string terrainTileVisualVariant;
    std::int32_t terrainTileTargetElevationLevel = 0;
    std::int32_t terrainTileRelativeElevationDelta = 0;
    std::vector<WorkspaceTerrainTileStamp> terrainTileStampTiles;
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
    int assetPreviewGraphicsQuality = 3;
    int assetPreviewMaterialDebugView = 0;
    int assetPreviewLightingProfile = 1;
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
    bool isEditingText() const;
    bool initialized() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace engine::editor
