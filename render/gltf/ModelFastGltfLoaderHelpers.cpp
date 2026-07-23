#include "ModelFastGltfLoaderHelpers.h"
#include "engine/core/Environment.h"

#include <fastgltf/glm_element_traits.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pac::model_fastgltf {

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

glm::vec3 safeNormalizeVec3(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return fallback;
}

}  // namespace

bool envTruthy(const char* name) {
    return engine::env::truthyNonZero(name);
}

bool ciContains(const std::string& s, const std::string& needle) {
    return toLowerCopy(s).find(toLowerCopy(needle)) != std::string::npos;
}

int requiredTexCoordForMaterial(const fastgltf::Asset& asset, int materialIndex) {
    if (materialIndex < 0 || materialIndex >= static_cast<int>(asset.materials.size())) return 0;

    const auto& m = asset.materials[static_cast<size_t>(materialIndex)];

    if (m.pbrData.baseColorTexture.has_value()) {
        return static_cast<int>(m.pbrData.baseColorTexture->texCoordIndex);
    }
    if (m.emissiveTexture.has_value()) {
        return static_cast<int>(m.emissiveTexture->texCoordIndex);
    }
    return 0;
}

void readScalarFloat(const fastgltf::Asset& asset,
                     const fastgltf::Accessor& acc,
                     std::vector<float>& out,
                     fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<float>(
        asset,
        acc,
        [&](float v, size_t) { out.push_back(v); },
        adapter);
}

void readVec3AsVec4(const fastgltf::Asset& asset,
                    const fastgltf::Accessor& acc,
                    std::vector<glm::vec4>& out,
                    fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        asset,
        acc,
        [&](glm::vec3 v, size_t) { out.emplace_back(v.x, v.y, v.z, 0.0f); },
        adapter);
}

void readVec4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::vec4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::vec4>(
        asset,
        acc,
        [&](glm::vec4 v, size_t) { out.push_back(v); },
        adapter);
}

void readMat4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::mat4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::mat4>(
        asset,
        acc,
        [&](glm::mat4 m, size_t) { out.push_back(m); },
        adapter);
}

void computeNormalsFromGeometry(const std::vector<glm::vec3>& positions,
                                const std::vector<std::uint32_t>& indices,
                                std::vector<glm::vec3>& outNormals) {
    outNormals.assign(positions.size(), glm::vec3(0.0f));
    if (positions.empty()) return;

    const std::size_t triCount = indices.size() / 3u;
    for (std::size_t triIdx = 0; triIdx < triCount; ++triIdx) {
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = indices[i + 0u];
        const std::uint32_t i1 = indices[i + 1u];
        const std::uint32_t i2 = indices[i + 2u];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;
        const glm::vec3 e1 = positions[i1] - positions[i0];
        const glm::vec3 e2 = positions[i2] - positions[i0];
        const glm::vec3 n = glm::cross(e1, e2);
        const float lenSq = glm::dot(n, n);
        if (lenSq <= 1e-12f) continue;
        outNormals[i0] += n;
        outNormals[i1] += n;
        outNormals[i2] += n;
    }
    for (auto& n : outNormals) {
        const float lenSq = glm::dot(n, n);
        n = (lenSq > 1e-12f) ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void computeTangentsFromGeometry(const std::vector<glm::vec3>& positions,
                                 const std::vector<glm::vec2>& uvs,
                                 const std::vector<glm::vec3>& normals,
                                 const std::vector<std::uint32_t>& indices,
                                 std::vector<glm::vec4>& outTangents) {
    outTangents.assign(positions.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    if (positions.empty() || uvs.size() != positions.size() || normals.size() != positions.size()) {
        return;
    }

    std::vector<glm::vec3> tan1(positions.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(positions.size(), glm::vec3(0.0f));

    const std::size_t triCount = indices.size() / 3u;
    for (std::size_t triIdx = 0; triIdx < triCount; ++triIdx) {
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = indices[i + 0u];
        const std::uint32_t i1 = indices[i + 1u];
        const std::uint32_t i2 = indices[i + 2u];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;

        const glm::vec3& p0 = positions[i0];
        const glm::vec3& p1 = positions[i1];
        const glm::vec3& p2 = positions[i2];
        const glm::vec2& uv0 = uvs[i0];
        const glm::vec2& uv1 = uvs[i1];
        const glm::vec2& uv2 = uvs[i2];

        const glm::vec3 e1 = p1 - p0;
        const glm::vec3 e2 = p2 - p0;
        const glm::vec2 dUV1 = uv1 - uv0;
        const glm::vec2 dUV2 = uv2 - uv0;
        const float det = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
        if (std::fabs(det) <= 1e-8f) continue;
        const float invDet = 1.0f / det;

        const glm::vec3 sdir = (e1 * dUV2.y - e2 * dUV1.y) * invDet;
        const glm::vec3 tdir = (e2 * dUV1.x - e1 * dUV2.x) * invDet;
        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    for (std::size_t vi = 0; vi < positions.size(); ++vi) {
        const glm::vec3 n = safeNormalizeVec3(normals[vi], glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 t = tan1[vi] - n * glm::dot(n, tan1[vi]);
        const float tLenSq = glm::dot(t, t);
        if (tLenSq <= 1e-10f) {
            const glm::vec3 helper =
                (std::fabs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            t = safeNormalizeVec3(glm::cross(helper, n), glm::vec3(1.0f, 0.0f, 0.0f));
            outTangents[vi] = glm::vec4(t, 1.0f);
            continue;
        }
        t = glm::normalize(t);
        const float handedness = (glm::dot(glm::cross(n, t), tan2[vi]) < 0.0f) ? -1.0f : 1.0f;
        outTangents[vi] = glm::vec4(t, handedness);
    }
}

}  // namespace pac::model_fastgltf
