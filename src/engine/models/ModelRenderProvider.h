#pragma once
#include "ModelInstance.h"
#include "ModelLighting.h"
#include "engine/render/ModelRenderPlugin.h"
namespace engine::models {
// Creates a runtime-only provider; no editor host or graphics context is needed
// for construction, loading or CPU pose queries. The returned API owns context.
bool createModelRenderProvider(std::uint32_t abi, render::ModelRenderPluginApi* out,
    std::shared_ptr<const ModelProfile> profile = {}, ModelLighting lighting = {}) noexcept;
}
