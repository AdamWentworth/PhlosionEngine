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

inline constexpr std::uint32_t kEditorProjectPluginAbiVersion = 16u;
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
    std::uint32_t encounterGrassInstanceCount = 0u;
    std::uint32_t vegetationInstanceCount = 0u;
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

struct EditorProjectLayoutObject {
    const char* stableId = nullptr;
    const char* displayName = nullptr;
    const char* typeName = nullptr;
    const char* coordinateSystem = nullptr;
    const char* reason = nullptr;
    const char* targetKind = nullptr;
    const char* categoryPath = nullptr;
    const char* prefabAssetId = nullptr;
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

struct EditorProjectBoardClearanceRequest {
    float paddingCells = 0.35f;
    bool clearTerrain = true;
    bool clearVegetation = true;
    bool clearObjects = true;
    bool retainRamps = true;
    bool addGroundInfill = true;
};

struct EditorProjectBoardClearanceResult {
    std::uint32_t suppressedTerrainCount = 0u;
    std::uint32_t suppressedVegetationCount = 0u;
    std::uint32_t suppressedObjectCount = 0u;
    std::uint32_t retainedRampCount = 0u;
    std::uint32_t skippedUnsafeAggregateCount = 0u;
    bool groundInfillCreated = false;
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
    const char* surface = nullptr;
    const char* shape = nullptr;
    std::array<float, 8> viewportCorners{};
    bool viewportVisible = false;
    bool sourceOccupied = false;
    bool authored = false;
};

struct EditorProjectTerrainSurface {
    const char* id = nullptr;
    const char* displayName = nullptr;
};

struct EditorProjectTerrainTileEditRequest {
    const EditorProjectTerrainTileCoordinate* coordinates = nullptr;
    std::size_t coordinateCount = 0u;
    // create, raise, lower, swap_prefab, paint_surface, set_shape,
    // or restore_source.
    const char* operation = nullptr;
    const char* surface = nullptr;
    const char* shape = nullptr;
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
    virtual bool supportsBoardClearance() const noexcept {
        return false;
    }
    virtual bool applyBoardClearance(
        const EditorProjectBoardClearanceRequest& request,
        EditorProjectBoardClearanceResult& outResult,
        std::string* outError = nullptr) {
        (void)request;
        outResult = {};
        if (outError) {
            *outError =
                "This project does not provide a board-clearance operation.";
        }
        return false;
    }
    virtual bool resetSceneToSource(
        std::string* outError = nullptr) {
        if (outError) {
            *outError =
                "This project does not provide a source-scene reset.";
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

} // namespace engine::editor

#if defined(_WIN32)
#define PHLOSION_EDITOR_PROJECT_EXPORT extern "C" __declspec(dllexport)
#else
#define PHLOSION_EDITOR_PROJECT_EXPORT \
    extern "C" __attribute__((visibility("default")))
#endif
