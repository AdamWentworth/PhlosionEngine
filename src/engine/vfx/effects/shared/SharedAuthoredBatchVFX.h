// src/vfx/effects/shared/SharedAuthoredBatchVFX.h
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/Random.h"

class Camera3D;
class Model;
class Shader;

class SharedAuthoredBatchVFX {
public:
    struct Config {
        struct AuthoredStreakSegment {
            glm::vec3 startLocal{0.0f};
            glm::vec3 endLocal{0.0f, 0.0f, 1.0f};
            float alphaMul = 1.0f;
        };

        struct AuthoredBillboardInstance {
            glm::vec3 positionLocal{0.0f};
            float scaleMul = 1.0f;
            float scaleXMul = 1.0f;
            float scaleYMul = 1.0f;
            float alphaMul = 1.0f;
            float spinDeg = 0.0f;
        };

        struct AuthoredBillboardOffsetFrame {
            int frameIndex = 0;
            std::vector<glm::vec3> offsetsLocal;
        };

        struct AuthoredBillboardScaleFrame {
            int frameIndex = 0;
            std::vector<glm::vec2> scaleMulLocal;
        };

        struct AuthoredBillboardSpinFrame {
            int frameIndex = 0;
            std::vector<float> spinDegLocal;
        };

        struct PassAlphaFrame {
            int frameIndex = 0;
            float alphaMul = 1.0f;
        };

        struct PassScaleFrame {
            int frameIndex = 0;
            glm::vec3 scaleMul = glm::vec3(1.0f, 1.0f, 1.0f);
        };

        struct PassOffsetFrame {
            int frameIndex = 0;
            glm::vec3 offset = glm::vec3(0.0f);
        };

        struct PassRotationFrame {
            int frameIndex = 0;
            glm::quat rotationQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        };

        struct PassUvScaleFrame {
            int frameIndex = 0;
            glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);
        };

        struct PassUvOffsetFrame {
            int frameIndex = 0;
            glm::vec2 uvOffset = glm::vec2(0.0f, 0.0f);
        };

        struct DrawPass {
            std::string id;
            int eid = 0;
            std::string renderMode = "mesh";
            std::string meshPath;
            std::string texturePath;
            std::string textureCacheGroup;
            std::string textureBakeMode = "tev_lerp";
            std::string textureAlphaMode = "texture";
            std::string vertShaderPath;
            std::string fragShaderPath;
            bool overrideBlendMode = false;
            std::uint8_t blendMode = 1u;
            bool overrideColorWriteMask = false;
            std::uint8_t colorWriteMask = 0xFu;
            bool dualSourceBlend = false;
            bool textureQuarterRing = false;
            // Material behavior is explicit and independent of asset filenames.
            bool linePass = false;
            bool quarterTextureBake = false;
            bool preserveGlowAlpha = true;
            bool cameraFacing = false;
            // Optional override for glow billboards:
            // camera, upright, shared, shared_upright, attack_plane (world-locked).
            std::string billboardFacingMode;
            int quarterCount = 4;
            float quarterStepDeg = 90.0f;
            float quarterStartDeg = 0.0f;
            float quarterRotationOffsetDeg = 0.0f;
            std::string quarterLayout = "ring";
            float billboardSpinTurns = 0.0f;
            float billboardSpinStartDeg = 0.0f;
            glm::vec3 tintColor = glm::vec3(0.93f, 0.28f, 0.14f);
            bool useAlphaMaskForColor = true;
            float scaleMul = 1.0f;
            float alphaMul = 1.0f;
            float alphaWindowMin = 0.0f;
            float alphaWindowMax = 1.0f;
            float passAlphaFps = 0.0f;
            bool passAlphaUseGlobalTime = false;
            std::vector<PassAlphaFrame> passAlphaFrames;
            float passScaleFps = 0.0f;
            bool passScaleUseGlobalTime = false;
            std::vector<PassScaleFrame> passScaleFrames;
            float passPositionOffsetFps = 0.0f;
            bool passPositionOffsetUseGlobalTime = false;
            std::vector<PassOffsetFrame> passPositionOffsetFrames;
            float passMeshOffsetFps = 0.0f;
            bool passMeshOffsetUseGlobalTime = false;
            std::vector<PassOffsetFrame> passMeshOffsetFrames;
            float passMeshRotationFps = 0.0f;
            bool passMeshRotationUseGlobalTime = false;
            std::vector<PassRotationFrame> passMeshRotationFrames;
            float passUvScaleFps = 0.0f;
            bool passUvScaleUseGlobalTime = false;
            std::vector<PassUvScaleFrame> passUvScaleFrames;
            float passUvOffsetFps = 0.0f;
            bool passUvOffsetUseGlobalTime = false;
            std::vector<PassUvOffsetFrame> passUvOffsetFrames;
            glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);
            glm::vec2 uvOffset = glm::vec2(0.0f, 0.0f);
            float forwardOffset = 0.0f;
            float heightOffset = 0.0f;
            float startRadiusMul = 1.0f;
            float timeStartSec = 0.0f;
            float timeEndSec = -1.0f;
            bool timeFadeLocal = false;
            float timeFadeStart = -1.0f;
            float localScaleStartMul = 1.0f;
            float localScaleEndMul = 1.0f;
            float localScaleRampSec = 0.0f;
            int sequenceCount = 1;
            int sequenceIndex = -1;
            float sequenceStep = 0.0f;
            float sequenceLife = 1.0f;
            bool sequenceFadeLocal = false;
            float radiusGrowthMul = 1.0f;
            bool overrideMeshForwardAxis = false;
            glm::vec3 meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 meshLocalOffset = glm::vec3(0.0f);
            glm::vec3 positionLocalOffset = glm::vec3(0.0f);
            bool overrideDirection = false;
            // Local direction in caster-space basis: +X right, +Y up, +Z forward.
            glm::vec3 directionLocal = glm::vec3(0.0f, 0.0f, 1.0f);
            // Optional multi-line fan for line passes.
            std::vector<glm::vec3> directionsLocal;
            std::string authoredSegmentsPath;
            std::vector<AuthoredStreakSegment> authoredSegmentsLocal;
            std::vector<AuthoredBillboardInstance> authoredBillboardsLocal;
            float authoredBillboardPositionScale = 1.0f;
            float authoredBillboardOffsetFps = 0.0f;
            std::vector<AuthoredBillboardOffsetFrame> authoredBillboardOffsetFrames;
            float authoredBillboardScaleFps = 0.0f;
            std::vector<AuthoredBillboardScaleFrame> authoredBillboardScaleFrames;
            float authoredBillboardSpinFps = 0.0f;
            std::vector<AuthoredBillboardSpinFrame> authoredBillboardSpinFrames;
            bool authoredSegmentCenterOrigin = false;
            float authoredSegmentPositionScale = 1.0f;
            float authoredSegmentLengthScale = 1.0f;
            float authoredSegmentTravelMul = 0.0f;
            float authoredSegmentTravelDecayPerFrame = 1.0f;
            float authoredSegmentTravelFrameRate = 30.0f;
            float authoredSegmentLengthDecayPerFrame = 1.0f;
            float authoredSegmentAlphaDecayPerFrame = 1.0f;
            float authoredSegmentMaxVisibleDistance = -1.0f;
            // Optional generated circular direction set when directions_local is omitted.
            int generatedDirectionCount = 0;
            std::string generatedDirectionMode = "circle";
            float generatedDirectionStartDeg = 0.0f;
            float generatedDirectionArcDeg = 360.0f;
            float generatedDirectionForward = 0.0f;
            // Per-emission angular jitter for directions_local (degrees).
            // 0 = disabled (uses authored even spacing as-is).
            float directionSpacingJitterDeg = 0.0f;
            // Random radial spawn distance multiplier for per-direction clumps / streaks.
            float radialDistanceMinMul = 1.0f;
            float radialDistanceMaxMul = 1.0f;
            // Per-line alpha variation range (multiplies alpha_mul), for dynamic line brightness.
            float lineAlphaMin = 1.0f;
            float lineAlphaMax = 1.0f;
            float radiusMul = 1.0f;
            float thicknessMul = 1.0f;
            float meshCornerPositionScale = 1.0f;
            bool meshCornerFlattenToLayoutPlane = false;
            std::string meshCornerAnchorMode = "bounds_center";
            bool meshCornerReconstructRect = false;
            float meshCornerGroupSpacingScale = 1.0f;
            bool overrideTev = false;
            glm::vec3 tevC0 = glm::vec3(1.0f, 1.0f, 1.0f);
            glm::vec3 tevC1 = glm::vec3(0.0f, 0.0f, 0.0f);
            glm::vec3 tevK0 = glm::vec3(1.0f, 1.0f, 1.0f);
            float tevC0A = 1.0f;
            float tevC1A = 0.0f;
            float tevK1A = 1.0f;
            bool enabled = true;
        };

        float spawnForwardOffset = 0.20f;
        float spawnHeightOffset = 0.40f;

        // Lead ring + trailing rings.
        float ringForwardOffset = 0.03f;
        float ringMinSpeed = 0.30f;
        float ringMaxSpeed = 0.45f;
        float ringMinLifeSec = 0.58f;
        float ringMaxLifeSec = 0.92f;
        float ringMinSize = 0.14f;
        float ringMaxSize = 0.20f;
        int ringTrailCount = 0;
        float ringTrailSpacingMin = 0.10f;
        float ringTrailSpacingMax = 0.18f;
        float ringTrailLateralJitter = 0.03f;
        float ringLeadSizeMul = 1.28f;
        float ringTrailSizeFalloff = 0.88f;
        float ringTrailLifeFalloff = 0.92f;
        float ringTrailSpeedFalloff = 0.90f;
        float ringScaleGrowth = 1.15f;
        float fadeStart = 0.65f;
        float effectScale = 1.0f;
        float clipSpaceDepthBias = 0.0f;
        int impactGroupCount = 1;
        float impactGroupStepSec = 0.0f;
        std::string impactGroupMode = "none";
        glm::vec2 impactGroupJitterRange = glm::vec2(0.0f);

        // Local forward axis of authored meshes.
        glm::vec3 meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);

        // Shared shader + TEV-ish constants.
        std::string vertShaderPath;
        std::string fragShaderPath;
        glm::vec3 tevC0 = glm::vec3(1.0f, 1.0f, 1.0f);
        glm::vec3 tevC1 = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 tevK0 = glm::vec3(1.0f, 1.0f, 1.0f);
        float tevC0A = 1.0f;
        float tevC1A = 0.0f;
        float tevK1A = 1.0f;
        std::uint8_t blendMode = 1u;
        std::uint8_t colorWriteMask = 0xFu;

        // Optional manifest and caller-supplied passes.
        std::string drawManifestPath;
        std::vector<DrawPass> drawPasses;

        bool depthTest = true;
        bool depthWrite = false;
    };

    struct RenderRing {
        glm::vec3 pos{0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        float lifeSec = 0.0f;
        float ageSec = 0.0f;
        float startScale = 1.0f;
        float endScale = 1.0f;
        std::uint32_t randomSeed = 0u;
    };

    struct RenderSnapshot {
        Config config{};
        std::vector<Config::DrawPass> drawPasses;
        std::vector<RenderRing> rings;
    };

public:
    SharedAuthoredBatchVFX();
    ~SharedAuthoredBatchVFX();

    void setConfig(const Config& c);
    const Config& getConfig() const { return cfg; }

    void update(float dt);
    void render(const Camera3D& camera);
    bool buildRenderSnapshot(RenderSnapshot& out) const;
    std::uint32_t activeRingCount() const;

    void emitFrom(const glm::vec3& mouthWorldPos,
                  const glm::vec3& forwardDir,
                  const glm::mat4* viewMatrix = nullptr);

private:
    struct DrawPassRuntime {
        Config::DrawPass cfg;
        std::unique_ptr<Model> meshModel;
        std::shared_ptr<Shader> shader;
        unsigned int textureID = 0;
        int locMVP = -1;
        int locTexture = -1;
        int locFade = -1;
        int locTevC0 = -1;
        int locTevC1 = -1;
        int locTevK0 = -1;
        int locTevC0A = -1;
        int locTevC1A = -1;
        int locTevK1A = -1;
        int locTintColor = -1;
        int locUseAlphaMaskForColor = -1;
        int locPassAlphaMul = -1;
        int locDualSourceBlendEnabled = -1;
    };

    struct RingInstance {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
        bool hasFrozenViewRotation = false;
        float lifeSec = 0.0f;
        float ageSec = 0.0f;
        float startScale = 1.0f;
        float endScale = 1.0f;
        std::uint32_t randomSeed = 0u;
    };

    void ensureConfigured();
    void releaseResources();
    void applyDrawManifestOverrides();
    void ensureQuarterQuadResources();
    void ensureCenteredQuadResources();
    void ensureStreakQuadResources();
    void drawQuarterQuad(const Camera3D& camera,
                         const glm::mat4& world,
                         int locMVP,
                         const Config::DrawPass& pass) const;
    void drawCenteredQuad(const Camera3D& camera,
                          const glm::mat4& world,
                          int locMVP,
                          const Config::DrawPass& pass) const;
    void drawStreakQuad(const Camera3D& camera, const glm::mat4& world, int locMVP) const;
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;
    glm::quat rotationFromToSafe(const glm::vec3& from, const glm::vec3& to) const;

private:
    std::vector<DrawPassRuntime> drawPasses;

    std::vector<RingInstance> rings;
    Config cfg{};
    bool configured = false;
    bool configFailed = false;
    unsigned int quarterQuadVAO = 0;
    unsigned int quarterQuadVBO = 0;
    unsigned int centeredQuadVAO = 0;
    unsigned int centeredQuadVBO = 0;
    unsigned int streakQuadVAO = 0;
    unsigned int streakQuadVBO = 0;

    engine::XorShift32 rng{0xA17F2Du};
};
