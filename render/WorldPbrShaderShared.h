#pragma once

#include <string>
#include <string_view>

namespace engine::render::world_pbr_shader_shared {

struct Tunables {
    float directIntensity = 0.72f;
    float ambientIntensity = 0.56f;
    float diffuseIblScale = 1.26f;
    float specularIblScale = 0.44f;
    float toneMappingExposure = 1.15f;
};

const Tunables& getTunables();

enum class ShaderLanguage {
    Glsl,
    Hlsl,
};

// Replaces shared PBR placeholders in backend shader source with canonical literals.
// `hlslLiterals=true` emits HLSL-friendly `f` suffixes for numeric literals.
std::string injectTunables(std::string_view source, bool hlslLiterals);

// Injects both shared world-PBR function sections and tunables.
std::string injectSharedWorldPbr(std::string_view source, ShaderLanguage language);

} // namespace engine::render::world_pbr_shader_shared
