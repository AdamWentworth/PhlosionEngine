// tests/TestGltfSceneAssetRenderer.cpp
//
// CPU contract for the generic glTF scene draw adapter. A recording backend
// captures exactly what the adapter submits through the existing cached
// textured world-mesh entry point: five-map pointers, scalar factors, node
// matrices and skin palettes. No graphics context is created.

#include "gltf_import/GltfSceneTestSupport.h"

#include "engine/render/gltf/GltfSceneAsset.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using engine::render::gltf::scene::GltfSceneAsset;
using engine::render::gltf::scene::GltfSceneDrawOptions;
using engine::render::gltf::scene::GltfSceneDrawStats;
using engine::render::gltf::scene::GltfScenePose;
using engine::render::gltf::scene::kBindPoseClip;
using engine::render::gltf::scene::loadGltfSceneAsset;
using engine::render::gltf::scene::sampleGltfScenePose;
using gltf_import_contract::FixtureDirectory;
using gltf_import_contract::nearlyEqual;
using gltf_scene_contract::AuthoredRig;
using gltf_scene_contract::SceneSource;

bool writeBytes(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return output.good();
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return output.good();
}

std::filesystem::path stageScene(const FixtureDirectory& directory,
                                 const SceneSource& source) {
    const std::filesystem::path document = directory.file("scene.gltf");
    if (document.empty() || !writeText(document, source.document.dump(2)) ||
        !writeBytes(directory.file("scene.bin"), source.binary) ||
        !writeBytes(directory.file("base_color.png"),
                    gltf_scene_contract::authoredBaseColorPngBytes())) {
        return {};
    }
    return document;
}

// Records every submission the adapter makes through the shared cached
// textured world-mesh entry point.
class RecordingBackend final : public IRenderBackend {
public:
    struct Submission {
        std::string geometryKey;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
        engine::render::backend::WorldTextureData texture;
        std::vector<float> palette;
        int surfaceWidth = 0;
        int surfaceHeight = 0;
    };

    std::vector<Submission> submissions;

    const char* backendId() const override { return "recording"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return true; }
    void shutdown() override {}

    void drawWorldIndexedMeshTexturedCached(
        const char* geometryKey,
        const engine::render::backend::WorldMeshVertex* vertices,
        std::size_t vertexCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        const engine::render::backend::WorldTextureData* texture,
        const float* viewProjectionMatrix,
        int surfaceWidth,
        int surfaceHeight) override {
        (void)vertices;
        (void)indices;
        (void)viewProjectionMatrix;
        Submission submission;
        submission.geometryKey = geometryKey != nullptr ? geometryKey : "";
        submission.vertexCount = vertexCount;
        submission.indexCount = indexCount;
        submission.surfaceWidth = surfaceWidth;
        submission.surfaceHeight = surfaceHeight;
        if (texture != nullptr) {
            submission.texture = *texture;
            if (texture->skinMatrices != nullptr && texture->skinMatrixCount > 0u) {
                const std::size_t floats =
                    static_cast<std::size_t>(texture->skinMatrixCount) * 16u;
                submission.palette.assign(texture->skinMatrices,
                                          texture->skinMatrices + floats);
            }
        }
        submissions.push_back(std::move(submission));
    }
};

}  // namespace

bool test_gltf_scene_renderer_contract(std::string& outFail) {
    const gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
    const auto check = [&outFail](bool condition, std::string message) {
        return gltf_import_contract::expect(
            condition, std::move(message), outFail);
    };

    FixtureDirectory directory("phlosion-gltf-scene-renderer");
    if (!check(directory.ready(), "could not create the contract directory: " +
                                      directory.error())) {
        return false;
    }
    const SceneSource source = gltf_scene_contract::makeValidSceneSource();
    const std::filesystem::path document = stageScene(directory, source);
    if (!check(!document.empty(), "could not stage the authored glTF document")) {
        return false;
    }

    std::shared_ptr<const GltfSceneAsset> asset;
    std::string error;
    const bool loaded = loadGltfSceneAsset(document.string(), asset, &error);
    if (!check(loaded,
               "the authored glTF scene did not load: " + error)) {
        return false;
    }
    GltfScenePose pose;
    const bool poseSampled = sampleGltfScenePose(
        asset, AuthoredRig::kAnimatedTimeSec, 0, pose, &error);
    if (!check(poseSampled,
               "pose sampling failed: " + error)) {
        return false;
    }

    RecordingBackend backend;
    GltfSceneDrawOptions options;
    options.surfaceWidth = 320;
    options.surfaceHeight = 180;
    GltfSceneDrawStats stats;
    const bool drew = engine::render::gltf::scene::drawGltfSceneAsset(
        backend, *asset, pose, options, stats, &error);
    if (!check(drew,
               "the scene adapter failed to draw: " + error)) {
        return false;
    }

    if (!check(backend.submissions.size() == 2u,
               "expected two submissions, recorded " +
                   std::to_string(backend.submissions.size()))) {
        return false;
    }
    if (!check(stats.drawCalls == 2u && stats.triangles == 4u &&
                   stats.materialBinds == 2u && stats.textureRoles == 10u,
               "draw statistics are wrong")) {
        return false;
    }
    if (!check(stats.paletteDraws == 1u && stats.skinnedDraws == 1u &&
                   stats.paletteMatrices == 2u,
               "palette statistics are wrong")) {
        return false;
    }

    const RecordingBackend::Submission& skinned = backend.submissions[0];
    const RecordingBackend::Submission& prop = backend.submissions[1];
    if (!check(skinned.geometryKey == asset->primitives[0].geometryKey &&
                   prop.geometryKey == asset->primitives[1].geometryKey,
               "the adapter did not submit the asset's geometry identities")) {
        return false;
    }
    if (!check(skinned.vertexCount == 4u && skinned.indexCount == 6u &&
                   prop.vertexCount == 4u && prop.indexCount == 6u,
               "the adapter submitted the wrong vertex/index counts")) {
        return false;
    }

    // Five-map pointers and factors as actually submitted.
    const auto& blockMaterial = asset->materials[0];
    const auto& propMaterial = asset->materials[1];
    if (!check(skinned.texture.rgba == blockMaterial.baseColor.rgba.data() &&
                   skinned.texture.width == blockMaterial.baseColor.width &&
                   skinned.texture.height == blockMaterial.baseColor.height &&
                   skinned.texture.textureSrgb == 1u,
               "the base-color role was not bound through the asset's pixels")) {
        return false;
    }
    if (!check(skinned.texture.normalRgba == blockMaterial.normal.rgba.data() &&
                   skinned.texture.metallicRoughnessRgba ==
                       blockMaterial.metallicRoughness.rgba.data() &&
                   skinned.texture.occlusionRgba ==
                       blockMaterial.occlusion.rgba.data() &&
                   skinned.texture.emissiveRgba ==
                       blockMaterial.emissive.rgba.data(),
               "the normal/MR/AO/emissive roles were not bound from the asset")) {
        return false;
    }
    if (!check(skinned.texture.emissiveRgba != nullptr &&
                   skinned.texture.emissiveWidth == 1 &&
                   skinned.texture.emissiveRgba[0] == 255u &&
                   skinned.texture.emissiveRgba[3] == 255u,
               "an absent emissive map must bind implicit white")) {
        return false;
    }
    if (!check(nearlyEqual(skinned.texture.vertexColorMulR,
                           gltf_scene_contract::kAuthoredBaseColorFactor[0]) &&
                   nearlyEqual(skinned.texture.vertexColorMulG,
                               gltf_scene_contract::kAuthoredBaseColorFactor[1]) &&
                   nearlyEqual(skinned.texture.vertexColorMulB,
                               gltf_scene_contract::kAuthoredBaseColorFactor[2]) &&
                   nearlyEqual(skinned.texture.vertexColorMulA,
                               gltf_scene_contract::kAuthoredBaseColorFactor[3]),
               "the base-color factor must reach the linear vertex-color field "
               "exactly once")) {
        return false;
    }
    if (!check(nearlyEqual(skinned.texture.emissiveFactorR,
                           gltf_scene_contract::kAuthoredEmissiveFactor[0]) &&
                   nearlyEqual(skinned.texture.emissiveFactorG,
                               gltf_scene_contract::kAuthoredEmissiveFactor[1]) &&
                   nearlyEqual(skinned.texture.emissiveFactorB,
                               gltf_scene_contract::kAuthoredEmissiveFactor[2]),
               "the emissive factor was not bound separately")) {
        return false;
    }
    if (!check(nearlyEqual(skinned.texture.metallicFactor,
                           gltf_scene_contract::kAuthoredMetallicFactor) &&
                   nearlyEqual(skinned.texture.roughnessFactor,
                               gltf_scene_contract::kAuthoredRoughnessFactor),
               "the metallic/roughness factors were not bound")) {
        return false;
    }
    if (!check(skinned.texture.materialMode == options.materialMode &&
                   prop.texture.materialMode == options.materialMode,
               "the adapter did not select the shared composed material path")) {
        return false;
    }

    // Node matrices and palettes as actually submitted.
    const glm::mat4 meshGlobal =
        glm::translate(glm::mat4(1.0f),
                       glm::vec3(AuthoredRig::kMeshNodeOffsetX, 0.0f, 0.0f));
    const glm::mat4 propGlobal =
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
    if (!check(nearlyEqual(
                   glm::make_mat4(skinned.texture.modelMatrix.data()),
                   meshGlobal) &&
                   nearlyEqual(glm::make_mat4(prop.texture.modelMatrix.data()),
                               propGlobal),
               "the submitted model matrices are not the node globals")) {
        return false;
    }
    if (!check(skinned.texture.gpuSkinning == 1u &&
                   skinned.texture.gpuSkinningMode == 0u &&
                   skinned.texture.skinMatrixCount == 2u &&
                   skinned.palette.size() == 32u &&
                   prop.texture.gpuSkinning == 0u &&
                   prop.texture.skinMatrices == nullptr &&
                   prop.palette.empty(),
               "skinning was not bound only for the skinned primitive")) {
        return false;
    }
    const glm::mat4 submittedJoint0 =
        glm::make_mat4(skinned.palette.data());
    const glm::mat4 submittedJoint1 =
        glm::make_mat4(skinned.palette.data() + 16);
    if (!check(nearlyEqual(submittedJoint0,
                           gltf_scene_contract::authoredPalette(
                               0, AuthoredRig::kAnimatedTimeSec),
                           1e-4f) &&
                   nearlyEqual(submittedJoint1,
                               gltf_scene_contract::authoredPalette(
                                   1, AuthoredRig::kAnimatedTimeSec),
                               1e-4f),
               "the submitted palette does not match the authored analytic "
               "result")) {
        return false;
    }
    if (!check(nearlyEqual(prop.texture.vertexColorMulR,
                           propMaterial.baseColorFactor[0]),
               "the default material's neutral factor was not bound")) {
        return false;
    }

    // The instance transform composes ahead of the node transform.
    RecordingBackend instanced;
    GltfSceneDrawOptions instancedOptions = options;
    const glm::mat4 instance =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    std::copy(&instance[0][0], &instance[0][0] + 16,
              instancedOptions.instanceTransform.begin());
    GltfSceneDrawStats instancedStats;
    const bool instancedDrew = engine::render::gltf::scene::drawGltfSceneAsset(
        instanced, *asset, pose, instancedOptions, instancedStats, &error);
    if (!check(instancedDrew,
               "the instanced draw failed: " + error)) {
        return false;
    }
    if (!check(instanced.submissions.size() == 2u &&
                   nearlyEqual(glm::make_mat4(instanced.submissions[1]
                                                  .texture.modelMatrix.data()),
                               instance * propGlobal),
               "the instance transform was not composed with the node global")) {
        return false;
    }

    // Invalid later submissions must not leave earlier draws or overwrite stats.
    {
        GltfSceneAsset bad = *asset;
        bad.primitives[1].materialIndex = -1;
        RecordingBackend backend;
        GltfSceneDrawStats saved;
        saved.drawCalls = 42u;
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                       backend, bad, pose, options, saved, &error) &&
                       backend.submissions.empty() && saved.drawCalls == 42u,
                   "bad second primitive must be rejected before any draw")) return false;
        GltfScenePose badPose = pose;
        badPose.palettes[1].push_back(glm::mat4(1.0f));
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                       backend, *asset, badPose, options, saved, &error) &&
                       backend.submissions.empty(),
                   "bad second palette must be rejected before any draw")) return false;
    }

    {
        SceneSource neutral = gltf_scene_contract::makeValidSceneSource();
        neutral.document["meshes"][1]["primitives"][0].erase("material");
        auto path = stageScene(directory, neutral);
        std::shared_ptr<const GltfSceneAsset> neutralAsset;
        GltfScenePose neutralPose;
        RecordingBackend backend;
        GltfSceneDrawStats stats;
        if (!check(loadGltfSceneAsset(path.string(), neutralAsset, &error) &&
                       sampleGltfScenePose(neutralAsset, 0.0f, kBindPoseClip, neutralPose, &error) &&
                       engine::render::gltf::scene::drawGltfSceneAsset(backend, *neutralAsset,
                           neutralPose, options, stats, &error) && backend.submissions.size() == 2u &&
                       backend.submissions[1].texture.rgba[0] == 255u &&
                       backend.submissions[1].texture.vertexColorMulR == 1.0f,
                   "a primitive without a material must draw with the legal default: " + error)) return false;
    }

    // A pose from another generation is rejected before anything is submitted.
    SceneSource replacement = gltf_scene_contract::makeValidSceneSource();
    replacement.document["materials"][0]["name"] = "OtherBlockSkin";
    const std::filesystem::path replacementDocument =
        stageScene(directory, replacement);
    std::shared_ptr<const GltfSceneAsset> replacementAsset;
    const bool replacementLoaded = loadGltfSceneAsset(
        replacementDocument.string(), replacementAsset, &error);
    if (!check(replacementLoaded,
               "the replacement scene did not load: " + error)) {
        return false;
    }
    RecordingBackend rejecting;
    GltfSceneDrawStats rejectedStats;
    std::string mismatchError;
    if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                   rejecting, *replacementAsset, pose, options, rejectedStats,
                   &mismatchError) &&
                   !mismatchError.empty() && rejecting.submissions.empty(),
               "a pose from an older asset generation must be rejected with a "
               "diagnostic before submission")) {
        return false;
    }

    // Invalid surface dimensions are rejected without submission.
    RecordingBackend noSurface;
    GltfSceneDrawOptions invalidOptions = options;
    invalidOptions.surfaceWidth = 0;
    GltfSceneDrawStats invalidStats;
    std::string surfaceError;
    if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                   noSurface, *asset, pose, invalidOptions, invalidStats,
                   &surfaceError) &&
                   noSurface.submissions.empty(),
               "a draw without a surface must be rejected")) {
        return false;
    }

    // Bind pose submitted through the same path keeps the static node transform
    // and propagates the identity-matching palettes.
    GltfScenePose bindPose;
    const bool bindSampled =
        sampleGltfScenePose(asset, 0.0f, kBindPoseClip, bindPose, &error);
    if (!check(bindSampled,
               "bind-pose sampling failed: " + error)) {
        return false;
    }
    RecordingBackend bindBackend;
    GltfSceneDrawStats bindStats;
    const bool bindDrew = engine::render::gltf::scene::drawGltfSceneAsset(
        bindBackend, *asset, bindPose, options, bindStats, &error);
    if (!check(bindDrew &&
                   bindBackend.submissions.size() == 2u &&
                   bindBackend.submissions[0].palette.size() == 32u,
               "the bind-pose draw failed: " + error)) {
        return false;
    }
    if (!check(nearlyEqual(
                   glm::make_mat4(
                       bindBackend.submissions[0].palette.data() + 16),
                   gltf_scene_contract::authoredPalette(1, 0.0f),
                   1e-4f),
               "the bind-pose palette does not match the authored analytic "
               "result")) {
        return false;
    }

    outFail.clear();
    return true;
}
