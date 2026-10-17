#pragma once

#include "engine/render/WorldEnvironmentLighting.h"
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <vector>

namespace engine::render {
namespace gltf::scene { struct GltfSceneAsset; }
inline constexpr std::size_t kDiffuseProbeCapacity = 32;
inline constexpr std::size_t kDiffuseProbeStride = 15; // SH9 irradiance + six distance moments
using WorldDiffuseProbeUniforms = std::array<std::array<float,4>,3+kDiffuseProbeCapacity*kDiffuseProbeStride>;
using DiffuseSh = std::array<std::array<float,3>,9>;
struct DiffuseProbeGrid {
    std::array<float,3> origin{};
    std::array<float,3> spacing{1,1,1};
    std::array<int,3> counts{2,2,2};
    float fadeDistance = 2;
};
struct DiffuseProbeBakeSettings {
    DiffuseProbeGrid grid;
    int rayCount = 2048;
    int areaSamplesU = 32, areaSamplesV = 2;
    float maxDistance = 40;
};
struct DiffuseProbeTransport {
    std::string sceneIdentity;
    DiffuseProbeBakeSettings settings;
    std::vector<WorldRectLight> sources;
    // Probe-major, source-minor; unit-white emitted radiance, cosine-convolved.
    std::vector<DiffuseSh> transfer;
    std::vector<std::array<std::array<float,2>,6>> distanceMoments;
    std::vector<float> validity;
};

std::size_t validateDiffuseGrid(const DiffuseProbeGrid&);
std::array<float,9> diffuseShBasis(glm::vec3 direction);
glm::vec3 evaluateDiffuseSh(const DiffuseSh&, glm::vec3 normal);
std::string diffuseSceneIdentity(const gltf::scene::GltfSceneAsset&);
DiffuseProbeTransport bakeDiffuseProbes(const gltf::scene::GltfSceneAsset&,
    std::span<const WorldRectLight>, const DiffuseProbeBakeSettings&);
WorldDiffuseProbeUniforms relightDiffuseProbes(const DiffuseProbeTransport&,
    std::span<const WorldRectLight>);
glm::vec3 sampleDiffuseProbes(const WorldDiffuseProbeUniforms&, glm::vec3 position, glm::vec3 normal);
void validateDiffuseProbeUniforms(const WorldDiffuseProbeUniforms&);
nlohmann::json diffuseProbeTransportJson(const DiffuseProbeTransport&);
DiffuseProbeTransport diffuseProbeTransportFromJson(const nlohmann::json&);

// Optional view-scoped diffuse irradiance. Every world color pass clears it.
class IWorldDiffuseProbes {
public:
    virtual ~IWorldDiffuseProbes() = default;
    virtual void setWorldDiffuseProbes(const WorldDiffuseProbeUniforms& value) {
        validateDiffuseProbeUniforms(value);
        worldDiffuseProbes_ = value;
    }
protected:
    WorldDiffuseProbeUniforms worldDiffuseProbes_{};
};
}
