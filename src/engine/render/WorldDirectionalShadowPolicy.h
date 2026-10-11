#pragma once

#include "engine/render/RenderBackendTypes.h"

namespace engine::render {

inline bool worldDirectionalShadowCaster(const backend::WorldTextureData* texture) {
    if (!texture) return true;
    const float mode = texture->materialMode;
    return texture->directionalShadowCaster != 0 && !texture->environment.skyDraw &&
        texture->depthTestEnabled != 0 && texture->alphaMode != 2 &&
        texture->blendMode == 0 && texture->vertexColorMulA >= .999f &&
        !(mode > 2.5f && mode < 3.5f) && !(mode > 26.5f && mode < 27.5f) && mode < 249.5f;
}

inline bool worldSceneShadowSwitch(const backend::WorldSceneMaterial& material,
                                  std::uint32_t bit) {
    return (material.sourceSwitchMask & bit) == 0 ||
           (material.sourceEnabledSwitchMask & bit) != 0;
}

} // namespace engine::render
