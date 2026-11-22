#define SDL_MAIN_HANDLED
#include "engine/platform/Window.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    SDL_SetMainReady();
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    Window window("Vulkan submission contract", 128, 96, Window::GraphicsApi::Vulkan, false, false);
    VulkanRenderBackendImpl renderer;
    struct Cleanup { VulkanRenderBackendImpl& r; ~Cleanup() { r.shutdown(); } } cleanup{renderer};
    renderer.initialize(window.getSDLWindow(), 128, 96, false, "");
    const bool overlapSupported = renderer.canDeferPresent;
    const auto surface = renderer.createEditorSurface(64,48);
    check(surface != 0, "Editor target creation failed");
    check(renderer.renderFinishedSemaphores.size() == renderer.swapchainImages.size(),
          "Presentation semaphores must follow swapchain images");
    std::array<IRenderBackend::WorldMeshVertex, 3> vertices{};
    const std::array<std::uint32_t, 3> indices{0, 1, 2}, invalid{0, 1, 3};
    check(!renderer.ensureCachedWorldMesh("invalid", vertices.data(), 3, invalid.data(), 3),
          "Invalid new geometry was accepted");
    auto* mesh = renderer.ensureCachedWorldMesh("triangle", vertices.data(), 3, indices.data(), 3);
    check(mesh, "Valid geometry did not upload");
    check(renderer.ensureCachedWorldMesh("triangle", vertices.data(), 3, indices.data(), 3) == mesh,
          "Immutable geometry was not reused");
    check(!renderer.ensureCachedWorldMesh("triangle", vertices.data(), 2, indices.data(), 3),
          "Changed geometry dimensions reused an immutable key");
    unsigned deferred = 0;
    const auto drawOverlays = [&renderer](int width, int height) {
        IRenderBackend::DebugQuad quad{4,8,12,16,.2f,.3f,.4f,.5f};
        IRenderBackend::DebugSprite sprite{};
        sprite.x=20;sprite.y=8;sprite.w=12;sprite.h=16;
        renderer.drawDebugQuads(&quad,1,width,height);
        renderer.drawDebugSprites(&sprite,1,width,height);
    };
    for (int frame = 0; frame < 20; ++frame) {
        if (frame == 5) {
            SDL_SetWindowSize(window.getSDLWindow(), 160, 112);
            SDL_PumpEvents();
            renderer.requestResize(160, 112);
        }
        if (frame == 9) renderer.requestVSync(true);
        if (frame == 13) renderer.requestVSync(false);
        if (frame == 11) {
            check(!renderer.worldHazeStencilTargets.empty(), "Haze mask was not allocated");
            check(renderer.resizeEditorSurface(surface,80,60), "Editor target resize failed");
            check(renderer.worldHazeStencilTargets.empty(), "Resized editor retained stale haze framebuffer");
        }
        renderer.beginFrame(float(frame % 2), 0, .25f, 1);
        check(renderer.frameActive, "Frame did not acquire a drawable image");
        const auto& current = renderer.frames[renderer.currentFrame];
        check(std::count(renderer.imagesInFlight.begin(), renderer.imagesInFlight.end(), current.inFlight) == 1,
              "A recycled fence still owns an unrelated image");
        check(renderer.frameSkinPalettes.rawByteSize() == 0, "Palette CPU data survived a frame reset");
        std::array<float, 16> palette{1,0,0,0, 0,1,0,0, 0,0,1,0, float(frame),0,0,1};
        std::uint32_t first = 0, repeated = 0, changed = 0;
        check(renderer.uploadWorldSkinPalette(palette.data(), palette.size(), first), "Palette upload failed");
        auto equalCopy = palette;
        check(renderer.uploadWorldSkinPalette(equalCopy.data(), equalCopy.size(), repeated) && first == repeated,
              "Equal palettes at different addresses did not reuse the upload");
        palette[12] += .5f;
        check(renderer.uploadWorldSkinPalette(palette.data(), palette.size(), changed) && changed != first,
              "Changed contents at the same address reused a stale palette");
        check(renderer.uploadWorldSkinPalette(equalCopy.data(), equalCopy.size(), repeated) && repeated == first,
              "A later palette overwrote an earlier upload");
        check(renderer.frameSkinPaletteUploads == 2 && renderer.frameSkinPaletteReuses == 2,
              "Unexpected palette upload/reuse count");
        check(renderer.frameSkinPalettes.rawByteSize() == 2 * sizeof(palette), "Palette CPU mirror grew on cache hits");
        check(renderer.beginEditorSurface(surface), "Editor display pass did not begin");
        drawOverlays(frame<11?64:80,frame<11?48:60);
        if ((frame % 2) == 0) {
            renderer.beginWorldSceneColorPass(frame<11?64:80,frame<11?48:60);
            check(renderer.worldSceneColorPassActive, "Editor HDR pass did not begin");
            check(renderer.beginWorldLocalHazeComposition(frame<11?64:80,frame<11?48:60), "Editor haze mask did not begin");
            renderer.endWorldLocalHazeComposition();
            drawOverlays(frame<11?64:80,frame<11?48:60);
            renderer.endWorldSceneColorPass();
        }
        renderer.endEditorSurface();
        check(renderer.mainRenderPassActive, "Editor target did not restore main pass");
        drawOverlays(frame<5?128:160,frame<5?96:112);
        const bool overlap = renderer.canDeferPresent;
        // Exercise the no-spare-image policy as well as the actual surface.
        if (frame == 16) renderer.canDeferPresent = false;
        renderer.endFrame();
        if (renderer.pendingPresentSemaphore != VK_NULL_HANDLE) ++deferred;
        if (frame == 16) {
            check(renderer.pendingPresentSemaphore == VK_NULL_HANDLE, "Fallback left a pending presentation");
            renderer.canDeferPresent = overlap;
        }
    }
    check(!overlapSupported || deferred > 0, "Eligible swapchain never overlapped CPU recording");
    renderer.destroyEditorSurface(surface);
    check(renderer.worldHazeStencilTargets.empty(), "Destroyed editor retained haze mask");
    renderer.shutdown(); // Includes a pending frame, then repeated teardown.
    check(renderer.pendingPresentSemaphore == VK_NULL_HANDLE && renderer.renderFinishedSemaphores.empty(),
          "Shutdown retained presentation state");
    std::cout << "PASS Vulkan palette mutation, immutable geometry, editor HDR/display targets, fence reuse, resize, vsync, fallback, shutdown; deferred="
              << deferred << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
