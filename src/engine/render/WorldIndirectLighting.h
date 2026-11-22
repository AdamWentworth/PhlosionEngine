#pragma once
#include "WorldEnvironmentLighting.h"
#include <glm/glm.hpp>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace engine::render {
namespace gltf::scene { struct GltfSceneAsset; }
using IndirectRow=std::array<float,4>;
using IndirectAxes=std::array<glm::vec3,6>;
struct IndirectSurfel {
    glm::vec3 position{},normal{0,1,0},albedo{},emission{};
    std::uint32_t closestProbe=0,channels=255;
    int emissionChannel=-1;
    bool permanentShadow=false;
};
struct IndirectBrick {
    glm::vec3 center{};
    std::uint32_t first=0,count=0;
};
struct IndirectFactor {std::uint32_t brick=0;float weight=0;};
struct IndirectProbe {
    glm::vec3 position{};
    std::array<std::uint32_t,6> first{},count{};
    std::array<float,6> sky{};
    unsigned volume=0;
};
struct IndirectTetra {
    std::array<std::uint32_t,4> probes{};
    std::array<int,4> neighbor{-1,-1,-1,-1};
    std::array<glm::vec3,3> inverse{};
    // Fifteen 8-bit depths on the opposing triangle; final byte flags a buried probe.
    std::array<std::array<std::uint8_t,16>,4> visibility{};
};
struct IndirectTerrainSample {glm::vec3 normal{0,1,0},albedo{};float height=0,sky=1;};
struct IndirectTerrainMaterial {float roughness=1,specular=.04f,dominantWeight=0,selectedWeight=0;std::uint32_t dominantLayer=0;};
struct IndirectBakeSettings {
    glm::vec3 origin{-32,.5f,-48},spacing{4,3,4};
    std::array<unsigned,3> counts{17,4,17};
    unsigned faceResolution=64,volume=0;
    float maxDistance=24,fadeDistance=4,surfelSpacing=.25f;
    std::vector<int> materialEmissionChannels;
    std::vector<glm::vec3> materialEmissionFactors;
    unsigned terrainResolution=0;
    glm::vec2 terrainOrigin{-2048,-2048};
    float terrainSpacing=16;
};
struct IndirectTransport {
    std::string sceneIdentity;
    IndirectBakeSettings settings;
    std::vector<IndirectSurfel> surfels;
    std::vector<IndirectBrick> bricks;
    std::vector<IndirectProbe> probes;
    std::vector<IndirectFactor> factors;
    std::vector<IndirectTetra> tetrahedra;
    std::vector<int> acceleration;
    std::vector<IndirectTerrainSample> terrain;
    std::vector<IndirectTerrainMaterial> terrainMaterials;
};
struct IndirectControls {
    float multibounce=.5f,saturation=1,compensation=0;
    float emissiveScale=1,localScale=1,skyScale=1;
    float shadowDistanceWeight=.1f,normalWeight=.2f,probeDistanceWeight=0;
    float visibilityBias=0,threshold=.28f;
    float reflectionDimming=.1f;
    std::array<float,4> distantRange{.0025f,-.25f,.04f,-.2f};
    std::array<float,4> distantControls{.001f,.6f,1,.5f};
};
struct IndirectEvaluation {glm::vec3 radiance{};float weight=0;int tetra=-1;unsigned steps=0;};
struct IndirectGpuData {
    std::vector<IndirectRow> rows;
    std::uint32_t probeOffset=0,tetraOffset=0,accelerationOffset=0,surfelOffset=0,brickOffset=0,factorOffset=0;
    std::uint32_t probeOutput=0,brickOutput=0,shadowOutput=0,ambientOutput=0,terrainOffset=0,terrainOutput=0,outputRows=0;
};
std::string indirectSceneIdentity(const gltf::scene::GltfSceneAsset&);
void validateIndirectTransport(const IndirectTransport&);
void validateIndirectControls(const IndirectControls&);
IndirectGpuData packIndirectTransport(const IndirectTransport&);
// Prepare immutable GPU packing while loading; retain it across view changes.
class PreparedIndirectTransport {
public:
    explicit PreparedIndirectTransport(std::shared_ptr<const IndirectTransport> source);
    const IndirectGpuData& dataFor(const std::shared_ptr<const IndirectTransport>& source) const;
private:
    std::shared_ptr<const IndirectTransport> source_;
    IndirectGpuData data_;
};
IndirectTransport bakeIndirectTransport(const gltf::scene::GltfSceneAsset&,const IndirectBakeSettings&);
void writeIndirectTransport(const std::filesystem::path&,const IndirectTransport&);
IndirectTransport readIndirectTransport(const std::filesystem::path&);
std::shared_ptr<const IndirectTransport> acquireIndirectTransport(const std::filesystem::path&);
glm::vec3 indirectAxisSample(const IndirectAxes&,glm::vec3);
glm::vec4 indirectBarycentric(const IndirectTransport&,const IndirectTetra&,glm::vec3);
float indirectVisibleDistance(glm::vec3,std::span<const std::uint8_t,16>,glm::vec3,glm::vec3,glm::vec3,glm::vec3,float);
IndirectEvaluation evaluateIndirectTransport(const IndirectTransport&,std::span<const IndirectAxes>,glm::vec3,glm::vec3,const IndirectControls& = {});
std::array<glm::vec3,15> indirectVisibilitySamples(glm::vec3,glm::vec3,glm::vec3);
void buildIndirectTetrahedra(IndirectTransport&);

struct WorldIndirectLightingUpdate {
    std::uint64_t identity=0,revision=0;
    // Immutable contents. Plugin-owned inputs must name their resource scope:
    // scope release drops CPU control blocks before unloading the plugin.
    std::shared_ptr<const IndirectTransport> transport;
    std::shared_ptr<const PreparedIndirectTransport> preparedTransport;
    std::uint64_t resourceScope=0;
    WorldEnvironmentLighting environment;
    std::array<std::array<float,4>,32> emission{};
    std::array<float,3> camera{};
    IndirectControls controls;
    std::array<std::uint64_t,4> environmentProbes{};
    unsigned volume=0,quality=1;
    bool reset=false;
    // Camera clip transform. Zero permits non-camera preparation.
    std::array<float,16> viewProjection{};
};
struct IndirectScheduleCandidate {
    const WorldIndirectLightingUpdate* update=nullptr;
    std::uint64_t generation=0,lastRelight=0;
};
std::vector<unsigned> scheduleIndirectLighting(std::span<const IndirectScheduleCandidate>,unsigned budget,unsigned capacity=8);
struct WorldIndirectLightingDiagnostic {
    std::uint64_t identity=0,revision=0,generation=0;
    std::size_t immutableBytes=0,workingBytes=0;
    unsigned bricks=0,probes=0,surfels=0,dispatches=0;
    std::vector<IndirectRow> output;
};
class IWorldIndirectLighting {
public:
    virtual ~IWorldIndirectLighting()=default;
    virtual bool updateWorldIndirectLighting(const WorldIndirectLightingUpdate&)=0;
    virtual bool prepareWorldIndirectLighting(std::span<const WorldIndirectLightingUpdate>,std::uint64_t consumer)=0;
    virtual bool bindWorldIndirectLighting(const WorldIndirectLightingUpdate&)=0;
    virtual void clearWorldIndirectLighting()=0;
    virtual bool captureWorldIndirectLighting()=0;
    virtual WorldIndirectLightingDiagnostic readWorldIndirectLighting()=0;
};
}
