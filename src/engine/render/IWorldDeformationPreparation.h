#pragma once
#include "engine/render/IRenderBackendWorld.h"
#include "engine/render/WorldDeformation.h"
#include <span>

namespace engine::render {
// Owner-thread admission. Implementations snapshot inputs, reserve disjoint
// fenced storage, join copies and publish only after host writes are visible.
// Immediate/legacy renderers may continue their ordinary draw-time upload path.
struct WorldDeformationPreparationStats {
    std::uint64_t preparedPalettes=0, preparedBytes=0, copyJoins=0, immediatePalettes=0, immediateBytes=0;
};
class IWorldDeformationPreparation {
public:
    virtual ~IWorldDeformationPreparation() = default;
    virtual void beginWorldDeformationBatch() = 0;
    virtual void commitWorldDeformationBatch() = 0;
    virtual void cancelWorldDeformationBatch() noexcept = 0;
    virtual void prepareWorldDeformations(std::span<const WorldSkinPalette>) = 0;
    virtual WorldDeformationPreparationStats worldDeformationPreparationStats() const = 0;
};
// A canceled batch prevents deformation consumption until the frame is reset.
// Failed commits retain owned inputs so the owner can retry before drawing.
class WorldDeformationBatch {
public:
    explicit WorldDeformationBatch(IRenderBackendWorld& renderer)
        : api_(dynamic_cast<IWorldDeformationPreparation*>(&renderer)) {
        if(api_)api_->beginWorldDeformationBatch();
    }
    ~WorldDeformationBatch() {if(api_)api_->cancelWorldDeformationBatch();}
    WorldDeformationBatch(const WorldDeformationBatch&)=delete;
    WorldDeformationBatch& operator=(const WorldDeformationBatch&)=delete;
    void commit() {
        if(api_) {api_->commitWorldDeformationBatch();api_=nullptr;}
    }
private:
    IWorldDeformationPreparation* api_;
};
inline void prepareWorldDeformations(IRenderBackendWorld& renderer,
                                     std::span<const WorldSkinPalette> palettes) {
    if(auto* api=dynamic_cast<IWorldDeformationPreparation*>(&renderer))
        api->prepareWorldDeformations(palettes);
}
}
