// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch scope: ResourceManager + ShaderCache (+ Events).
#include <cstdint>
#include <string>
#include <vector>

class ResourceManager;
class ShaderCache;
class EventBus;

struct EngineFixedPerfBreakdown {
    float preUpdateMs = 0.0f;
    float updatePhaseMs = 0.0f;
    float postUpdateMs = 0.0f;
    float postOtherMs = 0.0f;
    float phaseTransitionMs = 0.0f;
    float backendHydrateMs = 0.0f;
    float cameraMs = 0.0f;
    float unitInteractionMs = 0.0f;
    float shopMs = 0.0f;
    float roundMs = 0.0f;
    float stateManagerMs = 0.0f;
    float stateUpdateMs = 0.0f;
    float stateFlushMs = 0.0f;
    float movementMs = 0.0f;
    float movementPlanMs = 0.0f;
    float movementLuaMs = 0.0f;
    float movementFlushMs = 0.0f;
    float movementAdvanceMs = 0.0f;
    float combatMs = 0.0f;
    float combatPlanMs = 0.0f;
    float combatLuaMs = 0.0f;
    float combatFlushMs = 0.0f;
    float worldMs = 0.0f;
};

struct EngineRenderBuildBreakdown {
    float worldComposeMs = 0.0f;
    float worldBackdropMs = 0.0f;
    float worldVfxMs = 0.0f;
    float worldDepthFlushMs = 0.0f;
    float overlayPrepMs = 0.0f;
    float worldBackgroundMs = 0.0f;
    float worldTriangles3dMs = 0.0f;
    float worldIndexedMs = 0.0f;
    float worldSceneSubmitMs = 0.0f;
    float worldIndexedBatchSubmitMs = 0.0f;
    float worldDebugMs = 0.0f;
    float spriteMs = 0.0f;
    float uiMs = 0.0f;
    float otherMs = 0.0f;
};

enum class EngineTerminalLogMode : std::uint8_t {
    Performance = 0,
    GrowlVfx = 1,
    ScratchVfx = 2,
    TailFireDebug = 3,
    CombatDecision = 4,
    AnimationDecision = 5,
};

struct EngineGrowlPassDebugStats {
    std::string id;
    int eid = 0;
    std::string mode;
    std::string meshPath;
    std::string texturePath;
    bool quarterTextureBake = false;
    bool linePass = false;
    float scaleMul = 1.0f;
    float alphaMul = 1.0f;
    float forwardOffset = 0.0f;
    std::uint32_t submittedBatchCount = 0u;
    std::uint32_t submittedVertexCount = 0u;
    std::uint32_t submittedIndexCount = 0u;
    int submittedTextureWidth = 0;
    int submittedTextureHeight = 0;
    float submittedTranslateX = 0.0f;
    float submittedTranslateY = 0.0f;
    float submittedTranslateZ = 0.0f;
};

struct EngineGrowlDebugStats {
    bool snapshotAvailable = false;
    std::uint32_t activeRingCount = 0u;
    std::uint32_t configuredPassCount = 0u;
    std::uint32_t enabledPassCount = 0u;
    std::uint32_t meshPassCount = 0u;
    std::uint32_t linePassCount = 0u;
    std::uint32_t quarterRingPassCount = 0u;
    std::uint32_t quarterTextureBakePassCount = 0u;
    std::vector<EngineGrowlPassDebugStats> activePasses;
};

struct EngineScratchDebugStats {
    bool snapshotAvailable = false;
    std::uint32_t activeGlowCount = 0u;
    std::uint32_t snapshotRingCount = 0u;
    std::uint32_t configuredPassCount = 0u;
    std::uint32_t enabledPassCount = 0u;
    std::uint32_t submittedBatchCount = 0u;
    std::uint32_t submittedAlphaBatchCount = 0u;
    std::uint32_t submittedAdditiveBatchCount = 0u;
    std::uint32_t submittedPremulBatchCount = 0u;
    std::uint32_t submittedInstancedBatchCount = 0u;
    std::uint32_t submittedDynamicBatchCount = 0u;
    std::uint32_t submittedInstanceCount = 0u;
    std::uint32_t submittedVertexCount = 0u;
    std::uint32_t submittedIndexCount = 0u;
};

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
    std::uint32_t visibleAnimatedUnits = 0u;
    std::uint32_t particleCount = 0u;
    float projectedUnitsMs = 0.0f;
    float projectedPoseEvalMs = 0.0f;
    float projectedModelMs = 0.0f;
    float projectedModelPrepMs = 0.0f;
    float projectedModelGeometryMs = 0.0f;
    float projectedOverlayMs = 0.0f;
    std::uint32_t projectedUnitsProcessed = 0u;
    std::uint32_t projectedModelUnits = 0u;
    std::uint32_t projectedClipSkinnedUnits = 0u;
    std::uint32_t projectedSharedRigidBatches = 0u;
    std::uint32_t projectedGpuClipSkinBatches = 0u;
    std::uint32_t projectedGpuClipPaletteBatches = 0u;
    std::uint32_t projectedCpuRewriteBatches = 0u;
    std::uint32_t projectedIndexedBatchesQueued = 0u;
    EngineRenderBuildBreakdown renderBreakdown{};
    float renderMs = 0.0f;
    float swapMs = 0.0f;
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
    EngineFixedPerfBreakdown fixedBreakdown{};
};

struct EngineServices {
    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;

    // Engine-owned event bus (no singleton wrapper).
    EventBus* events = nullptr;

    // Updated by host loop each second (debug/perf overlay + logging).
    EngineFramePerfStats framePerf;
    // Updated by runtime each frame; host loop samples and aggregates.
    std::uint32_t frameVisibleAnimatedUnits = 0u;
    std::uint32_t frameParticleCount = 0u;
    EngineFixedPerfBreakdown frameFixedBreakdown{};
    float frameProjectedUnitsMs = 0.0f;
    float frameProjectedPoseEvalMs = 0.0f;
    float frameProjectedModelMs = 0.0f;
    float frameProjectedModelPrepMs = 0.0f;
    float frameProjectedModelGeometryMs = 0.0f;
    float frameProjectedOverlayMs = 0.0f;
    std::uint32_t frameProjectedUnitsProcessed = 0u;
    std::uint32_t frameProjectedModelUnits = 0u;
    std::uint32_t frameProjectedClipSkinnedUnits = 0u;
    std::uint32_t frameProjectedSharedRigidBatches = 0u;
    std::uint32_t frameProjectedGpuClipSkinBatches = 0u;
    std::uint32_t frameProjectedGpuClipPaletteBatches = 0u;
    std::uint32_t frameProjectedCpuRewriteBatches = 0u;
    std::uint32_t frameProjectedIndexedBatchesQueued = 0u;
    EngineRenderBuildBreakdown frameRenderBuildBreakdown{};

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
    bool sessionBackdropTilesEnabled = true;
    EngineTerminalLogMode terminalLogMode = EngineTerminalLogMode::Performance;
    int audioMasterVolume = 100;
    int audioMusicVolume = 100;
    int audioSfxVolume = 100;
    int audioVoiceVolume = 100;
    bool audioMute = false;
    std::string bootMenuScreen;
    EngineGrowlDebugStats frameGrowlDebug{};
    EngineScratchDebugStats frameScratchDebug{};
};
