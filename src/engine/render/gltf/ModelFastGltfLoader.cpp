// src/engine/render/gltf/ModelFastGltfLoader.cpp
// Extracted from ModelFastGltfLoad.inl to keep Model.cpp small and testable.
#include "engine/render/Model.h"
#include "engine/render/ModelStartupLog.h"
#include "FastGLTFLoader.h"
#include "ModelFastGltfLoaderHelpers.h"
#include "ModelFastGltfMaterial.h"
#include "ModelFastGltfSceneData.h"
#include "ModelFastGltfTextures.h"
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <nlohmann/json.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
void Model::loadGLTFFast(const std::string& filepath) {
// ------------------------------------------------------------
// FastGLTF load (full path)
// ------------------------------------------------------------
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        std::cerr << "[gltf][FASTGLTF] FAILED to parse: " << filepath << "\n";
        return;
    }
    const fastgltf::Asset& asset = fg->asset;
    const bool dbgThisModel = pac::model_fastgltf::envTruthy("PAC_GLTF_DEBUG") || pac::model_fastgltf::ciContains(filepath, "0019_rattata") || pac::model_fastgltf::ciContains(filepath, "rattata");
    if (dbgThisModel) {
        std::cerr << "[gltf][DEBUG] Extra logging ENABLED for: " << filepath << "\n";
        std::cerr << "[gltf][DEBUG] Env toggles: PAC_GLTF_DUMP_TEXTURES=1 will write debug PNGs; PAC_GLTF_RESPECT_TEXCOORD=1 will respect material texCoord indices.\n";
    }
    // Reset model state
    nodesDefault.clear();
    nodeNames.clear();
    nodeChildren.clear();
    nodeMesh.clear();
    nodeSkin.clear();
    sceneRoots.clear();
    skins.clear();
    animations.clear();
    submeshes.clear();
    nodeNameMapBuilt = false;
    nodeNameToIndex.clear();
    fastgltf::DefaultBufferDataAdapter adapter{};
    pac::model_fastgltf::buildSceneData(asset,
                                        adapter,
                                        nodesDefault,
                                        &nodeNames,
                                        nodeChildren,
                                        nodeMesh,
                                        nodeSkin,
                                        sceneRoots,
                                        skins,
                                        animations);
    std::cerr << "[gltf] fastgltf animations=" << animations.size()
              << " skins=" << skins.size()
              << " nodes=" << nodesDefault.size() << "\n";
    // ---- Meshes + textures ----
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(20000);
    indices.reserve(60000);
    std::vector<pac::model_fastgltf::CPUTexture> baseColorTexturesCPU;
    std::vector<pac::model_fastgltf::CPUTexture> emissiveTexturesCPU;
    baseColorTexturesCPU.reserve(64);
    emissiveTexturesCPU.reserve(64);
    float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max(), minZ = std::numeric_limits<float>::max();
    float maxX = -minX, maxY = -minY, maxZ = -minZ;
    for (size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        const auto& mesh = asset.meshes[meshIdx];
        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            const auto& p = mesh.primitives[primIdx];
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;
            int materialIndex = -1;
            if (p.materialIndex.has_value()) {
                materialIndex = static_cast<int>(p.materialIndex.value());
            }
            auto itPos = p.findAttribute("POSITION");
            if (itPos == p.attributes.end()) {
                std::cerr << "[fastgltf] Missing POSITION in primitive\n";
                continue;
            }
            // Determine which UV set this primitive *wants* based on the material texCoord indices.
            // Compatibility-safe behavior:
            // - If material wants TEXCOORD_0 -> use it (same as before)
            // - If material wants TEXCOORD_n -> try it, and fallback to TEXCOORD_0 if missing
            // - PAC_GLTF_RESPECT_TEXCOORD can still force logging/diagnostics semantics, but isn't required anymore.
            int requiredTexCoord = pac::model_fastgltf::requiredTexCoordForMaterial(asset, materialIndex);

            std::string uvAttr = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = p.findAttribute(uvAttr);

            // Fallback to TEXCOORD_0 if missing.
            if (itUv == p.attributes.end()) {
                if (dbgThisModel && requiredTexCoord != 0) {
                    std::cerr << "[gltf][WARN] Primitive material wants " << uvAttr
                              << " but it's missing; falling back to TEXCOORD_0.\n";
                }
                requiredTexCoord = 0;
                itUv = p.findAttribute("TEXCOORD_0");
            }

            const bool hasUv = (itUv != p.attributes.end());
            if (!hasUv) {
                std::cerr << "[fastgltf] Missing TEXCOORD_0 in primitive; generating planar UVs from POSITION.\n";
            }

            const size_t posAcc = itPos->accessorIndex;
            const size_t uvAcc  = hasUv ? itUv->accessorIndex : 0;

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            std::vector<glm::vec3> normals;
            pos.reserve(asset.accessors[posAcc].count);
            uv.reserve(hasUv ? asset.accessors[uvAcc].count : asset.accessors[posAcc].count);
            normals.reserve(asset.accessors[posAcc].count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, asset.accessors[posAcc],
                [&](glm::vec3 v, size_t) { pos.push_back(v); },
                adapter
            );

            if (hasUv) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset, asset.accessors[uvAcc],
                    [&](glm::vec2 v, size_t) { uv.push_back(v); },
                    adapter
                );
            } else {
                // Fallback UVs from projected position when TEXCOORD is missing.
                // Pick the two widest axes to avoid collapse on flat dimensions.
                glm::vec3 pMin( std::numeric_limits<float>::max());
                glm::vec3 pMax(-std::numeric_limits<float>::max());
                for (const auto& p3 : pos) {
                    pMin.x = (std::min)(pMin.x, p3.x); pMin.y = (std::min)(pMin.y, p3.y); pMin.z = (std::min)(pMin.z, p3.z);
                    pMax.x = (std::max)(pMax.x, p3.x); pMax.y = (std::max)(pMax.y, p3.y); pMax.z = (std::max)(pMax.z, p3.z);
                }

                const float r[3] = { pMax.x - pMin.x, pMax.y - pMin.y, pMax.z - pMin.z };
                int a = 0;
                if (r[1] > r[a]) a = 1;
                if (r[2] > r[a]) a = 2;

                int b = (a == 0) ? 1 : 0;
                for (int i = 0; i < 3; ++i) {
                    if (i == a) continue;
                    if (r[i] > r[b]) b = i;
                }

                const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
                const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
                const float denA = std::max(1e-6f, (a == 0) ? r[0] : ((a == 1) ? r[1] : r[2]));
                const float denB = std::max(1e-6f, (b == 0) ? r[0] : ((b == 1) ? r[1] : r[2]));

                uv.reserve(pos.size());
                for (const auto& p3 : pos) {
                    const float ca = (a == 0) ? p3.x : ((a == 1) ? p3.y : p3.z);
                    const float cb = (b == 0) ? p3.x : ((b == 1) ? p3.y : p3.z);
                    uv.emplace_back((ca - minA) / denA, (cb - minB) / denB);
                }
            }

            // ---- NORMAL ----
            bool hasExplicitNormals = false;
            normals.assign(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            auto itN = p.findAttribute("NORMAL");
            if (itN != p.attributes.end()) {
                normals.clear();
                const std::size_t normAcc = itN->accessorIndex;
                normals.reserve(asset.accessors[normAcc].count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset, asset.accessors[normAcc],
                    [&](glm::vec3 v, size_t) { normals.push_back(v); },
                    adapter
                );
                if (normals.size() != pos.size()) {
                    normals.assign(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
                } else {
                    hasExplicitNormals = true;
                }
            }

            // ---- TANGENT ----
            bool hasExplicitTangents = false;
            std::vector<glm::vec4> tangents(pos.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            auto itT = p.findAttribute("TANGENT");
            if (itT != p.attributes.end()) {
                tangents.clear();
                const std::size_t tanAcc = itT->accessorIndex;
                tangents.reserve(asset.accessors[tanAcc].count);
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, asset.accessors[tanAcc],
                    [&](glm::vec4 v, size_t) {
                        glm::vec3 xyz = glm::vec3(v);
                        const float lenSq = glm::dot(xyz, xyz);
                        if (lenSq > 1e-12f) {
                            xyz = glm::normalize(xyz);
                        } else {
                            xyz = glm::vec3(0.0f);
                        }
                        tangents.emplace_back(xyz, (v.w < 0.0f) ? -1.0f : 1.0f);
                    },
                    adapter
                );
                if (tangents.size() == pos.size()) {
                    hasExplicitTangents = true;
                } else {
                    tangents.assign(pos.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                }
            }

            // ---- COLOR_0 (vertex color) ----
            std::vector<glm::vec4> color;
            color.resize(pos.size(), glm::vec4(1.0f)); // default = white

            auto itC = p.findAttribute("COLOR_0");
            if (itC != p.attributes.end()) {
                const size_t colAcc = itC->accessorIndex;
                const auto& acc = asset.accessors[colAcc];

                color.clear();
                color.reserve(acc.count);

                if (acc.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, acc,
                        [&](glm::vec3 v, size_t) { color.emplace_back(v.x, v.y, v.z, 1.0f); },
                        adapter
                    );
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, acc,
                        [&](glm::vec4 v, size_t) { color.push_back(v); },
                        adapter
                    );
                }

                // Safety: if counts mismatch, ignore the attribute
                if (color.size() != pos.size()) {
                    color.assign(pos.size(), glm::vec4(1.0f));
                }
            }

            // --- Apply KHR_texture_transform (bake into UVs) if present ---
            auto applyKHRTextureTransform = [&](const fastgltf::TextureInfo* ti) {
                if (!ti) return;
                if (!ti->transform) return;

                const auto& tr = *ti->transform;

                // fastgltf uses uvOffset/uvScale for KHR_texture_transform
                glm::vec2 offset(tr.uvOffset[0], tr.uvOffset[1]);
                glm::vec2 scale (tr.uvScale[0],  tr.uvScale[1]);
                float rot = (float)tr.rotation;

                float c = std::cos(rot);
                float s = std::sin(rot);

                for (auto& t : uv) {
                    glm::vec2 p = t;
                    p *= scale;
                    p = glm::vec2(c * p.x - s * p.y,
                                  s * p.x + c * p.y);
                    p += offset;
                    t = p;
                }

                if (dbgThisModel) {
                    std::cerr << "[gltf][XFORM] Applied KHR_texture_transform: "
                              << "offset=(" << offset.x << "," << offset.y << ") "
                              << "scale=(" << scale.x << "," << scale.y << ") "
                              << "rot=" << rot << "\n";
                }
            };

            // Only bake transform for the baseColorTexture that matches the UV set we're using
            const fastgltf::TextureInfo* baseTI = nullptr;
            if (materialIndex >= 0 && materialIndex < (int)asset.materials.size()) {
                const auto& mat = asset.materials[(size_t)materialIndex];
                if (mat.pbrData.baseColorTexture.has_value()) {
                    const auto& ti = mat.pbrData.baseColorTexture.value();
                    if ((int)ti.texCoordIndex == requiredTexCoord) {
                        baseTI = &ti;
                    }
                }
            }
            applyKHRTextureTransform(baseTI);

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                std::cerr << "[fastgltf] Invalid POSITION/TEXCOORD sizes\n";
                continue;
            }

            if (dbgThisModel) {
                glm::vec2 uvMin( 1e9f), uvMax(-1e9f);
                for (const auto& t : uv) {
                    uvMin.x = (std::min)(uvMin.x, t.x); uvMin.y = (std::min)(uvMin.y, t.y);
                    uvMax.x = (std::max)(uvMax.x, t.x); uvMax.y = (std::max)(uvMax.y, t.y);
                }
                std::cerr << "[gltf][UV] mat=" << materialIndex
                          << " texCoord=" << requiredTexCoord
                          << " uvMin=(" << uvMin.x << "," << uvMin.y << ")"
                          << " uvMax=(" << uvMax.x << "," << uvMax.y << ")"
                          << " vertCount=" << uv.size() << "\n";
            }

            std::vector<glm::u16vec4> joints;
            std::vector<glm::vec4> weights;
            auto itJ = p.findAttribute("JOINTS_0");
            auto itW = p.findAttribute("WEIGHTS_0");
            if (itJ != p.attributes.end() && itW != p.attributes.end()) {
                joints.reserve(asset.accessors[itJ->accessorIndex].count);
                weights.reserve(asset.accessors[itW->accessorIndex].count);

                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset, asset.accessors[itJ->accessorIndex],
                    [&](glm::u16vec4 v, size_t) { joints.push_back(v); },
                    adapter
                );

                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, asset.accessors[itW->accessorIndex],
                    [&](glm::vec4 v, size_t) { weights.push_back(v); },
                    adapter
                );

                if (joints.size() != pos.size() || weights.size() != pos.size()) {
                    joints.clear();
                    weights.clear();
                }
            }

            std::vector<uint32_t> primIdxU32;
            if (p.indicesAccessor.has_value()) {
                const auto& idxAcc = asset.accessors[p.indicesAccessor.value()];
                primIdxU32.reserve(idxAcc.count);

                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, idxAcc,
                    [&](std::uint32_t v, size_t) { primIdxU32.push_back(v); },
                    adapter
                );
            }

            if (primIdxU32.empty()) {
                primIdxU32.resize(pos.size());
                for (size_t i = 0; i < pos.size(); ++i) primIdxU32[i] = (uint32_t)i;
            }

            if (!hasExplicitNormals) {
                pac::model_fastgltf::computeNormalsFromGeometry(pos, primIdxU32, normals);
            }

            if (!hasExplicitTangents) {
                pac::model_fastgltf::computeTangentsFromGeometry(pos, uv, normals, primIdxU32, tangents);
                // Mark generated tangents as non-authored so world shaders can
                // select derivative-tangent normal mapping for viewer parity.
                for (glm::vec4& t : tangents) t.w = 0.0f;
            } else {
                bool needsFallback = false;
                for (const glm::vec4& t : tangents) {
                    if (glm::dot(glm::vec3(t), glm::vec3(t)) <= 1e-10f) {
                        needsFallback = true;
                        break;
                    }
                }
                if (needsFallback) {
                    std::vector<glm::vec4> fallbackTangents;
                    pac::model_fastgltf::computeTangentsFromGeometry(pos, uv, normals, primIdxU32, fallbackTangents);
                    for (std::size_t vi = 0; vi < tangents.size(); ++vi) {
                        if (glm::dot(glm::vec3(tangents[vi]), glm::vec3(tangents[vi])) <= 1e-10f) {
                            tangents[vi] = fallbackTangents[vi];
                        }
                    }
                }
            }

            const size_t baseVertex = vertices.size();
            const size_t subIndexOffset = indices.size();

            for (size_t i = 0; i < pos.size(); ++i) {
                Vertex v{};
                v.px = pos[i].x; v.py = pos[i].y; v.pz = pos[i].z;
                v.u  = uv[i].x;  v.v  = uv[i].y;
                v.nx = normals[i].x; v.ny = normals[i].y; v.nz = normals[i].z;
                v.tx = tangents[i].x; v.ty = tangents[i].y; v.tz = tangents[i].z; v.tw = tangents[i].w;

                v.j0 = v.j1 = v.j2 = v.j3 = 0;
                v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;

                // Vertex color (linear)
                v.r = color[i].r;
                v.g = color[i].g;
                v.b = color[i].b;
                v.a = color[i].a;

                if (!joints.empty() && !weights.empty()) {
                    auto j = joints[i];
                    auto w = weights[i];

                    float sum = w.x + w.y + w.z + w.w;
                    if (sum <= 0.0001f) w = glm::vec4(1,0,0,0);
                    else w /= sum;

                    v.j0 = j.x; v.j1 = j.y; v.j2 = j.z; v.j3 = j.w;
                    v.w0 = w.x; v.w1 = w.y; v.w2 = w.z; v.w3 = w.w;
                }

                vertices.push_back(v);

                minX = (std::min)(minX, v.px); minY = (std::min)(minY, v.py); minZ = (std::min)(minZ, v.pz);
                maxX = (std::max)(maxX, v.px); maxY = (std::max)(maxY, v.py); maxZ = (std::max)(maxZ, v.pz);
            }

            for (auto idx : primIdxU32) {
                indices.push_back((uint32_t)(baseVertex + idx));
            }

            // --- material decode (minimal glTF) ---
            int baseTexCoordUsed = 0;
            int emissiveTexCoordUsed = 0;
            pac::model_fastgltf::CPUTexture baseCPU = pac::model_fastgltf::decodeBaseColorTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &baseTexCoordUsed);
            pac::model_fastgltf::CPUTexture emissiveCPU = pac::model_fastgltf::decodeEmissiveTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &emissiveTexCoordUsed);
            if (dbgThisModel && (baseTexCoordUsed != requiredTexCoord || emissiveTexCoordUsed != requiredTexCoord)) {
                std::cerr << "[gltf][INFO] Material texCoord(base=" << baseTexCoordUsed
                        << ", emissive=" << emissiveTexCoordUsed
                        << "), meshUV=" << requiredTexCoord << "\n";
            }

            const pac::model_fastgltf::MaterialRenderInfo materialInfo =
                pac::model_fastgltf::resolveMaterialRenderInfo(asset, materialIndex, baseCPU, dbgThisModel);

            const GLuint baseTexId = pac::model_fastgltf::uploadTexture2D(baseCPU, dbgThisModel, "baseTex");
            const GLuint emissiveTexId = pac::model_fastgltf::uploadTexture2D(emissiveCPU, dbgThisModel, "emissiveTex");

            Submesh sm;
            sm.indexOffset = subIndexOffset;
            sm.indexCount  = primIdxU32.size();
            sm.baseColorTexID = baseTexId;
            sm.emissiveTexID  = emissiveTexId;
            sm.emissiveFactor = materialInfo.emissiveFactor;
            sm.alphaMode      = materialInfo.alphaMode;
            sm.alphaCutoff    = materialInfo.alphaCutoff;
            sm.doubleSided    = materialInfo.doubleSided;
            sm.meshIndex   = (int)meshIdx;
            submeshes.push_back(sm);

            baseColorTexturesCPU.push_back(std::move(baseCPU));
            emissiveTexturesCPU.push_back(std::move(emissiveCPU));
        }
    }

    // ---- Bounds (model space) ----
    if (!vertices.empty()) {
        boundsMin = glm::vec3(minX, minY, minZ);
        boundsMax = glm::vec3(maxX, maxY, maxZ);
        boundsValid = true;

        const glm::vec3 ext = boundsMax - boundsMin;
        boundsRadius = 0.5f * glm::length(ext);

        // Pick the largest extent as "up", compute radius in the other two axes.
        int upAxis = 0;
        if (ext.y >= ext.x && ext.y >= ext.z) upAxis = 1;
        else if (ext.z >= ext.x && ext.z >= ext.y) upAxis = 2;

        float ex = ext.x, ey = ext.y, ez = ext.z;
        if (upAxis == 0) boundsRadiusHorizontal = 0.5f * std::sqrt(ey * ey + ez * ez);
        else if (upAxis == 1) boundsRadiusHorizontal = 0.5f * std::sqrt(ex * ex + ez * ez);
        else boundsRadiusHorizontal = 0.5f * std::sqrt(ex * ex + ey * ey);
    } else {
        boundsMin = boundsMax = glm::vec3(0.0f);
        boundsRadius = 0.0f;
        boundsRadiusHorizontal = 0.0f;
        boundsValid = false;
    }

    // ---- Scale factor ----
    float desiredHeight = 0.8f;
    const float ex = std::max(0.0f, maxX - minX);
    const float ey = std::max(0.0f, maxY - minY);
    const float ez = std::max(0.0f, maxZ - minZ);
    float denom = std::max(ex, std::max(ey, ez));
    if (std::abs(denom) < 1e-6f) denom = 1.0f;
    modelScaleFactor = desiredHeight / denom;

    STARTUP_LOG(
        std::string("[Model] Loaded (fastgltf): ") + filepath +
        " vertices=" + std::to_string(vertices.size()) +
        " indices=" + std::to_string(indices.size()) +
        " submeshes=" + std::to_string(submeshes.size())
    );

    // ---- Upload geometry ----
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(1);

    glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));
    glEnableVertexAttribArray(3);

    // COLOR_0 (vec4)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    // ---- Cache write ----
    writeCache(filepath, vertices, indices, baseColorTexturesCPU, emissiveTexturesCPU);
    std::cerr << "[gltf][FASTGLTF] COMPLETE for: " << filepath << "\n";
}
