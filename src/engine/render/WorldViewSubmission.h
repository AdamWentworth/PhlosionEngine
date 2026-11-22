#pragma once
#include "engine/render/IRenderBackend.h"
#include "engine/render/RenderPhaseProfile.h"

namespace engine::render {
// Keep compatible producers together; backend pass/barrier/immediate boundaries
// still flush. Nested producer batches cannot close this view's submission.
class WorldViewSubmission {
public:
    WorldViewSubmission(IRenderBackend& renderer,bool active=true)
        : phase_(renderer,"view-geometry"),renderer_(renderer),active_(active) {
        if(active_)renderer_.beginWorldIndexedBatchSubmission();
    }
    ~WorldViewSubmission() noexcept(false) {
        if(active_) {
            RenderCpuPhaseScope phase(renderer_,"view-recording");
            renderer_.endWorldIndexedBatchSubmission();
        }
    }
    WorldViewSubmission(const WorldViewSubmission&)=delete;
    WorldViewSubmission& operator=(const WorldViewSubmission&)=delete;
private:
    RenderPhaseScope phase_;
    IRenderBackend& renderer_;
    bool active_;
};
}
