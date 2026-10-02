// src/engine/render/ModelPoseSampler.cpp
//
// Extracted verbatim from Model::buildPoseMatrices so that CPU consumers and
// Model share one implementation. The engine policy follows the original:
//
// Root motion OFF (horizontal):
// - Ignore animated X/Z translation on selected "root motion carrier" nodes.
// - Keep animated Y translation (vertical bob).
// - Keep all rotations/scales, and all other node translations intact.
// This prevents run/walk clips from moving the mesh ahead of instanceTransform
// (grid/world).
//
// CUBICSPLINE (glTF 2.0, "interpolation-cubic") is supported on top of that
// policy: wrapped sample time is clamped into the channel's own key range, the
// raw per-key [in-tangent, value, out-tangent] triplets are evaluated with
// componentwise cubic Hermite interpolation, and malformed cubic data skips the
// channel so the destination keeps its previous local value. LINEAR and STEP
// keep the legacy wrapping and tail behavior above, unchanged.

#include "ModelPoseSampler.h"
#include "AnimationTrackSampler.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::render::animation {
namespace {

using model_types::AnimationSampler;
using model_types::ChannelPath;

float wrapTime(float t, float duration)
{
    if (duration <= 0.0f) return 0.0f;
    float x = std::fmod(t, duration);
    if (x < 0.0f) x += duration;
    if (x == 0.0f && t > 0.0f) {
        return std::nextafter(duration, 0.0f);
    }
    return x;
}

} // namespace

glm::mat4 trsToMat4(const model_types::NodeTRS& node)
{
    if (node.hasMatrix) return node.matrix;
    glm::mat4 T = glm::translate(glm::mat4(1.0f), node.t);
    glm::mat4 R = glm::toMat4(node.r);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), node.s);
    return T * R * S;
}

void samplePose(const PoseSource& source,
                float timeSec,
                int animIndex,
                std::vector<model_types::NodeTRS>& outLocal,
                std::vector<glm::mat4>& outGlobal,
                bool suppressHorizontalRootMotion,
                bool looping)
{
    const auto& nodesDefault = source.nodesDefault;
    const auto& nodeChildren = source.nodeChildren;
    const auto& skins = source.skins;
    const auto& animations = source.animations;
    const auto& nodeNames = source.nodeNames;

    outLocal = nodesDefault;
    outGlobal.assign(nodesDefault.size(), glm::mat4(1.0f));
    if (nodesDefault.empty()) return;

    const int nodeCount = (int)nodesDefault.size();

    // ---- Build parent pointers (best-effort) ----
    std::vector<int> parent(nodeCount, -1);
    for (int p = 0; p < nodeCount; ++p) {
        if (p >= (int)nodeChildren.size()) continue;
        for (int c : nodeChildren[p]) {
            if (c < 0 || c >= nodeCount) continue;
            if (parent[c] == -1) parent[c] = p;
        }
    }

    // ---- Root-motion carrier set (horizontal only) ----
    // We strip animated X/Z translation ONLY on these nodes:
    // - nodes named origin/waist/hips (common PM rigs)
    // - skeleton root joint per skin (joint with no parent among joints)
    // This avoids breaking grid placement (we do NOT touch scene roots or mesh nodes generically).
    std::unordered_set<int> rmNodes;

    auto addNodeByExactName = [&](const char* name) {
        for (int i = 0; i < (int)nodeNames.size(); ++i) {
            if (nodeNames[(size_t)i] == name) {
                if (i >= 0 && i < nodeCount) rmNodes.insert(i);
                return;
            }
        }
    };

    addNodeByExactName("origin");
    addNodeByExactName("waist");
    addNodeByExactName("hips");

    for (const auto& sk : skins) {
        if (sk.joints.empty()) continue;
        std::unordered_set<int> jointSet;
        jointSet.reserve(sk.joints.size());
        for (int j : sk.joints) jointSet.insert(j);

        // Find joints that have no parent in the joint set => skeleton roots.
        for (int j : sk.joints) {
            int pj = (j >= 0 && j < nodeCount) ? parent[j] : -1;
            if (pj == -1 || jointSet.find(pj) == jointSet.end()) {
                rmNodes.insert(j);
            }
        }
    }

    auto sampleVec4 = [looping](const AnimationSampler& sampler, float time, float duration) {
        return sampleVectorTrack(sampler, time, duration, looping);
    };
    auto sampleQuat = [looping](const AnimationSampler& sampler, float time, float duration) {
        return sampleRotationTrack(sampler, time, duration, looping);
    };

    // ---- CUBICSPLINE helpers ----
    // A cubic sampler is well formed when it has at least two keys, exactly
    // three outputs per key, and finite strictly increasing key times with
    // finite relevant components. The count comparison is written so that
    // keys * 3 cannot overflow.
    const auto cubicKeysValid = [](const AnimationSampler& s, bool rotation) {
        const size_t keys = s.inputs.size();
        if (keys < 2u) return false;
        if (keys > s.outputs.size() / 3u) return false;
        if (s.outputs.size() != keys * 3u) return false;
        const int components = rotation ? 4 : 3;
        for (size_t k = 0; k < keys; ++k) {
            const float time = s.inputs[k];
            if (!std::isfinite(time)) return false;
            if (k > 0u && !(time > s.inputs[k - 1u])) return false;
            const size_t base = k * 3u;
            for (int c = 0; c < components; ++c) {
                if (!std::isfinite(s.outputs[base][c]) ||
                    !std::isfinite(s.outputs[base + 1u][c]) ||
                    !std::isfinite(s.outputs[base + 2u][c])) {
                    return false;
                }
            }
        }
        return true;
    };

    const auto sampleCubicVec4 =
        [&](const AnimationSampler& s, float t, bool rotation, glm::vec4& out)->bool {
        if (!cubicKeysValid(s, rotation)) return false;
        if (!std::isfinite(t)) return false;

        const auto keyValue = [&](size_t index) {
            return s.outputs[index * 3u + 1u];
        };
        // glTF clamps the sampled time of a cubic channel to its own key range;
        // the engine's wrap above still decides which cycle of the clip applies.
        // Clamped endpoints and exact keys return the authored value directly:
        // the interpolation is not needed there, and with finite but extreme
        // tangents the polynomial form could overflow to a non-finite value for
        // a key whose authored value is perfectly valid.
        const float firstTime = s.inputs.front();
        const float lastTime = s.inputs.back();
        if (t <= firstTime) {
            out = keyValue(0u);
            return true;
        }
        if (t >= lastTime) {
            out = keyValue(s.inputs.size() - 1u);
            return true;
        }
        const auto atOrAfter = std::lower_bound(s.inputs.begin(), s.inputs.end(), t);
        if (atOrAfter != s.inputs.end() && *atOrAfter == t) {
            out = keyValue(static_cast<size_t>(atOrAfter - s.inputs.begin()));
            return true;
        }

        const size_t i = static_cast<size_t>(atOrAfter - s.inputs.begin()) - 1u;
        const float t0 = s.inputs[i];
        const float t1 = s.inputs[i + 1u];
        const float span = t1 - t0;
        const float a = (t - t0) / span;
        const float a2 = a * a;
        const float a3 = a2 * a;
        const float h00 = 2.0f * a3 - 3.0f * a2 + 1.0f;
        const float h10 = a3 - 2.0f * a2 + a;
        const float h01 = -2.0f * a3 + 3.0f * a2;
        const float h11 = a3 - a2;

        const glm::vec4 v0 = s.outputs[i * 3u + 1u];
        const glm::vec4 outTangent0 = s.outputs[i * 3u + 2u];
        const glm::vec4 inTangent1 = s.outputs[(i + 1u) * 3u];
        const glm::vec4 v1 = s.outputs[(i + 1u) * 3u + 1u];

        // Tangents are authored per second, so they scale by the real interval.
        const glm::vec4 result = h00 * v0 + h10 * (span * outTangent0) +
            h01 * v1 + h11 * (span * inTangent1);
        const int components = rotation ? 4 : 3;
        for (int c = 0; c < components; ++c) {
            if (!std::isfinite(result[c])) return false;
        }
        out = result;
        return true;
    };

    const auto sampleCubicQuat =
        [&](const AnimationSampler& s, float t, glm::quat& out)->bool {
        glm::vec4 components;
        if (!sampleCubicVec4(s, t, true, components)) return false;
        const float lengthSquared = glm::dot(components, components);
        if (!std::isfinite(lengthSquared) || !(lengthSquared > 0.0f)) {
            return false;
        }
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        const glm::quat q(components.w * inverseLength,
                          components.x * inverseLength,
                          components.y * inverseLength,
                          components.z * inverseLength);
        if (!std::isfinite(q.w) || !std::isfinite(q.x) ||
            !std::isfinite(q.y) || !std::isfinite(q.z)) {
            return false;
        }
        out = q;
        return true;
    };

    if (animIndex >= 0 && animIndex < (int)animations.size()) {
        const auto& clip = animations[animIndex];
        float t = looping ? wrapTime(timeSec, clip.durationSec)
                          : std::clamp(timeSec, 0.0f, std::max(0.0f, clip.durationSec));

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)outLocal.size()) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)clip.samplers.size()) continue;

            const auto& s = clip.samplers[ch.samplerIndex];
            const bool cubic = (s.interpolation == "CUBICSPLINE");

            if (ch.path == ChannelPath::Translation) {
                glm::vec3 animT;
                if (cubic) {
                    glm::vec4 v;
                    if (!sampleCubicVec4(s, t, false, v)) continue;
                    animT = glm::vec3(v.x, v.y, v.z);
                } else {
                    const glm::vec4 v = sampleVec4(s, t, clip.durationSec);
                    animT = glm::vec3(v.x, v.y, v.z);
                }

                if (suppressHorizontalRootMotion &&
                    rmNodes.find(ch.targetNode) != rmNodes.end()) {
                    // Freeze horizontal displacement (X/Z) to bind pose, but keep animated Y.
                    const model_types::NodeTRS& bind = nodesDefault[ch.targetNode];

                    // Bind translation source depends on whether node was authored as matrix or TRS.
                    glm::vec3 bindT;
                    if (bind.hasMatrix) {
                        // GLM mat4 translation is column 3 (m[3].xyz)
                        bindT = glm::vec3(bind.matrix[3]);
                        // Preserve the bind matrix and only patch translation.
                        outLocal[ch.targetNode] = bind;           // copies matrix + hasMatrix=true
                        outLocal[ch.targetNode].matrix[3].x = bindT.x;
                        outLocal[ch.targetNode].matrix[3].y = animT.y;  // keep vertical motion
                        outLocal[ch.targetNode].matrix[3].z = bindT.z;
                        outLocal[ch.targetNode].matrix[3].w = 1.0f;
                        outLocal[ch.targetNode].hasMatrix = true;
                    } else {
                        bindT = bind.t;
                        outLocal[ch.targetNode].t = glm::vec3(bindT.x, animT.y, bindT.z);
                        outLocal[ch.targetNode].hasMatrix = false;
                    }
                    continue;
                }

                // Normal translation application
                outLocal[ch.targetNode].t = animT;
                outLocal[ch.targetNode].hasMatrix = false;
            }

            else if (ch.path == ChannelPath::Scale) {
                glm::vec3 scale;
                if (cubic) {
                    glm::vec4 v;
                    if (!sampleCubicVec4(s, t, false, v)) continue;
                    scale = glm::vec3(v.x, v.y, v.z);
                } else {
                    const glm::vec4 v = sampleVec4(s, t, clip.durationSec);
                    scale = glm::vec3(v.x, v.y, v.z);
                }
                outLocal[ch.targetNode].s = scale;
                outLocal[ch.targetNode].hasMatrix = false;
            }
            else if (ch.path == ChannelPath::Rotation) {
                if (cubic) {
                    glm::quat q;
                    if (!sampleCubicQuat(s, t, q)) continue;
                    outLocal[ch.targetNode].r = q;
                } else {
                    outLocal[ch.targetNode].r = sampleQuat(s, t, clip.durationSec);
                }
                outLocal[ch.targetNode].hasMatrix = false;
            }
        }
    }

    composePose(source, outLocal, outGlobal);
}

void composePose(const PoseSource& source,
                 const std::vector<model_types::NodeTRS>& local,
                 std::vector<glm::mat4>& outGlobal)
{
    const auto& nodeChildren = source.nodeChildren;
    const auto& sceneRoots = source.sceneRoots;
    const int nodeCount = static_cast<int>(local.size());
    outGlobal.assign(local.size(), glm::mat4(1.0f));
    std::vector<int> parent(local.size(), -1);
    for (int p = 0; p < nodeCount; ++p) {
        if (p >= static_cast<int>(nodeChildren.size())) continue;
        for (int c : nodeChildren[p]) {
            if (c >= 0 && c < nodeCount && parent[c] == -1) parent[c] = p;
        }
    }
    std::function<void(int, const glm::mat4&)> dfs = [&](int node, const glm::mat4& parentM) {
        if (node < 0 || node >= nodeCount) return;
        glm::mat4 localM = trsToMat4(local[node]);
        const int parentNode = parent[static_cast<std::size_t>(node)];
        if (local[node].segmentScaleCompensate &&
            parentNode >= 0 && parentNode < nodeCount) {
            const glm::vec3 parentScale = local[static_cast<std::size_t>(parentNode)].s;
            const glm::vec3 inverseParentScale(
                std::abs(parentScale.x) > 1e-8f ? 1.0f / parentScale.x : 1.0f,
                std::abs(parentScale.y) > 1e-8f ? 1.0f / parentScale.y : 1.0f,
                std::abs(parentScale.z) > 1e-8f ? 1.0f / parentScale.z : 1.0f);
            // Game Freak's row-vector rule is local * inverseParentScale *
            // parentWorld. Transposed into GLM's column-vector convention it
            // becomes parentWorld * inverseParentScale * local.
            localM = glm::scale(glm::mat4(1.0f), inverseParentScale) * localM;
        }
        glm::mat4 global = parentM * localM;
        outGlobal[node] = global;

        if (node < (int)nodeChildren.size()) {
            for (int c : nodeChildren[node]) dfs(c, global);
        }
    };

    if (sceneRoots.empty()) {
        dfs(0, glm::mat4(1.0f));
    } else {
        for (int r : sceneRoots) dfs(r, glm::mat4(1.0f));
    }
}

} // namespace engine::render::animation
