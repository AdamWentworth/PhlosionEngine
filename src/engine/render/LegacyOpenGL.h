#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/WorldMaterialProfile.h"
#include <memory>
#include <stdexcept>
#if PHLOSION_ENABLE_OPENGL
#include "engine/render/OpenGLRenderBackend.h"
#endif

namespace engine::render {
inline std::unique_ptr<IRenderBackend> createLegacyOpenGLBackend(const WorldMaterialProfile& profile = {}) {
#if PHLOSION_ENABLE_OPENGL
    return std::make_unique<OpenGLRenderBackend>(profile);
#else
    (void)profile;
    throw std::runtime_error("OpenGL is shelved; this legacy path requires PHLOSION_ENABLE_OPENGL=ON");
#endif
}
} // namespace engine::render
