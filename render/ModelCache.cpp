// src/engine/render/ModelCache.cpp

#include "Model.h"
#include "ModelStartupLog.h"
#include "engine/core/Environment.h"
#include "engine/utils/Log.h"
#include "engine/utils/LogSink.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <functional>
#include <cstdint>
#include <cstddef>   // offsetof
#include <limits>
#include <vector>
#include <string>
#include <exception>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

// Model.cpp provides this helper (keep definition there).
extern bool isMipmapMinFilter(GLint minF);

// Fix: use the real animation types (they live in pac_model_types)
using pac_model_types::AnimationClip;
using pac_model_types::AnimationSampler;
using pac_model_types::AnimationChannel;
using pac_model_types::ChannelPath;

namespace pac_model_cache_detail {

namespace fs = std::filesystem;

static engine::log::Sink& modelCacheDebugLog() {
    static engine::log::Sink log("ModelCache", &std::cout, &std::cerr);
    return log;
}

template<typename T>
static bool readPod(std::istream& in, T& v) {
    return (bool)in.read(reinterpret_cast<char*>(&v), sizeof(T));
}
template<typename T>
static bool writePod(std::ostream& out, const T& v) {
    return (bool)out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

static bool readString(std::istream& in, std::string& s) {
    uint32_t n = 0;
    if (!readPod(in, n)) return false;
    s.clear();
    if (n == 0) return true;
    s.resize(n);
    return (bool)in.read(s.data(), n);
}
static bool writeString(std::ostream& out, const std::string& s) {
    uint32_t n = (uint32_t)s.size();
    if (!writePod(out, n)) return false;
    if (n == 0) return true;
    return (bool)out.write(s.data(), n);
}

static std::string hexHash64(uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

// Treat any non-empty value other than "0" as true.
static bool envTruthy(const char* name) {
    return engine::env::truthyNonZero(name);
}

// Cache format constants
static constexpr uint64_t kModelCacheMagic = 0x4C444D434150554FULL; // "PACMDML" in little-endian-ish
static constexpr uint32_t kModelCacheVersion = 8;

#pragma pack(push, 1)
struct CacheHeader {
    uint64_t magic = kModelCacheMagic;
    uint32_t version = kModelCacheVersion;

    int64_t  srcWriteTime = 0;
    uint64_t srcFileSize  = 0;

    float    modelScaleFactor = 1.0f;

    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t submeshCount = 0;

    uint32_t nodeCount    = 0;
    uint32_t skinCount    = 0;
    uint32_t animCount    = 0;
};
#pragma pack(pop)

static uint64_t fnv1a64(const std::string& s) {
    // Stable hash across platforms/compilers for cache filenames (FNV-1a 64-bit).
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

static fs::path cachePathForModel(const std::string& filepath) {
    // cache/models/<hash>.pacmdl
    const uint64_t h = fnv1a64(filepath);
    fs::path dir = fs::path("cache") / "models";
    return dir / (hexHash64(h) + ".pacmdl");
}

} // namespace pac_model_cache_detail

// ------------------------------------------------------------
// Cache I/O (read)
// ------------------------------------------------------------
bool Model::tryLoadCache(const std::string& filepath)
{
    using namespace pac_model_cache_detail;

    // This keeps "test fastgltf" runs deterministic without forcing you to delete cache files.
    if (envTruthy("PAC_DISABLE_MODELCACHE")) return false;

    try {
        const fs::path cpath = cachePathForModel(filepath);
        if (!fs::exists(cpath)) return false;

        // Validate source file metadata
        if (!fs::exists(filepath)) return false;

        CacheHeader hdr{};
        {
            std::ifstream in(cpath, std::ios::binary);
            if (!in.is_open()) return false;
            if (!readPod(in, hdr)) return false;
            if (hdr.magic != kModelCacheMagic) return false;
            if (hdr.version != kModelCacheVersion) return false;

            const auto srcSz = (uint64_t)fs::file_size(filepath);
            const auto srcWt = fs::last_write_time(filepath).time_since_epoch().count();

            if (hdr.srcFileSize != srcSz) return false;
            if (hdr.srcWriteTime != (int64_t)srcWt) return false;

            // Apply cached state
            modelScaleFactor = hdr.modelScaleFactor;

            // Read animation/node/skin state first (so drawAnimated works)
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

            // Nodes
            nodesDefault.resize(hdr.nodeCount);
            nodeNames.resize(hdr.nodeCount);
            nodeChildren.resize(hdr.nodeCount);
            nodeMesh.resize(hdr.nodeCount, -1);
            nodeSkin.resize(hdr.nodeCount, -1);

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                NodeTRS n{};
                if (!readPod(in, n.t)) return false;
                if (!readPod(in, n.r)) return false;
                if (!readPod(in, n.s)) return false;
                uint8_t hm = 0;
                if (!readPod(in, hm)) return false;
                n.hasMatrix = (hm != 0);
                if (!readPod(in, n.matrix)) return false;
                nodesDefault[i] = n;
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                if (!readString(in, nodeNames[i])) return false;
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                uint32_t cc = 0;
                if (!readPod(in, cc)) return false;
                nodeChildren[i].resize(cc);
                for (uint32_t k = 0; k < cc; ++k) {
                    int32_t vv = 0;
                    if (!readPod(in, vv)) return false;
                    nodeChildren[i][k] = (int)vv;
                }
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeMesh[i] = (int)v;
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeSkin[i] = (int)v;
            }

            // scene roots
            {
                uint32_t rc = 0;
                if (!readPod(in, rc)) return false;
                sceneRoots.resize(rc);
                for (uint32_t i = 0; i < rc; ++i) {
                    int32_t v = -1;
                    if (!readPod(in, v)) return false;
                    sceneRoots[i] = (int)v;
                }
            }

            // Skins
            skins.resize(hdr.skinCount);
            for (uint32_t si = 0; si < hdr.skinCount; ++si) {
                uint32_t jc = 0;
                if (!readPod(in, jc)) return false;
                skins[si].joints.resize(jc);
                skins[si].inverseBind.resize(jc);

                for (uint32_t j = 0; j < jc; ++j) {
                    int32_t v = -1;
                    if (!readPod(in, v)) return false;
                    skins[si].joints[j] = (int)v;
                }
                for (uint32_t j = 0; j < jc; ++j) {
                    if (!readPod(in, skins[si].inverseBind[j])) return false;
                }
            }

            // Animations
            animations.resize(hdr.animCount);
            for (uint32_t ai = 0; ai < hdr.animCount; ++ai) {
                AnimationClip clip{};
                if (!readString(in, clip.name)) return false;
                if (!readPod(in, clip.durationSec)) return false;

                uint32_t sc = 0;
                if (!readPod(in, sc)) return false;
                clip.samplers.resize(sc);
                for (uint32_t s = 0; s < sc; ++s) {
                    AnimationSampler samp{};
                    if (!readString(in, samp.interpolation)) return false;
                    uint8_t iv = 0;
                    if (!readPod(in, iv)) return false;
                    samp.isVec4 = (iv != 0);

                    uint32_t ic = 0;
                    if (!readPod(in, ic)) return false;
                    samp.inputs.resize(ic);
                    for (uint32_t k = 0; k < ic; ++k) {
                        if (!readPod(in, samp.inputs[k])) return false;
                    }

                    uint32_t oc = 0;
                    if (!readPod(in, oc)) return false;
                    samp.outputs.resize(oc);
                    for (uint32_t k = 0; k < oc; ++k) {
                        if (!readPod(in, samp.outputs[k])) return false;
                    }

                    clip.samplers[s] = std::move(samp);
                }

                uint32_t cc = 0;
                if (!readPod(in, cc)) return false;
                clip.channels.resize(cc);
                for (uint32_t c = 0; c < cc; ++c) {
                    int32_t si2 = -1;
                    int32_t tn  = -1;
                    uint8_t path = 0;
                    if (!readPod(in, si2)) return false;
                    if (!readPod(in, tn)) return false;
                    if (!readPod(in, path)) return false;

                    AnimationChannel ch{};
                    ch.samplerIndex = (int)si2;
                    ch.targetNode   = (int)tn;
                    ch.path = (path == 1u) ? ChannelPath::Rotation :
                              (path == 2u) ? ChannelPath::Scale :
                                             ChannelPath::Translation;
                    clip.channels[c] = ch;
                }

                animations[ai] = std::move(clip);
            }

            // Geometry
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            vertices.resize(hdr.vertexCount);
            indices.resize(hdr.indexCount);

            if (hdr.vertexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(vertices.data()), vertices.size() * sizeof(Vertex))) return false;
            }
            if (hdr.indexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(indices.data()), indices.size() * sizeof(uint32_t))) return false;
            }

            // ---- Bounds (model space) ----
            if (!vertices.empty()) {
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float minZ = std::numeric_limits<float>::max();
                float maxX = -minX, maxY = -minY, maxZ = -minZ;
                for (const auto& v : vertices) {
                    minX = (std::min)(minX, v.px); minY = (std::min)(minY, v.py); minZ = (std::min)(minZ, v.pz);
                    maxX = (std::max)(maxX, v.px); maxY = (std::max)(maxY, v.py); maxZ = (std::max)(maxZ, v.pz);
                }

                boundsMin = glm::vec3(minX, minY, minZ);
                boundsMax = glm::vec3(maxX, maxY, maxZ);
                boundsValid = true;

                const glm::vec3 ext = boundsMax - boundsMin;
                boundsRadius = 0.5f * glm::length(ext);

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

            // Submeshes + textures (upload textures to GL here)
            submeshes.resize(hdr.submeshCount);
            std::vector<CPUTexture> baseColorCPU;
            std::vector<CPUTexture> emissiveCPU;
            baseColorCPU.resize(hdr.submeshCount);
            emissiveCPU.resize(hdr.submeshCount);

            for (uint32_t i = 0; i < hdr.submeshCount; ++i) {
                uint64_t off = 0, cnt = 0;
                int32_t meshIdx = -1;
                if (!readPod(in, off)) return false;
                if (!readPod(in, cnt)) return false;
                if (!readPod(in, meshIdx)) return false;

                float ef[3] = {0.0f, 0.0f, 0.0f};
                uint8_t alphaMode = 0;
                float alphaCutoff = 0.5f;
                uint8_t doubleSided = 0;
                if (!readPod(in, ef[0])) return false;
                if (!readPod(in, ef[1])) return false;
                if (!readPod(in, ef[2])) return false;
                if (!readPod(in, alphaMode)) return false;
                if (!readPod(in, alphaCutoff)) return false;
                if (!readPod(in, doubleSided)) return false;

                submeshes[i].indexOffset = (size_t)off;
                submeshes[i].indexCount  = (size_t)cnt;
                submeshes[i].meshIndex   = (int)meshIdx;
                submeshes[i].emissiveFactor = glm::vec3(ef[0], ef[1], ef[2]);
                submeshes[i].alphaMode      = (int)alphaMode;
                submeshes[i].alphaCutoff    = alphaCutoff;
                submeshes[i].doubleSided    = (doubleSided != 0);

                auto readCPUTexture = [&](CPUTexture& t)->bool {
                    if (!readPod(in, t.width)) return false;
                    if (!readPod(in, t.height)) return false;
                    if (!readPod(in, t.wrapS)) return false;
                    if (!readPod(in, t.wrapT)) return false;
                    if (!readPod(in, t.minF)) return false;
                    if (!readPod(in, t.magF)) return false;
                    uint32_t bytes = 0;
                    if (!readPod(in, bytes)) return false;
                    t.rgba.resize(bytes);
                    if (bytes > 0) {
                        if (!in.read(reinterpret_cast<char*>(t.rgba.data()), bytes)) return false;
                    }
                    return true;
                };

                CPUTexture base{};
                CPUTexture emissive{};
                if (!readCPUTexture(base)) return false;
                if (!readCPUTexture(emissive)) return false;

                baseColorCPU[i] = std::move(base);
                emissiveCPU[i]  = std::move(emissive);
            }

            
            const bool dbgThisModel = envTruthy("PAC_GLTF_DEBUG") ||
                                      (filepath.find("0019_rattata") != std::string::npos) ||
                                      (filepath.find("0019-Rattata") != std::string::npos) ||
                                      (filepath.find("rattata") != std::string::npos) ||
                                      (filepath.find("Rattata") != std::string::npos);

            if (dbgThisModel) {
                modelCacheDebugLog().warn("[gltf][CACHE][DEBUG] Cache read complete for: " + filepath);
                modelCacheDebugLog().warn(
                    "[gltf][CACHE][DEBUG] submeshes=" + std::to_string(hdr.submeshCount) +
                    " vertices=" + std::to_string(hdr.vertexCount) +
                    " indices=" + std::to_string(hdr.indexCount));
                for (uint32_t i = 0; i < hdr.submeshCount; ++i) {
                    const auto& b = baseColorCPU[i];
                    const auto& e = emissiveCPU[i];
                    modelCacheDebugLog().warn(
                        "[gltf][CACHE][TEX] submesh[" + std::to_string(i) + "] base " +
                        std::to_string(b.width) + "x" + std::to_string(b.height) +
                        " bytes=" + std::to_string(b.rgba.size()) +
                        " wrapS=" + std::to_string(b.wrapS) +
                        " wrapT=" + std::to_string(b.wrapT) +
                        " minF=" + std::to_string(b.minF) +
                        " magF=" + std::to_string(b.magF));
                    if (b.rgba.empty()) {
                        modelCacheDebugLog().warn(
                            "[gltf][CACHE][WARN] submesh[" + std::to_string(i) +
                            "] baseColor RGBA blob is EMPTY. Expect undefined texture contents.");
                    }
                    modelCacheDebugLog().warn(
                        "[gltf][CACHE][TEX] submesh[" + std::to_string(i) + "] emissive " +
                        std::to_string(e.width) + "x" + std::to_string(e.height) +
                        " bytes=" + std::to_string(e.rgba.size()));
                    if (e.rgba.empty()) {
                        modelCacheDebugLog().warn(
                            "[gltf][CACHE][WARN] submesh[" + std::to_string(i) +
                            "] emissive RGBA blob is EMPTY.");
                    }
                }
                LOG_ERROR("[gltf][CACHE][DEBUG] If you suspect stale/corrupt cache, run with PAC_DISABLE_MODELCACHE=1 once.\n");
            }

// Upload VBO/EBO/VAO exactly like the slow path
            glGenVertexArrays(1, &VAO);
            glGenBuffers(1, &VBO);
            glGenBuffers(1, &EBO);

            glBindVertexArray(VAO);

            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

            // attributes: pos (0), uv (1), joints (2), weights (3)
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

            glEnableVertexAttribArray(2);
            glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));

            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));

            // COLOR_0 (vec4) must exist in cache path too
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

            glBindVertexArray(0);

            // Upload textures
            auto uploadCPUTexture = [&](const CPUTexture& t)->GLuint {
                const int w = (int)(t.width  == 0 ? 1u : t.width);
                const int h = (int)(t.height == 0 ? 1u : t.height);
                const void* pixels = t.rgba.empty() ? nullptr : t.rgba.data();

                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)t.wrapS);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t.wrapT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)t.minF);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)t.magF);

                if (isMipmapMinFilter((GLint)t.minF)) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                return tex;
            };

            for (size_t i = 0; i < submeshes.size(); ++i) {
                submeshes[i].baseColorTexID = uploadCPUTexture(baseColorCPU[i]);
                submeshes[i].emissiveTexID  = uploadCPUTexture(emissiveCPU[i]);
            }

            glBindTexture(GL_TEXTURE_2D, 0);

            STARTUP_LOG(std::string("[Model] Cache hit: ") + filepath + " -> " + cpath.string());
            return true;
        }
    } catch (const std::exception& e) {
        STARTUP_WARN(std::string("[Model][Cache] Exception while reading cache for: ")
                     + filepath + " (" + e.what() + ")");
        return false;
    } catch (...) {
        STARTUP_WARN(std::string("[Model][Cache] Unknown exception while reading cache for: ")
                     + filepath);
        return false;
    }
}


// ------------------------------------------------------------
// Cache I/O (write)
// ------------------------------------------------------------
void Model::writeCache(const std::string& filepath,
                       const std::vector<Vertex>& vertices,
                       const std::vector<uint32_t>& indices,
                       const std::vector<CPUTexture>& baseColorTexturesCPU,
                       const std::vector<CPUTexture>& emissiveTexturesCPU) const
{
    using namespace pac_model_cache_detail;

    auto warn = [&](const char* why) {
        STARTUP_WARN(std::string("[Model][Cache] Write skipped/failed: ")
                     + why + " (src=" + filepath + ")");
    };

    try {
        if (!fs::exists(filepath)) { warn("source file does not exist"); return; }

        fs::path cpath = cachePathForModel(filepath);

        std::error_code ec;
        fs::create_directories(cpath.parent_path(), ec);
        if (ec) { warn("create_directories failed"); return; }

        CacheHeader hdr{};
        hdr.srcFileSize = (uint64_t)fs::file_size(filepath, ec);
        if (ec) { warn("file_size failed"); return; }
        hdr.srcWriteTime = (int64_t)fs::last_write_time(filepath, ec).time_since_epoch().count();
        if (ec) { warn("last_write_time failed"); return; }

        hdr.modelScaleFactor = modelScaleFactor;

        hdr.vertexCount  = (uint32_t)vertices.size();
        hdr.indexCount   = (uint32_t)indices.size();
        hdr.submeshCount = (uint32_t)submeshes.size();

        hdr.nodeCount = (uint32_t)nodesDefault.size();
        hdr.skinCount = (uint32_t)skins.size();
        hdr.animCount = (uint32_t)animations.size();

        // Basic sanity: require matching texture entries
        if (baseColorTexturesCPU.size() != submeshes.size()) { warn("baseColorTexturesCPU size mismatch"); return; }
        if (emissiveTexturesCPU.size() != submeshes.size()) { warn("emissiveTexturesCPU size mismatch"); return; }

        std::ofstream out(cpath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) { warn("failed to open cache file for write"); return; }


        if (!writePod(out, hdr)) { warn("writePod failed"); return; }

        // Nodes
        for (const auto& n : nodesDefault) {
            if (!writePod(out, n.t)) { warn("writePod failed"); return; }
            if (!writePod(out, n.r)) { warn("writePod failed"); return; }
            if (!writePod(out, n.s)) { warn("writePod failed"); return; }
            uint8_t hm = n.hasMatrix ? 1u : 0u;
            if (!writePod(out, hm)) { warn("writePod failed"); return; }
            if (!writePod(out, n.matrix)) { warn("writePod failed"); return; }
        }

        for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
            const std::string& nodeName =
                (i < nodeNames.size()) ? nodeNames[static_cast<std::size_t>(i)] : std::string{};
            if (!writeString(out, nodeName)) { warn("writeString failed"); return; }
        }

        for (const auto& ch : nodeChildren) {
            uint32_t cc = (uint32_t)ch.size();
            if (!writePod(out, cc)) { warn("writePod failed"); return; }
            for (int v : ch) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) { warn("writePod failed"); return; }
            }
        }

        for (int v : nodeMesh) {
            int32_t vv = (int32_t)v;
            if (!writePod(out, vv)) { warn("writePod failed"); return; }
        }
        for (int v : nodeSkin) {
            int32_t vv = (int32_t)v;
            if (!writePod(out, vv)) { warn("writePod failed"); return; }
        }

        // scene roots
        {
            uint32_t rc = (uint32_t)sceneRoots.size();
            if (!writePod(out, rc)) { warn("writePod failed"); return; }
            for (int v : sceneRoots) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) { warn("writePod failed"); return; }
            }
        }

        // Skins
        for (const auto& s : skins) {
            uint32_t jc = (uint32_t)s.joints.size();
            if (!writePod(out, jc)) { warn("writePod failed"); return; }
            for (int v : s.joints) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) { warn("writePod failed"); return; }
            }
            for (const auto& m : s.inverseBind) {
                if (!writePod(out, m)) { warn("writePod failed"); return; }
            }
        }

        // Animations
        for (const auto& a : animations) {
            if (!writeString(out, a.name)) { warn("writeString failed"); return; }
            if (!writePod(out, a.durationSec)) { warn("writePod failed"); return; }

            uint32_t sc = (uint32_t)a.samplers.size();
            if (!writePod(out, sc)) { warn("writePod failed"); return; }
            for (const auto& s : a.samplers) {
                if (!writeString(out, s.interpolation)) { warn("writeString failed"); return; }
                uint8_t iv = s.isVec4 ? 1u : 0u;
                if (!writePod(out, iv)) { warn("writePod failed"); return; }

                uint32_t ic = (uint32_t)s.inputs.size();
                if (!writePod(out, ic)) { warn("writePod failed"); return; }
                for (float v : s.inputs) {
                    if (!writePod(out, v)) { warn("writePod failed"); return; }
                }

                uint32_t oc = (uint32_t)s.outputs.size();
                if (!writePod(out, oc)) { warn("writePod failed"); return; }
                for (const auto& v : s.outputs) {
                    if (!writePod(out, v)) { warn("writePod failed"); return; }
                }
            }

            uint32_t cc = (uint32_t)a.channels.size();
            if (!writePod(out, cc)) { warn("writePod failed"); return; }
            for (const auto& ch : a.channels) {
                int32_t si = (int32_t)ch.samplerIndex;
                int32_t tn = (int32_t)ch.targetNode;
                uint8_t path = (ch.path == ChannelPath::Rotation) ? 1u : (ch.path == ChannelPath::Scale) ? 2u : 0u;
                if (!writePod(out, si)) { warn("writePod failed"); return; }
                if (!writePod(out, tn)) { warn("writePod failed"); return; }
                if (!writePod(out, path)) { warn("writePod failed"); return; }
            }
        }

        // Geometry
        if (!vertices.empty()) {
            if (!out.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(Vertex))) return;
        }
        if (!indices.empty()) {
            if (!out.write(reinterpret_cast<const char*>(indices.data()), indices.size() * sizeof(uint32_t))) return;
        }

        // Submeshes + textures + minimal material params
        for (size_t i = 0; i < submeshes.size(); ++i) {
            const auto& sm = submeshes[i];
            uint64_t off = (uint64_t)sm.indexOffset;
            uint64_t cnt = (uint64_t)sm.indexCount;
            int32_t meshIdx = (int32_t)sm.meshIndex;
            if (!writePod(out, off)) { warn("writePod failed"); return; }
            if (!writePod(out, cnt)) { warn("writePod failed"); return; }
            if (!writePod(out, meshIdx)) { warn("writePod failed"); return; }

            // material params
            if (!writePod(out, sm.emissiveFactor.x)) { warn("writePod failed"); return; }
            if (!writePod(out, sm.emissiveFactor.y)) { warn("writePod failed"); return; }
            if (!writePod(out, sm.emissiveFactor.z)) { warn("writePod failed"); return; }
            uint8_t alphaMode = (uint8_t)sm.alphaMode;
            uint8_t doubleSided = sm.doubleSided ? 1u : 0u;
            if (!writePod(out, alphaMode)) { warn("writePod failed"); return; }
            if (!writePod(out, sm.alphaCutoff)) { warn("writePod failed"); return; }
            if (!writePod(out, doubleSided)) { warn("writePod failed"); return; }

            auto writeCPUTexture = [&](const CPUTexture& t)->bool {
                if (!writePod(out, t.width)) return false;
                if (!writePod(out, t.height)) return false;
                if (!writePod(out, t.wrapS)) return false;
                if (!writePod(out, t.wrapT)) return false;
                if (!writePod(out, t.minF)) return false;
                if (!writePod(out, t.magF)) return false;
                uint32_t bytes = (uint32_t)t.rgba.size();
                if (!writePod(out, bytes)) return false;
                if (bytes > 0) {
                    if (!out.write(reinterpret_cast<const char*>(t.rgba.data()), bytes)) return false;
                }
                return true;
            };

            if (!writeCPUTexture(baseColorTexturesCPU[i])) return;
            if (!writeCPUTexture(emissiveTexturesCPU[i])) return;
        }

                STARTUP_LOG(std::string("[Model] Cache wrote: ") + filepath + " -> " + cpath.string());
    } catch (const std::exception& e) {
        STARTUP_WARN(std::string("[Model][Cache] Exception while writing cache for: ")
                     + filepath + " (" + e.what() + ")");
    } catch (...) {
        STARTUP_WARN(std::string("[Model][Cache] Unknown exception while writing cache for: ")
                     + filepath);
    }
}
