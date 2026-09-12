#pragma once

#include "engine/editor/ProjectDescriptor.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class Camera3D;

namespace engine::editor {

inline constexpr std::uint32_t kEditorProjectPluginAbiVersion = 32u;
inline constexpr char kEditorProjectPluginContractSymbol[] =
    "phlosionEditorProjectPluginContract";
inline constexpr char kEditorProjectPluginAbiSymbol[] =
    "phlosionEditorProjectPluginAbiVersion";
inline constexpr char kCreateEditorProjectRuntimeSymbol[] =
    "phlosionCreateEditorProjectRuntime";
inline constexpr char kDestroyEditorProjectRuntimeSymbol[] =
    "phlosionDestroyEditorProjectRuntime";

struct EditorProjectOpenContext {
    const ProjectDescriptor* descriptor = nullptr;
    const char* descriptorPath = nullptr;
    const char* projectRoot = nullptr;
    const char* startupScenePath = nullptr;
};

struct EditorProjectCameraContext {
    const float* cameraWorldPosition3 = nullptr;
    const float* cameraForward3 = nullptr;
    const float* cameraTarget3 = nullptr;
};

struct EditorProjectRenderContext {
    IRenderBackend* renderer = nullptr;
    const float* viewProjectionMatrix4x4 = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    const float* cameraWorldPosition3 = nullptr;
    const float* cameraForward3 = nullptr;
    const float* cameraTarget3 = nullptr;
};

struct EditorProjectStats {
    std::uint32_t sceneCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t drawClassCount = 0u;
    std::uint64_t visibleTriangleCount = 0u;
    std::uint64_t shadowTriangleCount = 0u;
    std::size_t archiveFileCount = 0u;
};

struct EditorProjectSceneContext {
    const char* sceneId = nullptr;
    const char* displayName = nullptr;
    const char* environmentAssetId = nullptr;
    const char* environmentKind = nullptr;
    const char* environmentPath = nullptr;
    const char* authoredScenePath = nullptr;
    const char* runtimePath = nullptr;
    const char* status = nullptr;
};

struct EditorProjectGamePreview {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* group = nullptr;
    const char* description = nullptr;
    const char* sceneId = nullptr;
};

struct EditorProjectAsset {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* typeName = nullptr;
    const char* category = nullptr;
    const char* path = nullptr;
    const char* description = nullptr;
    bool previewable = false;
    bool sceneInstantiable = false;
};

enum class EditorProjectAssetPreviewKind : std::uint8_t {
    Model = 0u,
    VisualEffect = 1u,
};

struct EditorProjectGamePreviewContext {
    IRenderBackend* renderer = nullptr;
    Camera3D* camera = nullptr;
    int surfaceWidth = 1280;
    int surfaceHeight = 720;
};

struct EditorProjectAssetPreviewOptions {
    int animationIndex = -1;
    int graphicsQuality = 3;
    int materialDebugView = 0;
    int lightingProfile = 1;
    float playbackSpeed = 1.0f;
    float seekTimeSeconds = 0.0f;
    bool seekRequested = false;
    bool animationPlaying = true;
    bool showMesh = true;
    bool showMaterials = true;
    bool showTextures = true;
    bool showWireframe = false;
    bool showSkeleton = false;
};

struct EditorProjectAssetPreviewInfo {
    EditorProjectAssetPreviewKind kind =
        EditorProjectAssetPreviewKind::Model;
    const char* assetId = nullptr;
    const char* status = nullptr;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t textureCount = 0u;
    std::uint32_t boneCount = 0u;
    std::uint32_t activeElementCount = 0u;
    std::size_t animationCount = 0u;
    int animationIndex = -1;
    int graphicsQuality = 3;
    int materialDebugView = 0;
    int lightingProfile = 1;
    float animationTimeSeconds = 0.0f;
    float animationDurationSeconds = 0.0f;
    float boundsRadius = 1.0f;
    float boundsCenterY = 0.0f;
    bool ready = false;
};

struct EditorProjectAssetAnimation {
    const char* name = nullptr;
    float durationSeconds = 0.0f;
};

enum EditorProjectLayoutCapability : std::uint32_t {
    EditorProjectLayoutTranslate = 1u << 0u,
    EditorProjectLayoutRotate = 1u << 1u,
    EditorProjectLayoutScale = 1u << 2u,
    EditorProjectLayoutRename = 1u << 3u,
    EditorProjectLayoutReparent = 1u << 4u,
    EditorProjectLayoutDuplicate = 1u << 5u,
    EditorProjectLayoutDelete = 1u << 6u,
    EditorProjectLayoutSuppress = 1u << 7u,
    EditorProjectLayoutReset = 1u << 8u,
};

inline constexpr std::uint32_t kEditorProjectLayoutDefaultCapabilities =
    EditorProjectLayoutTranslate |
    EditorProjectLayoutRotate |
    EditorProjectLayoutScale |
    EditorProjectLayoutRename |
    EditorProjectLayoutReparent |
    EditorProjectLayoutDuplicate |
    EditorProjectLayoutDelete |
    EditorProjectLayoutSuppress |
    EditorProjectLayoutReset;

enum EditorProjectLayoutViewport : std::uint8_t {
    EditorProjectLayoutViewportNone = 0u,
    EditorProjectLayoutViewportScene = 1u << 0u,
    EditorProjectLayoutViewportGame = 1u << 1u,
};

struct EditorProjectGridRegion {
    const char* label = nullptr;
    std::array<std::int32_t, 2> origin{};
    std::array<std::uint32_t, 2> extent{};
    std::uint32_t outlineRgba = 0x5bd9e6ffu;
};

struct EditorProjectLayoutObject {
    const char* stableId = nullptr;
    const char* displayName = nullptr;
    const char* typeName = nullptr;
    const char* coordinateSystem = nullptr;
    const char* reason = nullptr;
    const char* targetKind = nullptr;
    const char* categoryPath = nullptr;
    const char* prefabAssetId = nullptr;
    const char* inspectorTitle = nullptr;
    const char* inspectorSummary = nullptr;
    const char* translationLabel = nullptr;
    const char* viewportHint = nullptr;
    const char* resetLabel = nullptr;
    const char* scaleReadOnlyLabel = nullptr;
    const char* scaleReadOnlyDescription = nullptr;
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
    // Selects the reusable integer grid-origin/elevation Inspector instead of
    // the ordinary floating-point translation editor. Terrain binding alone
    // does not imply that an object should be positioned in grid coordinates.
    bool useGridTranslationEditor = false;
    std::array<EditorProjectGridRegion, 4> terrainRegions{};
    std::size_t terrainRegionCount = 0u;
    // Current source-space AABB used by project-owned clearance tools. The
    // project adapter derives this from the exact prefab geometry rather than
    // asking the editor shell to guess an obstruction radius.
    std::array<float, 3> boundsMinimum{};
    std::array<float, 3> boundsMaximum{};
    // Scene-viewport projection supplied by the project adapter. Positions
    // are local to the rendered scene surface rather than desktop pixels.
    std::array<float, 2> viewportPosition{};
    // Normalized screen directions for the source-local X, Y and Z axes.
    std::array<float, 6> viewportAxisDirections{};
    // Source translation units represented by one screen pixel per axis.
    std::array<float, 3> viewportSourceUnitsPerPixel{};
    bool viewportVisible = false;
    bool suppressed = false;
    bool hasOverride = false;
};

struct EditorProjectLayoutEdit {
    const char* stableId = nullptr;
    std::array<float, 3> translation{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = false;
    const char* reason = nullptr;
};

struct EditorProjectLayoutObjectCommand {
    const char* stableId = nullptr;
    const char* value = nullptr;
};

enum class EditorProjectCommandFieldKind : std::uint8_t {
    Boolean = 0u,
    Float = 1u,
};

struct EditorProjectCommandField {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* description = nullptr;
    EditorProjectCommandFieldKind kind =
        EditorProjectCommandFieldKind::Boolean;
    bool defaultBoolean = false;
    float defaultFloat = 0.0f;
    float minimumFloat = 0.0f;
    float maximumFloat = 1.0f;
    float stepFloat = 0.05f;
};

struct EditorProjectCommand {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* category = nullptr;
    const char* description = nullptr;
    const char* buttonLabel = nullptr;
    const char* confirmationText = nullptr;
    const EditorProjectCommandField* fields = nullptr;
    std::size_t fieldCount = 0u;
    bool confirmationRequired = false;
};

struct EditorProjectCommandValue {
    const char* id = nullptr;
    bool booleanValue = false;
    float floatValue = 0.0f;
};

struct EditorProjectCommandResult {
    const char* status = nullptr;
    bool sceneChanged = false;
    bool assetsChanged = false;
};

struct EditorProjectTerrainTileCoordinate {
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
};

struct EditorProjectTerrainTile {
    EditorProjectTerrainTileCoordinate coordinate{};
    std::int32_t sourceElevationLevel = 0;
    std::int32_t elevationLevel = 0;
    const char* sourceSurface = nullptr;
    const char* sourceShape = nullptr;
    const char* surface = nullptr;
    const char* shape = nullptr;
    const char* visualVariant = nullptr;
    EditorProjectTerrainTileCoordinate sourceReference{};
    std::array<float, 8> viewportCorners{};
    // Projected flat corners at elevationLevel plus the per-corner screen
    // delta for one +50 cm source level. The editor uses these to draw an
    // exact-height platform ghost before an authored edit is committed.
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

struct EditorProjectTerrainSurface {
    const char* id = nullptr;
    const char* displayName = nullptr;
};

enum class EditorProjectTerrainPrefabKind : std::uint8_t {
    Ground = 0u,
    Ramp = 1u,
    Platform = 2u,
};

// A project-authored, renderable terrain combination. The editor deliberately
// consumes these instead of manufacturing a surface x shape cartesian product:
// not every project surface is valid for every project shape.
struct EditorProjectTerrainPrefab {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* category = nullptr;
    const char* surface = nullptr;
    const char* shape = nullptr;
    // Project-defined appearance inside one surface/shape combination.
    const char* visualVariant = nullptr;
    // Project-authored RGBA8 colors packed as 0xRRGGBBAA. They describe the
    // Inspector thumbnail, not a replacement for the runtime material.
    std::uint32_t previewTopRgba = 0x6f7a82ffu;
    std::uint32_t previewSideRgba = 0x343b40ffu;
    std::uint32_t previewAccentRgba = 0xaeb7bdffu;
    // NESW connection bits used only by the generic Inspector thumbnail.
    // UINT32_MAX means this is not a connected-path preview.
    std::uint32_t previewConnectionMask = 0xffffffffu;
    EditorProjectTerrainPrefabKind kind =
        EditorProjectTerrainPrefabKind::Ground;
    // Relative source-level change applied when this prefab is chosen. A
    // raised platform uses +1; ordinary ground and ramps preserve elevation.
    std::int32_t elevationDelta = 0;
};

struct EditorProjectTerrainTileStamp {
    std::int32_t offsetGridX = 0;
    std::int32_t offsetGridZ = 0;
    // Signed height from the copied anchor cell. Relative paste maps that
    // anchor to the destination cell and preserves every internal tier.
    std::int32_t relativeElevationLevel = 0;
    // Original source-grid height used by exact paste. This is intentionally
    // separate from the relative value so restoring a clipped source terrain
    // assembly never depends on the destination cell's current height.
    std::int32_t absoluteElevationLevel = 0;
    const char* surface = nullptr;
    const char* shape = nullptr;
    const char* visualVariant = nullptr;
    EditorProjectTerrainTileCoordinate sourceReference{};
    bool hasSourceReference = false;
    bool receivesProjectedShadow = true;
    bool normalizeSourceTint = false;
    bool suppressOverlappingVegetation = false;
};

struct EditorProjectTerrainTileEditRequest {
    const EditorProjectTerrainTileCoordinate* coordinates = nullptr;
    std::size_t coordinateCount = 0u;
    // create, raise, lower, terrace_raise, terrace_lower, flatten_tidy,
    // tidy_surface, platform_set, swap_prefab, paste_tiles_relative,
    // paste_tiles_exact, paint_surface, set_shape,
    // suppress_overlapping_vegetation, restore_overlapping_vegetation,
    // or restore_source.
    const char* operation = nullptr;
    const char* surface = nullptr;
    const char* shape = nullptr;
    const char* visualVariant = nullptr;
    std::int32_t targetElevationLevel = 0;
    std::int32_t relativeElevationDelta = 0;
    const EditorProjectTerrainTileStamp* stampTiles = nullptr;
    std::size_t stampTileCount = 0u;
};

class IEditorProjectRuntime {
public:
    virtual ~IEditorProjectRuntime() = default;

    virtual bool open(
        const EditorProjectOpenContext& context,
        std::string* outError = nullptr) = 0;
    virtual void prewarm(
        IRenderBackend& renderer,
        const EditorProjectCameraContext& camera) = 0;
    virtual void update(float simulationSeconds) = 0;
    virtual void render(
        const EditorProjectRenderContext& context) = 0;
    virtual EditorProjectStats stats() const = 0;
    virtual const char* status() const noexcept = 0;

    virtual bool openScene(
        const EditorProjectSceneContext& context,
        std::string* outError = nullptr) {
        (void)context;
        if (outError) {
            *outError =
                "This project does not support switching cooked scenes.";
        }
        return false;
    }

    virtual std::size_t gamePreviewCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectGamePreview gamePreview(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual bool initializeGamePreview(
        const EditorProjectGamePreviewContext& context,
        std::string* outError = nullptr) {
        (void)context;
        if (outError) {
            *outError =
                "This project does not provide an embedded game preview.";
        }
        return false;
    }
    virtual bool selectGamePreview(
        const char* id,
        std::string* outError = nullptr) {
        (void)id;
        if (outError) {
            *outError =
                "This project does not provide an embedded game preview.";
        }
        return false;
    }
    virtual void resetGamePreview() {}
    virtual void fixedUpdateGamePreview(float deltaSeconds) {
        (void)deltaSeconds;
    }
    virtual void renderGamePreview(
        const EditorProjectRenderContext& context) {
        (void)context;
    }
    virtual void handleGamePreviewInput(
        const InputEvent& event) {
        (void)event;
    }
    virtual bool gamePreviewReady() const noexcept {
        return false;
    }

    virtual std::size_t assetCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectAsset asset(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual bool instantiateAsset(
        const char* assetId,
        std::string* outCreatedStableId = nullptr,
        std::string* outError = nullptr) {
        (void)assetId;
        if (outCreatedStableId) {
            outCreatedStableId->clear();
        }
        if (outError) {
            *outError =
                "This asset cannot be instantiated in the active scene.";
        }
        return false;
    }
    virtual bool selectAssetPreview(
        const char* assetId,
        const char* assetPath,
        std::string* outError = nullptr) {
        (void)assetId;
        (void)assetPath;
        if (outError) {
            *outError =
                "This project does not provide cooked asset previews.";
        }
        return false;
    }
    virtual EditorProjectAssetPreviewInfo
    assetPreviewInfo() const noexcept {
        return {};
    }
    virtual EditorProjectAssetAnimation
    assetPreviewAnimation(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual void setAssetPreviewOptions(
        const EditorProjectAssetPreviewOptions& options) {
        (void)options;
    }
    virtual void updateAssetPreview(float deltaSeconds) {
        (void)deltaSeconds;
    }
    virtual void renderAssetPreview(
        const EditorProjectRenderContext& context) {
        (void)context;
    }

    virtual std::size_t layoutObjectCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectLayoutObject layoutObject(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual bool setLayoutObjectOverride(
        const EditorProjectLayoutEdit& edit,
        std::string* outError = nullptr) {
        (void)edit;
        if (outError) {
            *outError =
                "This project does not provide editable scene layout objects.";
        }
        return false;
    }
    // Applies an in-memory edit for responsive viewport/inspector feedback.
    // The project must not persist it until commitLayoutObjectOverride().
    virtual bool previewLayoutObjectOverride(
        const EditorProjectLayoutEdit& edit,
        std::string* outError = nullptr) {
        (void)edit;
        if (outError) {
            *outError =
                "This project does not support live layout previews.";
        }
        return false;
    }
    virtual bool commitLayoutObjectOverride(
        const char* stableId,
        std::string* outError = nullptr) {
        (void)stableId;
        if (outError) {
            *outError =
                "This project does not support committing live layout previews.";
        }
        return false;
    }
    virtual void cancelLayoutObjectOverride(
        const char* stableId) {
        (void)stableId;
    }
    virtual bool resetLayoutObjectOverride(
        const char* stableId,
        std::string* outError = nullptr) {
        (void)stableId;
        if (outError) {
            *outError =
                "This project does not provide editable scene layout objects.";
        }
        return false;
    }
    virtual bool duplicateLayoutObject(
        const char* stableId,
        std::string* outCreatedStableId = nullptr,
        std::string* outError = nullptr) {
        (void)stableId;
        if (outCreatedStableId) {
            outCreatedStableId->clear();
        }
        if (outError) {
            *outError =
                "This project does not support prefab duplication.";
        }
        return false;
    }
    virtual bool deleteLayoutObject(
        const char* stableId,
        std::string* outError = nullptr) {
        (void)stableId;
        if (outError) {
            *outError =
                "This project does not support deleting scene objects.";
        }
        return false;
    }
    virtual bool deleteLayoutObjects(
        const char* const* stableIds,
        std::size_t stableIdCount,
        std::string* outError = nullptr) {
        if (!stableIds || stableIdCount == 0u) {
            if (outError) {
                *outError = "At least one scene object is required.";
            }
            return false;
        }
        for (std::size_t index = 0u;
             index < stableIdCount;
             ++index) {
            if (!deleteLayoutObject(stableIds[index], outError)) {
                return false;
            }
        }
        return true;
    }
    virtual bool renameLayoutObject(
        const EditorProjectLayoutObjectCommand& command,
        std::string* outError = nullptr) {
        (void)command;
        if (outError) {
            *outError =
                "This project does not support renaming scene objects.";
        }
        return false;
    }
    virtual bool reparentLayoutObject(
        const EditorProjectLayoutObjectCommand& command,
        std::string* outError = nullptr) {
        (void)command;
        if (outError) {
            *outError =
                "This project does not support hierarchy reparenting.";
        }
        return false;
    }
    virtual bool canUndoSceneEdit() const noexcept {
        return false;
    }
    virtual bool canRedoSceneEdit() const noexcept {
        return false;
    }
    virtual bool undoSceneEdit(
        std::string* outError = nullptr) {
        if (outError) {
            *outError = "There is no scene edit to undo.";
        }
        return false;
    }
    virtual bool redoSceneEdit(
        std::string* outError = nullptr) {
        if (outError) {
            *outError = "There is no scene edit to redo.";
        }
        return false;
    }
    virtual void selectLayoutObject(const char* stableId) {
        (void)stableId;
    }
    virtual bool layoutOverlayVisible() const noexcept {
        return false;
    }
    virtual void setLayoutOverlayVisible(bool visible) {
        (void)visible;
    }
    virtual std::size_t projectCommandCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectCommand projectCommand(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual bool executeProjectCommand(
        const char* commandId,
        const EditorProjectCommandValue* values,
        std::size_t valueCount,
        EditorProjectCommandResult& outResult,
        std::string* outError = nullptr) {
        (void)commandId;
        (void)values;
        (void)valueCount;
        outResult = {};
        if (outError) {
            *outError =
                "This project does not provide custom editor commands.";
        }
        return false;
    }
    virtual bool supportsTerrainTileEditing() const noexcept {
        return false;
    }
    virtual std::size_t terrainTileCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectTerrainTile terrainTile(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual std::size_t terrainSurfaceCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectTerrainSurface terrainSurface(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual std::size_t terrainPrefabCount() const noexcept {
        return 0u;
    }
    virtual EditorProjectTerrainPrefab terrainPrefab(
        std::size_t index) const noexcept {
        (void)index;
        return {};
    }
    virtual bool applyTerrainTileEdit(
        const EditorProjectTerrainTileEditRequest& request,
        std::string* outError = nullptr) {
        (void)request;
        if (outError) {
            *outError =
                "This project does not provide terrain-tile authoring.";
        }
        return false;
    }
};

using EditorProjectPluginAbiVersionFn = std::uint32_t (*)();
using CreateEditorProjectRuntimeFn = IEditorProjectRuntime* (*)();
using DestroyEditorProjectRuntimeFn =
    void (*)(IEditorProjectRuntime*);

struct EditorProjectPluginContract {
    std::uint32_t abiVersion = 0u;
    std::uint32_t structureSize = 0u;
    std::uint64_t layoutFingerprint = 0u;
    const char* buildConfiguration = nullptr;
    const char* compilerAbi = nullptr;
    CreateEditorProjectRuntimeFn createRuntime = nullptr;
    DestroyEditorProjectRuntimeFn destroyRuntime = nullptr;
};

using EditorProjectPluginContractFn =
    const EditorProjectPluginContract* (*)();

namespace detail {

constexpr std::uint64_t appendEditorProjectAbiValue(
    std::uint64_t hash,
    std::uint64_t value) noexcept {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= value & 0xffu;
        hash *= 1099511628211ull;
        value >>= 8u;
    }
    return hash;
}

template <typename T>
constexpr std::uint64_t appendEditorProjectAbiType(
    std::uint64_t hash) noexcept {
    hash = appendEditorProjectAbiValue(hash, sizeof(T));
    return appendEditorProjectAbiValue(hash, alignof(T));
}

constexpr std::uint64_t editorProjectPluginLayoutFingerprint() noexcept {
    std::uint64_t hash = 14695981039346656037ull;
#define PHLOSION_EDITOR_ABI_TYPE(type) \
    hash = appendEditorProjectAbiType<type>(hash)
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectOpenContext);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCameraContext);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectRenderContext);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectStats);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectSceneContext);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectGamePreview);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectAsset);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectAssetPreviewKind);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectGamePreviewContext);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectAssetPreviewOptions);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectAssetPreviewInfo);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectAssetAnimation);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectLayoutCapability);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectLayoutViewport);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectGridRegion);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectLayoutObject);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectLayoutEdit);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectLayoutObjectCommand);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCommandFieldKind);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCommandField);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCommand);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCommandValue);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectCommandResult);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainTileCoordinate);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainTile);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainSurface);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainPrefabKind);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainPrefab);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainTileStamp);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectTerrainTileEditRequest);
    PHLOSION_EDITOR_ABI_TYPE(IEditorProjectRuntime);
    PHLOSION_EDITOR_ABI_TYPE(IRenderBackend);
    PHLOSION_EDITOR_ABI_TYPE(ProjectDescriptor);
    PHLOSION_EDITOR_ABI_TYPE(std::string);
    PHLOSION_EDITOR_ABI_TYPE(EditorProjectPluginContract);
#undef PHLOSION_EDITOR_ABI_TYPE
    return hash;
}

} // namespace detail

inline constexpr std::uint64_t kEditorProjectPluginLayoutFingerprint =
    detail::editorProjectPluginLayoutFingerprint();

#define PHLOSION_EDITOR_STRINGIZE_DETAIL(value) #value
#define PHLOSION_EDITOR_STRINGIZE(value) \
    PHLOSION_EDITOR_STRINGIZE_DETAIL(value)
#if defined(_MSC_VER)
#if defined(_ITERATOR_DEBUG_LEVEL)
inline constexpr char kEditorProjectPluginCompilerAbi[] =
    "msvc-" PHLOSION_EDITOR_STRINGIZE(_MSC_VER)
    ";iterator-debug-level="
    PHLOSION_EDITOR_STRINGIZE(_ITERATOR_DEBUG_LEVEL);
#else
inline constexpr char kEditorProjectPluginCompilerAbi[] =
    "msvc-" PHLOSION_EDITOR_STRINGIZE(_MSC_VER)
    ";iterator-debug-level=unknown";
#endif
#elif defined(__clang__)
inline constexpr char kEditorProjectPluginCompilerAbi[] =
    "clang-" PHLOSION_EDITOR_STRINGIZE(__clang_major__) "."
    PHLOSION_EDITOR_STRINGIZE(__clang_minor__);
#elif defined(__GNUC__)
inline constexpr char kEditorProjectPluginCompilerAbi[] =
    "gcc-" PHLOSION_EDITOR_STRINGIZE(__GNUC__) "."
    PHLOSION_EDITOR_STRINGIZE(__GNUC_MINOR__);
#else
inline constexpr char kEditorProjectPluginCompilerAbi[] = "unknown";
#endif
#undef PHLOSION_EDITOR_STRINGIZE
#undef PHLOSION_EDITOR_STRINGIZE_DETAIL

} // namespace engine::editor

#if defined(_WIN32)
#define PHLOSION_EDITOR_PROJECT_EXPORT extern "C" __declspec(dllexport)
#else
#define PHLOSION_EDITOR_PROJECT_EXPORT \
    extern "C" __attribute__((visibility("default")))
#endif
