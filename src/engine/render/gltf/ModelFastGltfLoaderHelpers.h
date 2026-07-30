#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>

namespace pac::model_fastgltf {

bool envTruthy(const char* name);
bool ciContains(const std::string& s, const std::string& needle);

int requiredTexCoordForMaterial(const fastgltf::Asset& asset, int materialIndex);

void readScalarFloat(const fastgltf::Asset& asset,
                     const fastgltf::Accessor& acc,
                     std::vector<float>& out,
                     fastgltf::DefaultBufferDataAdapter& adapter);

void readVec3AsVec4(const fastgltf::Asset& asset,
                    const fastgltf::Accessor& acc,
                    std::vector<glm::vec4>& out,
                    fastgltf::DefaultBufferDataAdapter& adapter);

void readVec4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::vec4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter);

void readMat4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::mat4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter);

void computeNormalsFromGeometry(const std::vector<glm::vec3>& positions,
                                const std::vector<std::uint32_t>& indices,
                                std::vector<glm::vec3>& outNormals);

void computeTangentsFromGeometry(const std::vector<glm::vec3>& positions,
                                 const std::vector<glm::vec2>& uvs,
                                 const std::vector<glm::vec3>& normals,
                                 const std::vector<std::uint32_t>& indices,
                                 std::vector<glm::vec4>& outTangents);

}  // namespace pac::model_fastgltf
