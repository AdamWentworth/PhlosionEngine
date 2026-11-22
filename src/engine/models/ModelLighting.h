#pragma once
#include "WorldIndexedBatches.h"
#include <functional>
namespace engine::models {
// Preset meanings are supplied by the selected material profile. prepare must
// produce the same descriptors for identical collected batches/environment and
// preset throughout a collection; it must not retain mutable per-draw pointers.
// Different collected objects can prepare concurrently. Shared lazy resources
// must synchronize publication; each invocation owns its batches and error.
// Native pass state and motion are bound after this CPU-only preparation.
struct ModelLighting {
    std::function<bool(std::vector<batches::WorldIndexedBatch>&, int, std::string&)> prepare;
    std::function<float(int)> forwardScale;
};
}
