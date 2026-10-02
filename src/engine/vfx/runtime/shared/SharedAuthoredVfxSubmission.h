#pragma once

#include <vector>

#include "engine/render/IRenderBackend.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxBatches.h"

namespace vfx::runtime::authored_submit {

std::size_t prewarmBatches(IRenderBackend& renderer,
                           const std::vector<authored_batches::WorldIndexedBatch>& batches,
                           const float* cameraWorldPos3 = nullptr,
                           const float* cameraForward3 = nullptr,
                           const float* cameraTarget3 = nullptr);

void submitBatches(IRenderBackend& renderer,
                   const std::vector<authored_batches::WorldIndexedBatch>& batches,
                   const float* viewProjectionMatrix4x4,
                   int surfaceWidth,
                   int surfaceHeight,
                   const float* cameraWorldPos3 = nullptr,
                   const float* cameraForward3 = nullptr,
                   const float* cameraTarget3 = nullptr);

} // namespace vfx::runtime::authored_submit
