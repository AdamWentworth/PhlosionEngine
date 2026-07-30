#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>

#include "engine/render/NeutralPmrem.h"

void VulkanRenderBackendImpl::createEnvironmentResources() {
    const auto& atlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const std::size_t expectedValues = static_cast<std::size_t>(atlas.width) *
                                       static_cast<std::size_t>(atlas.height) * 4u;
    if (atlas.width <= 0 || atlas.height <= 0 ||
        atlas.rgba16f.size() != expectedValues) {
        throw std::runtime_error("Vulkan neutral PMREM atlas is invalid.");
    }

    neutralPmremTexture = createTextureRgba16Float(
        atlas.rgba16f.data(),
        atlas.width,
        atlas.height,
        33071,
        33071,
        false);
    std::cout << "[Vulkan][PMREM] Uploaded linear RGBA16F atlas "
              << atlas.width << "x" << atlas.height << "\n"
              << std::flush;
}

void VulkanRenderBackendImpl::destroyEnvironmentResources() {
    destroyTexture(neutralPmremTexture);
}
