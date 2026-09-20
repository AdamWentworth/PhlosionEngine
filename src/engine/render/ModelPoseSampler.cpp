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

#include "ModelPoseSampler.h"

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

size_t findKeyframe(const std::vector<float>& times, float t)
{
    if (times.empty()) return 0;
    if (t <= times.front()) return 0;
    if (t >= times.back()) return times.size() - 1;

    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t idx = (it == times.begin()) ? 0 : (size_t)((it - times.begin()) - 1);
    return idx;
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
                bool suppressHorizontalRootMotion)
{
    const auto& nodesDefault = source.nodesDefault;
    const auto& nodeChildren = source.nodeChildren;
    const auto& sceneRoots = source.sceneRoots;
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

    auto sampleVec4 = [&](const AnimationSampler& s, float t, float duration)->glm::vec4 {
        if (s.inputs.empty() || s.outputs.empty()) return glm::vec4(0.0f);
        const auto valueAt = [&](size_t index) {
            return s.outputs[(std::min)(index, s.outputs.size() - 1)];
        };
        if (s.inputs.size() == 1u || s.outputs.size() == 1u) {
            return valueAt(0u);
        }

        const float firstTime = s.inputs.front();
        const float lastTime = s.inputs.back();
        const glm::vec4 firstValue = valueAt(0u);
        const glm::vec4 lastValue = valueAt(s.inputs.size() - 1u);
        const bool outsideKeyRange = t < firstTime || t > lastTime;
        if (outsideKeyRange) {
            if (s.interpolation == "STEP") return lastValue;
            const float wrappedSpan = (duration - lastTime) + firstTime;
            if (!(wrappedSpan > 0.0f)) return lastValue;
            const float wrappedT =
                (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
            const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
            return glm::mix(lastValue, firstValue, a);
        }

        size_t i = findKeyframe(s.inputs, t);
        if (i >= s.inputs.size() - 1) {
            if (s.interpolation == "STEP") return lastValue;
            const float wrappedSpan = (duration - lastTime) + firstTime;
            if (!(wrappedSpan > 0.0f)) return lastValue;
            const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
            return glm::mix(lastValue, firstValue, a);
        }

        float t0 = s.inputs[i];
        float t1 = s.inputs[i + 1];
        float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;

        glm::vec4 v0 = valueAt(i);
        glm::vec4 v1 = valueAt(i + 1u);

        if (s.interpolation == "STEP") return v0;
        return glm::mix(v0, v1, a);
    };

    auto sampleQuat = [&](const AnimationSampler& s, float t, float duration)->glm::quat {
        if (s.inputs.empty() || s.outputs.empty()) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        const auto quatAt = [&](size_t index) {
            const glm::vec4 v = s.outputs[(std::min)(index, s.outputs.size() - 1)];
            return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
        };
        if (s.inputs.size() == 1u || s.outputs.size() == 1u) {
            return quatAt(0u);
        }

        const float firstTime = s.inputs.front();
        const float lastTime = s.inputs.back();
        const glm::quat firstValue = quatAt(0u);
        const glm::quat lastValue = quatAt(s.inputs.size() - 1u);
        const bool outsideKeyRange = t < firstTime || t > lastTime;
        if (outsideKeyRange) {
            if (s.interpolation == "STEP") return lastValue;
            const float wrappedSpan = (duration - lastTime) + firstTime;
            if (!(wrappedSpan > 0.0f)) return lastValue;
            const float wrappedT =
                (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
            const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
            return glm::normalize(glm::slerp(lastValue, firstValue, a));
        }

        size_t i = findKeyframe(s.inputs, t);
        if (i >= s.inputs.size() - 1) {
            if (s.interpolation == "STEP") return lastValue;
            const float wrappedSpan = (duration - lastTime) + firstTime;
            if (!(wrappedSpan > 0.0f)) return lastValue;
            const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
            return glm::normalize(glm::slerp(lastValue, firstValue, a));
        }

        float t0 = s.inputs[i];
        float t1 = s.inputs[i + 1];
        float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
        const glm::quat q0 = quatAt(i);
        const glm::quat q1 = quatAt(i + 1u);
        if (s.interpolation == "STEP") return q0;
        return glm::normalize(glm::slerp(q0, q1, std::clamp(a, 0.0f, 1.0f)));
    };

    if (animIndex >= 0 && animIndex < (int)animations.size()) {
        const auto& clip = animations[animIndex];
        float t = wrapTime(timeSec, clip.durationSec);

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)outLocal.size()) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)clip.samplers.size()) continue;

            const auto& s = clip.samplers[ch.samplerIndex];

            if (ch.path == ChannelPath::Translation) {
                glm::vec4 v = sampleVec4(s, t, clip.durationSec);
                glm::vec3 animT(v.x, v.y, v.z);

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
                glm::vec4 v = sampleVec4(s, t, clip.durationSec);
                outLocal[ch.targetNode].s = glm::vec3(v.x, v.y, v.z);
                outLocal[ch.targetNode].hasMatrix = false;
            }
            else if (ch.path == ChannelPath::Rotation) {
                outLocal[ch.targetNode].r = sampleQuat(s, t, clip.durationSec);
                outLocal[ch.targetNode].hasMatrix = false;
            }
        }
    }

    std::function<void(int, const glm::mat4&)> dfs = [&](int node, const glm::mat4& parentM) {
        if (node < 0 || node >= (int)outLocal.size()) return;
        glm::mat4 localM = trsToMat4(outLocal[node]);
        const int parentNode = parent[static_cast<std::size_t>(node)];
        if (outLocal[node].segmentScaleCompensate &&
            parentNode >= 0 && parentNode < nodeCount) {
            const glm::vec3 parentScale = outLocal[static_cast<std::size_t>(parentNode)].s;
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
