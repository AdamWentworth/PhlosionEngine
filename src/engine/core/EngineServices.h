#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ResourceManager;
class ShaderCache;
class EventBus;

// Backend-neutral frame measurements; project-specific counters live in the game.
struct EngineFramePerfStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    float fixedMs = 0.0f;
    float fixedTickMs = 0.0f;
    float renderBuildMs = 0.0f;
    float renderSubmitMs = 0.0f;
    float presentWaitMs = 0.0f;
    float gpuFrameMs = 0.0f;
    bool gpuFrameValid = false;
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t indexedOpaqueDraws = 0u;
    std::uint32_t indexedBlendDraws = 0u;
    std::uint32_t indexedCachedDraws = 0u;
    std::uint32_t indexedDynamicDraws = 0u;
    std::uint32_t indexedInstancedDraws = 0u;
    std::uint32_t indexedOutlineBatches = 0u;
    std::uint32_t indexedGeometrySwitches = 0u;
    std::uint32_t indexedMaterialSwitches = 0u;
    std::uint32_t indexedTextureSwitches = 0u;
    std::uint32_t indexedGlTextureBindCalls = 0u;
    std::uint32_t indexedD3d12PsoSets = 0u;
    std::uint32_t indexedD3d12DescriptorTableSets = 0u;
    std::uint32_t fastSceneInstances = 0u;
    std::uint32_t fastSceneDrawClasses = 0u;
    std::uint32_t fastSceneVisibleSkeletons = 0u;
    std::uint64_t fastScenePaletteUploadBytes = 0u;
    std::uint32_t fastSceneMaterialTableBinds = 0u;
    std::uint32_t fastSceneIndirectCommands = 0u;
    float renderMs = 0.0f;
    float swapMs = 0.0f;
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
};

struct EngineServices {
    // Project hosts may extend the bundle with their own per-session state.
    virtual ~EngineServices() = default;

    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;

    // Engine-owned event bus (no singleton wrapper).
    EventBus* events = nullptr;

    // Render backend + GPU diagnostics.
    std::string videoPreferencesPath;
    std::string requestedRendererBackend = "auto";
    std::string activeRendererBackend = "opengl";
    bool rendererBackendFallback = false;
    std::string rendererBackendFallbackReason;
    std::string gpuVendor;
    std::string gpuRenderer;
    std::vector<std::string> availableGpuAdapters;
    std::string preferredGpuAdapter;
    bool gpuDiscrete = false;
    bool vsyncEnabled = false;
    int fpsCap = 0;
    int graphicsQuality = 3;
    std::uint32_t graphicsQualityGeneration = 1u;
    bool requireDiscreteGpu = false;
    bool characterInkingEnabled = false;
    int audioMasterVolume = 100;
    int audioMusicVolume = 100;
    int audioSfxVolume = 100;
    int audioVoiceVolume = 100;
    bool audioMute = false;
};
