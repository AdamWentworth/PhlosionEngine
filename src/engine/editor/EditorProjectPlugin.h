#pragma once

#include "engine/editor/ProjectDescriptor.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include <cstddef>
#include <cstdint>
#include <string>

class Camera3D;

namespace engine::editor {

inline constexpr std::uint32_t kEditorProjectPluginAbiVersion = 4u;
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

struct EditorProjectGamePreview {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* group = nullptr;
    const char* description = nullptr;
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
    const char* assetId = nullptr;
    const char* status = nullptr;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t textureCount = 0u;
    std::uint32_t boneCount = 0u;
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
