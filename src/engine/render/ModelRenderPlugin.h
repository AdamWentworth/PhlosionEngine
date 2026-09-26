#pragma once

#include "engine/render/IRenderBackend.h"

#include <cstdint>

namespace engine::render {

inline constexpr std::uint32_t modelRenderPluginAbi = 1;
inline constexpr char modelRenderPluginEntryPoint[] = "phlosionModelRenderPluginCreate";

// Project renderers may present cooked models outside the asset inspector.
// Strings and frame pointers live for the call.
struct ModelRenderFrame {
    IRenderBackend* renderer = nullptr;
    const char* assetId = nullptr;
    const char* phloPath = nullptr;
    const char* animation = nullptr;
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
};

struct ModelRenderPluginApi {
    std::uint32_t abi = 0;
    std::uint32_t size = 0;
    void* context = nullptr;
    bool (*render)(void*, const ModelRenderFrame*) noexcept = nullptr;
    const char* (*lastError)(void*) noexcept = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
};

using CreateModelRenderPlugin =
    bool (*)(std::uint32_t, ModelRenderPluginApi*) noexcept;

} // namespace engine::render
