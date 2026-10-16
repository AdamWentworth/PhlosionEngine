#pragma once

#include "engine/render/IRenderBackend.h"

#include <cstdint>

namespace engine::render {

// ABI 16 adds rectangular emitters to the world-lighting frame tail.
inline constexpr std::uint32_t modelRenderPluginAbi = 16;
inline constexpr char modelRenderPluginEntryPoint[] = "phlosionModelRenderPluginCreate";

// Project renderers may present cooked models outside the asset inspector.
// Strings and frame pointers live for the call.
struct ModelRenderFrame {
    IRenderBackend* renderer = nullptr;
    const char* assetId = nullptr;
    const char* phloPath = nullptr;
    const char* animation = nullptr; // Null or empty selects the authored bind pose.
    float animationTime = 0.0f;
    float position[3]{};
    float yawRadians = 0.0f;
    float scale = 1.0f;
    float cameraPosition[3]{};
    float cameraTarget[3]{};
    float cameraUp[3]{0.0f, 1.0f, 0.0f};
    float verticalFovDegrees = 36.0f;
    float nearPlane = 0.01f;
    float farPlane = 100.0f;
    int width = 0;
    int height = 0;
    // Generic translucent model overlays, e.g. motion trails. Defaults retain
    // the asset's materials. Alpha blending tests depth and does not write it.
    float tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    bool translucent = false;
    bool unlit = false;
    // Distinct poses of one asset submitted in the same frame need stable skin
    // palettes. Each slot (0..15) remains valid until reused or its resource replaced.
    std::uint32_t poseSlot = 0;
    // One-shot clips clamp at their endpoints and must not blend the final
    // pose into the first frame as a looping preview does.
    bool looping = true;
    // Placement is selected by the caller. Scale includes the resource's
    // authored modelScaleFactor; optional grounding uses its support anchor.
    bool groundPlaneEnabled = false;
    float groundPlaneY = 0.0f;
    float groundClearance = 0.0f;
    int lightingPreset = 0;
    // Lens shift in normalized device coordinates (+X right, +Y up). Places
    // a preview inside a UI pane without moving the camera or the viewport.
    float projectionOffset[2]{};
    // Load/prepare this pose and upload its resources without drawing or
    // advancing animation. Owner-thread use; no active render pass required.
    bool prewarmOnly = false;
    WorldEnvironmentLighting environment;
};

// CPU sampling uses the same animation/root-motion and instance transform as
// rendering. Strings are borrowed for the call; no render frame is required.
struct ModelNodePoseRequest {
    const char* assetId = nullptr;
    const char* phloPath = nullptr;
    const char* animation = nullptr;
    const char* nodeName = nullptr;
    float animationTime = 0.0f;
    float position[3]{};
    float yawRadians = 0.0f;
    float scale = 1.0f;
    bool looping = true;
    bool groundPlaneEnabled = false;
    float groundPlaneY = 0.0f;
    float groundClearance = 0.0f;
};

struct ModelNodePoseResult {
    float worldPosition[3]{};
};

struct ModelRenderPluginApi {
    std::uint32_t abi = 0;
    std::uint32_t size = 0;
    void* context = nullptr;
    bool (*render)(void*, const ModelRenderFrame*) noexcept = nullptr;
    const char* (*lastError)(void*) noexcept = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    // Missing nodes/clips fail explicitly and leave the result unchanged.
    bool (*sampleNodePose)(void*, const ModelNodePoseRequest*,
                           ModelNodePoseResult*) noexcept = nullptr;
};

using CreateModelRenderPlugin =
    bool (*)(std::uint32_t, ModelRenderPluginApi*) noexcept;

} // namespace engine::render
