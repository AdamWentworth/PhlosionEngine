#include "engine/render/RendererParityContract.h"

#include "engine/core/Environment.h"
#include "engine/render/WorldPbrShaderShared.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace engine::render::parity_contract {
namespace {

std::string boolToString(bool value) {
    return value ? "1" : "0";
}

std::string makeSignaturePayload(const RuntimeConfig& config) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(6)
        << "direct=" << config.pbrDirectIntensity
        << ";ambient=" << config.pbrAmbientIntensity
        << ";diffIbl=" << config.pbrDiffuseIblScale
        << ";specIbl=" << config.pbrSpecularIblScale
        << ";toneExp=" << config.pbrToneMappingExposure
        << ";frontCW=" << boolToString(config.worldFrontFaceClockwise)
        << ";depthLE=" << boolToString(config.worldDepthFuncLessEqual)
        << ";cull=" << boolToString(config.worldCullEnabled)
        << ";opaqueBlend=" << boolToString(config.worldOpaqueBlendEnabled)
        << ";blendPSO=" << boolToString(config.worldBlendPipelineEnabled)
        << ";dualSrc=" << boolToString(config.worldDualSourceBlendPolicyEnabled)
        << ";debugBlend=" << boolToString(config.debugBlendEnabled)
        << ";fbSrgb=" << boolToString(config.framebufferSrgbEnabled)
        << ";aniso=" << config.worldSamplerAnisotropy
        << ";pmremEncoding=" << neutralPmremEncodingName(config.neutralPmremEncoding)
        << ";pmremFormat=" << neutralPmremGpuFormatName(config.neutralPmremGpuFormat)
        << ";pmrem=" << (config.neutralPmremAtlasKey ? config.neutralPmremAtlasKey : "<null>");
    return oss.str();
}

std::string fnv1a64Hex(std::string_view payload) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : payload) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

} // namespace

const char* neutralPmremEncodingName(NeutralPmremEncoding encoding) {
    switch (encoding) {
        case NeutralPmremEncoding::Linear:
            return "linear";
        case NeutralPmremEncoding::Rgbm:
            return "rgbm";
    }
    return "unknown";
}

const char* neutralPmremGpuFormatName(NeutralPmremGpuFormat format) {
    switch (format) {
        case NeutralPmremGpuFormat::Rgba16Float:
            return "rgba16f";
        case NeutralPmremGpuFormat::Rgba8Unorm:
            return "rgba8_unorm";
    }
    return "unknown";
}

RuntimeConfig makeBaselineConfig() {
    RuntimeConfig config{};
    const world_pbr_shader_shared::Tunables& tunables = world_pbr_shader_shared::getTunables();
    config.pbrDirectIntensity = tunables.directIntensity;
    config.pbrAmbientIntensity = tunables.ambientIntensity;
    config.pbrDiffuseIblScale = tunables.diffuseIblScale;
    config.pbrSpecularIblScale = tunables.specularIblScale;
    config.pbrToneMappingExposure = tunables.toneMappingExposure;
    config.worldFrontFaceClockwise = kWorldFrontFaceClockwise;
    config.worldDepthFuncLessEqual = kWorldDepthFuncLessEqual;
    config.worldCullEnabled = kWorldCullEnabled;
    config.worldOpaqueBlendEnabled = kWorldOpaqueBlendEnabled;
    config.worldBlendPipelineEnabled = kWorldBlendPipelineEnabled;
    config.worldDualSourceBlendPolicyEnabled = kWorldDualSourceBlendPolicyEnabled;
    config.debugBlendEnabled = kDebugBlendEnabled;
    config.framebufferSrgbEnabled = kFramebufferSrgbEnabled;
    config.worldSamplerAnisotropy = kWorldSamplerAnisotropy;
    config.neutralPmremEncoding = kNeutralPmremEncoding;
    config.neutralPmremGpuFormat = kNeutralPmremGpuFormat;
    config.neutralPmremAtlasKey = kNeutralPmremAtlasKey;
    return config;
}

ValidationResult validate(const RuntimeConfig& config) {
    ValidationResult out;
    const std::string payload = makeSignaturePayload(config);
    out.signature = fnv1a64Hex(payload);

    std::ostringstream problems;
    bool firstProblem = true;
    const auto addProblem = [&](const char* msg) {
        if (!firstProblem) problems << "; ";
        problems << msg;
        firstProblem = false;
    };

    if (config.worldFrontFaceClockwise != kWorldFrontFaceClockwise) {
        addProblem("worldFrontFaceClockwise mismatch");
    }
    if (config.worldDepthFuncLessEqual != kWorldDepthFuncLessEqual) {
        addProblem("worldDepthFuncLessEqual mismatch");
    }
    if (config.worldCullEnabled != kWorldCullEnabled) {
        addProblem("worldCullEnabled mismatch");
    }
    if (config.worldOpaqueBlendEnabled != kWorldOpaqueBlendEnabled) {
        addProblem("worldOpaqueBlendEnabled mismatch");
    }
    if (config.worldBlendPipelineEnabled != kWorldBlendPipelineEnabled) {
        addProblem("worldBlendPipelineEnabled mismatch");
    }
    if (config.worldDualSourceBlendPolicyEnabled !=
        kWorldDualSourceBlendPolicyEnabled) {
        addProblem("worldDualSourceBlendPolicyEnabled mismatch");
    }
    if (config.debugBlendEnabled != kDebugBlendEnabled) {
        addProblem("debugBlendEnabled mismatch");
    }
    if (config.framebufferSrgbEnabled != kFramebufferSrgbEnabled) {
        addProblem("framebufferSrgbEnabled mismatch");
    }
    if (config.worldSamplerAnisotropy < 1) {
        addProblem("worldSamplerAnisotropy invalid");
    }
    if (config.neutralPmremEncoding != kNeutralPmremEncoding) {
        addProblem("neutralPmremEncoding mismatch");
    }
    if (config.neutralPmremGpuFormat != kNeutralPmremGpuFormat) {
        addProblem("neutralPmremGpuFormat mismatch");
    }
    if (!config.neutralPmremAtlasKey || config.neutralPmremAtlasKey[0] == '\0') {
        addProblem("neutralPmremAtlasKey missing");
    }
    if (config.pbrDirectIntensity <= 0.0f ||
        config.pbrAmbientIntensity <= 0.0f ||
        config.pbrDiffuseIblScale <= 0.0f ||
        config.pbrSpecularIblScale <= 0.0f ||
        config.pbrToneMappingExposure <= 0.0f) {
        addProblem("PBR tunables must be positive");
    }
    if (out.signature != kExpectedBaselineSignature) {
        addProblem("baseline signature mismatch");
    }

    out.ok = firstProblem;
    out.message = out.ok ? "OK" : problems.str();
    return out;
}

void logValidation(const char* backendName, const RuntimeConfig& config) {
    const ValidationResult result = validate(config);
    std::cout
        << "[ParityContract][" << (backendName ? backendName : "unknown") << "] "
        << (result.ok ? "PASS" : "FAIL")
        << " signature=" << result.signature
        << " direct=" << config.pbrDirectIntensity
        << " ambient=" << config.pbrAmbientIntensity
        << " diffIbl=" << config.pbrDiffuseIblScale
        << " specIbl=" << config.pbrSpecularIblScale
        << " toneExp=" << config.pbrToneMappingExposure
        << " frontCW=" << boolToString(config.worldFrontFaceClockwise)
        << " depthLE=" << boolToString(config.worldDepthFuncLessEqual)
        << " cull=" << boolToString(config.worldCullEnabled)
        << " opaqueBlend=" << boolToString(config.worldOpaqueBlendEnabled)
        << " blendPSO=" << boolToString(config.worldBlendPipelineEnabled)
        << " dualSrc=" << boolToString(config.worldDualSourceBlendPolicyEnabled)
        << " debugBlend=" << boolToString(config.debugBlendEnabled)
        << " fbSrgb=" << boolToString(config.framebufferSrgbEnabled)
        << " aniso=" << config.worldSamplerAnisotropy
        << " pmremEncoding=" << neutralPmremEncodingName(config.neutralPmremEncoding)
        << " pmremFormat=" << neutralPmremGpuFormatName(config.neutralPmremGpuFormat)
        << " pmrem=" << (config.neutralPmremAtlasKey ? config.neutralPmremAtlasKey : "<null>")
        << " detail=" << result.message
        << "\n";

    if (!result.ok && engine::env::flagEnabled("PAC_PARITY_CONTRACT_FATAL")) {
        throw std::runtime_error(
            std::string("Parity contract validation failed for ") +
            (backendName ? backendName : "unknown") +
            ": " + result.message + " (signature=" + result.signature + ")");
    }
}

} // namespace engine::render::parity_contract
