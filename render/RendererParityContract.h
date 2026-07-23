#pragma once

#include <cstdint>
#include <string>

namespace engine::render::parity_contract {

// Central parity policy constants used by all active render backends.
inline constexpr bool kWorldFrontFaceClockwise = true;
inline constexpr bool kWorldDepthFuncLessEqual = true;
inline constexpr bool kWorldCullEnabled = false;
inline constexpr bool kWorldOpaqueBlendEnabled = false;
inline constexpr bool kWorldBlendPipelineEnabled = true;
inline constexpr bool kWorldDualSourceBlendPolicyEnabled = true;
inline constexpr bool kDebugBlendEnabled = true;
inline constexpr bool kFramebufferSrgbEnabled = false;
inline constexpr int kWorldSamplerAnisotropy = 16;
inline constexpr const char* kNeutralPmremAtlasKey = "__neutral_room_pmrem_rgba16f_v2__";
inline constexpr const char* kExpectedBaselineSignature = "2d637fef00f62903";

enum class NeutralPmremEncoding : std::uint8_t {
    Linear,
    Rgbm,
};

enum class NeutralPmremGpuFormat : std::uint8_t {
    Rgba16Float,
    Rgba8Unorm,
};

inline constexpr NeutralPmremEncoding kNeutralPmremEncoding =
    NeutralPmremEncoding::Linear;
inline constexpr NeutralPmremGpuFormat kNeutralPmremGpuFormat =
    NeutralPmremGpuFormat::Rgba16Float;

struct RuntimeConfig {
    float pbrDirectIntensity = 0.0f;
    float pbrAmbientIntensity = 0.0f;
    float pbrDiffuseIblScale = 0.0f;
    float pbrSpecularIblScale = 0.0f;
    float pbrToneMappingExposure = 0.0f;
    bool worldFrontFaceClockwise = kWorldFrontFaceClockwise;
    bool worldDepthFuncLessEqual = kWorldDepthFuncLessEqual;
    bool worldCullEnabled = kWorldCullEnabled;
    bool worldOpaqueBlendEnabled = kWorldOpaqueBlendEnabled;
    bool worldBlendPipelineEnabled = kWorldBlendPipelineEnabled;
    bool worldDualSourceBlendPolicyEnabled = kWorldDualSourceBlendPolicyEnabled;
    bool debugBlendEnabled = kDebugBlendEnabled;
    bool framebufferSrgbEnabled = kFramebufferSrgbEnabled;
    int worldSamplerAnisotropy = kWorldSamplerAnisotropy;
    NeutralPmremEncoding neutralPmremEncoding = kNeutralPmremEncoding;
    NeutralPmremGpuFormat neutralPmremGpuFormat = kNeutralPmremGpuFormat;
    const char* neutralPmremAtlasKey = kNeutralPmremAtlasKey;
};

struct ValidationResult {
    bool ok = false;
    std::string message;
    std::string signature;
};

RuntimeConfig makeBaselineConfig();
const char* neutralPmremEncodingName(NeutralPmremEncoding encoding);
const char* neutralPmremGpuFormatName(NeutralPmremGpuFormat format);
ValidationResult validate(const RuntimeConfig& config);
void logValidation(const char* backendName, const RuntimeConfig& config);

} // namespace engine::render::parity_contract
