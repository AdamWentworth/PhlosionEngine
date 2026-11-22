#pragma once
#include "engine/render/WorldLayeredMaterial.h"
#include <cstdint>
#include <memory>

namespace engine::render {
class IWorldLayeredMaterials {
public:
    virtual ~IWorldLayeredMaterials()=default;
    // Bind before a view family (surface, primary, probes and reflections).
    // GPU copies remain resident until scoped retirement. Host data is consumed
    // during this call; the renderer must not retain a plugin-owned shared_ptr
    // whose control-block destructor can outlive the loaded plugin.
    // Keep one immutable snapshot/identity per scope while rebinding it.
    // Null disables sampling. Rebinding an identical set does not upload it.
    virtual void bindWorldLayeredMaterials(std::uint64_t owner,
        std::shared_ptr<const WorldLayerMaterialSet>)=0;
};
}
