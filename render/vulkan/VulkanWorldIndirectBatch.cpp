#include "engine/render/vulkan/VulkanWorldIndirectBatch.h"

#include <limits>

namespace engine::render::vulkan_backend {

std::vector<WorldIndirectRun> buildWorldIndirectRuns(
    std::span<const WorldIndirectDrawKey> drawKeys) {
    std::vector<WorldIndirectRun> runs;
    runs.reserve(drawKeys.size());
    for (std::size_t drawIndex = 0u; drawIndex < drawKeys.size(); ++drawIndex) {
        if (runs.empty() || runs.back().key != drawKeys[drawIndex] ||
            runs.back().drawCount ==
                (std::numeric_limits<std::uint32_t>::max)()) {
            runs.push_back({drawKeys[drawIndex], drawIndex, 1u});
        } else {
            ++runs.back().drawCount;
        }
    }
    return runs;
}

} // namespace engine::render::vulkan_backend
