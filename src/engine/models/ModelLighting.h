#pragma once
#include "WorldIndexedBatches.h"
#include <functional>
namespace engine::models {
// Preset meanings are supplied by the selected material profile.
struct ModelLighting {
    std::function<bool(std::vector<batches::WorldIndexedBatch>&, int, std::string&)> prepare;
    std::function<float(int)> forwardScale;
};
}
