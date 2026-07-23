// src/engine/vfx/ParticleSystem.h
#pragma once

#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>

#include "engine/utils/ShaderCache.h"

class Shader;
class Camera3D;

class ParticleSystem {
public:
    struct Particle {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        glm::vec3 accel{0.0f};

        float lifeSec = 0.0f;
        float maxLifeSec = 0.0f;

        // Interpreted as a "world-ish" size scalar that gets turned into pixels via u_PointScale.
        float sizePx = 24.0f;

        // 0..1 random seed per particle
        float seed = 0.0f;
    };

    enum class BlendMode {
        Alpha,        // SrcAlpha, OneMinusSrcAlpha
        Additive,     // SrcAlpha, One
        Premultiplied // One, OneMinusSrcAlpha
    };

    struct RenderSettings {
        // Generic default (NOT fire)
        BlendMode blend = BlendMode::Alpha;
        bool depthTest = true;
        bool depthWrite = true;
        bool programPointSize = true;

        // Reserved for future (shader would need to clamp)
        float pointSizeMin = 3.0f;
        float pointSizeMax = 160.0f;
    };

    struct UpdateSettings {
        // Generic default (NOT fire)
        glm::vec3 acceleration = glm::vec3(0.0f);

        // Exponential damping base in (0,1]. vel *= pow(dampingBase, dt)
        // 1.0 = no damping.
        float dampingBase = 1.0f;
    };

    struct RenderSnapshot {
        std::vector<Particle> particles;
        RenderSettings renderSettings{};
        float pointScale = 220.0f;
        float timeSec = 0.0f;

        std::string shaderVertPath;
        std::string shaderFragPath;

        bool useFlipbook = false;
        std::string flipbookPath;
        int flipbookCols = 1;
        int flipbookRows = 1;
        int flipbookFrames = 1;
        float flipbookFps = 0.0f;

        bool useSecondaryFlipbook = false;
        std::string flipbookPath2;
        int flipbookCols2 = 1;
        int flipbookRows2 = 1;
        int flipbookFrames2 = 1;
        float flipbookFps2 = 0.0f;
    };

public:
    ParticleSystem() = default;
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // Permanent fix: allow engine-owned cache injection, but also safe fallback cache.
    void setShaderCache(ShaderCache& cache);

    void init();
    void shutdown();

    void update(float dt);
    void render(const Camera3D& camera);
    bool buildRenderSnapshot(RenderSnapshot& out) const;
    std::size_t particleCount() const { return particles.size(); }

    void emit(const Particle& p);

    // ----- Configuration (effect-level) -----
    void setPointScale(float s) { pointScale = s; }

    void setShaderPaths(const std::string& vertPath, const std::string& fragPath) {
        shaderVertPath = vertPath;
        shaderFragPath = fragPath;
        shaderDirty = true;
    }

    void setRenderSettings(const RenderSettings& rs) { renderSettings = rs; }
    const RenderSettings& getRenderSettings() const { return renderSettings; }

    void setUpdateSettings(const UpdateSettings& us) { updateSettings = us; }
    const UpdateSettings& getUpdateSettings() const { return updateSettings; }

    // Flipbook config (optional). If texturePath is empty, a 1x1 white texture is used.
    void setFlipbook(const std::string& texturePath, int cols, int rows, int frames, float fps) {
        flipbookPath = texturePath;
        flipbookCols = cols;
        flipbookRows = rows;
        flipbookFrames = frames;
        flipbookFps = fps;
        flipbookDirty = true;
    }

    // Secondary flipbook atlas (optional). When set, shaders can blend between two atlases for extra variation.
    // If texturePath is empty, the secondary atlas is disabled.
    void setSecondaryFlipbook(const std::string& texturePath, int cols, int rows, int frames, float fps) {
        flipbookPath2 = texturePath;
        flipbookCols2 = cols;
        flipbookRows2 = rows;
        flipbookFrames2 = frames;
        flipbookFps2 = fps;
        useSecondaryFlipbook = !flipbookPath2.empty();
        flipbookDirty2 = true;
    }

    bool getUseSecondaryFlipbook() const { return useSecondaryFlipbook; }

    // Disable flipbook entirely (for procedural sprites/shaders)
    void setUseFlipbook(bool enabled) { useFlipbook = enabled; }
    bool getUseFlipbook() const { return useFlipbook; }

private:
    struct GPUParticle {
        glm::vec3 pos;
        float age01;
        float sizePx;
        float seed;
    };

    // Incomplete-type-safe deleter (defined in .cpp where ShaderCache is complete)
    static void deleteShaderCache(ShaderCache* p);

private:
    bool initialized = false;

    std::shared_ptr<Shader> shader;
    std::string shaderVertPath = "assets/shaders/vfx/particle.vert";
    std::string shaderFragPath = "assets/shaders/vfx/particle.frag";
    bool shaderDirty = true;

    ShaderCache* shaderCache = nullptr; // not owning

    using ShaderCachePtr = std::unique_ptr<ShaderCache, void(*)(ShaderCache*)>;
    ShaderCachePtr internalShaderCache{nullptr, &ParticleSystem::deleteShaderCache};

    unsigned int vao = 0;
    unsigned int vbo = 0;

    std::vector<Particle> particles;
    std::vector<GPUParticle> gpuBuffer;

    float timeSec = 0.0f;
    float updateAccumulatorSec = 0.0f;
    float pointScale = 220.0f;

    RenderSettings renderSettings{};
    UpdateSettings updateSettings{};

    // Flipbook texture (optional)
    unsigned int flipbookTex = 0;
    std::string flipbookPath = ""; // generic default: no file => 1x1 white tex
    int   flipbookCols = 1;
    int   flipbookRows = 1;
    int   flipbookFrames = 1;
    float flipbookFps = 0.0f;
    bool  flipbookDirty = true;

    // Secondary flipbook texture (optional)
    unsigned int flipbookTex2 = 0;
    std::string  flipbookPath2 = "";
    int   flipbookCols2 = 1;
    int   flipbookRows2 = 1;
    int   flipbookFrames2 = 1;
    float flipbookFps2 = 0.0f;
    bool  flipbookDirty2 = true;

    bool useFlipbook = true;
    bool useSecondaryFlipbook = false;

private:
    void ensureFlipbookLoaded();
    void ensureSecondaryFlipbookLoaded();
    void ensureShaderLoaded();
};
