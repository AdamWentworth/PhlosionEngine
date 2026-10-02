#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxBatches.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

namespace vfx::runtime::authored_bridge {

using MeshResolver = std::function<authored_batches::MeshData*(const std::string&)>;
using TextureResolver = std::function<bool(
    const SharedAuthoredBatchVFX::Config::DrawPass&,
    const authored::TevState&,
    authored_batches::TextureView&)>;

bool appendBatches(const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                   std::vector<authored_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture);

} // namespace vfx::runtime::authored_bridge
