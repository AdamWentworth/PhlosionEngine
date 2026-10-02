#include "engine/vfx/runtime/shared/SharedAuthoredVfxBatches.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vfx::runtime::authored_batches {

namespace render_model = vfx::runtime::authored_batches;
namespace shared_world_batches = vfx::runtime::authored_batches;

namespace {

struct SharedMeshGeometry {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct SparkleSpriteDescriptor {
    glm::vec3 center{0.0f};
    float width = 1.0f;
    float height = 1.0f;
};

struct CornerBillboardQuadDescriptor {
    glm::vec3 center{0.0f};
    std::array<glm::vec3, 4> corners{
        glm::vec3(0.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f)};
    std::size_t cornerCount = 0u;
    glm::vec3 axisU{1.0f, 0.0f, 0.0f};
    glm::vec3 axisV{0.0f, 0.0f, 1.0f};
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    float width = 1.0f;
    float height = 1.0f;
    float spriteSize = 1.0f;
};

float hash01Local(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    const std::uint32_t v = x >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
}

float computeBillboardSpinRadLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass, float age01) {
    const float clampedAge01 = glm::clamp(age01, 0.0f, 1.0f);
    return glm::radians(pass.billboardSpinStartDeg + 360.0f * pass.billboardSpinTurns * clampedAge01);
}

float computeBillboardSpinJitterRadLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                         std::uint32_t randomSeed,
                                         std::uint32_t instanceSalt) {
    if (pass.directionSpacingJitterDeg <= 0.0001f) return 0.0f;
    const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
    const float noise = hash01Local(randomSeed ^ passSalt ^ instanceSalt ^ 0x2c1b3c6du);
    return glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
}

glm::quat rotationFromToSafeLocal(const glm::vec3 &from, const glm::vec3 &to) {
    glm::vec3 a = from;
    glm::vec3 b = to;
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la <= 0.0001f || lb <= 0.0001f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a /= la;
    b /= lb;
    const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f) {
        glm::vec3 ortho = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(ortho, ortho) <= 0.0001f) ortho = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
        ortho = glm::normalize(ortho);
        return glm::angleAxis(3.14159265f, ortho);
    }
    const glm::vec3 axis = glm::normalize(glm::cross(a, b));
    return glm::angleAxis(std::acos(d), axis);
}

glm::vec3 safeNormalize3Local(const glm::vec3 &v, const glm::vec3 &fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-9f) return glm::normalize(v);
    return fallback;
}

float fastLaunch01Local(float t) {
    const float clamped = glm::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - clamped;
    return 1.0f - inv * inv * inv;
}

std::string toLowerCopyLocal(std::string s);

glm::vec3 meshBoundsCenterLocal(const render_model::MeshData &mesh) {
    if (mesh.vertices.empty()) return glm::vec3(0.0f);
    glm::vec3 minP(std::numeric_limits<float>::max());
    glm::vec3 maxP(-std::numeric_limits<float>::max());
    for (const auto &vertex : mesh.vertices) {
        minP = glm::min(minP, vertex.position);
        maxP = glm::max(maxP, vertex.position);
    }
    return 0.5f * (minP + maxP);
}

glm::vec3 meshVertexAverageLocal(const render_model::MeshData &mesh) {
    if (mesh.vertices.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    for (const auto &vertex : mesh.vertices) {
        sum += vertex.position;
    }
    return sum / static_cast<float>(mesh.vertices.size());
}

glm::vec3 projectOntoPlaneLocal(const glm::vec3 &value, const glm::vec3 &planeNormal) {
    const glm::vec3 normal =
        safeNormalize3Local(planeNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    return value - glm::dot(value, normal) * normal;
}

glm::vec3 chooseOrthogonalAxisLocal(const glm::vec3 &normal) {
    const glm::vec3 axes[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
    };
    float bestAbsDot = std::numeric_limits<float>::max();
    glm::vec3 bestAxis = axes[0];
    for (const auto &axis : axes) {
        const float absDot = std::abs(glm::dot(normal, axis));
        if (absDot < bestAbsDot) {
            bestAbsDot = absDot;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

glm::quat buildPlaneAlignedRotationLocal(const glm::vec3 &meshForwardLocal,
                                         const glm::vec3 &worldNormal,
                                         const glm::vec3 &worldUpHint) {
    glm::vec3 localY = safeNormalize3Local(meshForwardLocal, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 localZ = projectOntoPlaneLocal(glm::vec3(0.0f, 0.0f, 1.0f), localY);
    if (glm::dot(localZ, localZ) <= 0.000001f) {
        localZ = projectOntoPlaneLocal(chooseOrthogonalAxisLocal(localY), localY);
    }
    localZ = safeNormalize3Local(localZ, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 localX = glm::normalize(glm::cross(localY, localZ));

    glm::vec3 worldY = safeNormalize3Local(worldNormal, glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 worldZ = projectOntoPlaneLocal(worldUpHint, worldY);
    if (glm::dot(worldZ, worldZ) <= 0.000001f) {
        worldZ = projectOntoPlaneLocal(chooseOrthogonalAxisLocal(worldY), worldY);
    }
    worldZ = safeNormalize3Local(worldZ, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 worldX = glm::normalize(glm::cross(worldY, worldZ));

    const glm::mat3 localBasis(localX, localY, localZ);
    const glm::mat3 worldBasis(worldX, worldY, worldZ);
    return glm::quat_cast(worldBasis * glm::transpose(localBasis));
}

enum class BillboardFacingModeLocal {
    None,
    Camera,
    CameraUpright,
    Shared,
    SharedUpright,
    AttackPlane,
};

BillboardFacingModeLocal resolveBillboardFacingModeLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    if (pass.billboardFacingMode.empty()) return BillboardFacingModeLocal::None;
    const std::string mode = toLowerCopyLocal(pass.billboardFacingMode);
    if (mode == "camera" || mode == "camera_face") return BillboardFacingModeLocal::Camera;
    if (mode == "upright" || mode == "camera_upright") return BillboardFacingModeLocal::CameraUpright;
    if (mode == "shared" || mode == "shared_camera") return BillboardFacingModeLocal::Shared;
    if (mode == "shared_upright") return BillboardFacingModeLocal::SharedUpright;
    if (mode == "attack_plane" || mode == "world_locked" || mode == "fixed_plane") {
        return BillboardFacingModeLocal::AttackPlane;
    }
    return BillboardFacingModeLocal::None;
}

glm::vec3 resolveAuthoredBillboardOffsetLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    std::size_t groupIndex,
    float ageSec) {
    if (pass.authoredBillboardOffsetFps <= 0.0f || pass.authoredBillboardOffsetFrames.empty()) {
        return glm::vec3(0.0f);
    }
    const float frameF = std::max(0.0f, ageSec) * pass.authoredBillboardOffsetFps;
    const auto &frames = pass.authoredBillboardOffsetFrames;
    if (frames.size() == 1u) {
        if (groupIndex < frames.front().offsetsLocal.size()) {
            return frames.front().offsetsLocal[groupIndex];
        }
        return glm::vec3(0.0f);
    }
    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        if (groupIndex < frames.front().offsetsLocal.size()) {
            return frames.front().offsetsLocal[groupIndex];
        }
        return glm::vec3(0.0f);
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        if (groupIndex < frames.back().offsetsLocal.size()) {
            return frames.back().offsetsLocal[groupIndex];
        }
        return glm::vec3(0.0f);
    }
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        const glm::vec3 aOffset =
            (groupIndex < a.offsetsLocal.size()) ? a.offsetsLocal[groupIndex] : glm::vec3(0.0f);
        const glm::vec3 bOffset =
            (groupIndex < b.offsetsLocal.size()) ? b.offsetsLocal[groupIndex] : glm::vec3(0.0f);
        return glm::mix(aOffset, bOffset, t);
    }
    return glm::vec3(0.0f);
}

glm::vec2 resolveAuthoredBillboardScaleLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    std::size_t groupIndex,
    float ageSec) {
    if (pass.authoredBillboardScaleFps <= 0.0f || pass.authoredBillboardScaleFrames.empty()) {
        return glm::vec2(1.0f);
    }
    const float frameF = std::max(0.0f, ageSec) * pass.authoredBillboardScaleFps;
    const auto &frames = pass.authoredBillboardScaleFrames;
    if (frames.size() == 1u) {
        if (groupIndex < frames.front().scaleMulLocal.size()) {
            return frames.front().scaleMulLocal[groupIndex];
        }
        return glm::vec2(1.0f);
    }
    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        if (groupIndex < frames.front().scaleMulLocal.size()) {
            return frames.front().scaleMulLocal[groupIndex];
        }
        return glm::vec2(1.0f);
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        if (groupIndex < frames.back().scaleMulLocal.size()) {
            return frames.back().scaleMulLocal[groupIndex];
        }
        return glm::vec2(1.0f);
    }
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        const glm::vec2 aScale =
            (groupIndex < a.scaleMulLocal.size()) ? a.scaleMulLocal[groupIndex] : glm::vec2(1.0f);
        const glm::vec2 bScale =
            (groupIndex < b.scaleMulLocal.size()) ? b.scaleMulLocal[groupIndex] : glm::vec2(1.0f);
        return glm::mix(aScale, bScale, t);
    }
    return glm::vec2(1.0f);
}

float resolveAuthoredBillboardSpinDeltaDegLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    std::size_t groupIndex,
    float ageSec) {
    if (pass.authoredBillboardSpinFps <= 0.0f || pass.authoredBillboardSpinFrames.empty()) {
        return 0.0f;
    }
    const float frameF = std::max(0.0f, ageSec) * pass.authoredBillboardSpinFps;
    const auto &frames = pass.authoredBillboardSpinFrames;
    if (frames.size() == 1u) {
        if (groupIndex < frames.front().spinDegLocal.size()) {
            return frames.front().spinDegLocal[groupIndex];
        }
        return 0.0f;
    }
    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        if (groupIndex < frames.front().spinDegLocal.size()) {
            return frames.front().spinDegLocal[groupIndex];
        }
        return 0.0f;
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        if (groupIndex < frames.back().spinDegLocal.size()) {
            return frames.back().spinDegLocal[groupIndex];
        }
        return 0.0f;
    }
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        const float aSpin =
            (groupIndex < a.spinDegLocal.size()) ? a.spinDegLocal[groupIndex] : 0.0f;
        const float bSpin =
            (groupIndex < b.spinDegLocal.size()) ? b.spinDegLocal[groupIndex] : 0.0f;
        return glm::mix(aSpin, bSpin, t);
    }
    return 0.0f;
}

glm::vec3 resolveMeshCornerAnchorLocal(const render_model::MeshData &mesh,
                                       const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    const std::string mode = toLowerCopyLocal(pass.meshCornerAnchorMode);
    if (mode == "origin") return glm::vec3(0.0f);
    if (mode == "centroid" || mode == "average" || mode == "vertex_average") {
        return meshVertexAverageLocal(mesh);
    }
    return meshBoundsCenterLocal(mesh);
}

glm::vec2 meshProjectionRangeLocal(const render_model::MeshData &mesh, const glm::vec3 &axis) {
    if (mesh.vertices.empty()) return glm::vec2(0.0f);
    const glm::vec3 normalizedAxis = safeNormalize3Local(axis, glm::vec3(0.0f, 0.0f, 1.0f));
    float minProj = std::numeric_limits<float>::max();
    float maxProj = -std::numeric_limits<float>::max();
    for (const auto &vertex : mesh.vertices) {
        const float proj = glm::dot(vertex.position, normalizedAxis);
        minProj = std::min(minProj, proj);
        maxProj = std::max(maxProj, proj);
    }
    if (minProj > maxProj) return glm::vec2(0.0f);
    return glm::vec2(minProj, maxProj);
}

std::array<float, 16> toModelMatrixArrayLocal(const glm::mat4 &matrix) {
    std::array<float, 16> out{};
    std::memcpy(out.data(), &matrix[0][0], sizeof(float) * out.size());
    return out;
}

std::vector<glm::vec3> makeFallbackDirectionsLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return authored::resolveGeneratedDirections(pass);
}

float resolveRadialDistanceMulLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                    std::uint32_t randomSeed,
                                    std::size_t dirIndex,
                                    int sequenceIndex) {
    float minMul = std::max(0.0f, pass.radialDistanceMinMul);
    float maxMul = std::max(0.0f, pass.radialDistanceMaxMul);
    if (maxMul < minMul) std::swap(minMul, maxMul);
    if (std::abs(maxMul - minMul) <= 0.0001f) return minMul;

    const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
    const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
    const std::uint32_t seqSalt = static_cast<std::uint32_t>(sequenceIndex + 17) * 0xc2b2ae35u;
    const float noise = hash01Local(randomSeed ^ passSalt ^ dirSalt ^ seqSalt ^ 0x6d2b79f5u);
    return glm::mix(minMul, maxMul, noise);
}

std::string toLowerCopyLocal(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool usesTouchingQuarterLayoutLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    const std::string layout = toLowerCopyLocal(pass.quarterLayout);
    return layout == "touching" || layout == "touching_cluster" || layout == "cluster";
}

bool hasDefaultQuarterUvTransformLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return std::abs(pass.uvScale.x - 1.0f) <= 0.0001f &&
           std::abs(pass.uvScale.y - 1.0f) <= 0.0001f &&
           std::abs(pass.uvOffset.x) <= 0.0001f &&
           std::abs(pass.uvOffset.y) <= 0.0001f;
}

const SharedMeshGeometry &getSharedMeshGeometryLocal(const render_model::MeshData &mesh,
                                                     const std::string &geometryKey,
                                                     const glm::vec2 &uvScale,
                                                     const glm::vec2 &uvOffset) {
    static thread_local std::unordered_map<std::string, SharedMeshGeometry> cache;

    const auto found = cache.find(geometryKey);
    if (found != cache.end()) return found->second;

    SharedMeshGeometry geometry;
    geometry.vertices.reserve(mesh.vertices.size());
    geometry.indices = mesh.indices;
    for (const auto &src : mesh.vertices) {
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = src.position.x;
        vtx.y = src.position.y;
        vtx.z = src.position.z;
        vtx.u = src.uv.x * uvScale.x + uvOffset.x;
        vtx.v = src.uv.y * uvScale.y + uvOffset.y;
        vtx.r = 1.0f;
        vtx.g = 1.0f;
        vtx.b = 1.0f;
        vtx.a = std::clamp(src.color.a, 0.0f, 1.0f);
        vtx.nx = src.normal.x;
        vtx.ny = src.normal.y;
        vtx.nz = src.normal.z;
        vtx.joint0 = static_cast<float>(src.j0);
        vtx.joint1 = static_cast<float>(src.j1);
        vtx.joint2 = static_cast<float>(src.j2);
        vtx.joint3 = static_cast<float>(src.j3);
        vtx.weight0 = src.w0;
        vtx.weight1 = src.w1;
        vtx.weight2 = src.w2;
        vtx.weight3 = src.w3;
        vtx.tx = src.tangent.x;
        vtx.ty = src.tangent.y;
        vtx.tz = src.tangent.z;
        vtx.tw = src.tangent.w;
        geometry.vertices.push_back(vtx);
    }

    return cache.emplace(geometryKey, std::move(geometry)).first->second;
}

const SharedMeshGeometry &getSharedLineMeshGeometryLocal(const render_model::MeshData &mesh,
                                                         const std::string &geometryKey,
                                                         float lineTevK1A) {
    static thread_local std::unordered_map<std::string, SharedMeshGeometry> cache;

    const auto found = cache.find(geometryKey);
    if (found != cache.end()) return found->second;

    SharedMeshGeometry geometry;
    geometry.vertices.reserve(mesh.vertices.size());
    geometry.indices = mesh.indices;
    for (const auto &src : mesh.vertices) {
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = src.position.x;
        vtx.y = src.position.y;
        vtx.z = src.position.z;
        vtx.u = src.uv.x;
        vtx.v = src.uv.y;
        vtx.r = 1.0f;
        vtx.g = 1.0f;
        vtx.b = 1.0f;
        vtx.a = authored::quantizeLineVertexAlpha(
            std::clamp(src.color.a, 0.0f, 1.0f),
            lineTevK1A,
            1.0f);
        vtx.nx = src.normal.x;
        vtx.ny = src.normal.y;
        vtx.nz = src.normal.z;
        vtx.joint0 = static_cast<float>(src.j0);
        vtx.joint1 = static_cast<float>(src.j1);
        vtx.joint2 = static_cast<float>(src.j2);
        vtx.joint3 = static_cast<float>(src.j3);
        vtx.weight0 = src.w0;
        vtx.weight1 = src.w1;
        vtx.weight2 = src.w2;
        vtx.weight3 = src.w3;
        vtx.tx = src.tangent.x;
        vtx.ty = src.tangent.y;
        vtx.tz = src.tangent.z;
        vtx.tw = src.tangent.w;
        geometry.vertices.push_back(vtx);
    }

    return cache.emplace(geometryKey, std::move(geometry)).first->second;
}

const std::vector<SparkleSpriteDescriptor> &getSparkleSpriteDescriptorsLocal(
    const render_model::MeshData &mesh,
    const std::string &descriptorKey) {
    static thread_local std::unordered_map<std::string, std::vector<SparkleSpriteDescriptor>> cache;

    const auto found = cache.find(descriptorKey);
    if (found != cache.end()) return found->second;

    std::vector<SparkleSpriteDescriptor> descriptors;
    if (!mesh.vertices.empty() && mesh.indices.size() >= 6u) {
        descriptors.reserve(mesh.indices.size() / 6u);
        for (std::size_t i = 0; i + 5u < mesh.indices.size(); i += 6u) {
            std::array<std::uint32_t, 4> unique{};
            std::size_t uniqueCount = 0u;
            for (std::size_t j = 0; j < 6u; ++j) {
                const std::uint32_t idx = mesh.indices[i + j];
                bool seen = false;
                for (std::size_t k = 0; k < uniqueCount; ++k) {
                    if (unique[k] == idx) {
                        seen = true;
                        break;
                    }
                }
                if (seen) continue;
                if (uniqueCount < unique.size()) unique[uniqueCount++] = idx;
            }
            if (uniqueCount < 4u) continue;
            if (unique[0] >= mesh.vertices.size() || unique[1] >= mesh.vertices.size() ||
                unique[2] >= mesh.vertices.size() || unique[3] >= mesh.vertices.size()) {
                continue;
            }

            const glm::vec3 p0 = mesh.vertices[unique[0]].position;
            const glm::vec3 p1 = mesh.vertices[unique[1]].position;
            const glm::vec3 p2 = mesh.vertices[unique[2]].position;
            const glm::vec3 p3 = mesh.vertices[unique[3]].position;
            SparkleSpriteDescriptor desc;
            desc.center = (p0 + p1 + p2 + p3) * 0.25f;
            desc.width = std::max(0.0001f, glm::length(p1 - p0));
            desc.height = std::max(0.0001f, glm::length(p2 - p0));
            descriptors.push_back(desc);
        }
    }

    if (descriptors.empty()) {
        SparkleSpriteDescriptor fallback;
        fallback.center = meshBoundsCenterLocal(mesh);
        descriptors.push_back(fallback);
    }

    return cache.emplace(descriptorKey, std::move(descriptors)).first->second;
}

const std::vector<CornerBillboardQuadDescriptor> &getCornerBillboardQuadDescriptorsLocal(
    const render_model::MeshData &mesh,
    const std::string &descriptorKey) {
    static thread_local std::unordered_map<std::string, std::vector<CornerBillboardQuadDescriptor>> cache;

    const auto found = cache.find(descriptorKey);
    if (found != cache.end()) return found->second;

    std::vector<CornerBillboardQuadDescriptor> descriptors;
    if (!mesh.vertices.empty() && mesh.indices.size() >= 6u) {
        descriptors.reserve(mesh.indices.size() / 6u);
        for (std::size_t i = 0; i + 5u < mesh.indices.size(); i += 6u) {
            std::array<std::uint32_t, 4> unique{};
            std::size_t uniqueCount = 0u;
            for (std::size_t j = 0; j < 6u; ++j) {
                const std::uint32_t idx = mesh.indices[i + j];
                bool seen = false;
                for (std::size_t k = 0; k < uniqueCount; ++k) {
                    if (unique[k] == idx) {
                        seen = true;
                        break;
                    }
                }
                if (seen) continue;
                if (uniqueCount < unique.size()) unique[uniqueCount++] = idx;
            }
            if (uniqueCount < 4u) continue;
            if (unique[0] >= mesh.vertices.size() || unique[1] >= mesh.vertices.size() ||
                unique[2] >= mesh.vertices.size() || unique[3] >= mesh.vertices.size()) {
                continue;
            }

            CornerBillboardQuadDescriptor desc;
            desc.cornerCount = uniqueCount;
            for (std::size_t cornerIndex = 0; cornerIndex < uniqueCount; ++cornerIndex) {
                desc.corners[cornerIndex] = mesh.vertices[unique[cornerIndex]].position;
            }

            desc.center = glm::vec3(0.0f);
            for (std::size_t cornerIndex = 0; cornerIndex < uniqueCount; ++cornerIndex) {
                desc.center += desc.corners[cornerIndex];
            }
            desc.center /= static_cast<float>(uniqueCount);

            const glm::vec3 p0 = desc.corners[0];
            std::array<std::pair<float, glm::vec3>, 3> edgeVectors = {{
                {glm::length(desc.corners[1] - p0), desc.corners[1] - p0},
                {glm::length(desc.corners[2] - p0), desc.corners[2] - p0},
                {glm::length(desc.corners[3] - p0), desc.corners[3] - p0},
            }};
            std::sort(
                edgeVectors.begin(),
                edgeVectors.end(),
                [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });

            desc.width = std::max(0.0001f, edgeVectors[0].first);
            desc.height = std::max(0.0001f, edgeVectors[1].first);
            desc.halfWidth = 0.5f * desc.width;
            desc.halfHeight = 0.5f * desc.height;
            desc.spriteSize = std::max(0.0001f, 0.5f * (desc.width + desc.height));

            const glm::vec3 edgeU = edgeVectors[0].second;
            desc.axisU = safeNormalize3Local(edgeU, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::vec3 edgeV = edgeVectors[1].second;
            edgeV -= desc.axisU * glm::dot(edgeV, desc.axisU);
            desc.axisV = safeNormalize3Local(edgeV, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::dot(glm::cross(desc.axisU, desc.axisV), glm::cross(edgeVectors[0].second, edgeVectors[1].second)) < 0.0f) {
                desc.axisV = -desc.axisV;
            }

            std::array<glm::vec3, 4> rebuiltCorners{
                desc.center - desc.axisU * desc.halfWidth + desc.axisV * desc.halfHeight,
                desc.center + desc.axisU * desc.halfWidth + desc.axisV * desc.halfHeight,
                desc.center - desc.axisU * desc.halfWidth - desc.axisV * desc.halfHeight,
                desc.center + desc.axisU * desc.halfWidth - desc.axisV * desc.halfHeight,
            };
            desc.corners = rebuiltCorners;
            descriptors.push_back(desc);
        }
    }

    if (descriptors.empty()) {
        CornerBillboardQuadDescriptor fallback;
        fallback.center = meshBoundsCenterLocal(mesh);
        fallback.corners[0] = meshBoundsCenterLocal(mesh);
        fallback.cornerCount = 1u;
        descriptors.push_back(fallback);
    }

    return cache.emplace(descriptorKey, std::move(descriptors)).first->second;
}

const std::array<IRenderBackend::WorldMeshVertex, 4> &quarterVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(1.0f, 0.0f, 1.0f),
        };
        constexpr std::array<glm::vec2, 4> kUvs = {
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f),
        };
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = kUvs[i].x;
            verts[i].v = kUvs[i].y;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = 1.0f;
        }
        return verts;
    }();
    return kVertices;
}

const std::array<IRenderBackend::WorldMeshVertex, 4> &centeredQuadVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(-0.5f, 0.0f, -0.5f),
            glm::vec3(0.5f, 0.0f, -0.5f),
            glm::vec3(-0.5f, 0.0f, 0.5f),
            glm::vec3(0.5f, 0.0f, 0.5f),
        };
        constexpr std::array<glm::vec2, 4> kUvs = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(1.0f, 1.0f),
        };
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = kUvs[i].x;
            verts[i].v = kUvs[i].y;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = 1.0f;
        }
        return verts;
    }();
    return kVertices;
}

const std::array<IRenderBackend::WorldMeshVertex, 4> &streakQuadVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(-0.05f, 0.0f, 0.0f),
            glm::vec3(0.05f, 0.0f, 0.0f),
            glm::vec3(-0.05f, 0.0f, 1.0f),
            glm::vec3(0.05f, 0.0f, 1.0f),
        };
        constexpr std::array<float, 4> kAlpha = {0.0f, 0.0f, 1.0f, 1.0f};
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = 0.0f;
            verts[i].v = 0.0f;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = kAlpha[i];
        }
        return verts;
    }();
    return kVertices;
}

const std::array<std::uint32_t, 6> &quarterIndicesLocal() {
    static const std::array<std::uint32_t, 6> kIndices = {0u, 1u, 2u, 2u, 1u, 3u};
    return kIndices;
}

bool hasCustomUvTransformLocal(const glm::vec2 &uvScale, const glm::vec2 &uvOffset) {
    return std::abs(uvScale.x - 1.0f) > 0.0001f ||
           std::abs(uvScale.y - 1.0f) > 0.0001f ||
           std::abs(uvOffset.x) > 0.0001f ||
           std::abs(uvOffset.y) > 0.0001f;
}

std::string makeMeshGeometryCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                          const glm::vec2 &uvScale,
                                          const glm::vec2 &uvOffset) {
    if (!hasCustomUvTransformLocal(uvScale, uvOffset)) {
        return std::string("__authored_vfx_geom_mesh_v1__:") + pass.meshPath;
    }
    const auto q = [](float value) {
        return static_cast<int>(std::lround(value * 100000.0f));
    };
    return std::string("__authored_vfx_geom_mesh_uv_v1__:") +
           pass.id + ":" +
           pass.meshPath + ":" +
           std::to_string(q(uvScale.x)) + ":" +
           std::to_string(q(uvScale.y)) + ":" +
           std::to_string(q(uvOffset.x)) + ":" +
           std::to_string(q(uvOffset.y));
}

std::string makeSparkleSpriteDescriptorCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return std::string("__authored_vfx_sparkle_desc_v1__:") + pass.meshPath;
}

std::string makeCornerBillboardDescriptorCacheKeyLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return std::string("__authored_vfx_corner_desc_v1__:") + pass.meshPath;
}

std::string makeLineGeometryCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                          float lineTevK1A) {
    const int k1aMilli = static_cast<int>(std::lround(std::clamp(lineTevK1A, 0.0f, 1.0f) * 1000.0f));
    return std::string("__authored_vfx_geom_line_v1__:") + pass.id + ":" + pass.meshPath + ":" + std::to_string(k1aMilli);
}

std::string makeQuarterGeometryCacheKeyLocal() {
    return "__authored_vfx_geom_quarter_v1__";
}

std::string makeCenteredQuadGeometryCacheKeyLocal() {
    return "__authored_vfx_geom_centered_quad_v1__";
}

std::string makeUvTransformedCenteredQuadGeometryCacheKeyLocal(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    const auto q = [](float value) {
        return static_cast<int>(std::lround(value * 100000.0f));
    };
    return std::string("__authored_vfx_geom_centered_quad_uv_v1__:") +
           pass.id + ":" +
           std::to_string(q(pass.uvScale.x)) + ":" +
           std::to_string(q(pass.uvScale.y)) + ":" +
           std::to_string(q(pass.uvOffset.x)) + ":" +
           std::to_string(q(pass.uvOffset.y));
}

std::string makeStreakQuadGeometryCacheKeyLocal(float lineTevK1A) {
    const int k1aMilli = static_cast<int>(std::lround(std::clamp(lineTevK1A, 0.0f, 1.0f) * 1000.0f));
    return std::string("__authored_vfx_geom_streak_quad_v2__:") + std::to_string(k1aMilli);
}

bool hasCustomUvTransformLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return hasCustomUvTransformLocal(pass.uvScale, pass.uvOffset);
}

std::vector<IRenderBackend::WorldMeshVertex> makeUvTransformedQuadVerticesLocal(
    const std::array<IRenderBackend::WorldMeshVertex, 4> &base,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    std::vector<IRenderBackend::WorldMeshVertex> verts(base.begin(), base.end());
    for (auto &vtx : verts) {
        vtx.u = vtx.u * pass.uvScale.x + pass.uvOffset.x;
        vtx.v = vtx.v * pass.uvScale.y + pass.uvOffset.y;
    }
    return verts;
}

bool computePassSequenceStateLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                   int sequenceCount,
                                   float age01,
                                   float fadeStart,
                                   int sequenceIndex,
                                   float &outLocalAge01,
                                   float &outFade) {
    if (sequenceCount <= 1) {
        outLocalAge01 = age01;
    } else {
        const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
        const float sequenceStep = std::max(0.0f, pass.sequenceStep);
        const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
        const float sequenceTimelineSpan =
            sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
        const float sequenceAge = age01 * sequenceTimelineSpan;
        outLocalAge01 = (sequenceAge - sequenceStart) / sequenceLife;
        if (outLocalAge01 < 0.0f || outLocalAge01 > 1.0f) return false;
    }

    outFade = 1.0f;
    if (outLocalAge01 > fadeStart) {
        const float t = (outLocalAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        outFade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    return outFade > 0.001f;
}

bool computeDelayedPassLaunchStateLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                        int sequenceCount,
                                        float age01,
                                        int sequenceIndex,
                                        float &outLaunchAge01) {
    if (sequenceCount <= 1) {
        outLaunchAge01 = age01;
        return true;
    }

    const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
    const float sequenceStep = std::max(0.0f, pass.sequenceStep);
    const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
    const float sequenceTimelineSpan =
        sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
    const float sequenceAge = age01 * sequenceTimelineSpan;
    if (sequenceAge < sequenceStart) return false;

    outLaunchAge01 = glm::clamp((sequenceAge - sequenceStart) / sequenceLife, 0.0f, 1.0f);
    return true;
}

float computeSharedDelayedFadeLocal(float age01, float fadeStart) {
    const float delayedFadeStart = std::max(fadeStart, 0.92f);
    if (age01 <= delayedFadeStart) return 1.0f;
    const float t = (age01 - delayedFadeStart) / std::max(0.0001f, 1.0f - delayedFadeStart);
    return 1.0f - glm::clamp(t, 0.0f, 1.0f);
}

float computeQuarterAnimatedScaleLocal(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                       const SharedAuthoredBatchVFX::RenderRing &ring,
                                       int sequenceCount,
                                       float age01,
                                       float localAge01) {
    const float passScale = std::max(0.0f, pass.scaleMul);
    if (sequenceCount <= 1) {
        return glm::mix(ring.startScale, ring.endScale, age01) * passScale;
    }

    const float radiusGrowthMul = std::max(1.0f, pass.radiusGrowthMul);
    return std::max(0.0f, ring.startScale) * passScale *
           glm::mix(1.0f, radiusGrowthMul, glm::clamp(localAge01, 0.0f, 1.0f));
}

shared_world_batches::WorldIndexedBatch makeBaseBatchLocal(
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const TextureView &texture) {
    shared_world_batches::WorldIndexedBatch batch;
    batch.textureKey =
        std::string("authored_vfx:") + pass.id + ":" +
        (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
    batch.textureCacheKey = authored::makeTextureCacheKey(snapshot.config, pass);
    batch.textureRgba = texture.rgba;
    batch.textureWidth = texture.width;
    batch.textureHeight = texture.height;
    batch.textureWrapS = pass.textureQuarterRing ? 33071 : 10497;
    batch.textureWrapT = pass.textureQuarterRing ? 33071 : 10497;
    batch.alphaMode = 2u;
    batch.blendMode = authored::resolveBlendMode(snapshot.config, pass);
    batch.dualSourceBlendEnabled = pass.dualSourceBlend ? 1u : 0u;
    batch.depthTestEnabled = snapshot.config.depthTest ? 1u : 0u;
    batch.clipSpaceDepthBias = snapshot.config.clipSpaceDepthBias;
    batch.alphaCutoff = 0.0f;
    batch.alphaWindowMin = glm::clamp(pass.alphaWindowMin, 0.0f, 1.0f);
    batch.alphaWindowMax = glm::clamp(pass.alphaWindowMax, 0.0f, 1.0f);
    return batch;
}

void appendTransformedMeshLocal(shared_world_batches::WorldIndexedBatch &batch,
                                const render_model::MeshData &mesh,
                                const glm::mat4 &world,
                                const glm::vec4 &color,
                                bool quantizeLineAlpha,
                                float lineTevK1A) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return;
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + mesh.vertices.size());
    for (const auto &src : mesh.vertices) {
        const glm::vec4 wp = world * glm::vec4(src.position, 1.0f);
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = src.uv.x;
        vtx.v = src.uv.y;
        vtx.r = color.r;
        vtx.g = color.g;
        vtx.b = color.b;
        const float srcAlpha = std::clamp(src.color.a, 0.0f, 1.0f);
        if (quantizeLineAlpha) {
            vtx.a = authored::quantizeLineVertexAlpha(srcAlpha, lineTevK1A, color.a);
        } else {
            vtx.a = std::clamp(color.a * srcAlpha, 0.0f, 1.0f);
        }
        batch.vertices.push_back(vtx);
    }
    batch.indices.reserve(batch.indices.size() + mesh.indices.size());
    for (std::uint32_t idx : mesh.indices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

void appendQuarterQuadLocal(shared_world_batches::WorldIndexedBatch &batch,
                            const glm::mat4 &world,
                            const glm::vec4 &color,
                            const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                            bool centered) {
    const std::array<glm::vec3, 4> positions = centered
                                                   ? std::array<glm::vec3, 4>{
                                                         glm::vec3(-0.5f, 0.0f, -0.5f),
                                                         glm::vec3(0.5f, 0.0f, -0.5f),
                                                         glm::vec3(-0.5f, 0.0f, 0.5f),
                                                         glm::vec3(0.5f, 0.0f, 0.5f),
                                                     }
                                                   : std::array<glm::vec3, 4>{
                                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                                         glm::vec3(1.0f, 0.0f, 0.0f),
                                                         glm::vec3(0.0f, 0.0f, 1.0f),
                                                         glm::vec3(1.0f, 0.0f, 1.0f),
                                                     };
    const std::array<glm::vec2, 4> baseUvs = centered
                                                 ? std::array<glm::vec2, 4>{
                                                       glm::vec2(0.0f, 0.0f),
                                                       glm::vec2(1.0f, 0.0f),
                                                       glm::vec2(0.0f, 1.0f),
                                                       glm::vec2(1.0f, 1.0f),
                                                   }
                                                 : std::array<glm::vec2, 4>{
                                                       glm::vec2(1.0f, 1.0f),
                                                       glm::vec2(0.0f, 1.0f),
                                                       glm::vec2(1.0f, 0.0f),
                                                       glm::vec2(0.0f, 0.0f),
                                                   };
    const auto &indices = quarterIndicesLocal();
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + positions.size());
    batch.indices.reserve(batch.indices.size() + indices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const glm::vec4 wp = world * glm::vec4(positions[i], 1.0f);
        const glm::vec2 uv = baseUvs[i] * pass.uvScale + pass.uvOffset;
        IRenderBackend::WorldMeshVertex vtx{};
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = uv.x;
        vtx.v = uv.y;
        vtx.r = color.r;
        vtx.g = color.g;
        vtx.b = color.b;
        vtx.a = color.a;
        batch.vertices.push_back(vtx);
    }
    for (std::uint32_t idx : indices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

void appendSharedGeometryLocal(shared_world_batches::WorldIndexedBatch &batch,
                               const SharedMeshGeometry &geometry,
                               const glm::mat4 &world,
                               const glm::vec4 &color) {
    if (geometry.vertices.empty() || geometry.indices.size() < 3u) return;

    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + geometry.vertices.size());
    batch.indices.reserve(batch.indices.size() + geometry.indices.size());

    for (const auto &src : geometry.vertices) {
        const glm::vec4 wp = world * glm::vec4(src.x, src.y, src.z, 1.0f);
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = src.u;
        vtx.v = src.v;
        vtx.r = std::clamp(src.r * color.r, 0.0f, 1.0f);
        vtx.g = std::clamp(src.g * color.g, 0.0f, 1.0f);
        vtx.b = std::clamp(src.b * color.b, 0.0f, 1.0f);
        vtx.a = std::clamp(src.a * color.a, 0.0f, 1.0f);
        batch.vertices.push_back(vtx);
    }
    for (std::uint32_t idx : geometry.indices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

bool appendSharedMeshPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const authored::TevState &passTev,
    const render_model::MeshData &passMesh,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

    const bool quarterTextureBake =
        authored::usesQuarterTextureBake(snapshot.config, pass);

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, false);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }
    const float localScaleMul =
        authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const float scale =
        glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
        std::max(0.0f, pass.scaleMul) *
        localScaleMul;
    if (scale <= 0.0001f) return false;

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    float passAlphaScale = std::clamp(
        timingState.fade * std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
        0.0f,
        1.0f);
    if (!quarterTextureBake) passAlphaScale *= passTev.k1a;
    const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;
    const glm::vec2 animatedUvScale =
        authored::resolvePassAnimatedUvScale(pass, ring.ageSec);
    const glm::vec2 animatedUvOffset =
        authored::resolvePassAnimatedUvOffset(pass, ring.ageSec);
    const glm::vec2 finalUvScale(pass.uvScale.x * animatedUvScale.x,
                                 pass.uvScale.y * animatedUvScale.y);
    const glm::vec2 finalUvOffset(pass.uvOffset.x * animatedUvScale.x + animatedUvOffset.x,
                                  pass.uvOffset.y * animatedUvScale.y + animatedUvOffset.y);

    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3> *localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->size() != 1u) return false;

    const glm::vec3 localDirBasisRaw = (*localDirections)[0];
    if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) return false;

    const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
    const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
    if (glm::dot(worldDir, worldDir) <= 0.000001f) return false;

    const glm::vec3 passForward = glm::normalize(worldDir);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const glm::vec3 animatedPositionLocalOffset =
        pass.positionLocalOffset + authored::resolvePassAnimatedPositionLocalOffset(pass, ring.ageSec);
    const glm::vec3 positionLocalOffset =
        right * animatedPositionLocalOffset.x +
        up * animatedPositionLocalOffset.y +
        ringForward * animatedPositionLocalOffset.z;
    const float forwardTravel =
        timingPlan.delayedSinglePass
            ? (pass.forwardOffset * fastLaunch01Local(timingState.localAge01))
            : pass.forwardOffset;
    const glm::vec3 passPos =
        ring.pos + passForward * forwardTravel + radialStartOffset + positionLocalOffset;
    const glm::vec3 facingDir =
        pass.cameraFacing
            ? safeNormalize3Local(cameraWorldPos - passPos, passForward)
            : passForward;
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, facingDir);
    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec3 passAnimatedScaleMul =
        authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
    const glm::vec3 finalScale = glm::vec3(scale) * passAnimatedScaleMul * axisScale;
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const std::string geometryCacheKey =
        makeMeshGeometryCacheKeyLocal(pass, finalUvScale, finalUvOffset);
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    const SharedMeshGeometry &sharedGeometry =
        getSharedMeshGeometryLocal(passMesh, geometryCacheKey, finalUvScale, finalUvOffset);
    if (sharedGeometry.vertices.empty() || sharedGeometry.indices.size() < 3u) return false;

    batch.sharedVertices = sharedGeometry.vertices.data();
    batch.sharedVertexCount = sharedGeometry.vertices.size();
    batch.sharedIndices = sharedGeometry.indices.data();
    batch.sharedIndexCount = sharedGeometry.indices.size();
    batch.geometryCacheKey = geometryCacheKey;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = passAlpha;
    const glm::vec3 animatedMeshLocalOffset =
        pass.meshLocalOffset + authored::resolvePassAnimatedMeshLocalOffset(pass, ring.ageSec);
    const glm::quat animatedMeshLocalRotation =
        authored::resolvePassAnimatedMeshRotationQuat(pass, ring.ageSec);
    batch.modelMatrix = toModelMatrixArrayLocal(
        glm::translate(glm::mat4(1.0f), passPos) *
        glm::mat4_cast(passRot) *
        glm::mat4_cast(animatedMeshLocalRotation) *
        glm::scale(glm::mat4(1.0f), finalScale) *
        glm::translate(glm::mat4(1.0f), animatedMeshLocalOffset));
    batch.sortDepth = distSq;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedSparkleBillboardPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const render_model::MeshData &passMesh,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }

    const float baseScale = std::max(0.0f, pass.scaleMul);
    if (baseScale <= 0.0001f) return false;

    const float passAlpha = std::clamp(
        timingState.fade * std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
        0.0f,
        1.0f);
    if (passAlpha <= 0.001f) return false;
    float offsetAgeSec = ring.ageSec;
    if (pass.timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, pass.timeEndSec - pass.timeStartSec);
        if (window > 0.0001f) {
            offsetAgeSec = glm::clamp(ring.ageSec - pass.timeStartSec, 0.0f, window);
        } else {
            offsetAgeSec = 0.0f;
        }
    }

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis =
        pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const auto &sharedVertices = quarterVerticesLocal();
    const auto &sharedIndices = quarterIndicesLocal();
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeQuarterGeometryCacheKeyLocal();

    const glm::vec3 meshCenter = meshBoundsCenterLocal(passMesh);
    const auto &descriptors = getSparkleSpriteDescriptorsLocal(
        passMesh, makeSparkleSpriteDescriptorCacheKeyLocal(pass));
    batch.instances.reserve(descriptors.size());

    const float startSpread = std::max(0.0f, pass.startRadiusMul);
    const float endSpread = std::max(startSpread, std::max(0.0f, pass.radiusMul));
    const float spreadT = glm::mix(startSpread, endSpread, timingState.localAge01);
    const float sizeMul = std::max(0.0f, pass.thicknessMul);
    float minCenteredLocalZ = 0.0f;
    float avgSpriteExtent = 0.0f;
    if (!descriptors.empty()) {
        minCenteredLocalZ = descriptors.front().center.z - meshCenter.z;
        for (const auto &desc : descriptors) {
            const glm::vec3 centeredLocal = desc.center - meshCenter;
            minCenteredLocalZ = std::min(minCenteredLocalZ, centeredLocal.z);
            avgSpriteExtent += 0.5f * (desc.width + desc.height);
        }
        avgSpriteExtent /= static_cast<float>(descriptors.size());
    }
    const float lateralWeight = 1.2f;
    const float verticalWeight = 1.15f;
    const float forwardWeight = 0.35f;
    const float forwardBiasLocal = std::max(0.0001f, avgSpriteExtent * 0.18f);
    const glm::vec3 sparkleOrigin =
        ring.pos + ringForward * pass.forwardOffset + up * pass.heightOffset;
    bool appendedAny = false;
    float sortDepth = 0.0f;

    for (const auto &desc : descriptors) {
        const glm::vec3 centeredLocal = desc.center - meshCenter;
        const glm::vec3 coneSeedLocal(
            centeredLocal.x * lateralWeight,
            centeredLocal.y * verticalWeight,
            ((centeredLocal.z - minCenteredLocalZ) + forwardBiasLocal) * forwardWeight);
        const float authoredDistance = std::max(0.0001f, glm::length(coneSeedLocal));
        const glm::vec3 worldDir =
            safeNormalize3Local(right * coneSeedLocal.x + up * coneSeedLocal.y + ringForward * coneSeedLocal.z,
                                ringForward);
        const glm::vec3 sparklePos = sparkleOrigin + worldDir * (authoredDistance * baseScale * spreadT);

        const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - sparklePos, ringForward);
        const glm::quat billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
        const glm::vec3 sparkleScale(
            std::max(0.0001f, desc.width * baseScale * sizeMul),
            1.0f,
            std::max(0.0001f, desc.height * baseScale * sizeMul));

        IRenderBackend::WorldMeshInstance instance;
        instance.modelMatrix = toModelMatrixArrayLocal(
            glm::translate(glm::mat4(1.0f), sparklePos) *
            glm::mat4_cast(billboardRot) *
            glm::scale(glm::mat4(1.0f), sparkleScale));
        instance.vertexColorMulA = passAlpha;
        batch.instances.push_back(std::move(instance));
        const float distSq = glm::dot(sparklePos - cameraWorldPos, sparklePos - cameraWorldPos);
        sortDepth = std::max(sortDepth, distSq);
        appendedAny = true;
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedGlowBillboardPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, false);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }
    const float localScaleMul = authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

    const float animatedScale =
        glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
        std::max(0.0f, pass.scaleMul) *
        localScaleMul;
    if (animatedScale <= 0.0001f) return false;

    const float passAlpha = std::clamp(
        timingState.fade * std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
        0.0f,
        1.0f);
    if (passAlpha <= 0.001f) return false;
    float offsetAgeSec = ring.ageSec;
    if (pass.timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, pass.timeEndSec - pass.timeStartSec);
        if (window > 0.0001f) {
            offsetAgeSec = glm::clamp(ring.ageSec - pass.timeStartSec, 0.0f, window);
        } else {
            offsetAgeSec = 0.0f;
        }
    }

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis =
        pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const BillboardFacingModeLocal facingMode = resolveBillboardFacingModeLocal(pass);
    const bool facingOverride = facingMode != BillboardFacingModeLocal::None;
    const bool useAttackPlane = facingMode == BillboardFacingModeLocal::AttackPlane;
    const bool useSharedFacing =
        facingMode == BillboardFacingModeLocal::Shared ||
        facingMode == BillboardFacingModeLocal::SharedUpright ||
        facingMode == BillboardFacingModeLocal::AttackPlane;
    const bool useUprightFacing =
        facingMode == BillboardFacingModeLocal::CameraUpright ||
        facingMode == BillboardFacingModeLocal::SharedUpright ||
        facingMode == BillboardFacingModeLocal::AttackPlane;
    glm::quat sharedFacingRot(1.0f, 0.0f, 0.0f, 0.0f);
    if (facingOverride && useSharedFacing) {
        if (useAttackPlane) {
            sharedFacingRot = buildPlaneAlignedRotationLocal(
                meshForwardLocal,
                ringForward,
                glm::vec3(0.0f, 1.0f, 0.0f));
        } else {
            const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - ring.pos, ringForward);
            sharedFacingRot =
                useUprightFacing
                    ? buildPlaneAlignedRotationLocal(
                          meshForwardLocal,
                          toCamera,
                          glm::vec3(0.0f, 1.0f, 0.0f))
                    : rotationFromToSafeLocal(meshForwardLocal, toCamera);
        }
    }

    std::vector<glm::vec3> fallbackDirections = makeFallbackDirectionsLocal(pass);
    const std::vector<glm::vec3> *localDirections = &fallbackDirections;
    if (localDirections->empty()) return false;

    const glm::vec3 finalScale(
        std::max(0.0001f, animatedScale * std::max(0.0f, pass.radiusMul)),
        1.0f,
        std::max(0.0001f, animatedScale * std::max(0.0f, pass.thicknessMul)));
    const bool hasAuthoredBillboards = !pass.authoredBillboardsLocal.empty();

    const auto &sharedIndices = quarterIndicesLocal();
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    if (hasCustomUvTransformLocal(pass)) {
        batch.vertices = makeUvTransformedQuadVerticesLocal(centeredQuadVerticesLocal(), pass);
        batch.geometryCacheKey = makeUvTransformedCenteredQuadGeometryCacheKeyLocal(pass);
    } else {
        const auto &sharedVertices = centeredQuadVerticesLocal();
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.geometryCacheKey = makeCenteredQuadGeometryCacheKeyLocal();
    }

    float sortDepth = 0.0f;
    bool appendedAny = false;
    if (hasAuthoredBillboards) {
        batch.instances.reserve(pass.authoredBillboardsLocal.size());
        const float baseSpinRad = computeBillboardSpinRadLocal(pass, timingState.localAge01);
        std::uint32_t authoredIndex = 0u;
        for (const auto &authored : pass.authoredBillboardsLocal) {
            const std::size_t groupIndex = authoredIndex / 4u;
            const glm::vec3 offset =
                resolveAuthoredBillboardOffsetLocal(pass, groupIndex, offsetAgeSec);
            const glm::vec2 scaleAnim =
                resolveAuthoredBillboardScaleLocal(pass, groupIndex, offsetAgeSec);
            const float spinAnimDeg =
                resolveAuthoredBillboardSpinDeltaDegLocal(pass, groupIndex, offsetAgeSec);
            const glm::vec3 localPos =
                (authored.positionLocal + offset) * pass.authoredBillboardPositionScale;
            const glm::vec3 glowPos =
                ring.pos + right * localPos.x + up * localPos.y + ringForward * localPos.z;
            glm::quat billboardRot(1.0f, 0.0f, 0.0f, 0.0f);
            if (!facingOverride) {
                const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
                billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
            } else if (useSharedFacing) {
                billboardRot = sharedFacingRot;
            } else {
                const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
                billboardRot =
                    useUprightFacing
                        ? buildPlaneAlignedRotationLocal(
                              meshForwardLocal,
                              toCamera,
                              glm::vec3(0.0f, 1.0f, 0.0f))
                        : rotationFromToSafeLocal(meshForwardLocal, toCamera);
            }
            const float spinRad =
                baseSpinRad +
                computeBillboardSpinJitterRadLocal(
                    pass,
                    ring.randomSeed,
                    authoredIndex * 0x85ebca6bu);
            const float instanceSpinRad = glm::radians(authored.spinDeg + spinAnimDeg);
            if (std::abs(spinRad + instanceSpinRad) > 0.0001f) {
                billboardRot *= glm::angleAxis(spinRad + instanceSpinRad, meshForwardLocal);
            }

            IRenderBackend::WorldMeshInstance instance;
            const glm::vec3 authoredScale(
                std::max(0.0f, authored.scaleXMul * scaleAnim.x),
                1.0f,
                std::max(0.0f, authored.scaleYMul * scaleAnim.y));
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), glowPos) *
                glm::mat4_cast(billboardRot) *
                glm::scale(
                    glm::mat4(1.0f),
                    finalScale * std::max(0.0f, authored.scaleMul) * authoredScale));
            instance.vertexColorMulA = passAlpha * std::max(0.0f, authored.alphaMul);
            batch.instances.push_back(std::move(instance));
            sortDepth = std::max(sortDepth, glm::dot(glowPos - cameraWorldPos, glowPos - cameraWorldPos));
            appendedAny = true;
            ++authoredIndex;
        }

        if (!appendedAny || batch.instances.empty()) return false;
        batch.sortDepth = sortDepth;
        outBatches.push_back(std::move(batch));
        return true;
    }

    for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
        glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
        if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

        if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
            const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
            const float xyLen = glm::length(baseXY);
            if (xyLen > 0.0001f) {
                const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                const float delta =
                    glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                const float angle = baseAngle + delta;
                localDirBasisRaw.x = std::cos(angle) * xyLen;
                localDirBasisRaw.y = std::sin(angle) * xyLen;
            }
        }

        float instanceAlpha = passAlpha;
        if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
            const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
            const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
            const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
            instanceAlpha *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
        }
        instanceAlpha = std::clamp(instanceAlpha, 0.0f, 1.0f);
        if (instanceAlpha <= 0.001f) continue;

        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        const glm::vec3 passForward = safeNormalize3Local(worldDir, ringForward);
        const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
        const glm::vec3 radialStartOffset =
            (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
        const glm::vec3 glowPos = ring.pos + radialStartOffset + passForward * pass.forwardOffset;
        glm::quat billboardRot(1.0f, 0.0f, 0.0f, 0.0f);
        if (!facingOverride) {
            const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
            billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
        } else if (useSharedFacing) {
            billboardRot = sharedFacingRot;
        } else {
            const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
            billboardRot =
                useUprightFacing
                    ? buildPlaneAlignedRotationLocal(
                          meshForwardLocal,
                          toCamera,
                          glm::vec3(0.0f, 1.0f, 0.0f))
                    : rotationFromToSafeLocal(meshForwardLocal, toCamera);
        }
        const float spinRad =
            computeBillboardSpinRadLocal(pass, timingState.localAge01) +
            computeBillboardSpinJitterRadLocal(
                pass,
                ring.randomSeed,
                static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu);
        if (std::abs(spinRad) > 0.0001f) {
            billboardRot *= glm::angleAxis(spinRad, meshForwardLocal);
        }

        IRenderBackend::WorldMeshInstance instance;
        instance.modelMatrix = toModelMatrixArrayLocal(
            glm::translate(glm::mat4(1.0f), glowPos) *
            glm::mat4_cast(billboardRot) *
            glm::scale(glm::mat4(1.0f), finalScale));
        instance.vertexColorMulA = instanceAlpha;
        batch.instances.push_back(std::move(instance));
        sortDepth = std::max(sortDepth, glm::dot(glowPos - cameraWorldPos, glowPos - cameraWorldPos));
        appendedAny = true;
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedMeshCornerBillboardPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const render_model::MeshData &passMesh,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, false);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }

    const float localScaleMul = authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
    const float animatedScale =
        glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
        std::max(0.0f, pass.scaleMul) *
        localScaleMul;
    if (animatedScale <= 0.0001f) return false;

    const float passAlpha = std::clamp(
        timingState.fade * std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
        0.0f,
        1.0f);
    if (passAlpha <= 0.001f) return false;

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 defaultLayoutForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 layoutForwardLocal =
        pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultLayoutForward;
    const glm::vec3 safeLayoutForward =
        (glm::dot(layoutForwardLocal, layoutForwardLocal) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(layoutForwardLocal);
    const glm::vec3 quadForwardLocal(0.0f, 1.0f, 0.0f);

    const auto &sharedIndices = quarterIndicesLocal();
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    if (hasCustomUvTransformLocal(pass)) {
        batch.vertices = makeUvTransformedQuadVerticesLocal(centeredQuadVerticesLocal(), pass);
        batch.geometryCacheKey = makeUvTransformedCenteredQuadGeometryCacheKeyLocal(pass);
    } else {
        const auto &sharedVertices = centeredQuadVerticesLocal();
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.geometryCacheKey = makeCenteredQuadGeometryCacheKeyLocal();
    }

    const glm::vec3 meshCornerAnchor = resolveMeshCornerAnchorLocal(passMesh, pass);
    const auto &descriptors = getCornerBillboardQuadDescriptorsLocal(
        passMesh,
        makeCornerBillboardDescriptorCacheKeyLocal(pass));

    std::vector<glm::vec3> fallbackDirections = makeFallbackDirectionsLocal(pass);
    const std::vector<glm::vec3> *localDirections = &fallbackDirections;
    if (localDirections->empty()) return false;

    std::size_t instanceReserve = 0u;
    for (const auto &desc : descriptors)
        instanceReserve += desc.cornerCount;
    batch.instances.reserve(instanceReserve * localDirections->size());

    const float baseSpinRad = computeBillboardSpinRadLocal(pass, timingState.localAge01);
    float sortDepth = 0.0f;
    bool appendedAny = false;
    std::uint32_t instanceOrdinal = 0u;

    for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
        glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
        if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

        if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
            const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
            const float xyLen = glm::length(baseXY);
            if (xyLen > 0.0001f) {
                const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                const float delta =
                    glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                const float angle = baseAngle + delta;
                localDirBasisRaw.x = std::cos(angle) * xyLen;
                localDirBasisRaw.y = std::sin(angle) * xyLen;
            }
        }

        float instanceAlpha = passAlpha;
        if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
            const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
            const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
            const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
            instanceAlpha *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
        }
        instanceAlpha = std::clamp(instanceAlpha, 0.0f, 1.0f);
        if (instanceAlpha <= 0.001f) continue;

        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        const glm::vec3 passForward = safeNormalize3Local(worldDir, ringForward);
        const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
        const glm::vec3 radialStartOffset =
            (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
        const glm::vec3 groupOrigin = ring.pos + radialStartOffset + passForward * pass.forwardOffset;
        const glm::quat layoutRot = rotationFromToSafeLocal(safeLayoutForward, passForward);

        for (const auto &desc : descriptors) {
            const glm::vec3 spriteScale(
                std::max(0.0001f, desc.spriteSize * animatedScale * std::max(0.0f, pass.radiusMul)),
                1.0f,
                std::max(0.0001f, desc.spriteSize * animatedScale * std::max(0.0f, pass.thicknessMul)));
            const float groupSpacingScale = std::max(0.0f, pass.meshCornerGroupSpacingScale);
            glm::vec3 localQuadCenter =
                (desc.center - meshCornerAnchor) *
                (animatedScale * std::max(0.0f, pass.meshCornerPositionScale));
            std::array<glm::vec3, 4> rectGroupCorners{
                localQuadCenter - desc.axisU * (desc.halfWidth * animatedScale * groupSpacingScale) +
                    desc.axisV * (desc.halfHeight * animatedScale * groupSpacingScale),
                localQuadCenter + desc.axisU * (desc.halfWidth * animatedScale * groupSpacingScale) +
                    desc.axisV * (desc.halfHeight * animatedScale * groupSpacingScale),
                localQuadCenter - desc.axisU * (desc.halfWidth * animatedScale * groupSpacingScale) -
                    desc.axisV * (desc.halfHeight * animatedScale * groupSpacingScale),
                localQuadCenter + desc.axisU * (desc.halfWidth * animatedScale * groupSpacingScale) -
                    desc.axisV * (desc.halfHeight * animatedScale * groupSpacingScale),
            };
            if (pass.meshCornerFlattenToLayoutPlane) {
                localQuadCenter = projectOntoPlaneLocal(localQuadCenter, safeLayoutForward);
                for (auto &corner : rectGroupCorners) {
                    corner = projectOntoPlaneLocal(corner, safeLayoutForward);
                }
            }
            for (std::size_t cornerIndex = 0; cornerIndex < desc.cornerCount; ++cornerIndex) {
                glm::vec3 localCorner;
                if (pass.meshCornerReconstructRect && cornerIndex < rectGroupCorners.size()) {
                    localCorner = rectGroupCorners[cornerIndex];
                } else {
                    localCorner =
                        (desc.corners[cornerIndex] - meshCornerAnchor) *
                        (animatedScale * std::max(0.0f, pass.meshCornerPositionScale));
                    if (pass.meshCornerFlattenToLayoutPlane) {
                        localCorner = projectOntoPlaneLocal(localCorner, safeLayoutForward);
                    }
                }
                const glm::vec3 billboardPos = groupOrigin + (layoutRot * localCorner);
                const glm::vec3 facingDir = pass.cameraFacing
                                                ? safeNormalize3Local(cameraWorldPos - billboardPos, passForward)
                                                : passForward;
                glm::quat billboardRot = rotationFromToSafeLocal(quadForwardLocal, facingDir);
                const float spinRad =
                    baseSpinRad +
                    computeBillboardSpinJitterRadLocal(pass, ring.randomSeed, instanceOrdinal * 0x85ebca6bu);
                if (std::abs(spinRad) > 0.0001f) {
                    billboardRot *= glm::angleAxis(spinRad, quadForwardLocal);
                }

                IRenderBackend::WorldMeshInstance instance;
                instance.modelMatrix = toModelMatrixArrayLocal(
                    glm::translate(glm::mat4(1.0f), billboardPos) *
                    glm::mat4_cast(billboardRot) *
                    glm::scale(glm::mat4(1.0f), spriteScale));
                instance.vertexColorMulA = instanceAlpha;
                batch.instances.push_back(std::move(instance));
                sortDepth =
                    std::max(sortDepth, glm::dot(billboardPos - cameraWorldPos, billboardPos - cameraWorldPos));
                appendedAny = true;
                ++instanceOrdinal;
            }
        }
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedQuarterPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3> *localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->size() != 1u) return false;

    const glm::vec3 localDirBasisRaw = (*localDirections)[0];
    if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) return false;

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;

    const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
    const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
    if (glm::dot(worldDir, worldDir) <= 0.000001f) return false;

    const glm::vec3 passForward = glm::normalize(worldDir);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const glm::vec3 passPos = ring.pos + passForward * pass.forwardOffset + radialStartOffset;
    const glm::vec3 facingDir = pass.cameraFacing
                                    ? safeNormalize3Local(cameraWorldPos - passPos, ringForward)
                                    : passForward;
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, facingDir);
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;

    const auto &sharedVertices = quarterVerticesLocal();
    const auto &sharedIndices = quarterIndicesLocal();
    const int quarterCount = std::max(1, pass.quarterCount);
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeQuarterGeometryCacheKeyLocal();
    batch.instances.reserve(static_cast<std::size_t>(quarterCount * timingPlan.sequenceLoopCount));
    bool appendedAny = false;
    for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
        authored::PassTimingState timingState;
        if (!authored::evaluatePassTiming(
                pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
            continue;
        }

        const float passAlpha = std::clamp(
            timingState.fade * std::max(0.0f, pass.alphaMul) *
                authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
            0.0f,
            1.0f);
        if (passAlpha <= 0.001f) continue;
        const float localScaleMul =
            authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

        const float animatedScale =
            timingPlan.repeatedSequencePass
                ? computeQuarterAnimatedScaleLocal(
                      pass,
                      ring,
                      timingPlan.rawSequenceCount,
                      timingState.globalAge01,
                      timingState.localAge01)
                : (glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                   std::max(0.0f, pass.scaleMul) *
                   localScaleMul);
        if (animatedScale <= 0.0001f) continue;
        const glm::vec3 passAnimatedScaleMul =
            authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
        const glm::vec3 finalScale = glm::vec3(animatedScale) * passAnimatedScaleMul * axisScale;

        for (int i = 0; i < quarterCount; ++i) {
            const float quarterDeg =
                pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
            const glm::quat quarterRot = glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot * quarterRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulA = passAlpha;
            batch.instances.push_back(std::move(instance));
            appendedAny = true;
        }
    }
    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = distSq;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendSharedLinePassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const authored::TevState &passTev,
    const render_model::MeshData &passMesh,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto &ring = snapshot.rings.front();
    const float life = std::max(0.0001f, ring.lifeSec);
    const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);
    const float scale =
        glm::mix(ring.startScale, ring.endScale, age01) * std::max(0.0f, pass.scaleMul);
    if (scale <= 0.0001f) return false;

    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    float fade = 1.0f;
    if (age01 > fadeStart) {
        const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    if (fade <= 0.001f) return false;

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint =
        glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                   glm::vec3(0.0f),
                   glm::vec3(1.0f));

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3> *localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->empty()) return false;

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec2 meshForwardRange = meshProjectionRangeLocal(passMesh, meshForwardLocal);
    const float meshForwardScale = glm::length(axisScale * glm::abs(meshForwardLocal));

    const std::string geometryCacheKey = makeLineGeometryCacheKeyLocal(pass, passTev.k1a);
    const SharedMeshGeometry &sharedGeometry =
        getSharedLineMeshGeometryLocal(passMesh, geometryCacheKey, passTev.k1a);
    if (sharedGeometry.vertices.empty() || sharedGeometry.indices.size() < 3u) return false;

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedGeometry.vertices.data();
    batch.sharedVertexCount = sharedGeometry.vertices.size();
    batch.sharedIndices = sharedGeometry.indices.data();
    batch.sharedIndexCount = sharedGeometry.indices.size();
    batch.geometryCacheKey = geometryCacheKey;
    const int rawSequenceCount = std::max(1, pass.sequenceCount);
    const int delayedSequenceIndex =
        (rawSequenceCount > 1) ? std::clamp(pass.sequenceIndex, -1, rawSequenceCount - 1) : -1;
    const bool delayedSinglePass = delayedSequenceIndex >= 0;
    const int sequenceLoopCount = delayedSinglePass ? 1 : rawSequenceCount;
    batch.instances.reserve(localDirections->size() * static_cast<std::size_t>(sequenceLoopCount));
    float sortDepth = 0.0f;
    bool appendedAny = false;
    for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
        glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
        if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

        if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
            const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
            const float xyLen = glm::length(baseXY);
            if (xyLen > 0.0001f) {
                const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                const float delta =
                    glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                const float angle = baseAngle + delta;
                localDirBasisRaw.x = std::cos(angle) * xyLen;
                localDirBasisRaw.y = std::sin(angle) * xyLen;
            }
        }

        float lineAlphaMul =
            std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec);
        if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
            const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
            const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
            const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
            lineAlphaMul *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
        }
        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

        const glm::vec3 passForward = glm::normalize(worldDir);
        const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
        const float radialDistanceMul =
            resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, 0);
        const float radialRadius =
            pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
        const glm::vec3 radialStartOffset =
            (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
        const glm::vec3 passPosBase =
            ring.pos + passForward * pass.forwardOffset + radialStartOffset;
        for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
            const int sequenceIndex = delayedSinglePass ? delayedSequenceIndex : sequenceOrdinal;
            float localAge01 = age01;
            float localFade = 1.0f;
            if (delayedSinglePass) {
                if (!computeDelayedPassLaunchStateLocal(
                        pass, rawSequenceCount, age01, sequenceIndex, localAge01)) {
                    continue;
                }
                if (pass.sequenceFadeLocal) {
                    if (localAge01 >= 0.999f) {
                        localFade = 0.0f;
                    } else if (localAge01 > fadeStart) {
                        const float t =
                            (localAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                        localFade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
                    }
                    if (localFade <= 0.001f) continue;
                } else {
                    localFade = computeSharedDelayedFadeLocal(age01, fadeStart);
                    if (localFade <= 0.001f) continue;
                }
            } else if (!computePassSequenceStateLocal(
                           pass, rawSequenceCount, age01, fadeStart, sequenceIndex, localAge01, localFade)) {
                continue;
            }

            const float animatedScale =
                glm::mix(ring.startScale, ring.endScale, localAge01) * std::max(0.0f, pass.scaleMul);
            if (animatedScale <= 0.0001f) continue;
            const glm::vec3 passAnimatedScaleMul =
                authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
            const glm::vec3 finalScale =
                glm::vec3(animatedScale) * passAnimatedScaleMul * axisScale;
            const float tailAnchorOffset =
                -meshForwardRange.x * animatedScale * meshForwardScale;
            const float forwardTravel =
                ((delayedSinglePass || rawSequenceCount > 1))
                    ? (animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                       glm::clamp(localAge01, 0.0f, 1.0f))
                    : 0.0f;
            const glm::vec3 passPos =
                passPosBase + passForward * (tailAnchorOffset + forwardTravel);
            const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
            sortDepth = std::max(sortDepth, distSq);

            const float passAlpha = std::clamp(localFade * lineAlphaMul, 0.0f, 1.0f);
            if (passAlpha <= 0.001f) continue;

            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulR = passTint.r;
            instance.vertexColorMulG = passTint.g;
            instance.vertexColorMulB = passTint.b;
            instance.vertexColorMulA = passAlpha;
            batch.instances.push_back(std::move(instance));
            appendedAny = true;
        }
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendSharedStreakQuadPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const authored::TevState &passTev,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto &ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint =
        glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                   glm::vec3(0.0f),
                   glm::vec3(1.0f));

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const auto &authoredSegments = authored::resolveAuthoredStreakSegments(pass);
    if (!authoredSegments.empty()) {
        const float radiusMul = std::max(0.0f, pass.radiusMul);
        const float thicknessMul = std::max(0.0f, pass.thicknessMul);
        const float visualScaleMul = std::max(0.0f, pass.scaleMul);
        const bool centerOrigin = pass.authoredSegmentCenterOrigin;
        const float positionScale = std::max(0.0f, pass.authoredSegmentPositionScale);
        const float lengthScale = std::max(0.0f, pass.authoredSegmentLengthScale);

        shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
        const auto &sharedVertices = streakQuadVerticesLocal();
        const auto &sharedIndices = quarterIndicesLocal();
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.sharedIndices = sharedIndices.data();
        batch.sharedIndexCount = sharedIndices.size();
        batch.geometryCacheKey = makeStreakQuadGeometryCacheKeyLocal(passTev.k1a);

        batch.instances.reserve(authoredSegments.size() * static_cast<std::size_t>(timingPlan.sequenceLoopCount));
        float sortDepth = 0.0f;
        bool appendedAny = false;
        for (const auto &segment : authoredSegments) {
            for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
                authored::PassTimingState timingState;
                if (!authored::evaluatePassTiming(
                        pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                    continue;
                }

                const float localScaleMul =
                    authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
                const float spreadScale =
                    glm::mix(ring.startScale, ring.endScale, timingState.localAge01) * localScaleMul;
                if (spreadScale <= 0.0001f || visualScaleMul <= 0.0001f) continue;

                const glm::vec3 segmentDirection = authored::resolveAuthoredStreakDirection(segment);
                const float segmentLengthBase = authored::resolveAuthoredStreakLength(segment);
                if (segmentLengthBase <= 0.0001f) continue;
                const float lengthDecay =
                    authored::resolveAuthoredDecayFactor(
                        pass,
                        timingState.localAge01,
                        ring.lifeSec,
                        pass.authoredSegmentLengthDecayPerFrame);
                const float alphaDecay =
                    authored::resolveAuthoredDecayFactor(
                        pass,
                        timingState.localAge01,
                        ring.lifeSec,
                        pass.authoredSegmentAlphaDecayPerFrame);
                const float widthDecay = lengthDecay;

                glm::vec3 localStart(0.0f);
                glm::vec3 localVector(0.0f);
                if (centerOrigin) {
                    const float travelDistance =
                        authored::resolveAuthoredStreakTravelDistance(
                            pass,
                            timingState.localAge01,
                            ring.lifeSec);
                    localStart = segmentDirection * travelDistance;
                    localVector =
                        segmentDirection *
                        (segmentLengthBase * lengthScale * lengthDecay);
                } else {
                    localStart = segment.startLocal * (spreadScale * positionScale);
                    localVector =
                        (segment.endLocal - segment.startLocal) *
                        (spreadScale * lengthScale * lengthDecay);
                }
                const glm::vec3 localEnd = localStart + localVector;
                const float segmentLength = glm::length(localVector);
                if (segmentLength <= 0.0001f) continue;
                const float visibilityFade =
                    authored::resolveAuthoredStreakVisibilityFade(
                        pass,
                        localStart);
                if (visibilityFade <= 0.001f) continue;

                const glm::vec3 worldStart =
                    ring.pos +
                    right * localStart.x +
                    up * localStart.y +
                    ringForward * (localStart.z + pass.forwardOffset);
                const glm::vec3 worldEnd =
                    ring.pos +
                    right * localEnd.x +
                    up * localEnd.y +
                    ringForward * (localEnd.z + pass.forwardOffset);
                const glm::vec3 worldVector = worldEnd - worldStart;
                if (glm::dot(worldVector, worldVector) <= 0.000001f) continue;

                const glm::vec3 passForward = glm::normalize(worldVector);
                const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);

                IRenderBackend::WorldMeshInstance instance;
                instance.modelMatrix = toModelMatrixArrayLocal(
                    glm::translate(glm::mat4(1.0f), worldStart) *
                    glm::mat4_cast(passRot) *
                    glm::scale(
                        glm::mat4(1.0f),
                        glm::vec3(
                            visualScaleMul * radiusMul * widthDecay,
                            visualScaleMul * radiusMul * widthDecay,
                            visualScaleMul * thicknessMul * segmentLength)));
                instance.vertexColorMulR = passTint.r;
                instance.vertexColorMulG = passTint.g;
                instance.vertexColorMulB = passTint.b;
                instance.vertexColorMulA = std::clamp(
                    timingState.fade * std::max(0.0f, pass.alphaMul) *
                        authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec) *
                        std::max(0.0f, segment.alphaMul) * alphaDecay * lengthDecay * visibilityFade,
                    0.0f,
                    1.0f);
                batch.instances.push_back(std::move(instance));
                const glm::vec3 worldMid = 0.5f * (worldStart + worldEnd);
                sortDepth =
                    std::max(sortDepth, glm::dot(worldMid - cameraWorldPos, worldMid - cameraWorldPos));
                appendedAny = true;
            }
        }

        if (!appendedAny || batch.instances.empty()) return false;
        batch.sortDepth = sortDepth;
        outBatches.push_back(std::move(batch));
        return true;
    }

    std::vector<glm::vec3> fallbackDirections = makeFallbackDirectionsLocal(pass);
    const std::vector<glm::vec3> *localDirections = &fallbackDirections;
    if (localDirections->empty()) return false;

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    const auto &sharedVertices = streakQuadVerticesLocal();
    const auto &sharedIndices = quarterIndicesLocal();
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeStreakQuadGeometryCacheKeyLocal(passTev.k1a);

    batch.instances.reserve(localDirections->size() * static_cast<std::size_t>(timingPlan.sequenceLoopCount));
    float sortDepth = 0.0f;
    bool appendedAny = false;
    for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
        glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
        if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

        if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
            const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
            const float xyLen = glm::length(baseXY);
            if (xyLen > 0.0001f) {
                const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                const float delta =
                    glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                const float angle = baseAngle + delta;
                localDirBasisRaw.x = std::cos(angle) * xyLen;
                localDirBasisRaw.y = std::sin(angle) * xyLen;
            }
        }

        float lineAlphaMul =
            std::max(0.0f, pass.alphaMul) *
            authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec);
        if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
            const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
            const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
            const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
            lineAlphaMul *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
        }
        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

        const glm::vec3 passForward = glm::normalize(worldDir);
        const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
        for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
            authored::PassTimingState timingState;
            if (!authored::evaluatePassTiming(
                    pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                continue;
            }
            const float localScaleMul =
                authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

            const float animatedScale =
                glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                std::max(0.0f, pass.scaleMul) *
                localScaleMul;
            if (animatedScale <= 0.0001f) continue;
            const glm::vec3 passAnimatedScaleMul =
                authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
            const glm::vec3 finalScale =
                glm::vec3(animatedScale) * passAnimatedScaleMul * axisScale;
            const float radialDistanceMul =
                resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, sequenceOrdinal);
            const float radialRadius =
                pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
            const glm::vec3 radialStartOffset =
                (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
            const glm::vec3 passPosBase =
                ring.pos + passForward * pass.forwardOffset + radialStartOffset;
            const float forwardTravel =
                animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                glm::clamp(timingState.localAge01, 0.0f, 1.0f);
            const glm::vec3 passPos = passPosBase + passForward * forwardTravel;

            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulR = passTint.r;
            instance.vertexColorMulG = passTint.g;
            instance.vertexColorMulB = passTint.b;
            instance.vertexColorMulA = std::clamp(timingState.fade * lineAlphaMul, 0.0f, 1.0f);
            batch.instances.push_back(std::move(instance));
            sortDepth = std::max(sortDepth, glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos));
            appendedAny = true;
        }
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendDynamicPassBatchLocal(
    std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const authored::TevState &passTev,
    const render_model::MeshData *passMesh,
    const TextureView &texture,
    const glm::vec3 &cameraWorldPos) {
    const bool drawQuarterRing = pass.textureQuarterRing;
    const bool quarterTextureBake =
        authored::usesQuarterTextureBake(snapshot.config, pass);
    const bool drawLinePass = authored::isLinePass(snapshot.config, pass);
    const bool glowBillboardPass = authored::isGlowBillboardPass(pass);
    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);

    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint = drawLinePass
                                   ? glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                                                glm::vec3(0.0f),
                                                glm::vec3(1.0f))
                                   : glm::vec3(1.0f, 1.0f, 1.0f);

    float sortDepth = 0.0f;
    bool hasGeometry = false;
    bool usesInstancedGeometry = false;

    for (const auto &ring : snapshot.rings) {
        const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
        right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 up = glm::cross(ringForward, right);
        up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

        const float radiusMul = std::max(0.0f, pass.radiusMul);
        const float thicknessMul = std::max(0.0f, pass.thicknessMul);
        const glm::vec3 axisScale =
            glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
        const auto timingPlan = authored::planPassTiming(pass, drawQuarterRing || drawLinePass);
        const int sequenceLoopCount = timingPlan.sequenceLoopCount;
        const auto appendQuarterClusterAt = [&](const glm::vec3 &clusterPos,
                                                const glm::quat &clusterRot,
                                                const glm::vec3 &finalScale,
                                                const glm::vec4 &color) {
            const bool touchingQuarterLayout = usesTouchingQuarterLayoutLocal(pass);
            const glm::vec3 clusterBaseOffsetLocal(0.5f, 0.0f, 0.5f);
            const int quarterCount = std::max(1, pass.quarterCount);
            for (int i = 0; i < quarterCount; ++i) {
                const float quarterDeg =
                    pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
                const glm::quat quarterRot =
                    glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
                const glm::quat pieceRot =
                    glm::angleAxis(
                        glm::radians(quarterDeg + pass.quarterRotationOffsetDeg),
                        meshForwardLocal);
                if (touchingQuarterLayout) {
                    const glm::vec3 pieceOffsetLocal = quarterRot * clusterBaseOffsetLocal;
                    const glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), clusterPos) *
                        glm::mat4_cast(clusterRot) *
                        glm::scale(glm::mat4(1.0f), finalScale) *
                        glm::translate(glm::mat4(1.0f), pieceOffsetLocal) *
                        glm::mat4_cast(pieceRot);
                    appendQuarterQuadLocal(batch, world, color, pass, true);
                } else {
                    const glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), clusterPos) *
                        glm::mat4_cast(clusterRot * pieceRot) *
                        glm::scale(glm::mat4(1.0f), finalScale);
                    appendQuarterQuadLocal(batch, world, color, pass, false);
                }
                hasGeometry = true;
            }
        };
        const bool hasAuthoredQuarterBillboards =
            drawQuarterRing && !pass.authoredBillboardsLocal.empty();

        if (hasAuthoredQuarterBillboards) {
            for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
                authored::PassTimingState timingState;
                if (!authored::evaluatePassTiming(
                        pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                    continue;
                }

                float passAlphaScale = std::clamp(
                    timingState.fade * std::max(0.0f, pass.alphaMul) *
                        authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec),
                    0.0f,
                    1.0f);
                if (!quarterTextureBake) {
                    passAlphaScale *= passTev.k1a;
                }
                const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
                if (passAlpha <= 0.001f) continue;

                const float localScaleMul =
                    authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
                const float animatedScale = timingPlan.repeatedSequencePass
                                                ? computeQuarterAnimatedScaleLocal(
                                                      pass,
                                                      ring,
                                                      timingPlan.rawSequenceCount,
                                                      timingState.globalAge01,
                                                      timingState.localAge01)
                                                : (glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                                                   std::max(0.0f, pass.scaleMul) *
                                                   localScaleMul);
                if (animatedScale <= 0.0001f) continue;

                for (const auto &authored : pass.authoredBillboardsLocal) {
                    const float instanceScaleMul = std::max(0.0f, authored.scaleMul);
                    const float instanceAlpha =
                        std::clamp(passAlpha * std::max(0.0f, authored.alphaMul), 0.0f, 1.0f);
                    if (instanceScaleMul <= 0.0001f || instanceAlpha <= 0.001f) continue;

                    const glm::vec3 localPos =
                        authored.positionLocal * pass.authoredBillboardPositionScale;
                    const glm::vec3 passPos =
                        ring.pos +
                        right * localPos.x +
                        up * localPos.y +
                        ringForward * localPos.z;
                    const glm::vec3 facingDir = pass.cameraFacing
                                                    ? safeNormalize3Local(cameraWorldPos - passPos, ringForward)
                                                    : ringForward;
                    const glm::quat orientedPassRot =
                        rotationFromToSafeLocal(meshForwardLocal, facingDir);
                    const glm::vec3 passAnimatedScaleMul =
                        authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
                    const glm::vec3 finalScale =
                        glm::vec3(animatedScale * instanceScaleMul) * passAnimatedScaleMul * axisScale;
                    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
                    sortDepth = std::max(sortDepth, distSq);
                    appendQuarterClusterAt(passPos,
                                           orientedPassRot,
                                           finalScale,
                                           glm::vec4(passTint, instanceAlpha));
                }
            }
            continue;
        }

        const bool hasAuthoredGlowBillboards =
            glowBillboardPass && !drawQuarterRing && !pass.authoredBillboardsLocal.empty();
        if (hasAuthoredGlowBillboards) {
            if (!usesInstancedGeometry) {
                const auto &sharedIndices = quarterIndicesLocal();
                batch.sharedIndices = sharedIndices.data();
                batch.sharedIndexCount = sharedIndices.size();
                if (hasCustomUvTransformLocal(pass)) {
                    batch.vertices = makeUvTransformedQuadVerticesLocal(centeredQuadVerticesLocal(), pass);
                    batch.geometryCacheKey = makeUvTransformedCenteredQuadGeometryCacheKeyLocal(pass);
                } else {
                    const auto &sharedVertices = centeredQuadVerticesLocal();
                    batch.sharedVertices = sharedVertices.data();
                    batch.sharedVertexCount = sharedVertices.size();
                    batch.geometryCacheKey = makeCenteredQuadGeometryCacheKeyLocal();
                }
                usesInstancedGeometry = true;
            }

            const BillboardFacingModeLocal facingMode = resolveBillboardFacingModeLocal(pass);
            const bool facingOverride = facingMode != BillboardFacingModeLocal::None;
            const bool useAttackPlane = facingMode == BillboardFacingModeLocal::AttackPlane;
            const bool useSharedFacing =
                facingMode == BillboardFacingModeLocal::Shared ||
                facingMode == BillboardFacingModeLocal::SharedUpright ||
                facingMode == BillboardFacingModeLocal::AttackPlane;
            const bool useUprightFacing =
                facingMode == BillboardFacingModeLocal::CameraUpright ||
                facingMode == BillboardFacingModeLocal::SharedUpright ||
                facingMode == BillboardFacingModeLocal::AttackPlane;

            for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
                authored::PassTimingState timingState;
                if (!authored::evaluatePassTiming(
                        pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                    continue;
                }

                const float passAnimatedAlphaMul =
                    authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec);
                const float passAlpha = std::clamp(
                    timingState.fade * std::max(0.0f, pass.alphaMul) * passAnimatedAlphaMul,
                    0.0f,
                    1.0f);
                if (passAlpha <= 0.001f) continue;

                const float localScaleMul =
                    authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
                const bool localTimedPass =
                    timingPlan.explicitTimeWindow ||
                    timingPlan.delayedSinglePass ||
                    timingPlan.repeatedSequencePass;
                const float animatedScale =
                    glm::mix(ring.startScale, ring.endScale, localTimedPass ? timingState.localAge01
                                                                            : timingState.globalAge01) *
                    std::max(0.0f, pass.scaleMul) *
                    (localTimedPass ? localScaleMul : 1.0f);
                if (animatedScale <= 0.0001f) continue;

                glm::quat sharedFacingRot(1.0f, 0.0f, 0.0f, 0.0f);
                if (facingOverride && useSharedFacing) {
                    if (useAttackPlane) {
                        sharedFacingRot = buildPlaneAlignedRotationLocal(
                            meshForwardLocal,
                            ringForward,
                            glm::vec3(0.0f, 1.0f, 0.0f));
                    } else {
                        const glm::vec3 toCamera =
                            safeNormalize3Local(cameraWorldPos - ring.pos, ringForward);
                        sharedFacingRot =
                            useUprightFacing
                                ? buildPlaneAlignedRotationLocal(
                                      meshForwardLocal,
                                      toCamera,
                                      glm::vec3(0.0f, 1.0f, 0.0f))
                                : rotationFromToSafeLocal(meshForwardLocal, toCamera);
                    }
                }

                const float baseSpinRad =
                    computeBillboardSpinRadLocal(pass, localTimedPass ? timingState.localAge01
                                                                   : timingState.globalAge01);
                float offsetAgeSec = ring.ageSec;
                if (pass.timeEndSec >= 0.0f) {
                    const float window = std::max(0.0f, pass.timeEndSec - pass.timeStartSec);
                    if (window > 0.0001f) {
                        offsetAgeSec = glm::clamp(ring.ageSec - pass.timeStartSec, 0.0f, window);
                    } else {
                        offsetAgeSec = 0.0f;
                    }
                }

                std::uint32_t authoredIndex = 0u;
                for (const auto &authored : pass.authoredBillboardsLocal) {
                    const std::size_t groupIndex = authoredIndex / 4u;
                    const glm::vec3 offset =
                        resolveAuthoredBillboardOffsetLocal(pass, groupIndex, offsetAgeSec);
                    const glm::vec2 scaleAnim =
                        resolveAuthoredBillboardScaleLocal(pass, groupIndex, offsetAgeSec);
                    const float spinAnimDeg =
                        resolveAuthoredBillboardSpinDeltaDegLocal(pass, groupIndex, offsetAgeSec);
                    const glm::vec3 localPos =
                        (authored.positionLocal + offset) * pass.authoredBillboardPositionScale;
                    const glm::vec3 passPos =
                        ring.pos +
                        right * localPos.x +
                        up * localPos.y +
                        ringForward * localPos.z;

                    glm::quat billboardRot(1.0f, 0.0f, 0.0f, 0.0f);
                    if (!facingOverride) {
                        const glm::vec3 toCamera =
                            safeNormalize3Local(cameraWorldPos - passPos, ringForward);
                        billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
                    } else if (useSharedFacing) {
                        billboardRot = sharedFacingRot;
                    } else {
                        const glm::vec3 toCamera =
                            safeNormalize3Local(cameraWorldPos - passPos, ringForward);
                        billboardRot =
                            useUprightFacing
                                ? buildPlaneAlignedRotationLocal(
                                      meshForwardLocal,
                                      toCamera,
                                      glm::vec3(0.0f, 1.0f, 0.0f))
                                : rotationFromToSafeLocal(meshForwardLocal, toCamera);
                    }

                    const float spinRad =
                        baseSpinRad +
                        computeBillboardSpinJitterRadLocal(
                            pass,
                            ring.randomSeed,
                            authoredIndex * 0x85ebca6bu);
                    const float instanceSpinRad = glm::radians(authored.spinDeg + spinAnimDeg);
                    if (std::abs(spinRad + instanceSpinRad) > 0.0001f) {
                        billboardRot *= glm::angleAxis(spinRad + instanceSpinRad, meshForwardLocal);
                    }

                    IRenderBackend::WorldMeshInstance instance;
                    const glm::vec3 authoredScale(
                        std::max(0.0f, authored.scaleXMul * scaleAnim.x),
                        1.0f,
                        std::max(0.0f, authored.scaleYMul * scaleAnim.y));
                    const glm::vec3 passAnimatedScaleMul =
                        authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
                    instance.modelMatrix = toModelMatrixArrayLocal(
                        glm::translate(glm::mat4(1.0f), passPos) *
                        glm::mat4_cast(billboardRot) *
                        glm::scale(
                            glm::mat4(1.0f),
                            glm::vec3(animatedScale) * passAnimatedScaleMul *
                                std::max(0.0f, authored.scaleMul) *
                                authoredScale * axisScale));
                    instance.vertexColorMulA =
                        std::clamp(passAlpha * std::max(0.0f, authored.alphaMul), 0.0f, 1.0f);
                    batch.instances.push_back(std::move(instance));
                    sortDepth = std::max(sortDepth, glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos));
                    hasGeometry = true;
                    ++authoredIndex;
                }
            }
            continue;
        }

        std::vector<glm::vec3> localDirectionsFallback = makeFallbackDirectionsLocal(pass);
        const std::vector<glm::vec3> *localDirections = &localDirectionsFallback;
        if (localDirections->empty()) continue;

        for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
            glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
            if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

            if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
                const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
                const float xyLen = glm::length(baseXY);
                if (xyLen > 0.0001f) {
                    const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                    const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                    const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                    const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                    const float delta =
                        glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                    const float angle = baseAngle + delta;
                    localDirBasisRaw.x = std::cos(angle) * xyLen;
                    localDirBasisRaw.y = std::sin(angle) * xyLen;
                }
            }

            float lineAlphaMul =
                std::max(0.0f, pass.alphaMul) *
                authored::resolvePassAnimatedAlphaMul(pass, ring.ageSec);
            if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
                lineAlphaMul *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
            }
            const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
            const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
            if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

            const glm::vec3 passForward = glm::normalize(worldDir);
            const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
            const float radialDistanceMul =
                resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, 0);
            const float radialRadius =
                pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
            const glm::vec3 radialStartOffset =
                (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
            const glm::vec3 passPosBase = ring.pos + radialStartOffset;

            for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
                authored::PassTimingState timingState;
                if (!authored::evaluatePassTiming(
                        pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                    continue;
                }

                float passAlphaScale = std::clamp(timingState.fade * lineAlphaMul, 0.0f, 1.0f);
                if (!quarterTextureBake && !drawLinePass) {
                    passAlphaScale *= passTev.k1a;
                }
                const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
                if (passAlpha <= 0.001f) continue;

                const float localScaleMul =
                    authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
                const float animatedScale = drawQuarterRing
                                                ? (timingPlan.repeatedSequencePass
                                                       ? computeQuarterAnimatedScaleLocal(
                                                             pass,
                                                             ring,
                                                             timingPlan.rawSequenceCount,
                                                             timingState.globalAge01,
                                                             timingState.localAge01)
                                                       : (glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                                                          std::max(0.0f, pass.scaleMul) *
                                                          localScaleMul))
                                                : ((drawLinePass && (timingPlan.repeatedSequencePass || timingPlan.delayedSinglePass))
                                                       ? (glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                                                          std::max(0.0f, pass.scaleMul))
                                                       : (glm::mix(ring.startScale,
                                                                   ring.endScale,
                                                                   timingPlan.delayedSinglePass ? timingState.localAge01 : timingState.globalAge01) *
                                                          std::max(0.0f, pass.scaleMul)));
                if (animatedScale <= 0.0001f) continue;
                const glm::vec3 passAnimatedScaleMul =
                    authored::resolvePassAnimatedScaleMul(pass, ring.ageSec);
                const glm::vec3 finalScale =
                    glm::vec3(animatedScale) * passAnimatedScaleMul * axisScale;
                const float forwardTravel =
                    (drawLinePass && (timingPlan.repeatedSequencePass || timingPlan.delayedSinglePass))
                        ? (pass.forwardOffset +
                           animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                               glm::clamp(timingState.localAge01, 0.0f, 1.0f))
                        : ((timingPlan.delayedSinglePass && !drawQuarterRing)
                               ? (pass.forwardOffset * fastLaunch01Local(timingState.localAge01))
                               : pass.forwardOffset);
                const glm::vec3 passPos = passPosBase + passForward * forwardTravel;
                const glm::vec3 facingDir = (drawQuarterRing && pass.cameraFacing)
                                                ? safeNormalize3Local(cameraWorldPos - passPos, ringForward)
                                                : passForward;
                const glm::quat orientedPassRot =
                    rotationFromToSafeLocal(meshForwardLocal, facingDir);
                const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
                sortDepth = std::max(sortDepth, distSq);

                const glm::vec4 color(passTint, passAlpha);
                if (drawQuarterRing) {
                    appendQuarterClusterAt(passPos, orientedPassRot, finalScale, color);
                } else if (passMesh) {
                    const glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), passPos) *
                        glm::mat4_cast(orientedPassRot) *
                        glm::scale(glm::mat4(1.0f), finalScale);
                    appendTransformedMeshLocal(
                        batch, *passMesh, world, color, drawLinePass, passTev.k1a);
                    hasGeometry = true;
                }
            }
        }
    }

    const bool hasInstancedQuadGeometry =
        ((batch.sharedVertices != nullptr && batch.sharedVertexCount > 0u) ||
         !batch.vertices.empty()) &&
        ((batch.sharedIndices != nullptr && batch.sharedIndexCount > 0u) || !batch.indices.empty()) &&
        !batch.instances.empty();
    const bool hasExplicitGeometry = !batch.vertices.empty() && !batch.indices.empty();
    if (!hasGeometry || (!hasExplicitGeometry && !hasInstancedQuadGeometry)) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

} // namespace

bool appendPassBatch(std::vector<shared_world_batches::WorldIndexedBatch> &outBatches,
                     const SharedAuthoredBatchVFX::RenderSnapshot &snapshot,
                     const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                     const authored::TevState &passTev,
                     const render_model::MeshData *passMesh,
                     const TextureView &texture,
                     const glm::vec3 &cameraWorldPos) {
    if (!pass.enabled) return false;
    if (snapshot.rings.empty()) return false;
    if (texture.rgba == nullptr || texture.width <= 0 || texture.height <= 0) return false;

    const bool drawQuarterRing = pass.textureQuarterRing;
    const bool glowBillboardPass = authored::isGlowBillboardPass(pass);
    const bool streakQuadPass = authored::isStreakQuadPass(pass);
    if (!drawQuarterRing && !glowBillboardPass && !streakQuadPass && passMesh == nullptr) return false;

    const bool drawLinePass = authored::isLinePass(snapshot.config, pass);
    const bool singleRingSnapshot = snapshot.rings.size() == 1u;
    if (drawLinePass && streakQuadPass && singleRingSnapshot) {
        return appendSharedStreakQuadPassSingleRingLocal(
            outBatches, snapshot, pass, passTev, texture, cameraWorldPos);
    }
    const bool sparkleMeshPass = authored::isSparkleMeshPass(pass);
    const bool meshCornerBillboardPass = authored::isMeshCornerBillboardPass(pass);
    if (drawLinePass && passMesh && singleRingSnapshot) {
        return appendSharedLinePassSingleRingLocal(
            outBatches, snapshot, pass, passTev, *passMesh, texture, cameraWorldPos);
    }

    if (sparkleMeshPass && passMesh && singleRingSnapshot) {
        return appendSharedSparkleBillboardPassSingleRingLocal(
            outBatches, snapshot, pass, *passMesh, texture, cameraWorldPos);
    }

    if (meshCornerBillboardPass && passMesh && singleRingSnapshot) {
        return appendSharedMeshCornerBillboardPassSingleRingLocal(
            outBatches, snapshot, pass, *passMesh, texture, cameraWorldPos);
    }

    if (glowBillboardPass && singleRingSnapshot) {
        return appendSharedGlowBillboardPassSingleRingLocal(
            outBatches, snapshot, pass, texture, cameraWorldPos);
    }

    if (!drawLinePass && passMesh &&
        appendSharedMeshPassSingleRingLocal(
            outBatches, snapshot, pass, passTev, *passMesh, texture, cameraWorldPos)) {
        return true;
    }

    const bool defaultQuarterLayout = !usesTouchingQuarterLayoutLocal(pass);
    const bool defaultQuarterUvs = hasDefaultQuarterUvTransformLocal(pass);
    if (drawQuarterRing && singleRingSnapshot &&
        (pass.directionsLocal.empty() || pass.directionsLocal.size() == 1u) &&
        pass.directionSpacingJitterDeg <= 0.0001f &&
        defaultQuarterLayout &&
        defaultQuarterUvs) {
        return appendSharedQuarterPassSingleRingLocal(
            outBatches, snapshot, pass, texture, cameraWorldPos);
    }

    return appendDynamicPassBatchLocal(
        outBatches, snapshot, pass, passTev, passMesh, texture, cameraWorldPos);
}

} // namespace vfx::runtime::authored_batches
