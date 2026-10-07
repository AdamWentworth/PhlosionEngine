#pragma once

#include "engine/render/IRenderBackend.h"
#include "vfxlibrary/Effect.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vfx::portable {

struct Draw {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::filesystem::path texture;
    vfxlibrary::Blend blend = vfxlibrary::Blend::Alpha;
    bool depthTest = true;
    bool depthWrite = false;
    int wrapS = 33071;
    int wrapT = 33071;
    std::uint8_t materialMode = 255;
};

// Renderer-neutral sampled meshes, cards and ordered gradient lines.
// Unsupported package operations fail at load instead of disappearing in play.
class PortableEffect {
public:
    explicit PortableEffect(const std::filesystem::path& manifest);
    explicit PortableEffect(vfxlibrary::Effect effect);

    const vfxlibrary::Effect& effect() const noexcept { return effect_; }
    std::vector<Draw> sample(std::int64_t timeMs, const glm::mat4& world,
                             const glm::vec3& cameraRight,
                             const glm::vec3& cameraUp,
                             const float* viewProjection = nullptr,
                             int surfaceHeight = 720) const;
    void render(IRenderBackend& renderer, std::int64_t timeMs,
                const glm::mat4& world, const glm::vec3& cameraRight,
                const glm::vec3& cameraUp, const float* viewProjection,
                int surfaceWidth, int surfaceHeight) const;

private:
    struct Texture {
        std::string key;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };
    const Texture& texture(const std::filesystem::path& path) const;
    std::filesystem::path intensityTexture(const std::filesystem::path& path,
        const vfxlibrary::Rgba& high, const vfxlibrary::Rgba& low) const;
    vfxlibrary::Effect effect_;
    mutable std::unordered_map<std::string, Texture> textures_;
};

} // namespace vfx::portable
