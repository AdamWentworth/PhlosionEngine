// src/engine/render/ModelAnimation.cpp
//
// GL-side animation drawing. CPU pose sampling, including the horizontal root
// motion policy below, lives in ModelPoseSampler.cpp and is shared with CPU
// consumers:
// - Ignore animated X/Z translation on selected "root motion carrier" nodes.
// - Keep animated Y translation (vertical bob).
// - Keep all rotations/scales, and all other node translations intact.
// This prevents run/walk clips from moving the mesh ahead of instanceTransform (grid/world).

#include "Model.h"
#include "ModelPoseSampler.h"

#include <glad/glad.h>

#include <functional>
#include <iostream>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

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

void Model::buildPoseMatrices(float timeSec,
                              int animIndex,
                              std::vector<NodeTRS>& outLocal,
                              std::vector<glm::mat4>& outGlobal,
                              bool suppressHorizontalRootMotion) const
{
    // Sampling lives in ModelPoseSampler so CPU consumers and Model share the
    // same implementation. Model's defaults, diagnostics and draw behavior are
    // unchanged.
    const engine::render::animation::PoseSource source{
        nodesDefault,
        nodeChildren,
        sceneRoots,
        skins,
        animations,
        nodeNames};
    engine::render::animation::samplePose(
        source,
        timeSec,
        animIndex,
        outLocal,
        outGlobal,
        suppressHorizontalRootMotion);
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
