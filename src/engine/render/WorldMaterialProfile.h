#pragma once

#include <array>
#include <bitset>
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
inline constexpr int kWorldMaterialProfileVersion = 3;

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
    // Exports standard local-light attributes while retaining custom primary shading.
    bool sharedLocalLighting = false;
    bool foliageComposition = false;
    bool openglDebugParameter = true;
    bool d3d12DebugParameter = true;
    float occlusionMaximum = 1.0f;
    bool operator==(const WorldMaterialModeOptions &) const = default;
};

struct WorldPipelineWarmup {
    std::uint32_t mode = 0;
    std::uint32_t variant = 0;
    bool operator==(const WorldPipelineWarmup&) const = default;
};

// Color-only transparent families own their vertex/fragment programs. Opaque
// surface/shadow passes retain the base profile; scene submission routes these
// materials through the ordinary direct/instanced path.
struct WorldMaterialFamily {
    std::string id;
    std::bitset<256> modes;
    WorldMaterialShaderSource d3d12;
    WorldMaterialShaderSource d3d12Vertex;
    // Standard fragment, dual-source fragment, vertex.
    std::array<std::vector<std::uint32_t>, 3> vulkan;
    bool operator==(const WorldMaterialFamily&) const = default;
};

struct WorldMaterialProfile {
    std::string id;
    unsigned textureMipDrop=0,maxAnisotropy=16;
    WorldMaterialShaderSource opengl;
    WorldMaterialShaderSource d3d12;
    WorldMaterialShaderSource openglVertex;
    WorldMaterialShaderSource d3d12Vertex;
    // Direct/indirect fragments, each standard/dual-source; direct/indirect vertices.
    // Optional directional-shadow and surface-data fragments follow the six required variants.
    std::array<std::vector<std::uint32_t>, 9> vulkan;
    std::array<std::vector<WorldMaterialConstantOverride>, 256> d3d12Constants;
    std::array<WorldMaterialModeOptions, 256> modes;
    std::vector<WorldPipelineWarmup> d3d12PipelineWarmup;
    // Direct ordinary Vulkan pipelines: material mode and standard blend/depth index.
    // Both display and HDR scene variants are prepared before drawing.
    std::vector<WorldPipelineWarmup> vulkanPipelineWarmup;

    std::vector<WorldMaterialFamily> transparentFamilies;
    // -1 selects the base program; no alternate family is silently substituted.
    int transparentFamilyIndex(std::uint32_t mode) const noexcept;
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
