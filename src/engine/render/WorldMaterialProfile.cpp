#include "engine/render/WorldMaterialProfile.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace engine::render {
namespace {
using MemberOffsets = std::unordered_map<std::string, std::size_t>;

const MemberOffsets &sourceMembers() {
    static const MemberOffsets members{
        {"projectedShadowSamplingScale", offsetof(IRenderBackend::WorldTextureData, projectedShadowSamplingScale)},
        {"projectedShadowBias", offsetof(IRenderBackend::WorldTextureData, projectedShadowBias)},
        {"clipSpaceDepthBias", offsetof(IRenderBackend::WorldTextureData, clipSpaceDepthBias)},
        {"alphaCutoff", offsetof(IRenderBackend::WorldTextureData, alphaCutoff)},
        {"alphaWindowMin", offsetof(IRenderBackend::WorldTextureData, alphaWindowMin)},
        {"alphaWindowMax", offsetof(IRenderBackend::WorldTextureData, alphaWindowMax)},
        {"normalScale", offsetof(IRenderBackend::WorldTextureData, normalScale)},
        {"metallicFactor", offsetof(IRenderBackend::WorldTextureData, metallicFactor)},
        {"roughnessFactor", offsetof(IRenderBackend::WorldTextureData, roughnessFactor)},
        {"occlusionStrength", offsetof(IRenderBackend::WorldTextureData, occlusionStrength)},
        {"emissiveFactorR", offsetof(IRenderBackend::WorldTextureData, emissiveFactorR)},
        {"emissiveFactorG", offsetof(IRenderBackend::WorldTextureData, emissiveFactorG)},
        {"emissiveFactorB", offsetof(IRenderBackend::WorldTextureData, emissiveFactorB)},
        {"vertexColorMulR", offsetof(IRenderBackend::WorldTextureData, vertexColorMulR)},
        {"vertexColorMulG", offsetof(IRenderBackend::WorldTextureData, vertexColorMulG)},
        {"vertexColorMulB", offsetof(IRenderBackend::WorldTextureData, vertexColorMulB)},
        {"vertexColorMulA", offsetof(IRenderBackend::WorldTextureData, vertexColorMulA)},
        {"cameraPosX", offsetof(IRenderBackend::WorldTextureData, cameraPosX)},
        {"cameraPosY", offsetof(IRenderBackend::WorldTextureData, cameraPosY)},
        {"cameraPosZ", offsetof(IRenderBackend::WorldTextureData, cameraPosZ)},
        {"cameraForwardX", offsetof(IRenderBackend::WorldTextureData, cameraForwardX)},
        {"cameraForwardY", offsetof(IRenderBackend::WorldTextureData, cameraForwardY)},
        {"cameraForwardZ", offsetof(IRenderBackend::WorldTextureData, cameraForwardZ)},
        {"cameraTargetX", offsetof(IRenderBackend::WorldTextureData, cameraTargetX)},
        {"cameraTargetY", offsetof(IRenderBackend::WorldTextureData, cameraTargetY)},
        {"cameraTargetZ", offsetof(IRenderBackend::WorldTextureData, cameraTargetZ)},
        {"materialTimeSec", offsetof(IRenderBackend::WorldTextureData, materialTimeSec)},
        {"materialFlags", offsetof(IRenderBackend::WorldTextureData, materialFlags)},
        {"materialAtlasWidth", offsetof(IRenderBackend::WorldTextureData, materialAtlasWidth)},
        {"materialAtlasHeight", offsetof(IRenderBackend::WorldTextureData, materialAtlasHeight)},
        {"materialRect0U", offsetof(IRenderBackend::WorldTextureData, materialRect0U)},
        {"materialRect0V", offsetof(IRenderBackend::WorldTextureData, materialRect0V)},
        {"materialRect0W", offsetof(IRenderBackend::WorldTextureData, materialRect0W)},
        {"materialRect0H", offsetof(IRenderBackend::WorldTextureData, materialRect0H)},
        {"materialRect1U", offsetof(IRenderBackend::WorldTextureData, materialRect1U)},
        {"materialRect1V", offsetof(IRenderBackend::WorldTextureData, materialRect1V)},
        {"materialRect1W", offsetof(IRenderBackend::WorldTextureData, materialRect1W)},
        {"materialRect1H", offsetof(IRenderBackend::WorldTextureData, materialRect1H)},
        {"materialFlipbook0Cols", offsetof(IRenderBackend::WorldTextureData, materialFlipbook0Cols)},
        {"materialFlipbook0Rows", offsetof(IRenderBackend::WorldTextureData, materialFlipbook0Rows)},
        {"materialFlipbook0Frames", offsetof(IRenderBackend::WorldTextureData, materialFlipbook0Frames)},
        {"materialFlipbook0Fps", offsetof(IRenderBackend::WorldTextureData, materialFlipbook0Fps)},
        {"materialFlipbook1Cols", offsetof(IRenderBackend::WorldTextureData, materialFlipbook1Cols)},
        {"materialFlipbook1Rows", offsetof(IRenderBackend::WorldTextureData, materialFlipbook1Rows)},
        {"materialFlipbook1Frames", offsetof(IRenderBackend::WorldTextureData, materialFlipbook1Frames)},
        {"materialFlipbook1Fps", offsetof(IRenderBackend::WorldTextureData, materialFlipbook1Fps)},
    };
    return members;
}

const MemberOffsets &destinationMembers() {
    static const MemberOffsets members{
        {"useTexture", offsetof(d3d12_internal::WorldPsConstants, useTexture)},
        {"wrapS", offsetof(d3d12_internal::WorldPsConstants, wrapS)},
        {"wrapT", offsetof(d3d12_internal::WorldPsConstants, wrapT)},
        {"alphaMode", offsetof(d3d12_internal::WorldPsConstants, alphaMode)},
        {"alphaCutoff", offsetof(d3d12_internal::WorldPsConstants, alphaCutoff)},
        {"alphaWindowMin", offsetof(d3d12_internal::WorldPsConstants, alphaWindowMin)},
        {"alphaWindowMax", offsetof(d3d12_internal::WorldPsConstants, alphaWindowMax)},
        {"vertexColorMulR", offsetof(d3d12_internal::WorldPsConstants, vertexColorMulR)},
        {"vertexColorMulG", offsetof(d3d12_internal::WorldPsConstants, vertexColorMulG)},
        {"vertexColorMulB", offsetof(d3d12_internal::WorldPsConstants, vertexColorMulB)},
        {"vertexColorMulA", offsetof(d3d12_internal::WorldPsConstants, vertexColorMulA)},
        {"materialMode", offsetof(d3d12_internal::WorldPsConstants, materialMode)},
        {"materialTimeSec", offsetof(d3d12_internal::WorldPsConstants, materialTimeSec)},
        {"materialFlags", offsetof(d3d12_internal::WorldPsConstants, materialFlags)},
        {"materialAtlasWidth", offsetof(d3d12_internal::WorldPsConstants, materialAtlasWidth)},
        {"materialAtlasHeight", offsetof(d3d12_internal::WorldPsConstants, materialAtlasHeight)},
        {"materialRect0U", offsetof(d3d12_internal::WorldPsConstants, materialRect0U)},
        {"materialRect0V", offsetof(d3d12_internal::WorldPsConstants, materialRect0V)},
        {"materialRect0W", offsetof(d3d12_internal::WorldPsConstants, materialRect0W)},
        {"materialRect0H", offsetof(d3d12_internal::WorldPsConstants, materialRect0H)},
        {"materialRect1U", offsetof(d3d12_internal::WorldPsConstants, materialRect1U)},
        {"materialRect1V", offsetof(d3d12_internal::WorldPsConstants, materialRect1V)},
        {"materialRect1W", offsetof(d3d12_internal::WorldPsConstants, materialRect1W)},
        {"materialRect1H", offsetof(d3d12_internal::WorldPsConstants, materialRect1H)},
        {"materialFlipbook0Cols", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook0Cols)},
        {"materialFlipbook0Rows", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook0Rows)},
        {"materialFlipbook0Frames", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook0Frames)},
        {"materialFlipbook0Fps", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook0Fps)},
        {"materialFlipbook1Cols", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook1Cols)},
        {"materialFlipbook1Rows", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook1Rows)},
        {"materialFlipbook1Frames", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook1Frames)},
        {"materialFlipbook1Fps", offsetof(d3d12_internal::WorldPsConstants, materialFlipbook1Fps)},
        {"sceneColorPostEnabled", offsetof(d3d12_internal::WorldPsConstants, sceneColorPostEnabled)},
        {"projectedShadowEnabled", offsetof(d3d12_internal::WorldPsConstants, projectedShadowEnabled)},
        {"projectedShadowSamplingScale", offsetof(d3d12_internal::WorldPsConstants, projectedShadowSamplingScale)},
        {"projectedShadowBias", offsetof(d3d12_internal::WorldPsConstants, projectedShadowBias)},
    };
    return members;
}

bool containsOffset(const MemberOffsets &members, std::size_t offset) {
    return std::any_of(members.begin(), members.end(),
                       [offset](const auto &member) { return member.second == offset; });
}

std::filesystem::path resolveFile(const std::filesystem::path &root,
                                  const std::filesystem::path &relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) {
        throw std::runtime_error("Material profile requires a project-relative file path.");
    }
    const auto base = std::filesystem::weakly_canonical(root);
    const auto path = std::filesystem::weakly_canonical(base / relative);
    const auto within = path.lexically_relative(base);
    if (within.empty() || *within.begin() == "..") {
        throw std::runtime_error("Material profile file escapes its project root.");
    }
    return path;
}

std::string readText(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open material profile file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

void WorldMaterialProfile::validate() const {
    if (empty()) {
        if (!opengl.declarations.empty() || !opengl.evaluation.empty() ||
            !d3d12.declarations.empty() || !d3d12.evaluation.empty() ||
            std::any_of(vulkan.begin(), vulkan.end(), [](const auto &v) { return !v.empty(); }) ||
            std::any_of(d3d12Constants.begin(), d3d12Constants.end(), [](const auto &v) { return !v.empty(); })) {
            throw std::runtime_error("An unnamed material profile must be empty.");
        }
        return;
    }
    if (opengl.declarations.empty() || opengl.evaluation.empty() ||
        d3d12.declarations.empty() || d3d12.evaluation.empty()) {
        throw std::runtime_error("Material profile must provide OpenGL and D3D12 shader sources.");
    }
    for (const auto &words : vulkan) {
        if (words.size() < 5 || words.front() != 0x07230203u) {
            throw std::runtime_error("Material profile must provide all four valid Vulkan shader variants.");
        }
    }
    for (const auto &mode : d3d12Constants) {
        for (const auto &mapping : mode) {
            if (!containsOffset(destinationMembers(), mapping.destinationOffset) ||
                (mapping.fromTexture && !containsOffset(sourceMembers(), mapping.sourceOffset)) ||
                !std::isfinite(mapping.value)) {
                throw std::runtime_error("Invalid material constant mapping.");
            }
        }
    }
}

WorldMaterialProfile loadWorldMaterialProfile(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &manifest) {
    if (manifest.empty()) return {};
    const auto json = nlohmann::json::parse(readText(resolveFile(projectRoot, manifest)));
    if (json.at("version").get<int>() != kWorldMaterialProfileVersion) {
        throw std::runtime_error("Material profile shader interface version does not match this engine.");
    }
    WorldMaterialProfile profile;
    profile.id = json.at("id").get<std::string>();
    if (profile.id.empty()) throw std::runtime_error("Material profile id is required.");
    const auto source = [&](const char *backend) {
        const auto &section = json.at(backend);
        return WorldMaterialShaderSource{
            readText(resolveFile(projectRoot, section.at("declarations").get<std::string>())),
            readText(resolveFile(projectRoot, section.at("evaluation").get<std::string>()))};
    };
    profile.opengl = source("opengl");
    profile.d3d12 = source("d3d12");
    constexpr std::array keys{"direct", "direct_dual_source", "indirect", "indirect_dual_source"};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto path = resolveFile(projectRoot, json.at("vulkan").at(keys[index]).get<std::string>());
        const auto bytes = readText(path);
        if (bytes.size() % sizeof(std::uint32_t)) {
            throw std::runtime_error("Invalid material SPIR-V byte count: " + path.string());
        }
        auto &words = profile.vulkan[index];
        words.resize(bytes.size() / sizeof(std::uint32_t));
        if (!bytes.empty()) std::memcpy(words.data(), bytes.data(), bytes.size());
    }
    if (json.contains("d3d12_constant_overrides")) {
        for (const auto &[modeText, overrides] : json.at("d3d12_constant_overrides").items()) {
            std::size_t used = 0;
            const auto mode = std::stoul(modeText, &used);
            if (used != modeText.size() || mode >= profile.d3d12Constants.size()) {
                throw std::runtime_error("Material mode is outside the byte-sized material interface.");
            }
            for (const auto &[destination, value] : overrides.items()) {
                WorldMaterialConstantOverride mapping;
                mapping.destinationOffset = destinationMembers().at(destination);
                mapping.fromTexture = value.is_string();
                if (mapping.fromTexture) mapping.sourceOffset = sourceMembers().at(value.get<std::string>());
                else mapping.value = value.get<float>();
                profile.d3d12Constants[mode].push_back(mapping);
            }
        }
    }
    profile.validate();
    return profile;
}

std::string injectWorldMaterialProfile(std::string_view shader, const WorldMaterialShaderSource &source) {
    std::string result(shader);
    const auto replace = [&](std::string_view marker, std::string_view replacement) {
        const auto position = result.find(marker);
        if (position == std::string::npos || result.find(marker, position + marker.size()) != std::string::npos) {
            throw std::runtime_error("World shader has an incompatible material profile insertion point.");
        }
        result.replace(position, marker.size(), replacement);
    };
    replace("__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__", source.declarations);
    replace("__PHLOSION_PROJECT_MATERIAL_EVALUATION__", source.evaluation);
    return result;
}
} // namespace engine::render
