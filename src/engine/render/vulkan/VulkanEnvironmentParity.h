#pragma once

#include <vulkan/vulkan.h>

#include "engine/render/RendererParityContract.h"

namespace engine::render::vulkan_backend {

inline void applyNeutralPmremParityContract(
    VkFormat format,
    parity_contract::RuntimeConfig &config) {
    if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        config.neutralPmremEncoding = parity_contract::NeutralPmremEncoding::Linear;
        config.neutralPmremGpuFormat = parity_contract::NeutralPmremGpuFormat::Rgba16Float;
        return;
    }

    config.neutralPmremEncoding = parity_contract::NeutralPmremEncoding::Rgbm;
    config.neutralPmremGpuFormat = parity_contract::NeutralPmremGpuFormat::Rgba8Unorm;
}

} // namespace engine::render::vulkan_backend
