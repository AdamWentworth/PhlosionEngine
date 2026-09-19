#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// Versioned additions to the world fragment shader. Profiles are project data;
// an empty profile is the standalone engine's standard material implementation.
inline constexpr int kWorldMaterialProfileVersion = 1;

struct WorldMaterialShaderSource {
    std::string declarations;
    std::string evaluation;
    bool operator==(const WorldMaterialShaderSource &) const = default;
};

struct WorldMaterialConstantOverride {
    std::size_t destinationOffset = 0;
    std::size_t sourceOffset = 0;
    float value = 0;
    bool fromTexture = false;
    bool operator==(const WorldMaterialConstantOverride &) const = default;
};

struct WorldMaterialProfile {
    std::string id;
    WorldMaterialShaderSource opengl;
    WorldMaterialShaderSource d3d12;
    // Direct, direct dual-source, indirect, indirect dual-source.
    std::array<std::vector<std::uint32_t>, 4> vulkan;
    std::array<std::vector<WorldMaterialConstantOverride>, 256> d3d12Constants;

    bool operator==(const WorldMaterialProfile &) const = default;
    bool empty() const noexcept { return id.empty(); }
    void validate() const;
};

// All manifest paths are relative to the project root. Missing or incompatible
// profile artifacts fail explicitly; they never select a fallback material.
WorldMaterialProfile loadWorldMaterialProfile(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &manifest);

std::string injectWorldMaterialProfile(
    std::string_view shader,
    const WorldMaterialShaderSource &source);

} // namespace engine::render
