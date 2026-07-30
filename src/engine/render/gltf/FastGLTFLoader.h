// FastGLTFLoader.h
#pragma once

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

#include "engine/core/Environment.h"

namespace pac::fastgltf_loader {

namespace detail {

inline bool envFlagEnabled(const char* name) {
    return engine::env::flagEnabled(name);
}

} // namespace detail

inline constexpr const char* errorName(fastgltf::Error e) {
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

inline constexpr fastgltf::Extensions kSupportedExtensionsMask =
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

struct LoadResult {
    fastgltf::Asset asset;
    std::filesystem::path baseDir;
};

// Note: consider returning a richer error type instead of printing.
// Keeping this header-only: return nullopt on failure and let caller log.
inline std::optional<LoadResult> tryLoad(std::string_view filepath) {
    std::filesystem::path path(filepath);

    std::error_code ec;
    const auto absPath = std::filesystem::absolute(path, ec);
    const auto& usePath = ec ? path : absPath;

    auto data = fastgltf::GltfDataBuffer::FromPath(usePath);
    if (data.error() != fastgltf::Error::None) {
        return std::nullopt;
    }

    fastgltf::Parser parser(kSupportedExtensionsMask);
    const auto baseDir = usePath.parent_path();

    constexpr fastgltf::Options kOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::DecomposeNodeMatrices |
        fastgltf::Options::GenerateMeshIndices;

    auto asset = parser.loadGltf(data.get(), baseDir, kOptions, fastgltf::Category::All);
    if (asset.error() != fastgltf::Error::None) {
        return std::nullopt;
    }

    return LoadResult{std::move(asset.get()), baseDir};
}

} // namespace pac::fastgltf_loader
