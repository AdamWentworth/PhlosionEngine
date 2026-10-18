#include "engine/render/LegacyOpenGL.h"
#include "engine/vfx/preview/shared/SharedAuthoredVfxRenderer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "engine/core/Paths.h"
#include "engine/render/Camera3D.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfLoaderHelpers.h"
#include "engine/utils/LogSink.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxSubmission.h"

namespace vfx::preview::authored {
namespace {

using MeshData = vfx::runtime::authored_batches::MeshData;
using MeshVertex = vfx::runtime::authored_batches::MeshVertex;
using TextureCacheEntry = SharedAuthoredVfxRenderer::TextureCacheEntry;

struct ScopedGlPreviewRenderState final {
    static constexpr int kTrackedTextureUnits = 8;

    GLint framebuffer = 0;
    GLint viewport[4]{0, 0, 0, 0};
    GLint scissorBox[4]{0, 0, 0, 0};
    GLint currentProgram = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint elementArrayBuffer = 0;
    GLint activeTexture = 0;
    GLint currentTexture2D = 0;
    GLint texture2DPerUnit[kTrackedTextureUnits]{0};
    GLint frontFace = GL_CCW;
    GLint cullFaceMode = GL_BACK;
    GLint depthFunc = GL_LESS;
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint blendEqRgb = GL_FUNC_ADD;
    GLint blendEqAlpha = GL_FUNC_ADD;
    GLboolean depthMask = GL_TRUE;
    bool depthTestEnabled = false;
    bool blendEnabled = false;
    bool cullEnabled = false;
    bool scissorEnabled = false;
    bool framebufferSrgbEnabled = false;

    ScopedGlPreviewRenderState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementArrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &currentTexture2D);
        for (int unit = 0; unit < kTrackedTextureUnits; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2DPerUnit[unit]);
        }
        glActiveTexture(static_cast<GLenum>(activeTexture));

        glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqAlpha);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

        depthTestEnabled = (glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
        blendEnabled = (glIsEnabled(GL_BLEND) == GL_TRUE);
        cullEnabled = (glIsEnabled(GL_CULL_FACE) == GL_TRUE);
        scissorEnabled = (glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
        framebufferSrgbEnabled = (glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE);
    }

    ~ScopedGlPreviewRenderState() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        if (scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        glBindVertexArray(static_cast<GLuint>(vertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(elementArrayBuffer));
        glUseProgram(static_cast<GLuint>(currentProgram));

        for (int unit = 0; unit < kTrackedTextureUnits; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2DPerUnit[unit]));
        }
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(currentTexture2D));

        glFrontFace(static_cast<GLenum>(frontFace));
        glCullFace(static_cast<GLenum>(cullFaceMode));
        glDepthFunc(static_cast<GLenum>(depthFunc));
        glDepthMask(depthMask);
        glBlendEquationSeparate(static_cast<GLenum>(blendEqRgb), static_cast<GLenum>(blendEqAlpha));
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb),
                            static_cast<GLenum>(blendDstRgb),
                            static_cast<GLenum>(blendSrcAlpha),
                            static_cast<GLenum>(blendDstAlpha));
        if (depthTestEnabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        if (blendEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        if (cullEnabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        if (framebufferSrgbEnabled) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        } else {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }
    }
};

std::vector<glm::vec2> buildFallbackUvs(const std::vector<glm::vec3>& positions) {
    std::vector<glm::vec2> uvs;
    if (positions.empty()) return uvs;

    glm::vec3 pMin(std::numeric_limits<float>::max());
    glm::vec3 pMax(-std::numeric_limits<float>::max());
    for (const auto& position : positions) {
        pMin = glm::min(pMin, position);
        pMax = glm::max(pMax, position);
    }

    const float ranges[3] = {
        pMax.x - pMin.x,
        pMax.y - pMin.y,
        pMax.z - pMin.z,
    };

    int a = 0;
    if (ranges[1] > ranges[a]) a = 1;
    if (ranges[2] > ranges[a]) a = 2;
    int b = (a == 0) ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        if (i == a) continue;
        if (ranges[i] > ranges[b]) b = i;
    }

    const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
    const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
    const float denA = std::max(1e-6f, ranges[a]);
    const float denB = std::max(1e-6f, ranges[b]);

    uvs.reserve(positions.size());
    for (const auto& position : positions) {
        const float ca = (a == 0) ? position.x : ((a == 1) ? position.y : position.z);
        const float cb = (b == 0) ? position.x : ((b == 1) ? position.y : position.z);
        uvs.emplace_back((ca - minA) / denA, (cb - minB) / denB);
    }
    return uvs;
}

std::string resolveDataPath(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return path;
    }
    const std::string dataPath = engine::paths::data(path);
    if (std::filesystem::exists(dataPath, ec) && !ec) {
        return dataPath;
    }
    return path;
}

std::string trimCopyLocal(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    return value;
}

std::string toLowerCopyLocal(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> splitCsvRowLocal(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream stream(line);
    while (std::getline(stream, token, ',')) {
        tokens.push_back(trimCopyLocal(token));
    }
    if (!line.empty() && line.back() == ',') {
        tokens.emplace_back();
    }
    return tokens;
}

struct CsvMeshColumns {
    int px = -1;
    int py = -1;
    int pz = -1;
    int tu = -1;
    int tv = -1;
    int cr = -1;
    int cg = -1;
    int cb = -1;
    int ca = -1;
};

int findColumnIndexLocal(const std::vector<std::string>& header, std::string_view name) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (header[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool parseCsvMeshColumnsLocal(const std::vector<std::string>& header,
                              CsvMeshColumns& outColumns,
                              std::string* outError) {
    outColumns.px = findColumnIndexLocal(header, "rawpos.x");
    outColumns.py = findColumnIndexLocal(header, "rawpos.y");
    outColumns.pz = findColumnIndexLocal(header, "rawpos.z");
    outColumns.tu = findColumnIndexLocal(header, "rawtex0.x");
    outColumns.tv = findColumnIndexLocal(header, "rawtex0.y");
    outColumns.cr = findColumnIndexLocal(header, "rawcolor0.x");
    outColumns.cg = findColumnIndexLocal(header, "rawcolor0.y");
    outColumns.cb = findColumnIndexLocal(header, "rawcolor0.z");
    outColumns.ca = findColumnIndexLocal(header, "rawcolor0.w");
    if (outColumns.px < 0 || outColumns.py < 0 || outColumns.pz < 0) {
        if (outError) {
            *outError = "CSV is missing rawpos.x/y/z columns";
        }
        return false;
    }
    if ((outColumns.tu >= 0) != (outColumns.tv >= 0)) {
        if (outError) {
            *outError = "CSV has an incomplete rawtex0 column pair";
        }
        return false;
    }
    const int colorColumnsPresent =
        (outColumns.cr >= 0 ? 1 : 0) + (outColumns.cg >= 0 ? 1 : 0) +
        (outColumns.cb >= 0 ? 1 : 0) + (outColumns.ca >= 0 ? 1 : 0);
    if (colorColumnsPresent != 0 && colorColumnsPresent != 4) {
        if (outError) {
            *outError = "CSV has an incomplete rawcolor0 RGBA set";
        }
        return false;
    }
    return true;
}

bool parseCsvFloatLocal(const std::vector<std::string>& tokens,
                        int columnIndex,
                        float defaultValue,
                        float& outValue) {
    if (columnIndex < 0 || columnIndex >= static_cast<int>(tokens.size())) {
        outValue = defaultValue;
        return true;
    }
    const std::string token = trimCopyLocal(tokens[static_cast<std::size_t>(columnIndex)]);
    if (token.empty()) {
        outValue = defaultValue;
        return true;
    }
    try {
        outValue = std::stof(token);
        return true;
    } catch (...) {
        return false;
    }
}

void rebuildCsvMeshNormalsLocal(MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return;

    std::vector<glm::vec3> accumulated(mesh.vertices.size(), glm::vec3(0.0f));
    for (std::size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = mesh.indices[i + 0u];
        const std::uint32_t ib = mesh.indices[i + 1u];
        const std::uint32_t ic = mesh.indices[i + 2u];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            continue;
        }
        const glm::vec3 a = mesh.vertices[ia].position;
        const glm::vec3 b = mesh.vertices[ib].position;
        const glm::vec3 c = mesh.vertices[ic].position;
        const glm::vec3 faceNormal = glm::cross(b - a, c - a);
        if (glm::dot(faceNormal, faceNormal) <= 0.000001f) continue;
        accumulated[ia] += faceNormal;
        accumulated[ib] += faceNormal;
        accumulated[ic] += faceNormal;
    }

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const glm::vec3 normal = accumulated[i];
        if (glm::dot(normal, normal) <= 0.000001f) {
            mesh.vertices[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
            continue;
        }
        mesh.vertices[i].normal = glm::normalize(normal);
    }
}

bool loadMeshFromRenderDocCsv(const std::string& modelPath, MeshData& out, std::string* outError) {
    out = {};
    const std::string resolvedPath = resolveDataPath(modelPath);
    std::ifstream in(resolvedPath);
    if (!in.is_open()) {
        if (outError) {
            *outError = "Unable to open RenderDoc CSV mesh";
        }
        return false;
    }

    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        if (outError) {
            *outError = "RenderDoc CSV mesh is empty";
        }
        return false;
    }

    CsvMeshColumns columns;
    if (!parseCsvMeshColumnsLocal(splitCsvRowLocal(headerLine), columns, outError)) {
        return false;
    }

    std::vector<std::uint32_t> strip;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> tokens = splitCsvRowLocal(line);
        if (columns.px >= static_cast<int>(tokens.size())) continue;

        const std::string pxToken =
            trimCopyLocal(tokens[static_cast<std::size_t>(columns.px)]);
        if (toLowerCopyLocal(pxToken) == "restart") {
            strip.clear();
            continue;
        }

        MeshVertex vertex{};
        if (!parseCsvFloatLocal(tokens, columns.px, 0.0f, vertex.position.x) ||
            !parseCsvFloatLocal(tokens, columns.py, 0.0f, vertex.position.y) ||
            !parseCsvFloatLocal(tokens, columns.pz, 0.0f, vertex.position.z)) {
            if (outError) {
                *outError = "Failed to parse RenderDoc CSV position row";
            }
            return false;
        }
        if (!parseCsvFloatLocal(tokens, columns.tu, 0.0f, vertex.uv.x) ||
            !parseCsvFloatLocal(tokens, columns.tv, 0.0f, vertex.uv.y)) {
            if (outError) {
                *outError = "Failed to parse RenderDoc CSV UV row";
            }
            return false;
        }
        if (!parseCsvFloatLocal(tokens, columns.cr, 1.0f, vertex.color.r) ||
            !parseCsvFloatLocal(tokens, columns.cg, 1.0f, vertex.color.g) ||
            !parseCsvFloatLocal(tokens, columns.cb, 1.0f, vertex.color.b) ||
            !parseCsvFloatLocal(tokens, columns.ca, 1.0f, vertex.color.a)) {
            if (outError) {
                *outError = "Failed to parse RenderDoc CSV color row";
            }
            return false;
        }
        vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.j0 = 0u;
        vertex.j1 = 0u;
        vertex.j2 = 0u;
        vertex.j3 = 0u;
        vertex.w0 = 1.0f;
        vertex.w1 = 0.0f;
        vertex.w2 = 0.0f;
        vertex.w3 = 0.0f;

        const std::uint32_t newIndex = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.push_back(vertex);
        strip.push_back(newIndex);
        if (strip.size() < 3u) {
            continue;
        }

        const std::uint32_t a = strip[strip.size() - 3u];
        const std::uint32_t b = strip[strip.size() - 2u];
        const std::uint32_t c = strip[strip.size() - 1u];
        if (a == b || b == c || a == c) {
            continue;
        }

        const std::size_t triIndex = strip.size() - 3u;
        if ((triIndex % 2u) == 0u) {
            out.indices.push_back(a);
            out.indices.push_back(b);
            out.indices.push_back(c);
        } else {
            out.indices.push_back(b);
            out.indices.push_back(a);
            out.indices.push_back(c);
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) {
            *outError = "RenderDoc CSV mesh produced no triangles";
        }
        return false;
    }

    rebuildCsvMeshNormalsLocal(out);
    return true;
}

bool loadMeshFromGltf(const std::string& modelPath, MeshData& out, std::string* outError) {
    out = {};
    const std::string resolvedPath = resolveDataPath(modelPath);
    auto loaded = engine::render::gltf::loader::tryLoad(resolvedPath);
    if (!loaded.has_value()) {
        if (outError) {
            *outError = "Unable to parse glTF/GLB";
        }
        return false;
    }

    fastgltf::DefaultBufferDataAdapter adapter{};
    const fastgltf::Asset& asset = loaded->asset;
    for (const auto& mesh : asset.meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) continue;

            const auto itPos = primitive.findAttribute("POSITION");
            if (itPos == primitive.attributes.end()) continue;
            int materialIndex = -1;
            if (primitive.materialIndex.has_value()) {
                materialIndex = static_cast<int>(primitive.materialIndex.value());
            }

            std::vector<glm::vec3> positions;
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset,
                asset.accessors[itPos->accessorIndex],
                [&](glm::vec3 value, std::size_t) { positions.push_back(value); },
                adapter);
            if (positions.empty()) continue;

            std::vector<glm::vec2> uvs = buildFallbackUvs(positions);
            int requiredTexCoord =
                engine::render::gltf::model::requiredTexCoordForMaterial(asset, materialIndex);
            std::string uvAttribute = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = primitive.findAttribute(uvAttribute);
            if (itUv == primitive.attributes.end()) {
                requiredTexCoord = 0;
                itUv = primitive.findAttribute("TEXCOORD_0");
            }
            if (itUv != primitive.attributes.end()) {
                std::vector<glm::vec2> authoredUvs;
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset,
                    asset.accessors[itUv->accessorIndex],
                    [&](glm::vec2 value, std::size_t) { authoredUvs.push_back(value); },
                    adapter);
                if (authoredUvs.size() == positions.size()) {
                    uvs = std::move(authoredUvs);
                }
            }

            std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            if (const auto itNormal = primitive.findAttribute("NORMAL");
                itNormal != primitive.attributes.end()) {
                std::vector<glm::vec3> authoredNormals;
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset,
                    asset.accessors[itNormal->accessorIndex],
                    [&](glm::vec3 value, std::size_t) { authoredNormals.push_back(value); },
                    adapter);
                if (authoredNormals.size() == positions.size()) {
                    normals = std::move(authoredNormals);
                }
            }

            std::vector<glm::vec4> tangents(positions.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (const auto itTangent = primitive.findAttribute("TANGENT");
                itTangent != primitive.attributes.end()) {
                std::vector<glm::vec4> authoredTangents;
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset,
                    asset.accessors[itTangent->accessorIndex],
                    [&](glm::vec4 value, std::size_t) { authoredTangents.push_back(value); },
                    adapter);
                if (authoredTangents.size() == positions.size()) {
                    tangents = std::move(authoredTangents);
                }
            }

            std::vector<glm::vec4> colors(positions.size(), glm::vec4(1.0f));
            if (const auto itColor = primitive.findAttribute("COLOR_0");
                itColor != primitive.attributes.end()) {
                const auto& accessor = asset.accessors[itColor->accessorIndex];
                std::vector<glm::vec4> authoredColors;
                authoredColors.reserve(accessor.count);
                if (accessor.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset,
                        accessor,
                        [&](glm::vec3 value, std::size_t) {
                            authoredColors.emplace_back(value, 1.0f);
                        },
                        adapter);
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset,
                        accessor,
                        [&](glm::vec4 value, std::size_t) { authoredColors.push_back(value); },
                        adapter);
                }
                if (authoredColors.size() == positions.size()) {
                    colors = std::move(authoredColors);
                }
            }

            std::vector<glm::u16vec4> joints(positions.size(), glm::u16vec4(0u));
            std::vector<glm::vec4> weights(positions.size(), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            if (const auto itJoints = primitive.findAttribute("JOINTS_0");
                itJoints != primitive.attributes.end()) {
                std::vector<glm::u16vec4> authoredJoints;
                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset,
                    asset.accessors[itJoints->accessorIndex],
                    [&](glm::u16vec4 value, std::size_t) { authoredJoints.push_back(value); },
                    adapter);
                if (authoredJoints.size() == positions.size()) {
                    joints = std::move(authoredJoints);
                }
            }
            if (const auto itWeights = primitive.findAttribute("WEIGHTS_0");
                itWeights != primitive.attributes.end()) {
                std::vector<glm::vec4> authoredWeights;
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset,
                    asset.accessors[itWeights->accessorIndex],
                    [&](glm::vec4 value, std::size_t) { authoredWeights.push_back(value); },
                    adapter);
                if (authoredWeights.size() == positions.size()) {
                    weights = std::move(authoredWeights);
                }
            }

            std::vector<std::uint32_t> indices;
            if (primitive.indicesAccessor.has_value()) {
                const auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
                indices.reserve(accessor.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset,
                    accessor,
                    [&](std::uint32_t value, std::size_t) { indices.push_back(value); },
                    adapter);
            } else {
                indices.resize(positions.size());
                for (std::size_t i = 0; i < positions.size(); ++i) {
                    indices[i] = static_cast<std::uint32_t>(i);
                }
            }
            if (indices.size() < 3u) continue;

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(out.vertices.size());
            out.vertices.reserve(out.vertices.size() + positions.size());
            out.indices.reserve(out.indices.size() + indices.size());
            for (std::size_t i = 0; i < positions.size(); ++i) {
                MeshVertex vertex;
                vertex.position = positions[i];
                vertex.normal = normals[i];
                vertex.tangent = tangents[i];
                vertex.uv = uvs[i];
                vertex.color = colors[i];
                vertex.j0 = joints[i].x;
                vertex.j1 = joints[i].y;
                vertex.j2 = joints[i].z;
                vertex.j3 = joints[i].w;
                vertex.w0 = weights[i].x;
                vertex.w1 = weights[i].y;
                vertex.w2 = weights[i].z;
                vertex.w3 = weights[i].w;
                out.vertices.push_back(vertex);
            }
            for (std::uint32_t index : indices) {
                out.indices.push_back(baseVertex + index);
            }
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) {
            *outError = "No triangle mesh data";
        }
        return false;
    }
    return true;
}

} // namespace

bool detail::loadMeshForPreview(const std::string& modelPath,
                                MeshData& out,
                                std::string* outError) {
    const std::string lowerPath = toLowerCopyLocal(modelPath);
    if (lowerPath.size() >= 4u && lowerPath.substr(lowerPath.size() - 4u) == ".csv") {
        return loadMeshFromRenderDocCsv(modelPath, out, outError);
    }
    return loadMeshFromGltf(modelPath, out, outError);
}
namespace {

TextureCacheEntry makeWhiteTexture() {
    TextureCacheEntry white;
    white.attemptedLoad = true;
    white.valid = true;
    white.width = 1;
    white.height = 1;
    white.rgba = {255u, 255u, 255u, 255u};
    return white;
}

bool loadTextureRgba(const std::string& texturePath,
                     bool flipVertical,
                     TextureCacheEntry& outEntry) {
    outEntry = {};
    outEntry.attemptedLoad = true;
    if (texturePath.empty()) {
        outEntry = makeWhiteTexture();
        return true;
    }

    const std::string resolvedPath = resolveDataPath(texturePath);
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    outEntry.valid = true;
    outEntry.width = width;
    outEntry.height = height;
    outEntry.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return true;
}

} // namespace

SharedAuthoredVfxRenderer::~SharedAuthoredVfxRenderer() = default;

void SharedAuthoredVfxRenderer::setRenderBackend(
    IRenderBackend* backend) {
    externalBackend_ = backend;
}

IRenderBackend* SharedAuthoredVfxRenderer::activeBackend() {
    if (externalBackend_) {
        return externalBackend_;
    }
    if (!fallbackBackend_) {
        fallbackBackend_ =
            engine::render::createLegacyOpenGLBackend();
        fallbackBackend_->onResize(
            surfaceWidth_,
            surfaceHeight_);
    }
    return fallbackBackend_.get();
}

void SharedAuthoredVfxRenderer::onResize(int width, int height) {
    surfaceWidth_ = std::max(1, width);
    surfaceHeight_ = std::max(1, height);
    if (fallbackBackend_) {
        fallbackBackend_->onResize(
            surfaceWidth_,
            surfaceHeight_);
    }
}

void SharedAuthoredVfxRenderer::render(const SharedAuthoredBatchVFX& effect,
                                       const Camera3D& camera,
                                       int surfaceWidth,
                                       int surfaceHeight) {
    IRenderBackend* backend = activeBackend();
    if (!backend) return;
    std::unique_ptr<ScopedGlPreviewRenderState>
        savedGlState;
    if (backend->requiresOpenGLContext()) {
        savedGlState =
            std::make_unique<
                ScopedGlPreviewRenderState>();
    }
    SharedAuthoredBatchVFX::RenderSnapshot snapshot;
    if (!effect.buildRenderSnapshot(snapshot)) return;

    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size() * 4u);

    const auto resolveMesh =
        [&](const std::string& modelPath) -> vfx::runtime::authored_batches::MeshData* {
            return ensureBackendMeshLoaded(modelPath);
        };

    const auto resolveTexture =
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
            const vfx::runtime::authored::TevState& tev,
            vfx::runtime::authored_batches::TextureView& outView) -> bool {
            return fillTextureView(pass, snapshot.config, tev, outView);
        };

    if (!vfx::runtime::authored_bridge::appendBatches(
            snapshot,
            batches,
            camera.getPosition(),
            resolveMesh,
            resolveTexture)) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    const glm::vec3 cameraPos = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    vfx::runtime::authored_submit::submitBatches(
        *backend,
        batches,
        glm::value_ptr(viewProj),
        surfaceWidth,
        surfaceHeight,
        glm::value_ptr(cameraPos),
        glm::value_ptr(cameraForward),
        glm::value_ptr(cameraTarget));
}

vfx::runtime::authored_batches::MeshData* SharedAuthoredVfxRenderer::ensureBackendMeshLoaded(
    const std::string& modelPath) {
    auto& cacheEntry = backendMeshByModelPath_[modelPath];
    if (!cacheEntry.attemptedLoad) {
        cacheEntry.attemptedLoad = true;
        cacheEntry.mesh = {};
        cacheEntry.error.clear();
        if (!detail::loadMeshForPreview(modelPath, cacheEntry.mesh, &cacheEntry.error)) {
            cacheEntry.mesh = {};
        }
    }

    if (!cacheEntry.error.empty()) {
        if (!cacheEntry.reportedFailure) {
            static engine::log::Sink log("VfxLab", &std::cout, &std::cerr);
            log.warn("[VfxLab] Unable to load mesh '" + modelPath
                     + "' (" + cacheEntry.error + ")");
            cacheEntry.reportedFailure = true;
        }
        return nullptr;
    }
    if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) return nullptr;
    return &cacheEntry.mesh;
}

SharedAuthoredVfxRenderer::TextureCacheEntry* SharedAuthoredVfxRenderer::ensureBackendTextureLoaded(
    const std::string& texturePath,
    bool flipVertical) {
    const std::string cacheKey = flipVertical ? texturePath + "#flip" : texturePath;
    auto& cacheEntry = backendTextureByPath_[cacheKey];
    if (!cacheEntry.attemptedLoad) {
        if (!loadTextureRgba(texturePath, flipVertical, cacheEntry)) {
            cacheEntry.attemptedLoad = true;
            cacheEntry.valid = false;
            cacheEntry.width = 0;
            cacheEntry.height = 0;
            cacheEntry.rgba.clear();
        }
    }
    return &cacheEntry;
}

bool SharedAuthoredVfxRenderer::fillTextureViewFromEntry(
    const TextureCacheEntry* texture,
    vfx::runtime::authored_batches::TextureView& outView) {
    if (!texture || !texture->valid || texture->rgba.empty() ||
        texture->width <= 0 || texture->height <= 0) {
        return false;
    }
    outView.rgba = texture->rgba.data();
    outView.width = texture->width;
    outView.height = texture->height;
    return true;
}

bool SharedAuthoredVfxRenderer::fillTextureView(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                                const SharedAuthoredBatchVFX::Config& config,
                                                const vfx::runtime::authored::TevState& tev,
                                                vfx::runtime::authored_batches::TextureView& outView) {
    if (vfx::runtime::authored::isLinePass(config, pass) || pass.texturePath.empty()) {
        return fillTextureViewFromEntry(ensureBackendTextureLoaded("", false), outView);
    }

    TextureCacheEntry* rawTexture = ensureBackendTextureLoaded(pass.texturePath, false);
    if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) return false;

    const bool quarterPass = vfx::runtime::authored::isQuarterRingPass(config, pass);
    const std::string bakedKey = vfx::runtime::authored::makeBakedTextureKey(pass, quarterPass);
    auto& baked = backendTextureByPath_[bakedKey];
    if (!baked.attemptedLoad) {
        baked.attemptedLoad = true;
        baked.valid = false;
        baked.width = rawTexture->width;
        baked.height = rawTexture->height;
        baked.rgba.clear();
        if (!vfx::runtime::authored::bakePassTextureRgba(
                pass,
                tev,
                quarterPass,
                rawTexture->rgba,
                baked.rgba)) {
            return false;
        }
        baked.valid = true;
    }

    return fillTextureViewFromEntry(&baked, outView);
}

} // namespace vfx::preview::authored
