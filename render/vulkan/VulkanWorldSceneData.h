#pragma once

#include "engine/render/IRenderBackend.h"

namespace engine::render::vulkan_backend {

IRenderBackend::WorldTextureData makeWorldSceneTextureData(
    const IRenderBackend::WorldSceneMaterial& material,
    const IRenderBackend::WorldSceneView& view);

} // namespace engine::render::vulkan_backend
