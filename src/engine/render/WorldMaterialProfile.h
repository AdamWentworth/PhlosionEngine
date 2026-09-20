#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// Versioned world vertex/fragment programs. Profiles are project data;
// an empty profile is the standalone engine's standard material implementation.
inline constexpr int kWorldMaterialProfileVersion = 2;

struct WorldMaterialShaderSource {
    std::string declarations;
    std::string evaluation;
    bool operator==(const WorldMaterialShaderSource &) const = default;
};

struct WorldMaterialConstantTerm {
    enum class Source { Literal,
                        Texture,
                        Constant };
    Source source = Source::Literal;
    std::size_t sourceOffset = 0;
    float value = 0;
    float minimum = std::numeric_limits<float>::lowest();
    float maximum = std::numeric_limits<float>::max();
    float bias = 0;
    float scale = 1;
    bool round = false;
    bool operator==(const WorldMaterialConstantTerm &) const = default;
};

struct WorldMaterialConstantOverride {
    std::size_t destinationOffset = 0;
    std::vector<WorldMaterialConstantTerm> terms;
    bool conditional = false;
    std::size_t conditionOffset = 0;
    float greaterThan = std::numeric_limits<float>::lowest();
    float lessThan = std::numeric_limits<float>::max();
    bool operator==(const WorldMaterialConstantOverride &) const = default;
};

struct WorldMaterialModeOptions {
    bool pbrPacking = true;
    bool openglDebugParameter = true;
    bool d3d12DebugParameter = true;
    float occlusionMaximum = 1.0f;
    bool operator==(const WorldMaterialModeOptions &) const = default;
};

struct WorldMaterialProfile {
    std::string id;
    WorldMaterialShaderSource opengl;
    WorldMaterialShaderSource d3d12;
    WorldMaterialShaderSource openglVertex;
    WorldMaterialShaderSource d3d12Vertex;
    // Direct/indirect fragments, each standard/dual-source; direct/indirect vertices.
    std::array<std::vector<std::uint32_t>, 6> vulkan;
    std::array<std::vector<WorldMaterialConstantOverride>, 256> d3d12Constants;
    std::array<WorldMaterialModeOptions, 256> modes;

    bool operator==(const WorldMaterialProfile &) const = default;
    bool empty() const noexcept { return id.empty(); }
    void validate() const;
};

void applyWorldMaterialConstantOverrides(
    const WorldMaterialProfile &profile, std::uint8_t mode,
    const void *textureData, void *constants);

// All manifest paths are relative to the project root. Missing or incompatible
// profile artifacts fail explicitly; they never select a fallback material.
WorldMaterialProfile loadWorldMaterialProfile(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &manifest);

std::string injectWorldMaterialProfile(
    std::string_view shader,
    const WorldMaterialShaderSource &source);

} // namespace engine::render
