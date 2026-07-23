// src/engine/render/ModelAnimation.cpp
//
// Root motion OFF (horizontal):
// - Ignore animated X/Z translation on selected "root motion carrier" nodes.
// - Keep animated Y translation (vertical bob).
// - Keep all rotations/scales, and all other node translations intact.
// This prevents run/walk clips from moving the mesh ahead of instanceTransform (grid/world).

#include "Model.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <unordered_set>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

using pac_model_types::AnimationSampler;
using pac_model_types::ChannelPath;

void Model::uploadSkinUniforms(const glm::mat4& meshGlobal,
                               int skinIndex,
                               const std::vector<glm::mat4>& nodeGlobals) const
{
    if (skinIndex < 0 || skinIndex >= (int)skins.size()) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    const auto& skin = skins[skinIndex];
    if (skin.joints.empty() || skin.inverseBind.size() != skin.joints.size()) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    constexpr int MAX_JOINTS = 128;
    if ((int)skin.joints.size() > MAX_JOINTS) {
        std::cerr << "[Model] WARNING: skin joints exceed MAX_JOINTS=128; skinning disabled.\n";
        glUniform1i(locUseSkin, 0);
        return;
    }

    glm::mat4 invMesh = glm::inverse(meshGlobal);

    std::vector<glm::mat4> mats;
    mats.resize(skin.joints.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < skin.joints.size(); ++i) {
        int jNode = skin.joints[i];
        if (jNode < 0 || jNode >= (int)nodeGlobals.size()) {
            mats[i] = glm::mat4(1.0f);
            continue;
        }
        mats[i] = invMesh * nodeGlobals[jNode] * skin.inverseBind[i];
    }

    glUniform1i(locUseSkin, 1);
    glUniformMatrix4fv(locJoints0, (GLsizei)mats.size(), GL_FALSE, glm::value_ptr(mats[0]));
}

static float wrapTime(float t, float duration)
{
    if (duration <= 0.0f) return 0.0f;
    float x = std::fmod(t, duration);
    if (x < 0.0f) x += duration;
    if (x == 0.0f && t > 0.0f) {
        return std::nextafter(duration, 0.0f);
    }
    return x;
}

static size_t findKeyframe(const std::vector<float>& times, float t)
{
    if (times.empty()) return 0;
    if (t <= times.front()) return 0;
    if (t >= times.back()) return times.size() - 1;

    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t idx = (it == times.begin()) ? 0 : (size_t)((it - times.begin()) - 1);
    return idx;
}

void Model::buildPoseMatrices(float timeSec,
                              int animIndex,
                              std::vector<NodeTRS>& outLocal,
                              std::vector<glm::mat4>& outGlobal,
                              bool suppressHorizontalRootMotion) const
{
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
                    const NodeTRS& bind = nodesDefault[ch.targetNode];

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

void Model::sampleAnimatedPose(float animTimeSec,
                               int animIndex,
                               AnimatedPose& outPose,
                               bool suppressHorizontalRootMotion) const {
    buildPoseMatrices(
        animTimeSec,
        animIndex,
        outPose.locals,
        outPose.globals,
        suppressHorizontalRootMotion);
}

void Model::drawAnimated(const Camera3D& camera,
                         const glm::mat4& instanceTransform,
                         float animTimeSec,
                         int animIndex,
                         const glm::vec3& tintColor,
                         float tintStrength,
                         const std::vector<std::uint8_t>* skipSubmeshMask) const {
    AnimatedPose pose;
    sampleAnimatedPose(animTimeSec, animIndex, pose);
    drawAnimatedSampled(
        camera,
        instanceTransform,
        pose,
        tintColor,
        tintStrength,
        skipSubmeshMask);
}

void Model::drawAnimatedSampled(const Camera3D& camera,
                                const glm::mat4& instanceTransform,
                                const AnimatedPose& pose,
                                const glm::vec3& tintColor,
                                float tintStrength,
                                const std::vector<std::uint8_t>* skipSubmeshMask) const {
    if (!modelShader || VAO == 0) return;

    const auto& locals = pose.locals;
    const auto& globals = pose.globals;
    modelShader->use();
    glBindVertexArray(VAO);

    if (locTintColor >= 0) glUniform3fv(locTintColor, 1, glm::value_ptr(tintColor));
    if (locTintStrength >= 0) glUniform1f(locTintStrength, tintStrength);

    // --- Save GL state (critical: UI relies on these) ---
    const GLboolean prevCullEnabled  = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevBlendEnabled = glIsEnabled(GL_BLEND);

    GLboolean prevDepthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthWrite);

    GLint prevActiveTex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    GLint prevSrcRGB = GL_ONE, prevDstRGB = GL_ZERO, prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);

    GLint prevEqRGB = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &prevEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);

    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex,  1);

    auto setMaterial = [&](const Submesh& sm) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sm.baseColorTexID);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sm.emissiveTexID);

        if (locEmissiveFactor >= 0) glUniform3fv(locEmissiveFactor, 1, glm::value_ptr(sm.emissiveFactor));
        if (locAlphaMode      >= 0) glUniform1i(locAlphaMode, sm.alphaMode);
        if (locAlphaCutoff    >= 0) glUniform1f(locAlphaCutoff, sm.alphaCutoff);

        if (sm.doubleSided) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE);

        if (sm.alphaMode == 2) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
    };

    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    bool hasNodeMesh = false;

    auto drawScenePass = [&](int pass) {
        std::function<void(int, const glm::mat4&)> dfsDrawPass = [&](int node, const glm::mat4& parentM) {
            if (node < 0 || node >= (int)locals.size()) return;

            glm::mat4 localM  = trsToMat4(locals[node]);
            glm::mat4 globalM = parentM * localM;

            if (node >= 0 && node < (int)nodeMesh.size()) {
                const int meshIdx = nodeMesh[node];
                if (meshIdx >= 0) {
                    hasNodeMesh = true;

                    const glm::mat4 mvp = vp * instanceTransform * globalM;
                    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

                    uploadSkinUniforms(globalM,
                                       (node < (int)nodeSkin.size() ? nodeSkin[node] : -1),
                                       globals);

                    for (std::size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex) {
                        if (skipSubmeshMask &&
                            submeshIndex < skipSubmeshMask->size() &&
                            (*skipSubmeshMask)[submeshIndex] != 0u) {
                            continue;
                        }
                        const auto& sm = submeshes[submeshIndex];
                        if (sm.meshIndex != meshIdx) continue;

                        const bool isBlend = (sm.alphaMode == 2);
                        if ((pass == 0 && isBlend) || (pass == 1 && !isBlend)) continue;

                        setMaterial(sm);

                        glDrawElements(GL_TRIANGLES,
                                       (GLsizei)sm.indexCount,
                                       GL_UNSIGNED_INT,
                                       (void*)(sm.indexOffset * sizeof(uint32_t)));
                    }
                }
            }

            if (node < (int)nodeChildren.size()) {
                for (int c : nodeChildren[node]) dfsDrawPass(c, globalM);
            }
        };

        if (sceneRoots.empty()) {
            dfsDrawPass(0, glm::mat4(1.0f));
        } else {
            for (int r : sceneRoots) dfsDrawPass(r, glm::mat4(1.0f));
        }
    };

    // Global material passes across the entire scene graph.
    // Pass 0: OPAQUE + MASK (depth-writing)
    // Pass 1: BLEND (depth-tested, no depth write)
    drawScenePass(0);
    drawScenePass(1);

    if (!hasNodeMesh) {
        glm::mat4 mvp = vp * instanceTransform;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1i(locUseSkin, 0);

        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex) {
                if (skipSubmeshMask &&
                    submeshIndex < skipSubmeshMask->size() &&
                    (*skipSubmeshMask)[submeshIndex] != 0u) {
                    continue;
                }
                const auto& sm = submeshes[submeshIndex];
                const bool isBlend = (sm.alphaMode == 2);
                if ((pass == 0 && isBlend) || (pass == 1 && !isBlend)) continue;

                setMaterial(sm);

                glDrawElements(GL_TRIANGLES,
                               (GLsizei)sm.indexCount,
                               GL_UNSIGNED_INT,
                               (void*)(sm.indexOffset * sizeof(uint32_t)));
            }
        }
    }

    // --- Restore previous GL state (critical: UI relies on these) ---
    glDepthMask(prevDepthWrite);

    if (prevBlendEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcA, prevDstA);
    glBlendEquationSeparate(prevEqRGB, prevEqA);

    if (prevCullEnabled) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);

    glActiveTexture((GLenum)prevActiveTex);

    glBindVertexArray(0);
}

void Model::drawGeometryWithBoundShader(const Camera3D& camera,
                                        const glm::mat4& instanceTransform,
                                        int mvpUniformLoc,
                                        float animTimeSec,
                                        int animIndex) const
{
    if (VAO == 0 || mvpUniformLoc < 0) return;

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    buildPoseMatrices(animTimeSec, animIndex, locals, globals);

    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();

    glBindVertexArray(VAO);

    bool hasNodeMesh = false;

    auto drawMeshAtNode = [&](int nodeIdx, const glm::mat4& nodeGlobal) {
        if (nodeIdx < 0 || nodeIdx >= (int)nodeMesh.size()) return;
        const int meshIdx = nodeMesh[(size_t)nodeIdx];
        if (meshIdx < 0) return;

        hasNodeMesh = true;

        const glm::mat4 mvp = vp * instanceTransform * nodeGlobal;
        glUniformMatrix4fv(mvpUniformLoc, 1, GL_FALSE, glm::value_ptr(mvp));

        for (const auto& sm : submeshes) {
            if (sm.meshIndex != meshIdx) continue;
            glDrawElements(GL_TRIANGLES,
                           (GLsizei)sm.indexCount,
                           GL_UNSIGNED_INT,
                           (void*)(sm.indexOffset * sizeof(uint32_t)));
        }
    };

    std::function<void(int, const glm::mat4&)> dfsDraw = [&](int node, const glm::mat4& parentM) {
        if (node < 0 || node >= (int)locals.size()) return;

        const glm::mat4 localM = trsToMat4(locals[(size_t)node]);
        const glm::mat4 globalM = parentM * localM;

        drawMeshAtNode(node, globalM);

        if (node < (int)nodeChildren.size()) {
            for (int c : nodeChildren[(size_t)node]) dfsDraw(c, globalM);
        }
    };

    if (sceneRoots.empty()) {
        dfsDraw(0, glm::mat4(1.0f));
    } else {
        for (int r : sceneRoots) dfsDraw(r, glm::mat4(1.0f));
    }

    if (!hasNodeMesh) {
        const glm::mat4 mvp = vp * instanceTransform;
        glUniformMatrix4fv(mvpUniformLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        for (const auto& sm : submeshes) {
            glDrawElements(GL_TRIANGLES,
                           (GLsizei)sm.indexCount,
                           GL_UNSIGNED_INT,
                           (void*)(sm.indexOffset * sizeof(uint32_t)));
        }
    }

    glBindVertexArray(0);
}

bool Model::getNodeGlobalTransformByIndex(float animTimeSec,
                                         int animIndex,
                                         int nodeIndex,
                                         glm::mat4& outNodeGlobal) const
{
    if (nodeIndex < 0 || nodeIndex >= (int)nodesDefault.size()) return false;

    AnimatedPose pose;
    sampleAnimatedPose(animTimeSec, animIndex, pose);

    if (nodeIndex < 0 || nodeIndex >= (int)pose.globals.size()) return false;

    outNodeGlobal = pose.globals[nodeIndex];
    return true;
}
