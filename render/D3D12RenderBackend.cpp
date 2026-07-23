#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <SDL2/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <SDL2/SDL_syswm.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

D3D12RenderBackend::D3D12RenderBackend(SDL_Window* window,
                                       int width,
                                       int height,
                                       bool vsyncEnabled,
                                       const std::string& preferredAdapterName)
    : window_(window)
    , width_((width > 1) ? width : 1)
    , height_((height > 1) ? height : 1)
    , vsyncEnabled_(vsyncEnabled) {
    if (!window_) {
        throw std::runtime_error("D3D12RenderBackend requires a valid SDL_Window.");
    }
    initDeviceAndSwapchain(preferredAdapterName);
    engine::render::parity_contract::RuntimeConfig parityCfg =
        engine::render::parity_contract::makeBaselineConfig();
    parityCfg.framebufferSrgbEnabled = false;
    engine::render::parity_contract::logValidation("D3D12", parityCfg);
    configureScreenshotCapture();
    initializeWorldSceneFastPathCaps();
}

D3D12RenderBackend::~D3D12RenderBackend() {
    shutdown();
}

bool D3D12RenderBackend::getLastFrameTimings(BackendFrameTimings& outTimings) const {
    outTimings.presentWaitMs = lastPresentWaitMs_;
    outTimings.gpuFrameMs = lastGpuFrameMs_;
    outTimings.gpuFrameValid = lastGpuFrameValid_;
    return true;
}

bool D3D12RenderBackend::getLastFrameStats(BackendFrameStats& outStats) const {
    outStats = BackendFrameStats{};
    outStats.drawCalls = lastFrameDrawCalls_;
    outStats.triangles = lastFrameTriangles_;
    outStats.indexedOpaqueDraws = lastFrameIndexedOpaqueDraws_;
    outStats.indexedBlendDraws = lastFrameIndexedBlendDraws_;
    outStats.indexedCachedDraws = lastFrameIndexedCachedDraws_;
    outStats.indexedDynamicDraws = lastFrameIndexedDynamicDraws_;
    outStats.indexedInstancedDraws = lastFrameIndexedInstancedDraws_;
    outStats.indexedOutlineBatches = lastFrameIndexedOutlineBatches_;
    outStats.indexedGeometrySwitches = lastFrameIndexedGeometrySwitches_;
    outStats.indexedMaterialSwitches = lastFrameIndexedMaterialSwitches_;
    outStats.indexedTextureSwitches = lastFrameIndexedTextureSwitches_;
    outStats.indexedD3d12PsoSets = lastFrameIndexedD3d12PsoSets_;
    outStats.indexedD3d12DescriptorTableSets =
        lastFrameIndexedD3d12DescriptorTableSets_;
    outStats.fastSceneInstances = lastFrameFastSceneInstances_;
    outStats.fastSceneDrawClasses = lastFrameFastSceneDrawClasses_;
    outStats.fastSceneVisibleSkeletons = lastFrameFastSceneVisibleSkeletons_;
    outStats.fastScenePaletteUploadBytes = lastFrameFastScenePaletteUploadBytes_;
    outStats.fastSceneMaterialTableBinds = lastFrameFastSceneMaterialTableBinds_;
    outStats.fastSceneIndirectCommands = lastFrameFastSceneIndirectCommands_;
    return true;
}

bool D3D12RenderBackend::getWorldSceneFastPathCaps(
    WorldSceneFastPathCaps& outCaps) const {
    outCaps = worldSceneFastPathCaps_;
    return outCaps.supported;
}

void D3D12RenderBackend::recordWorldIndexedSubmissionStats(
    const WorldIndexedSubmissionStats& stats) {
    frameIndexedOpaqueDraws_ += stats.opaqueDraws;
    frameIndexedBlendDraws_ += stats.blendDraws;
    frameIndexedCachedDraws_ += stats.cachedDraws;
    frameIndexedDynamicDraws_ += stats.dynamicDraws;
    frameIndexedInstancedDraws_ += stats.instancedDraws;
    frameIndexedOutlineBatches_ += stats.outlineBatches;
    frameIndexedGeometrySwitches_ += stats.geometrySwitches;
    frameIndexedMaterialSwitches_ += stats.materialSwitches;
    frameIndexedTextureSwitches_ += stats.textureSwitches;
}

void D3D12RenderBackend::configureScreenshotCapture() {
    const auto path = engine::env::get("PAC_BACKEND_SCREENSHOT_PATH");
    if (!path.has_value() || path->empty()) return;

    screenshotPath_ = *path;
    screenshotCaptureConfigured_ = true;
    screenshotCaptured_ = false;
    frameCounter_ = 0u;
    screenshotFrameTarget_ = 0u;

    if (const auto frame = engine::env::get("PAC_BACKEND_SCREENSHOT_FRAME")) {
        try {
            screenshotFrameTarget_ = static_cast<std::uint64_t>(std::stoull(*frame));
        } catch (...) {
            screenshotFrameTarget_ = 0u;
        }
    }
}

void D3D12RenderBackend::initializeWorldSceneFastPathCaps() {
    worldSceneFastPathCaps_ = WorldSceneFastPathCaps{};
#if defined(_WIN32)
    worldSceneFastPathCaps_.supported = initialized_ && device_ != nullptr;
    worldSceneFastPathCaps_.prefersD3d12SpecializedPath =
        worldSceneFastPathCaps_.supported;
    worldSceneFastPathCaps_.supportsComputeSkinning =
        worldSceneFastPathCaps_.supported;
    worldSceneFastPathCaps_.supportsExecuteIndirect =
        worldSceneFastPathCaps_.supported;
    worldSceneFastPathCaps_.supportsSkinnedInstancing =
        worldSceneFastPathCaps_.supported;
#endif
}

