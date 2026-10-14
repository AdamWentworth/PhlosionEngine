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
#include <limits>
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
class RecordingBackend : public IRenderBackend {
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
    int preparedGeometry=0,preparedTextures=0;
    void prewarmWorldTextureData(const WorldTextureData*) override {++preparedTextures;}
    void prewarmWorldIndexedMeshCached(const char*,const WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t) override {++preparedGeometry;}
    int batchBegins = 0, batchEnds = 0, batchDepth = 0;
    bool unbatchedDraw = false;
    void beginWorldIndexedBatchSubmission() override { ++batchBegins; ++batchDepth; }
    void endWorldIndexedBatchSubmission() override { ++batchEnds; --batchDepth; }

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
        unbatchedDraw |= batchDepth <= 0;
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

class ScopedRecordingBackend final : public RecordingBackend, public IWorldResourceScope {
public:
    bool active = false;
    bool reportFailure = false;
    int ended = 0;
    engine::render::WorldResourceScopeId createWorldResourceScope(std::uint64_t) override { return 1; }
    bool beginWorldResourceScope(engine::render::WorldResourceScopeId id) override {
        if (id != 1u || active) return false;
        active = true;
        return true;
    }
    bool endWorldResourceScope() override { active = false; ++ended; return true; }
    bool requestWorldResourceScopeRelease(engine::render::WorldResourceScopeId) override { return true; }
    bool getWorldResourceScopeStats(engine::render::WorldResourceScopeId id,
        engine::render::WorldResourceScopeStats& out) const override {
        if (id != 1u) return false;
        out.id = id;
        out.status = reportFailure ? engine::render::WorldResourceScopeStatus::AllocationFailed
                                   : engine::render::WorldResourceScopeStatus::Ready;
        out.error = reportFailure ? "authored allocation failure" : "";
        return true;
    }
    engine::render::WorldResourceStats getWorldResourceStats() const override { return {}; }
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
    options.prewarmOnly=true;
    if (!check(engine::render::gltf::scene::drawGltfSceneAsset(backend,*asset,pose,options,stats,&error) &&
        backend.submissions.empty() && backend.preparedGeometry==2 && backend.preparedTextures==2 && stats.drawCalls==0,
        "Preparation uploaded the wrong resources or drew the scene")) return false;
    options.prewarmOnly=false;
    const bool drew = engine::render::gltf::scene::drawGltfSceneAsset(
        backend, *asset, pose, options, stats, &error);
    if (!check(drew,
               "the scene adapter failed to draw: " + error)) {
        return false;
    }

    if (!check(backend.batchBegins == 1 && backend.batchEnds == 1 &&
                   backend.batchDepth == 0 && !backend.unbatchedDraw,
               "scene primitives must share one balanced submission batch")) return false;
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
                       backend.submissions.empty() && saved.drawCalls == 42u &&
                       backend.batchBegins == 0 && backend.batchEnds == 0,
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

    // Explicit LINEAR minification retains only the authored base level.
    {
        SceneSource linear = gltf_scene_contract::makeValidSceneSource();
        linear.document["samplers"][0]["minFilter"] = 9729;
        auto path = stageScene(directory, linear);
        std::shared_ptr<const GltfSceneAsset> linearAsset;
        GltfScenePose linearPose;
        RecordingBackend backend;
        GltfSceneDrawStats stats;
        if (!check(loadGltfSceneAsset(path.string(), linearAsset, &error) &&
                   linearAsset->materials[0].baseColor.linearWithoutMipmaps &&
                   sampleGltfScenePose(linearAsset, 0, kBindPoseClip, linearPose, &error) &&
                   engine::render::gltf::scene::drawGltfSceneAsset(backend, *linearAsset,
                       linearPose, options, stats, &error) &&
                   backend.submissions[0].texture.mipLevelCount == 1,
                   "explicit LINEAR must suppress automatic mip generation: " + error)) return false;
    }

    // Animated factors replace authored factors once, retain the texture, and
    // fail before submission when a caller binds the wrong material layout.
    {
        RecordingBackend backend;
        GltfSceneDrawStats stats;
        auto animated = options;
        const std::array<std::array<float, 4>, 2> colors{{{.2f,.4f,.6f,1}, {.8f,.6f,.4f,.5f}}};
        animated.baseColorFactors = colors;
        animated.tint = {.5f, 1, 1, .5f};
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                       backend, *asset, pose, animated, stats, &error) &&
                       nearlyEqual(backend.submissions[0].texture.vertexColorMulR, .1f) &&
                       nearlyEqual(backend.submissions[1].texture.vertexColorMulA, .25f) &&
                       backend.submissions[0].texture.rgba == asset->materials[0].baseColor.rgba.data(),
                   "animated material factors were not submitted independently: " + error)) return false;
        backend.submissions.clear();
        animated.baseColorFactors = std::span(colors).first(1);
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                       backend, *asset, pose, animated, stats, &error) && backend.submissions.empty(),
                   "incorrect material factor count must reject before drawing")) return false;
    }

    {
        RecordingBackend backend;
        GltfSceneDrawStats stats;
        auto animated = options;
        std::array<std::array<float,3>,2> colors{{{.8f,.1f,.3f},{0,0,0}}};
        animated.emissiveFactors = colors;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                backend,*asset,pose,animated,stats,&error) &&
                nearlyEqual(backend.submissions[0].texture.emissiveFactorR,.8f) &&
                backend.submissions[1].texture.emissiveFactorB == 0 &&
                backend.submissions[0].texture.emissiveRgba == asset->materials[0].emissive.rgba.data(),
                "emission overrides must retain textures and isolate material factors")) return false;
        for (float bad : {-1.f,1.1f,std::numeric_limits<float>::quiet_NaN()}) {
            backend.submissions.clear(); colors[1][0]=bad;
            if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                    backend,*asset,pose,animated,stats,&error) && backend.submissions.empty(),
                    "invalid emission must reject before any draw")) return false;
        }
        backend.submissions.clear(); animated.emissiveFactors=std::span(colors).first(1);
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                backend,*asset,pose,animated,stats,&error) && backend.submissions.empty(),
                "emission count must match the asset")) return false;
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

    {
        RecordingBackend filtered;
        auto filteredOptions = options;
        const std::array hidden{asset->primitives[0].nodeIndex};
        filteredOptions.excludedNodes = hidden;
        GltfSceneDrawStats filteredStats;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                filtered, *asset, pose, filteredOptions, filteredStats, &error) &&
                filtered.submissions.size() == 1 && filteredStats.drawCalls == 1 &&
                filtered.submissions[0].geometryKey == asset->primitives[1].geometryKey,
                "node exclusion removed the wrong primitive")) return false;
        filtered.submissions.clear();
        filteredOptions.prewarmOnly = true;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                filtered, *asset, pose, filteredOptions, filteredStats, &error) &&
                filtered.preparedGeometry == 1 && filtered.preparedTextures == 1 &&
                filtered.submissions.empty(), "prewarm ignored node exclusion")) return false;
        filteredOptions.prewarmOnly = false;
        filteredOptions.translucent = true;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                filtered, *asset, pose, filteredOptions, filteredStats, &error) &&
                filtered.submissions.size() == 1,
                "transparent pass ignored node exclusion")) return false;
        filtered.submissions.clear();
        const std::array hiddenAll{asset->primitives[0].nodeIndex, asset->primitives[1].nodeIndex};
        filteredOptions.excludedNodes = hiddenAll;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                filtered, *asset, pose, filteredOptions, filteredStats, &error) &&
                filtered.submissions.empty() && filteredStats.drawCalls == 0,
                "excluding every draw node failed")) return false;
        for (const int invalid : {-1, static_cast<int>(asset->nodes.size())}) {
            const std::array badNodes{hidden[0], invalid};
            filteredOptions.excludedNodes = badNodes;
            filteredStats.drawCalls = 42;
            if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                    filtered, *asset, pose, filteredOptions, filteredStats, &error) &&
                    filtered.submissions.empty() && filteredStats.drawCalls == 42,
                    "invalid node exclusion submitted or overwrote stats")) return false;
        }
    }
    {
        ScopedRecordingBackend scoped;
        GltfSceneDrawOptions managedOptions = options;
        managedOptions.resourceScope = 1u;
        GltfSceneDrawStats managedStats;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                scoped, *asset, pose, managedOptions, managedStats, &error) &&
                !scoped.active && scoped.ended == 1 && scoped.submissions.size() == 2u,
                "managed draw did not close its native scope")) return false;
        const auto priorDraws = managedStats.drawCalls;
        scoped.reportFailure = true;
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                scoped, *asset, pose, managedOptions, managedStats, &error) &&
                !scoped.active && scoped.ended == 2 && managedStats.drawCalls == priorDraws &&
                scoped.batchDepth == 0 && scoped.batchBegins == scoped.batchEnds &&
                error.find("authored allocation failure") != std::string::npos,
                "native allocation failure was hidden or scope remained active")) return false;
        managedOptions.resourceScope = 2u;
        scoped.submissions.clear();
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                scoped, *asset, pose, managedOptions, managedStats, &error) && scoped.submissions.empty(),
                "foreign resource scope submitted draws")) return false;
        RecordingBackend unsupported;
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                unsupported, *asset, pose, managedOptions, managedStats, &error) && unsupported.submissions.empty(),
                "unsupported resource ownership silently fell back")) return false;
    }
    {
        RecordingBackend overlay;
        auto overlayOptions = options;
        overlayOptions.materialMode = 255;
        overlayOptions.translucent = true;
        overlayOptions.tint = {.5f, .75f, 1.0f, .25f};
        GltfSceneDrawStats overlayStats;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                overlay, *asset, pose, overlayOptions, overlayStats, &error),
                "translucent overlay failed: " + error)) return false;
        const auto& before = bindBackend.submissions.front().texture;
        const auto& after = overlay.submissions.front().texture;
        if (!check(after.materialMode == 255 && after.alphaMode == 2 &&
                after.depthTestEnabled == 1 &&
                nearlyEqual(after.vertexColorMulR, before.vertexColorMulR * .5f) &&
                nearlyEqual(after.vertexColorMulA, before.vertexColorMulA * .25f) &&
                after.rgba == before.rgba,
                "overlay must retain textures and apply tint/alpha with depth testing")) return false;
        overlay.submissions.clear();
        overlayOptions.tint[3] = -1.0f;
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(
                overlay, *asset, pose, overlayOptions, overlayStats, &error) &&
                overlay.submissions.empty(), "invalid tint must reject before drawing")) return false;
    }
    for (const auto* alpha : {"MASK", "BLEND"}) {
        FixtureDirectory materialDirectory(std::string("phlosion-gltf-material-") + alpha);
        auto materialSource = source;
        materialSource.document["materials"][0]["alphaMode"] = alpha;
        materialSource.document["materials"][0]["alphaCutoff"] = .3f;
        materialSource.document["extensionsUsed"] = {"KHR_materials_unlit"};
        materialSource.document["extensionsRequired"] = {"KHR_materials_unlit"};
        materialSource.document["materials"][0]["extensions"]["KHR_materials_unlit"] = nlohmann::json::object();
        materialSource.document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = .6f;
        materialSource.document["samplers"][0]["wrapS"] = 33071;
        materialSource.document["samplers"][0]["wrapT"] = 33648;
        std::shared_ptr<const GltfSceneAsset> materialAsset;
        const auto materialPath = stageScene(materialDirectory, materialSource);
        if (!check(loadGltfSceneAsset(materialPath.string(), materialAsset, &error), error)) return false;
        GltfScenePose materialPose;
        if (!check(sampleGltfScenePose(materialAsset, 0, 0, materialPose, &error), error)) return false;
        RecordingBackend materialBackend;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(materialBackend,
                *materialAsset, materialPose, options, stats, &error), error)) return false;
        const bool blend = std::string_view(alpha) == "BLEND";
        const auto& draw = materialBackend.submissions[blend ? 1 : 0];
        if (!check(draw.texture.alphaMode == (blend ? 2 : 1) &&
                   materialAsset->materials[0].unlit && draw.texture.materialMode == 255 &&
                   nearlyEqual(draw.texture.alphaCutoff, .3f) &&
                   nearlyEqual(draw.texture.vertexColorMulA, .6f) &&
                   draw.texture.wrapS == 33071 && draw.texture.wrapT == 33648 &&
                   draw.geometryKey == materialAsset->primitives[0].geometryKey,
                   "source alpha/wrapping or opaque-before-blended order was lost")) return false;
    }
    outFail.clear();
    return true;
}
