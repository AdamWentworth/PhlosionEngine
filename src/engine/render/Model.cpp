// src/engine/render/Model.cpp
#include "Model.h"
#include "ModelStartupLog.h"
#include "engine/utils/ShaderCache.h"
#include "engine/utils/Shader.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// Helper used by cache and runtime: detect whether a GL min filter implies mipmaps.
bool isMipmapMinFilter(GLint minF)
{
    switch (minF) {
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            return true;
        default:
            return false;
    }
}

glm::mat4 Model::trsToMat4(const NodeTRS& n)
{
    if (n.hasMatrix) return n.matrix;
    glm::mat4 T = glm::translate(glm::mat4(1.0f), n.t);
    glm::mat4 R = glm::toMat4(n.r);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), n.s);
    return T * R * S;
}

Model::Model(const std::string& filepath, ShaderCache* shaderCache)
{
    loadGLTF(filepath);

    modelShader = shaderCache ? shaderCache->get("assets/shaders/model/model.vert", "assets/shaders/model/model.frag")
                           : std::make_shared<Shader>("assets/shaders/model/model.vert", "assets/shaders/model/model.frag");

    locMVP     = glGetUniformLocation(modelShader->getID(), "u_MVP");
    locUseSkin = glGetUniformLocation(modelShader->getID(), "u_UseSkin");
    locJoints0 = glGetUniformLocation(modelShader->getID(), "u_Joints[0]");

    // material uniforms
    locBaseColorTex   = glGetUniformLocation(modelShader->getID(), "u_BaseColorTex");
    locEmissiveTex    = glGetUniformLocation(modelShader->getID(), "u_EmissiveTex");
    locEmissiveFactor = glGetUniformLocation(modelShader->getID(), "u_EmissiveFactor");
    locAlphaMode      = glGetUniformLocation(modelShader->getID(), "u_AlphaMode");
    locAlphaCutoff    = glGetUniformLocation(modelShader->getID(), "u_AlphaCutoff");
    locTintColor      = glGetUniformLocation(modelShader->getID(), "u_TintColor");
    locTintStrength   = glGetUniformLocation(modelShader->getID(), "u_TintStrength");

    // tone mapping uniforms
    locTonemapMode = glGetUniformLocation(modelShader->getID(), "u_TonemapMode");
    locExposure    = glGetUniformLocation(modelShader->getID(), "u_Exposure");

    modelShader->use();
    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex, 1);

    if (locTonemapMode >= 0) glUniform1i(locTonemapMode, 1);
    if (locExposure    >= 0) glUniform1f(locExposure, 1.0f);
}

Model::~Model()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);

    for (auto& sm : submeshes) {
        if (sm.baseColorTexID) glDeleteTextures(1, &sm.baseColorTexID);
        if (sm.emissiveTexID)  glDeleteTextures(1, &sm.emissiveTexID);
    }

    modelShader.reset();
}

int Model::getAnimationCount() const
{
    return static_cast<int>(animations.size());
}

float Model::getAnimationDurationSec(int animIndex) const
{
    if (animIndex < 0 || animIndex >= (int)animations.size()) return 0.0f;
    return animations[(size_t)animIndex].durationSec;
}

int Model::findAnimationIndexByName(const std::string& name) const
{
    for (int i = 0; i < getAnimationCount(); i++) {
        if (animations[(size_t)i].name == name) return i;
    }
    return -1;
}

const std::string& Model::getAnimationName(int animIndex) const
{
    static const std::string kEmpty;
    if (animIndex < 0 || animIndex >= static_cast<int>(animations.size())) return kEmpty;
    return animations[static_cast<std::size_t>(animIndex)].name;
}

std::vector<std::uint8_t> Model::buildSubmeshMaskForNodeNameContainsInsensitive(
    const std::string& needle) const
{
    std::vector<std::uint8_t> out(submeshes.size(), 0u);
    if (needle.empty() || submeshes.empty() || nodeNames.empty() || nodeMesh.empty()) {
        return out;
    }

    auto containsInsensitive = [](const std::string& haystack, const std::string& pattern) {
        if (pattern.empty()) return true;
        if (pattern.size() > haystack.size()) return false;
        for (std::size_t offset = 0; offset + pattern.size() <= haystack.size(); ++offset) {
            bool match = true;
            for (std::size_t i = 0; i < pattern.size(); ++i) {
                const char a = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(haystack[offset + i])));
                const char b = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(pattern[i])));
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
        return false;
    };

    std::unordered_set<int> matchedMeshIndices;
    for (std::size_t nodeIndex = 0; nodeIndex < nodeNames.size() && nodeIndex < nodeMesh.size(); ++nodeIndex) {
        if (!containsInsensitive(nodeNames[nodeIndex], needle)) continue;
        const int meshIndex = nodeMesh[nodeIndex];
        if (meshIndex >= 0) {
            matchedMeshIndices.insert(meshIndex);
        }
    }

    if (matchedMeshIndices.empty()) {
        return out;
    }

    for (std::size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex) {
        if (matchedMeshIndices.find(submeshes[submeshIndex].meshIndex) != matchedMeshIndices.end()) {
            out[submeshIndex] = 1u;
        }
    }
    return out;
}

// IMPORTANT: the .inl is written to be included inside this function body.
void Model::loadGLTF(const std::string& filepath)
{
    if (tryLoadCache(filepath)) {
        std::cerr << "[gltf][CACHE] HIT (no parsing) for: " << filepath << "\n";
        return;
    }
    std::cerr << "[gltf][CACHE] MISS (will parse) for: " << filepath << "\n";
    loadGLTFFast(filepath);
}

bool Model::getNodeIndexByName(const std::string& nodeName, int& outNodeIndex) const
{
    // NOTE: This requires nodeNameToIndex and nodeNameMapBuilt to be mutable in Model.h,
    // because this method is const and builds a cache.

    if (!nodeNameMapBuilt) {
        nodeNameToIndex.clear();
        nodeNameToIndex.reserve(nodeNames.size());

        for (int i = 0; i < (int)nodeNames.size(); ++i) {
            const std::string& n = nodeNames[(size_t)i];
            if (!n.empty()) nodeNameToIndex.emplace(n, i);
        }
        nodeNameMapBuilt = true;
    }

    auto it = nodeNameToIndex.find(nodeName);
    if (it == nodeNameToIndex.end()) return false;

    outNodeIndex = it->second;
    return true;
}

bool Model::getNodeGlobalTransformByName(float animTimeSec,
                                        int animIndex,
                                        const std::string& nodeName,
                                        glm::mat4& outNodeGlobal) const
{
    int idx = -1;
    if (!getNodeIndexByName(nodeName, idx)) return false;
    return getNodeGlobalTransformByIndex(animTimeSec, animIndex, idx, outNodeGlobal);
}
