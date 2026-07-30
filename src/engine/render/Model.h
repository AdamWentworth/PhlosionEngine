// src/engine/render/Model.h
#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Camera3D.h"
#include "engine/utils/Shader.h"

class ShaderCache;

#include "ModelAnimationTypes.h"
#include "ModelMeshTypes.h"

struct Submesh {
    size_t indexOffset = 0;
    size_t indexCount = 0;
    unsigned int baseColorTexID = 0;
    unsigned int emissiveTexID  = 0;
    glm::vec3 emissiveFactor{0.0f};
    int alphaMode = 0; // 0=OPAQUE, 1=MASK, 2=BLEND
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    int meshIndex = -1;
};

class Model {
public:
    struct AnimatedPose {
        std::vector<pac_model_types::NodeTRS> locals;
        std::vector<glm::mat4> globals;
    };

    explicit Model(const std::string& filepath, ShaderCache* shaderCache = nullptr);
    ~Model();

    int   getAnimationCount() const;
    float getAnimationDurationSec(int animIndex) const;

    void sampleAnimatedPose(float animTimeSec,
                           int animIndex,
                           AnimatedPose& outPose,
                           bool suppressHorizontalRootMotion = true) const;

    void drawAnimated(const Camera3D& camera,
        const glm::mat4& instanceTransform,
        float animTimeSec,
        int animIndex,
        const glm::vec3& tintColor = glm::vec3(1.0f),
        float tintStrength = 0.0f,
        const std::vector<std::uint8_t>* skipSubmeshMask = nullptr) const;

    void drawAnimatedSampled(const Camera3D& camera,
                             const glm::mat4& instanceTransform,
                             const AnimatedPose& pose,
                             const glm::vec3& tintColor = glm::vec3(1.0f),
                             float tintStrength = 0.0f,
                             const std::vector<std::uint8_t>* skipSubmeshMask = nullptr) const;

    // Draw mesh geometry using the currently bound GL shader.
    // The shader must have an MVP uniform at locMVP and use the same vertex layout.
    void drawGeometryWithBoundShader(const Camera3D& camera,
                                     const glm::mat4& instanceTransform,
                                     int mvpUniformLoc,
                                     float animTimeSec = 0.0f,
                                     int animIndex = -1) const;

    float getScaleFactor() const { return modelScaleFactor; }

    // Approximate bounds (model space)
    bool hasBounds() const { return boundsValid; }
    glm::vec3 getBoundsMin() const { return boundsMin; }
    glm::vec3 getBoundsMax() const { return boundsMax; }
    float getBoundsRadius() const { return boundsRadius; }
    float getBoundsRadiusHorizontal() const { return boundsRadiusHorizontal; }

    // Animated node global transform (MODEL SPACE)
    bool getNodeGlobalTransformByIndex(float animTimeSec,
                                       int animIndex,
                                       int nodeIndex,
                                       glm::mat4& outNodeGlobal) const;

    // exact-match lookup of node index by glTF node name.
    bool getNodeIndexByName(const std::string& nodeName, int& outNodeIndex) const;

    bool getNodeGlobalTransformByName(float animTimeSec,
                                      int animIndex,
                                      const std::string& nodeName,
                                      glm::mat4& outNodeGlobal) const;

    // CPU-side texture blob for caching (always RGBA8)
    struct CPUTexture {
        uint32_t width = 1;
        uint32_t height = 1;
        int wrapS = 0;   // GL enum stored as int
        int wrapT = 0;   // GL enum stored as int
        int minF  = 0;   // GL enum stored as int
        int magF  = 0;   // GL enum stored as int
        std::vector<uint8_t> rgba; // width*height*4
    };

    // NEW: exact-match lookup of animation index by glTF animation name.
    // Returns -1 if not found.
    int findAnimationIndexByName(const std::string& name) const;
    const std::string& getAnimationName(int animIndex) const;

    // Build a submesh mask for any submesh whose owning mesh node name contains
    // the given ASCII substring, case-insensitively.
    std::vector<std::uint8_t> buildSubmeshMaskForNodeNameContainsInsensitive(
        const std::string& needle) const;

private:
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    std::vector<Submesh> submeshes;

    std::shared_ptr<Shader> modelShader;
    int locMVP = -1;
    int locUseSkin = -1;
    int locJoints0 = -1;
    int locBaseColorTex = -1;
    int locEmissiveTex  = -1;
    int locEmissiveFactor = -1;
    int locAlphaMode = -1;
    int locAlphaCutoff = -1;

    // Optional per-instance tint
    int locTintColor = -1;
    int locTintStrength = -1;

    // tone mapping uniforms
    int locTonemapMode = -1;
    int locExposure    = -1;

    float modelScaleFactor = 1.0f;

    // Model-space bounds computed from vertex positions.
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    float boundsRadius = 0.0f;
    float boundsRadiusHorizontal = 0.0f;
    bool boundsValid = false;

    using NodeTRS          = pac_model_types::NodeTRS;
    using SkinData         = pac_model_types::SkinData;
    using AnimationClip    = pac_model_types::AnimationClip;
    using Vertex           = pac_model_types::Vertex;

    std::vector<NodeTRS>           nodesDefault;
    std::vector<std::vector<int>>  nodeChildren;
    std::vector<int>               nodeMesh;
    std::vector<int>               nodeSkin;
    std::vector<int>               sceneRoots;
    std::vector<SkinData>          skins;
    std::vector<AnimationClip>     animations;

    // glTF node names (parallel to nodesDefault).
    std::vector<std::string>       nodeNames;

    mutable bool nodeNameMapBuilt = false;
    mutable std::unordered_map<std::string, int> nodeNameToIndex;

    mutable std::unordered_set<int> warnedMissingAnimIndex;

    void loadGLTF(const std::string& filepath);

    static glm::mat4 trsToMat4(const NodeTRS& n);
    void buildPoseMatrices(float timeSec,
                           int animIndex,
                           std::vector<NodeTRS>& outLocal,
                           std::vector<glm::mat4>& outGlobal,
                           bool suppressHorizontalRootMotion = true) const;

    void uploadSkinUniforms(const glm::mat4& meshGlobal,
                            int skinIndex,
                            const std::vector<glm::mat4>& nodeGlobals) const;

    // ---- On-disk cache helpers ----
    bool tryLoadCache(const std::string& filepath);
    void writeCache(const std::string& filepath,
                    const std::vector<Vertex>& vertices,
                    const std::vector<uint32_t>& indices,
                    const std::vector<CPUTexture>& baseColorTexturesCPU,
                    const std::vector<CPUTexture>& emissiveTexturesCPU) const;
    void loadGLTFFast(const std::string& filepath);
};
