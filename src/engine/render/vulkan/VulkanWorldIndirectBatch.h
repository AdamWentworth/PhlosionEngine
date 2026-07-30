#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::render::vulkan_backend {

struct WorldIndirectDrawKey {
    std::uint64_t geometryBufferKey = 0u;
    std::uint32_t pipelineIndex = 0u;

    auto operator<=>(const WorldIndirectDrawKey&) const = default;
};

struct WorldIndirectRun {
    WorldIndirectDrawKey key{};
    std::size_t firstDraw = 0u;
    std::uint32_t drawCount = 0u;
};

std::vector<WorldIndirectRun> buildWorldIndirectRuns(
    std::span<const WorldIndirectDrawKey> drawKeys);

} // namespace engine::render::vulkan_backend
