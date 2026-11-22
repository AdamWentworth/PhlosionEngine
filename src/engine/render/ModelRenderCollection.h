#pragma once
#include "engine/render/ModelRenderPlugin.h"
#include <stdexcept>
namespace engine::render {
// End the family before destroying its plugin provider, including exception paths.
class ModelRenderCollection {
public:
    explicit ModelRenderCollection(const ModelRenderPluginApi& api):api_(api) {
        if(!api_.beginCollection || !api_.collect || !api_.renderCollected || !api_.endCollection || !api_.planCollected || !api_.prepareCollected || !api_.collectedResourceRevision)
            throw std::runtime_error("Model provider lacks collection support");
        check(api_.beginCollection(api_.context));
    }
    ~ModelRenderCollection(){api_.endCollection(api_.context);}
    ModelRenderCollection(const ModelRenderCollection&)=delete;
    ModelRenderCollection& operator=(const ModelRenderCollection&)=delete;
    std::uint64_t collect(const ModelRenderFrame& frame) {
        std::uint64_t handle=0;check(api_.collect(api_.context,&frame,&handle));return handle;
    }
    void plan(std::uint64_t handle,const ModelRenderFrame& view) {
        check(api_.planCollected(api_.context,handle,&view));
    }
    std::uint64_t resourceRevision(std::uint64_t handle) const {
        const auto revision=api_.collectedResourceRevision(api_.context,handle);
        if(!revision)throw std::runtime_error("Invalid collected model resource");
        return revision;
    }
    void prepare(){check(api_.prepareCollected(api_.context));}
    void draw(std::uint64_t handle,const ModelRenderFrame& view) {
        check(api_.renderCollected(api_.context,handle,&view));
    }
private:
    void check(bool okay){if(!okay)throw std::runtime_error(api_.lastError(api_.context));}
    const ModelRenderPluginApi& api_;
};
}
