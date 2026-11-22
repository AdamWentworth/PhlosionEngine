#include "engine/render/WorldMaterialProfile.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
    static const MemberOffsets members = [] {
        MemberOffsets result{
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
        const auto addVector = [&](const char *name, std::size_t offset) {
            for (std::size_t component = 0; component < 4; ++component) {
                result.emplace(std::string(name) + "." + std::to_string(component),
                               offset + component * sizeof(float));
            }
        };
        addVector("projectedShadowRowX", offsetof(d3d12_internal::WorldPsConstants, projectedShadowRowX));
        addVector("projectedShadowRowY", offsetof(d3d12_internal::WorldPsConstants, projectedShadowRowY));
        addVector("projectedShadowRowZ", offsetof(d3d12_internal::WorldPsConstants, projectedShadowRowZ));
        addVector("lightProjectionUvRowU", offsetof(d3d12_internal::WorldPsConstants, lightProjectionUvRowU));
        addVector("lightProjectionUvRowV", offsetof(d3d12_internal::WorldPsConstants, lightProjectionUvRowV));
        return result;
    }();
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

std::size_t parseMode(const std::string &text) {
    std::size_t used = 0;
    const auto mode = std::stoul(text, &used);
    if (used != text.size() || mode > 255) {
        throw std::runtime_error("Material mode is outside the byte-sized material interface.");
    }
    return mode;
}

WorldMaterialConstantTerm parseTerm(const nlohmann::json &value) {
    WorldMaterialConstantTerm term;
    using Source = WorldMaterialConstantTerm::Source;
    if (value.is_number()) term.value = value.get<float>();
    else if (value.is_string()) {
        term.source = Source::Texture;
        term.sourceOffset = sourceMembers().at(value.get<std::string>());
    } else {
        if (value.contains("source")) {
            term.source = Source::Texture;
            term.sourceOffset = sourceMembers().at(value.at("source").get<std::string>());
        } else if (value.contains("constant")) {
            term.source = Source::Constant;
            term.sourceOffset = destinationMembers().at(value.at("constant").get<std::string>());
        } else {
            term.value = value.at("value").get<float>();
        }
        term.minimum = value.value("minimum", term.minimum);
        term.maximum = value.value("maximum", term.maximum);
        term.bias = value.value("bias", 0.0f);
        term.scale = value.value("scale", 1.0f);
        term.round = value.value("round", false);
    }
    return term;
}
} // namespace

int WorldMaterialProfile::transparentFamilyIndex(std::uint32_t mode) const noexcept {
    if (mode > 255) return -1;
    for (std::size_t i=0;i<transparentFamilies.size();++i)
        if (transparentFamilies[i].modes[mode]) return static_cast<int>(i);
    return -1;
}

void WorldMaterialProfile::validate() const {
    if(textureMipDrop>2 || (maxAnisotropy!=1&&maxAnisotropy!=4&&maxAnisotropy!=8&&maxAnisotropy!=16))
        throw std::runtime_error("Invalid material texture quality");
    if (transparentFamilies.size()>16) throw std::runtime_error("Too many transparent material families");
    std::bitset<256> ownedModes;
    std::vector<std::string> familyIds;
    for (const auto& family:transparentFamilies) {
        if (family.id.empty() || family.id.size()>64 || family.modes.none() ||
            (ownedModes&family.modes).any() ||
            std::find(familyIds.begin(),familyIds.end(),family.id)!=familyIds.end())
            throw std::runtime_error("Invalid or overlapping transparent material family");
        // These modes select engine composition/portable passes before a
        // project evaluation fragment is reached.
        for (const auto mode : {239u,246u,247u,248u,249u,250u,251u,252u,253u,255u})
            if (family.modes[mode])
                throw std::runtime_error("Engine-owned pass cannot be assigned to a transparent family.");
        ownedModes|=family.modes;familyIds.push_back(family.id);
        if (family.d3d12.declarations.empty() || family.d3d12.evaluation.empty() ||
            family.d3d12Vertex.declarations.empty() || family.d3d12Vertex.evaluation.empty())
            throw std::runtime_error("Transparent family requires complete D3D12 programs");
        for (const auto& words:family.vulkan)
            if (words.size()<5 || words.front()!=0x07230203u)
                throw std::runtime_error("Transparent family requires all three Vulkan programs");
    }

    if(d3d12PipelineWarmup.size()>256)throw std::runtime_error("Too many material warmup pipelines");
    for(const auto& request:d3d12PipelineWarmup)
        if(request.mode>255 || request.variant>4095 || (request.variant&15u)>12)
            throw std::runtime_error("Invalid material warmup pipeline");

    for (const auto& request : d3d12PipelineWarmup)
        if (transparentFamilyIndex(request.mode) >= 0 &&
                ((request.variant & 15u) == 0 || (request.variant & ~31u) != 0))
            throw std::runtime_error("Transparent family warmup requires a blended color pass.");

    if(vulkanPipelineWarmup.size()>256)throw std::runtime_error("Too many Vulkan material pipelines");
    std::vector<std::uint32_t> vulkanKeys;
    for(const auto& request:vulkanPipelineWarmup) {
        if(request.mode>255 || request.variant>13 || (request.variant>=8 && request.variant<=11))
            throw std::runtime_error("Invalid Vulkan material pipeline");
        if (transparentFamilyIndex(request.mode) >= 0 && request.variant < 2)
            throw std::runtime_error("Transparent family warmup requires a blended color pass.");
        const auto key=request.mode*32+request.variant;
        if(std::find(vulkanKeys.begin(),vulkanKeys.end(),key)!=vulkanKeys.end())
            throw std::runtime_error("Duplicate Vulkan material pipeline");
        vulkanKeys.push_back(key);
    }
    if (empty()) {
        if (!transparentFamilies.empty() || !vulkanPipelineWarmup.empty() || !d3d12PipelineWarmup.empty() || !opengl.declarations.empty() || !opengl.evaluation.empty() ||
            !d3d12.declarations.empty() || !d3d12.evaluation.empty() ||
            !openglVertex.declarations.empty() || !openglVertex.evaluation.empty() ||
            !d3d12Vertex.declarations.empty() || !d3d12Vertex.evaluation.empty() ||
            std::any_of(vulkan.begin(), vulkan.end(), [](const auto &v) { return !v.empty(); }) ||
            std::any_of(d3d12Constants.begin(), d3d12Constants.end(), [](const auto &v) { return !v.empty(); }) ||
            std::any_of(modes.begin(), modes.end(), [](const auto &mode) { return mode != WorldMaterialModeOptions{}; })) {
            throw std::runtime_error("An unnamed material profile must be empty.");
        }
        return;
    }
    if (opengl.declarations.empty() || opengl.evaluation.empty() ||
        d3d12.declarations.empty() || d3d12.evaluation.empty() ||
        openglVertex.declarations.empty() || openglVertex.evaluation.empty() ||
        d3d12Vertex.declarations.empty() || d3d12Vertex.evaluation.empty()) {
        throw std::runtime_error("Material profile must provide OpenGL and D3D12 shader sources.");
    }
    for (std::size_t index = 0; index < vulkan.size(); ++index) {
        const auto& words = vulkan[index];
        if (index >= 6 && words.empty()) continue;
        if (words.size() < 5 || words.front() != 0x07230203u) {
            throw std::runtime_error("Material profile must provide all six valid Vulkan shader variants.");
        }
    }
    for (const auto &mode : d3d12Constants) {
        for (const auto &mapping : mode) {
            if (!containsOffset(destinationMembers(), mapping.destinationOffset) || mapping.terms.empty() ||
                (mapping.conditional && (!containsOffset(sourceMembers(), mapping.conditionOffset) ||
                                         !std::isfinite(mapping.greaterThan) || !std::isfinite(mapping.lessThan) ||
                                         mapping.greaterThan >= mapping.lessThan))) {
                throw std::runtime_error("Invalid material constant mapping.");
            }
            for (const auto &term : mapping.terms) {
                using Source = WorldMaterialConstantTerm::Source;
                if ((term.source == Source::Texture && !containsOffset(sourceMembers(), term.sourceOffset)) ||
                    (term.source == Source::Constant && !containsOffset(destinationMembers(), term.sourceOffset)) ||
                    (term.source != Source::Literal && term.source != Source::Texture && term.source != Source::Constant) ||
                    !std::isfinite(term.value) || !std::isfinite(term.minimum) || !std::isfinite(term.maximum) ||
                    !std::isfinite(term.scale) || !std::isfinite(term.bias) || term.minimum > term.maximum) {
                    throw std::runtime_error("Invalid material constant term.");
                }
            }
        }
    }
    for (const auto &mode : modes) {
        if (!std::isfinite(mode.occlusionMaximum) || mode.occlusionMaximum < 0) {
            throw std::runtime_error("Invalid material parameter bound.");
        }
    }
}

WorldMaterialProfile loadWorldMaterialProfile(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &manifest) {
    if (manifest.empty()) return {};
    const auto json = nlohmann::json::parse(readText(resolveFile(projectRoot, manifest)));
    const auto version=json.at("version").get<int>();
    if (version != 2 && version != kWorldMaterialProfileVersion) {
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
    profile.openglVertex = source("opengl_vertex");
    profile.d3d12Vertex = source("d3d12_vertex");
    constexpr std::array keys{"direct", "direct_dual_source", "indirect", "indirect_dual_source", "direct_vertex", "indirect_vertex", "directional_shadow", "surface", "motion_vertex"};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index >= 6 && !json.at("vulkan").contains(keys[index])) continue;
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
            const auto mode = parseMode(modeText);
            for (const auto &[destination, value] : overrides.items()) {
                WorldMaterialConstantOverride mapping;
                mapping.destinationOffset = destinationMembers().at(destination);
                if (value.is_object() && value.contains("terms")) {
                    for (const auto &term : value.at("terms"))
                        mapping.terms.push_back(parseTerm(term));
                } else mapping.terms.push_back(parseTerm(value));
                if (value.is_object() && value.contains("when")) {
                    const auto &condition = value.at("when");
                    mapping.conditional = true;
                    mapping.conditionOffset = sourceMembers().at(condition.at("source").get<std::string>());
                    mapping.greaterThan = condition.value("greater_than", mapping.greaterThan);
                    mapping.lessThan = condition.value("less_than", mapping.lessThan);
                }
                profile.d3d12Constants[mode].push_back(mapping);
            }
        }
    }
    if (json.contains("modes")) {
        for (const auto &[modeText, values] : json.at("modes").items()) {
            auto &mode = profile.modes[parseMode(modeText)];
            mode.pbrPacking = values.value("pbr_packing", true);
            mode.sharedLocalLighting = values.value("shared_local_lighting", false);
            mode.foliageComposition = values.value("foliage_composition", false);
            mode.openglDebugParameter = values.value("opengl_debug_parameter", true);
            mode.d3d12DebugParameter = values.value("d3d12_debug_parameter", true);
            mode.occlusionMaximum = values.value("occlusion_maximum", 1.0f);
        }
    }
    if(json.contains("d3d12_pipeline_warmup")) {
        const auto& requests=json.at("d3d12_pipeline_warmup");
        if(!requests.is_array() || requests.size()>256)throw std::runtime_error("Invalid material warmup plan");
        for(const auto& request:requests) {
            if(!request.at("mode").is_number_unsigned() || !request.at("variant").is_number_unsigned())
                throw std::runtime_error("Warmup mode/variant must be unsigned integers");
            const auto mode=request.at("mode").get<std::uint64_t>();
            const auto variant=request.at("variant").get<std::uint64_t>();
            if(mode>255 || variant>4095)throw std::runtime_error("Material warmup pipeline out of range");
            profile.d3d12PipelineWarmup.push_back({static_cast<std::uint32_t>(mode),static_cast<std::uint32_t>(variant)});
        }
    }
    if(json.contains("vulkan_pipeline_warmup")) {
        const auto& requests=json.at("vulkan_pipeline_warmup");
        if(!requests.is_array() || requests.size()>256)throw std::runtime_error("Invalid Vulkan material plan");
        for(const auto& request:requests) {
            if(!request.at("mode").is_number_unsigned() || !request.at("variant").is_number_unsigned())
                throw std::runtime_error("Vulkan material mode/variant must be unsigned integers");
            const auto mode=request.at("mode").get<std::uint64_t>();
            const auto variant=request.at("variant").get<std::uint64_t>();
            if(mode>255 || variant>13)throw std::runtime_error("Vulkan material pipeline out of range");
            profile.vulkanPipelineWarmup.push_back({static_cast<std::uint32_t>(mode),static_cast<std::uint32_t>(variant)});
        }
    }
    if (json.contains("transparent_families")) {
        const auto& families=json.at("transparent_families");
        if(version<3 || !families.is_array() || families.size()>16)
            throw std::runtime_error("Invalid transparent family manifest/version");
        for(const auto& item:families) {
            WorldMaterialFamily family;
            family.id=item.at("id").get<std::string>();
            const auto& modes=item.at("modes");
            if(!modes.is_array() || modes.empty() || modes.size()>256)
                throw std::runtime_error("Invalid transparent family modes");
            for(const auto& value:modes) {
                if(!value.is_number_unsigned() || value.get<std::uint64_t>()>255)
                    throw std::runtime_error("Transparent family mode out of range");
                const auto mode=value.get<std::uint32_t>();
                if(family.modes[mode]) throw std::runtime_error("Duplicate transparent family mode");
                family.modes.set(mode);
            }
            const auto familySource=[&](const char* name) {
                const auto& section=item.at(name);
                return WorldMaterialShaderSource{
                    readText(resolveFile(projectRoot,section.at("declarations").get<std::string>())),
                    readText(resolveFile(projectRoot,section.at("evaluation").get<std::string>()))};
            };
            family.d3d12=familySource("d3d12");
            family.d3d12Vertex=familySource("d3d12_vertex");
            constexpr std::array familyKeys{"direct","direct_dual_source","direct_vertex"};
            for(std::size_t i=0;i<familyKeys.size();++i) {
                const auto bytes=readText(resolveFile(projectRoot,item.at("vulkan").at(familyKeys[i]).get<std::string>()));
                if(bytes.size()%4)throw std::runtime_error("Invalid transparent family SPIR-V byte count");
                family.vulkan[i].resize(bytes.size()/4);
                if(!bytes.empty())std::memcpy(family.vulkan[i].data(),bytes.data(),bytes.size());
            }
            profile.transparentFamilies.push_back(std::move(family));
        }
    }
    profile.validate();
    return profile;
}

void applyWorldMaterialConstantOverrides(const WorldMaterialProfile &profile, std::uint8_t mode,
                                         const void *textureData, void *constants) {
    const auto read = [](const void *data, std::size_t offset) {
        float value;
        std::memcpy(&value, static_cast<const unsigned char *>(data) + offset, sizeof(value));
        return value;
    };
    for (const auto &mapping : profile.d3d12Constants[mode]) {
        if (mapping.conditional) {
            const auto value = read(textureData, mapping.conditionOffset);
            if (!(value > mapping.greaterThan && value < mapping.lessThan)) continue;
        }
        float result = 0;
        bool first = true;
        for (const auto &term : mapping.terms) {
            using Source = WorldMaterialConstantTerm::Source;
            float value = term.source == Source::Texture ? read(textureData, term.sourceOffset) : term.source == Source::Constant ? read(constants, term.sourceOffset)
                                                                                                                                  : term.value;
            value = std::clamp(value, term.minimum, term.maximum);
            if (term.bias != 0) value += term.bias;
            if (term.scale != 1) value *= term.scale;
            if (term.round) value = std::round(value);
            if (first) result = value;
            else result += value;
            first = false;
        }
        std::memcpy(static_cast<unsigned char *>(constants) + mapping.destinationOffset, &result, sizeof(result));
    }
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
