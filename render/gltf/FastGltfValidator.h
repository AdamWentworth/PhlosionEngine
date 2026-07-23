// FastGltfValidator.cpp

#pragma once

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#include "engine/core/Environment.h"

namespace pac::fastgltf_validator {

inline bool envFlagEnabled(const char* name) {
    return engine::env::flagEnabled(name);
}

inline const char* errorName(fastgltf::Error e) {
    switch (e) {
        case fastgltf::Error::None: return "None";
        case fastgltf::Error::InvalidPath: return "InvalidPath";
        case fastgltf::Error::MissingExtensions: return "MissingExtensions";
        case fastgltf::Error::UnknownRequiredExtension: return "UnknownRequiredExtension";
        case fastgltf::Error::InvalidJson: return "InvalidJson";
        case fastgltf::Error::InvalidGltf: return "InvalidGltf";
        case fastgltf::Error::InvalidOrMissingAssetField: return "InvalidOrMissingAssetField";
        case fastgltf::Error::InvalidGLB: return "InvalidGLB";
        case fastgltf::Error::MissingField: return "MissingField";
        case fastgltf::Error::MissingExternalBuffer: return "MissingExternalBuffer";
        case fastgltf::Error::UnsupportedVersion: return "UnsupportedVersion";
        case fastgltf::Error::InvalidURI: return "InvalidURI";
        case fastgltf::Error::InvalidFileData: return "InvalidFileData";
        case fastgltf::Error::FailedWritingFiles: return "FailedWritingFiles";
        default: return "UnknownError";
    }
}

// "Enable everything fastgltf documents as supported extensions".
// This does NOT mean "enable every glTF extension in existence", only the ones fastgltf knows about.
constexpr fastgltf::Extensions kSupportedExtensionsMask =
    fastgltf::Extensions::KHR_texture_transform |
    fastgltf::Extensions::KHR_texture_basisu |
    fastgltf::Extensions::MSFT_texture_dds |
    fastgltf::Extensions::KHR_mesh_quantization |
    fastgltf::Extensions::EXT_meshopt_compression |
    fastgltf::Extensions::KHR_lights_punctual |
    fastgltf::Extensions::EXT_texture_webp |
    fastgltf::Extensions::KHR_materials_specular |
    fastgltf::Extensions::KHR_materials_ior |
    fastgltf::Extensions::KHR_materials_iridescence |
    fastgltf::Extensions::KHR_materials_volume |
    fastgltf::Extensions::KHR_materials_transmission |
    fastgltf::Extensions::KHR_materials_clearcoat |
    fastgltf::Extensions::KHR_materials_emissive_strength |
    fastgltf::Extensions::KHR_materials_sheen |
    fastgltf::Extensions::KHR_materials_unlit |
    fastgltf::Extensions::KHR_materials_anisotropy |
    fastgltf::Extensions::EXT_mesh_gpu_instancing |
    fastgltf::Extensions::MSFT_packing_normalRoughnessMetallic |
    fastgltf::Extensions::MSFT_packing_occlusionRoughnessMetallic |
    fastgltf::Extensions::KHR_materials_dispersion |
    fastgltf::Extensions::KHR_materials_variants;

inline void printSummary(const fastgltf::Asset& a, const std::filesystem::path& filePath) {
    std::size_t primitiveCount = 0;
    for (const auto& m : a.meshes) {
        primitiveCount += m.primitives.size();
    }

    std::cout << "[fastgltf] Parsed OK: " << filePath.filename().string() << "\n"
              << "  meshes=" << a.meshes.size()
              << " primitives=" << primitiveCount
              << " nodes=" << a.nodes.size()
              << " scenes=" << a.scenes.size()
              << " accessors=" << a.accessors.size()
              << " bufferViews=" << a.bufferViews.size()
              << " buffers=" << a.buffers.size()
              << " images=" << a.images.size()
              << " textures=" << a.textures.size()
              << " materials=" << a.materials.size()
              << " skins=" << a.skins.size()
              << " animations=" << a.animations.size()
              << "\n";
}

inline void logSummaryIfEnabled(const std::string& modelPath) {
    if (!envFlagEnabled("PAC_FASTGLTF_VALIDATE")) {
        return;
    }

    std::filesystem::path path(modelPath);
    std::error_code ec;
    auto absPath = std::filesystem::absolute(path, ec);
    const auto& usePath = ec ? path : absPath;

    auto data = fastgltf::GltfDataBuffer::FromPath(usePath);
    if (data.error() != fastgltf::Error::None) {
        std::cerr << "[fastgltf] Failed to read file into buffer: " << usePath.string()
                  << " (Error=" << errorName(data.error()) << ")\n";
        return;
    }

    // First attempt: no extensions enabled (helps catch MissingExtensions explicitly).
    {
        fastgltf::Parser parser(fastgltf::Extensions::None);
        auto asset = parser.loadGltf(data.get(), usePath.parent_path(), fastgltf::Options::None);
        if (asset.error() == fastgltf::Error::None) {
            printSummary(asset.get(), usePath);
            return;
        }

        if (asset.error() != fastgltf::Error::MissingExtensions) {
            std::cerr << "[fastgltf] Failed to parse: " << usePath.string()
                      << " (Error=" << errorName(asset.error()) << ")\n";
            return;
        }
    }

    // Second attempt: enable all extensions fastgltf documents as supported.
    {
        fastgltf::Parser parser(kSupportedExtensionsMask);
        auto asset = parser.loadGltf(data.get(), usePath.parent_path(), fastgltf::Options::None);
        if (asset.error() == fastgltf::Error::None) {
            std::cout << "[fastgltf] Note: model required extensions; parsed after enabling supported extension mask.\n";
            printSummary(asset.get(), usePath);
            return;
        }

        std::cerr << "[fastgltf] Failed to parse even with supported extensions enabled: " << usePath.string()
                  << " (Error=" << errorName(asset.error()) << ")\n";
    }
}

} // namespace pac::fastgltf_validator
