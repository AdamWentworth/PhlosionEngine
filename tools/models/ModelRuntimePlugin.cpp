#include "engine/models/ModelRenderProvider.h"

#if defined(_WIN32)
#define MODEL_EXPORT extern "C" __declspec(dllexport)
#else
#define MODEL_EXPORT extern "C" __attribute__((visibility("default")))
#endif

MODEL_EXPORT bool phlosionModelRenderPluginCreate(std::uint32_t abi,
    engine::render::ModelRenderPluginApi* out) noexcept {
    return engine::models::createModelRenderProvider(abi, out);
}
