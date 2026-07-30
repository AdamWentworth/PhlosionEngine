#pragma once

#include <filesystem>
#include <string>

#include <fastgltf/tools.hpp>

#include "engine/render/Model.h"

namespace pac::model_fastgltf {

using CPUTexture = Model::CPUTexture;

CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex);

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

}  // namespace pac::model_fastgltf
