#pragma once

// Authored public content and lifecycle for the glTF scene fixture.
//
// The sample owns content, camera and presentation choices only: the glTF
// document, its PNG maps and the variant/replacement authoring live here and in
// SceneFixtureAssets.cpp, while CPU import, palette composition and draw
// binding stay in the engine's src/engine/render/gltf scene path.

#include "SceneFixtureCase.h"

#include "engine/render/gltf/GltfSceneAsset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::scene_fixture {

// The two authored content generations used by the replacement case.
enum class SceneVariant {
    A = 0,
    B = 1,
};

const char* sceneVariantName(SceneVariant variant);

// The authored rig. Joint nodes live at these character-space positions, and
// the animation rotates the elbow joint about +Z by this angle.
struct AuthoredRig {
    static constexpr float kShoulderX = 0.42f;
    static constexpr float kShoulderY = 0.88f;
    static constexpr float kElbowLocalY = -0.29f;
    static constexpr float kPoseAngleDegrees = 70.0f;
    static constexpr float kPoseTimeSec = 1.0f;
    static constexpr float kArmMeshNodeOffsetX = 0.02f;
};

glm::mat4 authoredShoulderLocal(SceneVariant variant);
glm::mat4 authoredElbowLocal(SceneVariant variant, float poseBlend);
glm::mat4 authoredShoulderGlobal(SceneVariant variant, float poseBlend);
glm::mat4 authoredElbowGlobal(SceneVariant variant, float poseBlend);
glm::mat4 authoredJointInverseBind(SceneVariant variant, int joint);
// jointGlobal * inverseBind, the analytic skinning matrix for one joint.
glm::mat4 authoredJointPalette(SceneVariant variant, int joint, float poseBlend);
// The authored (non-identity) transform of the weighted limb's mesh node. The
// submitted palette is inverse(this) * authoredJointPalette, and the submitted
// model matrix is this, so the rendered result is the authored palette.
glm::mat4 authoredArmMeshNodeMatrix();

struct AuthoredLimbVertex {
    glm::vec3 restPosition{0.0f};
    // Joint indices into the authored skin (0 = shoulder, 1 = elbow).
    std::array<int, 4> joints{0, 0, 0, 0};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec2 uv{0.0f};
};

const std::vector<AuthoredLimbVertex>& authoredLimbVertices(SceneVariant variant);
const std::vector<std::uint16_t>& authoredLimbIndices();
// Analytic skinned world position of one authored limb vertex.
glm::vec3 authoredLimbSkinnedPosition(SceneVariant variant,
                                      std::size_t vertex,
                                      float poseBlend);

// Analytic world-space points for the guard's expected-content projections.
// Static parts are node transforms applied to authored geometry; the limb is
// skinned with the authored analytic palettes.
std::vector<glm::vec3> authoredCharacterPoints(SceneVariant variant,
                                               float poseBlend);
std::vector<glm::vec3> authoredLimbPoints(SceneVariant variant, float poseBlend);
std::vector<glm::vec3> authoredHeadPoints(SceneVariant variant);
std::vector<glm::vec3> authoredTorsoPoints(SceneVariant variant);
std::vector<glm::vec3> authoredLeftArmPoints(SceneVariant variant);
std::vector<glm::vec3> authoredFloorPoints();
std::vector<glm::vec3> authoredWallPoints(SceneVariant variant);

// Authored base-color factors and emission for one material slot.
struct AuthoredMaterial {
    const char* name = "";
    const char* image = nullptr;
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
};

const AuthoredMaterial& authoredMaterial(SceneVariant variant,
                                         const std::string& name);

// Authored PNG role maps: eight by eight RGBA with distinct halves.
struct AuthoredMap {
    const char* fileName = "";
    std::array<std::uint8_t, 4> left{0u, 0u, 0u, 255u};
    std::array<std::uint8_t, 4> right{0u, 0u, 0u, 255u};
};

const AuthoredMap& authoredMap(SceneVariant variant, const std::string& name);
std::vector<std::uint8_t> authoredMapPixels(const AuthoredMap& map);

// The authored albedo inspection surface. It is a separate small document drawn
// through the same generic adapter with the shared material debug view that
// shows authored tint-resolved albedo, so the guard can verify linear factor
// multiplication against authored source bytes instead of a rendered capture.
struct AuthoredProbe {
    glm::vec3 center{-1.0f, 1.0f, 0.0f};
    glm::vec2 halfExtent{0.3f, 0.2f};
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    AuthoredMap map{};
    int debugView = 2;
};

const AuthoredProbe& authoredProbe();
// The four authored world-space corners of the inspection surface.
std::vector<glm::vec3> authoredProbePoints();
// The authored RGBA source pixels of the inspection surface's base map.
std::vector<std::uint8_t> authoredProbePixels();
// The displayed value one authored texel must show: the authored sRGB bytes are
// decoded to linear, multiplied once by the authored linear base-color factor,
// and encoded back through the documented sRGB transfer.
std::array<std::uint8_t, 3> authoredProbeExpectedPixel(std::uint8_t red,
                                                       std::uint8_t green,
                                                       std::uint8_t blue);

// Writes the inspection surface's glTF document and base map into a directory
// the caller owns.
bool stageProbeDocument(const std::filesystem::path& directory,
                        std::string* outError = nullptr);

// RAII owner of one run-owned staging directory. Names are reserved with
// create_directory, so two hosts can never share or delete each other's inputs;
// the destructor removes only the directory this instance created.
class SceneStagingDirectory {
public:
    SceneStagingDirectory() = default;
    explicit SceneStagingDirectory(std::string prefix);
    ~SceneStagingDirectory();

    SceneStagingDirectory(const SceneStagingDirectory&) = delete;
    SceneStagingDirectory& operator=(const SceneStagingDirectory&) = delete;
    SceneStagingDirectory(SceneStagingDirectory&& other) noexcept;
    SceneStagingDirectory& operator=(SceneStagingDirectory&& other) noexcept;

    bool valid() const noexcept { return !path_.empty(); }
    const std::filesystem::path& path() const noexcept { return path_; }
    std::filesystem::path file(const std::string& name) const;
    bool contains(const std::filesystem::path& candidate) const;
    const std::string& error() const noexcept { return error_; }

private:
    void release() noexcept;

    std::filesystem::path path_;
    std::string error_;
};

// The authored document path inside a staging directory.
const char* sceneDocumentFileName();

// Writes the variant's glTF document, binary payload and PNG maps into a
// directory the caller owns. `emissionOff` authors the same content with a zero
// emissive factor on the emissive-factor-only surface.
bool stageSceneDocument(SceneVariant variant,
                        bool emissionOff,
                        const std::filesystem::path& directory,
                        std::string* outError = nullptr);

// Writes a deliberately malformed document over the staged document path so the
// failed-replacement case exercises the real production load failure.
bool stageMalformedDocument(const std::filesystem::path& directory,
                            std::string* outError = nullptr);

}  // namespace engine::scene_fixture
