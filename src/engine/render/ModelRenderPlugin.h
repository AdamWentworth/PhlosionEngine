#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/WorldTemporalMaterial.h"

#include <cstdint>

namespace engine::render {

// ABI 38 returns the full sampled node transform for rigid effect attachments.
// Rebuild providers; ModelNodePoseResult changed.
class WorldMotionHistory;
inline constexpr std::uint32_t modelRenderPluginAbi = 38;
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
    // RGB multipliers [0,2]; alpha remains [0,1].
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
    // Borrowed during this primary-view submission only. Auxiliary views leave
    // these empty. The caller owns identity; the provider owns the sampled pose.
    WorldMotionHistory* motionHistory = nullptr;
    std::uint64_t motionObject = 0;
    // Caller-owned scene lifetime. Zero keeps the unmanaged preview path.
    std::uint64_t resourceScope = 0;
    WorldTemporalMaterial temporalMaterial = WorldTemporalMaterial::Geometry;
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
    // Column-major transform, including authored scale and instance placement.
    float worldTransform[16]{};
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
    // One owner-thread family at a time. Collect samples immutable CPU inputs
    // without GPU calls. Handles expire at endCollection and are never recycled.
    bool (*beginCollection)(void*) noexcept = nullptr;
    bool (*collect)(void*,const ModelRenderFrame*,std::uint64_t*) noexcept = nullptr;
    // Reads live camera/environment/lighting/prewarm/motion inputs only; pose,
    // placement and material state come from the collected handle.
    bool (*renderCollected)(void*,std::uint64_t,const ModelRenderFrame*) noexcept = nullptr;
    void (*endCollection)(void*) noexcept = nullptr;
    // Owner thread copies CPU view inputs and backend capabilities. No retained
    // request pointers. Once prepared, collected draws require a planned view.
    bool (*planCollected)(void*,std::uint64_t,const ModelRenderFrame*) noexcept = nullptr;
    // Joins per-object CPU jobs before returning, including failure paths.
    bool (*prepareCollected)(void*) noexcept = nullptr;
    // Loaded resource version captured by this handle; zero for an expired/foreign handle.
    std::uint64_t (*collectedResourceRevision)(void*,std::uint64_t) noexcept = nullptr;

};

using CreateModelRenderPlugin =
    bool (*)(std::uint32_t, ModelRenderPluginApi*) noexcept;

} // namespace engine::render
