#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

#include <fastgltf/tools.hpp>

#include "engine/render/Model.h"

namespace engine::render::gltf::model {

using CPUTexture = Model::CPUTexture;

// How the base-color route treats pbrMetallicRoughness.baseColorFactor.
//
// LegacyBakeFactor is the historical behaviour every existing caller and the
// Model cache depend on: the factor is multiplied into the decoded bytes (and
// an absent base map resolves to a one-texel baseColorFactor image).
//
// PreserveSource is the additive policy for CPU-owned scene consumers: the
// decoded source pixels are returned unchanged so the factor can be applied
// exactly once in a shader field, and an absent base map resolves to opaque
// white. No existing caller's default changes.
enum class BaseColorDecodePolicy : std::uint8_t {
    LegacyBakeFactor = 0u,
    PreserveSource = 1u,
};

CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex,
                                      BaseColorDecodePolicy policy =
                                          BaseColorDecodePolicy::LegacyBakeFactor);

CPUTexture decodeEmissiveTextureFast(const fastgltf::Asset& asset,
                                     const std::filesystem::path& baseDir,
                                     int materialIndex,
                                     bool dbg,
                                     const std::string& modelPath,
                                     int* outTexCoordIndex);

CPUTexture decodeNormalTextureFast(const fastgltf::Asset& asset,
                                   const std::filesystem::path& baseDir,
                                   int materialIndex,
                                   bool dbg,
                                   const std::string& modelPath,
                                   int* outTexCoordIndex);

CPUTexture decodeMetallicRoughnessTextureFast(const fastgltf::Asset& asset,
                                              const std::filesystem::path& baseDir,
                                              int materialIndex,
                                              bool dbg,
                                              const std::string& modelPath,
                                              int* outTexCoordIndex);

CPUTexture decodeOcclusionTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex);

}  // namespace engine::render::gltf::model
