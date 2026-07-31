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

inline constexpr std::uint32_t kEditorProjectPluginAbiVersion = 8u;
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
    std::array<float, 3> sourceTranslation{};
    std::array<float, 3> sourceRotationDegrees{};
    std::array<float, 3> sourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translation{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
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
    virtual void selectLayoutObject(const char* stableId) {
        (void)stableId;
    }
    virtual bool layoutOverlayVisible() const noexcept {
        return false;
    }
    virtual void setLayoutOverlayVisible(bool visible) {
        (void)visible;
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
