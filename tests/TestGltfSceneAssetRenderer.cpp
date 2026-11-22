#include <thread>
// tests/TestGltfSceneAssetRenderer.cpp
//
// CPU contract for the generic glTF scene draw adapter. A recording backend
// captures exactly what the adapter submits through the existing cached
// textured world-mesh entry point: five-map pointers, scalar factors, node
// matrices and skin palettes. No graphics context is created.

#include "gltf_import/GltfSceneTestSupport.h"

#include "engine/render/gltf/GltfSceneAsset.h"
#include "engine/render/gltf/GltfSceneRenderer.h"
#include "engine/render/gltf/GltfSceneDrawPlan.h"
#include <cstring>
#include "engine/render/IWorldSurfaceBuffer.h"

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

class ComposedRecordingBackend final : public RecordingBackend, public engine::render::IWorldSurfaceBuffer {
public:
    engine::render::WorldClusteredLightingView lighting;
    bool composed=true;
    const engine::render::WorldClusteredLightingView* worldSurfaceCompositionLighting() const override {
        return composed?&lighting:nullptr;
    }
    bool beginWorldSurfaceBuffer(const float*,int,int) override {return false;}
    bool endWorldSurfaceBuffer() override {return false;}
    void clearWorldSurfaceBuffer() override {}
    bool resolveWorldSurfaceContact(const engine::render::WorldContactUniforms&) override {return false;}
    bool resolveWorldSurfaceScreenReflection() override {return false;}
    bool showWorldSurfaceBuffer(unsigned) override {return false;}
};

class ScopedRecordingBackend : public RecordingBackend, public IWorldResourceScope {
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

class PreparedRecordingBackend final : public ScopedRecordingBackend, public engine::render::IWorldPreparedResources {
public:
    using Token=engine::render::WorldPreparedResourceToken;
    engine::render::WorldPreparedResourceStats resources;
    Token prepareWorldGeometry(const char*,const WorldMeshVertex*,std::size_t,
        const std::uint32_t*,std::size_t) override {
        ++resources.geometryPreparations;return {this,1,1,0};
    }
    Token prepareWorldMaterial(const WorldTextureData&) override {
        ++resources.materialPreparations;return {this,1,1,0};
    }
    bool worldPreparedGeometryValid(Token t) const override {return active && t.owner==this;}
    bool worldPreparedMaterialValid(Token t) const override {return active && t.owner==this;}
    bool drawWorldPreparedIndexed(Token,Token,const WorldTextureData&,const float*,int,int) override {
        ++resources.draws;return false;
    }
    engine::render::WorldPreparedResourceStats worldPreparedResourceStats() const override {return resources;}
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

    {
        PreparedRecordingBackend prepared;
        GltfSceneDrawOptions warm;warm.surfaceWidth=320;warm.surfaceHeight=180;
        warm.resourceScope=1;warm.prewarmOnly=true;
        GltfSceneDrawStats warmed;
        if(!check(engine::render::gltf::scene::drawGltfSceneAsset(prepared,*asset,pose,warm,warmed,&error),error))return false;
        warm.materialMode=7;
        if(!check(engine::render::gltf::scene::drawGltfSceneAsset(prepared,*asset,pose,warm,warmed,&error),error))return false;
        if(!check(prepared.preparedTextures==4 && prepared.preparedGeometry==0 &&
            prepared.resources.geometryPreparations==2 && prepared.resources.materialPreparations==2 &&
            prepared.resources.draws==0 && warmed.drawCalls==0 && !prepared.active,
            "Retained bindings must preserve shader prewarming without drawing or rebuilding resources"))return false;
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
        std::array<std::array<float,3>,2> colors{{{8.f,1.f,3.f},{0,0,0}}};
        animated.emissiveFactors = colors;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(
                backend,*asset,pose,animated,stats,&error) &&
                nearlyEqual(backend.submissions[0].texture.emissiveFactorR,8.f) &&
                backend.submissions[1].texture.emissiveFactorB == 0 &&
                backend.submissions[0].texture.emissiveRgba == asset->materials[0].emissive.rgba.data(),
                "HDR emission overrides must retain textures and isolate material factors")) return false;
        for (float bad : {-1.f,65505.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
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
        overlayOptions.tint={2,1,1,1};
        if(!check(engine::render::gltf::scene::drawGltfSceneAsset(
                overlay,*asset,pose,overlayOptions,overlayStats,&error) &&
                nearlyEqual(overlay.submissions.front().texture.vertexColorMulR,before.vertexColorMulR*2),
                "Double-intensity RGB tint lost"))return false;
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
    // Imported bounds must enclose decoded vertices, even if accessor metadata
    // is absent. Opt-in culling must never affect skinning or prewarm.
    for (const auto& primitive : asset->primitives) {
        if (!check(primitive.boundsValid, "import did not compute bounds")) return false;
        for (const auto& v : primitive.vertices) {
            const std::array position{v.x,v.y,v.z};
            for (int axis=0;axis<3;++axis)
                if (!check(position[axis]>=primitive.boundsMin[axis] &&
                           position[axis]<=primitive.boundsMax[axis],
                           "decoded vertex outside imported bounds")) return false;
        }
    }
    {
        auto boxAsset = *asset;
        boxAsset.primitives = {asset->primitives[1]};
        auto& box = boxAsset.primitives.front();
        box.skinIndex = -1;
        box.boundsMin = {-.5f,-.5f,-.5f}; box.boundsMax = {.5f,.5f,.5f};
        auto boxPose = pose;
        boxPose.palettes = {{}};
        boxPose.global[box.nodeIndex] = glm::mat4(1.f);
        GltfSceneDrawOptions culledOptions;
        culledOptions.surfaceWidth=320; culledOptions.surfaceHeight=180;
        culledOptions.cullingViewProjection = culledOptions.viewProjectionMatrix;
        const auto setMatrix=[](auto& out,const glm::mat4& matrix) {
            std::copy_n(glm::value_ptr(matrix),16,out.begin());
        };
        const auto expectDraws=[&](unsigned expected,const char* label) {
            RecordingBackend b; GltfSceneDrawStats s;
            return check(engine::render::gltf::scene::drawGltfSceneAsset(
                b,boxAsset,boxPose,culledOptions,s,&error) &&
                b.submissions.size()==expected && s.drawCalls==expected &&
                s.culledDraws==1-expected &&
                s.culledTriangles==(1-expected)*box.indices.size()/3 &&
                b.batchDepth==0, label);
        };
        if (!expectDraws(1,"inside box culled")) return false;
        for (int axis=0;axis<3;++axis) for (float sign : {-1.f,1.f}) {
            glm::vec3 offset(0); offset[axis]=sign*2;
            setMatrix(culledOptions.instanceTransform,glm::translate(glm::mat4(1),offset));
            if (!expectDraws(0,"outside box submitted")) return false;
            offset[axis]=sign*1.5f;
            setMatrix(culledOptions.instanceTransform,glm::translate(glm::mat4(1),offset));
            if (!expectDraws(1,"plane-touching box culled")) return false;
        }
        // Instance + animated node placement, mirrored scale and shear.
        boxPose.global[box.nodeIndex]=glm::translate(glm::mat4(1),glm::vec3(-2,0,0));
        setMatrix(culledOptions.instanceTransform,glm::translate(glm::mat4(1),glm::vec3(2,0,0)));
        if (!expectDraws(1,"composed node transform ignored")) return false;
        boxPose.global[box.nodeIndex]=glm::mat4(1);
        auto transform=glm::scale(glm::mat4(1),glm::vec3(-6,3,.5f)); transform[1][0]=4;
        setMatrix(culledOptions.instanceTransform,transform);
        if (!expectDraws(1,"frustum-enclosing sheared box culled")) return false;
        *culledOptions.cullingViewProjection=culledOptions.viewProjectionMatrix;
        setMatrix(culledOptions.instanceTransform,glm::mat4(1));
        setMatrix(*culledOptions.cullingViewProjection,glm::perspective(glm::radians(90.f),1.f,.1f,10.f));
        if (!expectDraws(1,"eye-crossing box culled")) return false;
        setMatrix(culledOptions.instanceTransform,glm::translate(glm::mat4(1),glm::vec3(0,0,2)));
        if (!expectDraws(0,"behind-eye box submitted")) return false;
        box.boundsValid=false;
        if (!expectDraws(1,"missing bounds did not fail open")) return false;
        box.boundsValid=true; box.boundsMin[0]=2;
        if (!expectDraws(1,"invalid bounds did not fail open")) return false;
        box.boundsMin[0]=-.5f; culledOptions.materialMode=248;
        if (!expectDraws(1,"custom vertex material was culled")) return false;
        culledOptions.materialMode=2; culledOptions.prewarmOnly=true;
        RecordingBackend warm; GltfSceneDrawStats warmStats;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(warm,boxAsset,boxPose,
            culledOptions,warmStats,&error) && warm.preparedGeometry==1 &&
            warm.preparedTextures==1 && warmStats.culledDraws==0,
            "outside primitive skipped prewarm")) return false;
        culledOptions.prewarmOnly=false;
        (*culledOptions.cullingViewProjection)[0]=std::numeric_limits<float>::quiet_NaN();
        warmStats.drawCalls=99;
        if (!check(!engine::render::gltf::scene::drawGltfSceneAsset(warm,boxAsset,boxPose,
            culledOptions,warmStats,&error) && warm.submissions.empty() &&
            warmStats.drawCalls==99,"nonfinite culling matrix accepted")) return false;
        // Far outside in every plane: the skinned mesh still gets its palette.
        culledOptions.cullingViewProjection=culledOptions.viewProjectionMatrix;
        setMatrix(culledOptions.instanceTransform,glm::translate(glm::mat4(1),glm::vec3(10000)));
        RecordingBackend rig; GltfSceneDrawStats rigStats;
        if (!check(engine::render::gltf::scene::drawGltfSceneAsset(rig,*asset,pose,
            culledOptions,rigStats,&error) && rigStats.culledDraws==1 &&
            rigStats.skinnedDraws==1 && rig.submissions.size()==1,
            "skinned mesh was culled using bind-pose bounds")) return false;
        culledOptions.cullSkinnedShadow=true;
        RecordingBackend shadowRig;GltfSceneDrawStats shadowStats;
        if(!check(drawGltfSceneAsset(shadowRig,*asset,pose,culledOptions,shadowStats,&error) &&
            shadowStats.culledDraws==2 && shadowRig.submissions.empty(),
            "Shadow query retained a distant current-pose caster"))return false;
        culledOptions.cullSkinnedShadow=false;
        RecordingBackend visibleRig;GltfSceneDrawStats visibleStats;
        if(!check(drawGltfSceneAsset(visibleRig,*asset,pose,culledOptions,visibleStats,&error) &&
            visibleStats.skinnedDraws==1,"Shadow selection leaked into the ordinary receiver"))return false;
    }

    // Exercise the producer through actual sampled glTF poses, including a
    // second instance of the same asset. Never infer identity from geometry.
    {
        using namespace engine::render;
        WorldMotionHistory history;
        WorldMotionView view;view.identity=1;view.generation=1;view.frame=1;
        view.width=320;view.height=180;
        GltfSceneDrawOptions capture=options;
        capture.motionHistory=&history;capture.motionObject=100;
        RecordingBackend recorder;
        GltfSceneDrawStats captured;
        if(!check(!drawGltfSceneAsset(recorder,*asset,pose,capture,captured,&error),
            "adapter accepted a missing motion view"))return false;
        WorldMotionCapture frame(history,view);
        capture.prewarmOnly=true;
        if(!check(drawGltfSceneAsset(recorder,*asset,pose,capture,captured,&error),
            "motion prewarm failed"))return false;
        capture.prewarmOnly=false;
        if(!check(drawGltfSceneAsset(recorder,*asset,pose,capture,captured,&error),
            "motion adapter rejected real pose"))return false;
        capture.motionObject=101;
        capture.instanceTransform[12]=20;
        if(!check(drawGltfSceneAsset(recorder,*asset,pose,capture,captured,&error),
            "two instances of same asset collided"))return false;
        frame.commit();
        if(!check(history.stats().objects==4 && history.stats().valid==0 &&
            history.stats().deformed==2,"motion producer coverage incorrect"))return false;
        // The next draw uses a newly sampled skeleton, still the same owner.
        GltfScenePose nextPose;
        if(!check(sampleGltfScenePose(asset,0,0,nextPose,&error),"next motion pose failed"))return false;
        view.frame=2;
        WorldMotionCapture next(history,view);
        if(!check(drawGltfSceneAsset(recorder,*asset,nextPose,capture,captured,&error),
            "next motion submission failed"))return false;
        next.commit();
        if(!check(history.stats().objects==2 && history.stats().valid==2,
            "sampled motion did not retain previous skeleton and prop"))return false;
        // Transparency has no trustworthy opaque motion/identity coverage.
        view.frame=3;WorldMotionCapture transparent(history,view);
        capture.translucent=true;
        if(!check(drawGltfSceneAsset(recorder,*asset,nextPose,capture,captured,&error),
            "transparent motion draw failed"))return false;
        transparent.commit();
        if(!check(history.stats().objects==0,"transparency entered opaque history"))return false;
    }
    {
        ComposedRecordingBackend composed;
        GltfSceneDrawOptions draw=options;draw.environment.enabled=true;
        draw.environment.sunDirection={0,1,0};draw.environment.exposure=1;
        draw.cameraForward={0,0,-1};
        composed.lighting={draw.environment,draw.cameraPosition,draw.cameraForward};
        auto composedAsset=*asset;
        for(auto& m:composedAsset.materials){m.unlit=false;m.alphaMode=0;}
        const auto run=[&](unsigned remaining,unsigned integrated,const char* why) {
            composed.submissions.clear();GltfSceneDrawStats result;
            return check(engine::render::gltf::scene::drawGltfSceneAsset(composed,composedAsset,pose,draw,result,&error) &&
                composed.submissions.size()==remaining && result.composedDraws==integrated &&
                result.drawCalls==remaining,why);
        };
        if(!run(0,2,"composed standard primitives reached backend"))return false;
        composedAsset.materials[composedAsset.primitives[1].materialIndex].alphaMode=2;
        if(!run(1,1,"transparent primitive was suppressed"))return false;
        composedAsset.materials[composedAsset.primitives[1].materialIndex].alphaMode=1;
        if(!run(0,2,"masked standard primitive was replayed"))return false;
        composedAsset.materials[composedAsset.primitives[1].materialIndex].unlit=true;
        if(!run(1,1,"unlit primitive was suppressed"))return false;
        draw.materialMode=240;
        if(!run(2,0,"specialized material was suppressed"))return false;
        draw.materialMode=2;draw.cameraPosition[0]+=1;
        if(!run(2,0,"different view was suppressed"))return false;
        draw.cameraPosition[0]-=1;draw.environment.pointLights[0].intensity={1,2,3};draw.environment.pointLights[0].radius=5;
        if(!run(2,0,"different lights were suppressed"))return false;
        draw.environment=composed.lighting.environment;
        composed.composed=false;
        if(!run(2,0,"ordinary draw was suppressed outside composition"))return false;
        composed.composed=true;draw.prewarmOnly=true;
        composed.submissions.clear();GltfSceneDrawStats warm;
        if(!check(engine::render::gltf::scene::drawGltfSceneAsset(composed,composedAsset,pose,draw,warm,&error) &&
            composed.preparedGeometry==2 && composed.preparedTextures==2 && warm.composedDraws==0,
            "composition suppressed prewarm uploads"))return false;
        draw.prewarmOnly=false;
        auto bad=pose;bad.global[composedAsset.primitives[0].nodeIndex][0][0]=std::numeric_limits<float>::quiet_NaN();
        composed.submissions.clear();
        if(!check(!engine::render::gltf::scene::drawGltfSceneAsset(composed,composedAsset,bad,draw,warm,&error) &&
            composed.submissions.empty(),"early composition bypassed transactional validation"))return false;
    }

    // Published snapshots retain CPU work; mutable copies must always reflect
    // their current content. Compare observable draw order, transforms, skinning
    // and dynamic factors rather than private cache representation.
    {
        using namespace engine::render::gltf::scene;
        std::shared_ptr<const GltfSceneAsset> cached;
        if(!check(loadGltfSceneAsset(document.string(),cached,&error),"cache fixture reload failed"))return false;
        GltfScenePose current;
        if(!check(sampleGltfScenePose(cached,0,0,current,&error),"cache pose failed"))return false;
        auto fresh=*cached;
        GltfSceneDrawOptions draw=options;
        draw.cullingViewProjection=draw.viewProjectionMatrix;
        std::vector<std::array<float,4>> colors(cached->materials.size(),{.2f,.4f,.6f,1});
        std::vector<std::array<float,3>> emissions(cached->materials.size(),{1,2,3});
        draw.baseColorFactors=colors;draw.emissiveFactors=emissions;
        const auto compare=[&]() {
            RecordingBackend retained,reference;GltfSceneDrawStats a,b;
            if(!drawGltfSceneAsset(retained,*cached,current,draw,a,&error) ||
               !drawGltfSceneAsset(reference,fresh,current,draw,b,&error))return check(false,error);
            if(!check(retained.submissions.size()==reference.submissions.size() &&
                a.drawCalls==b.drawCalls && a.culledDraws==b.culledDraws &&
                a.composedDraws==b.composedDraws && a.triangles==b.triangles &&
                retained.preparedGeometry==reference.preparedGeometry,"retained selection changed"))return false;
            for(std::size_t i=0;i<retained.submissions.size();++i) {
                const auto& x=retained.submissions[i];const auto& y=reference.submissions[i];
                if(!check(x.geometryKey==y.geometryKey && x.palette==y.palette &&
                    x.texture.modelMatrix==y.texture.modelMatrix &&
                    x.texture.alphaMode==y.texture.alphaMode && x.texture.materialMode==y.texture.materialMode &&
                    x.texture.vertexColorMulR==y.texture.vertexColorMulR &&
                    x.texture.emissiveFactorG==y.texture.emissiveFactorG &&
                    x.texture.cameraPosX==y.texture.cameraPosX &&
                    x.texture.environment.pointLights[0].intensity==y.texture.environment.pointLights[0].intensity,
                    "retained draw differs from fresh preparation"))return false;
                if(!check(x.texture.vertexColorMulR==colors[cached->primitives[i].materialIndex][0] &&
                    x.texture.emissiveFactorG==emissions[cached->primitives[i].materialIndex][1],
                    "live material factors were frozen"))return false;
            }
            return true;
        };
        for(int i=0;i<20;++i)if(!compare())return false;
        auto reuse=gltfScenePreparationStats(*cached);
        if(!check(reuse.instanceBuilds==1 && reuse.instanceReuses==19 &&
            reuse.visibilityBuilds==1 && reuse.visibilityReuses==19,
            "identical views repeated transform or visibility preparation"))return false;
        const auto materialReuse=gltfSceneMaterialPreparationStats(*cached);
        if(!check(materialReuse.builds==1 && materialReuse.reuses==19 &&
            materialReuse.materialEvaluations==cached->materials.size(),
            "identical views repeated material validation/evaluation"))return false;
        draw.cameraPosition[0]+=3;draw.environment.surfaceWetness=.7f;
        draw.environment.pointLights[0].intensity={11,3,5};
        if(!compare())return false;
        if(!check(gltfSceneMaterialPreparationStats(*cached).builds==1,
            "view lighting contaminated shared material inputs"))return false;
        colors[0][0]=.8f;emissions[0][1]=17;
        draw.environment.pointLights[0].intensity={5,7,9};draw.cameraPosition[0]=12;
        if(!compare())return false;
        if(!check(gltfScenePreparationStats(*cached).instanceBuilds==1,
            "dynamic lighting rebuilt static transforms"))return false;
        if(!check(gltfSceneMaterialPreparationStats(*cached).builds==2,
            "in-place factor mutation reused stale material inputs"))return false;
        const auto original=current;
        current.global[cached->primitives[1].nodeIndex][3][0]+=3;
        current.palettes[0][0][3][0]+=.3f;
        if(!compare())return false;
        if(!check(gltfScenePreparationStats(*cached).instanceBuilds==2,
            "in-place pose mutation reused stale transforms"))return false;
        current=original;
        if(!compare())return false;
        if(!check(gltfScenePreparationStats(*cached).instanceBuilds==2,
            "restored pose did not reuse owned input snapshot"))return false;
        draw.instanceTransform[12]=7;if(!compare())return false;
        (*draw.cullingViewProjection)[12]=19;if(!compare())return false;
        int omitted=cached->primitives[1].nodeIndex;draw.excludedNodes={&omitted,1};
        if(!compare())return false;
        draw.prewarmOnly=true;if(!compare())return false;
        draw.prewarmOnly=false;draw.translucent=true;if(!compare())return false;
        draw.materialMode=240;if(!compare())return false;
        draw.excludedNodes={};draw.translucent=false;draw.materialMode=2;
        // Vary views and placements beyond capacities. Old immutable selections
        // remain valid for an already prepared consumer even after eviction.
        auto plan=detail::GltfSceneDrawPlanAccess::get(*cached);
        std::shared_ptr<const detail::GltfPreparedInstance> held;
        if(!check(plan->prepareInstance(*cached,current,draw,held,&error),"held instance failed"))return false;
        const auto selection=plan->select(*cached,*held,draw);
        const auto heldModel=held->models;const auto heldDrawCount=selection->draws.size();
        for(int i=0;i<24;++i) {
            draw.instanceTransform[12]=float(i);
            for(int view=0;view<20;++view) {
                (*draw.cullingViewProjection)[12]=float(view);
                if(!compare())return false;
            }
        }
        const auto bounded=gltfScenePreparationStats(*cached);
        if(!check(bounded.instanceRecords<=4 && bounded.visibilityRecords<=16 &&
            bounded.retainedInstanceBytes<=(8u<<20) && bounded.retainedVisibilityBytes<=(2u<<20) &&
            held->models==heldModel && selection->draws.size()==heldDrawCount,
            "cache ownership or retention bound failed"))return false;
        std::shared_ptr<const detail::GltfPreparedMaterials> heldMaterials;
        if(!check(plan->prepareMaterials(*cached,draw,heldMaterials,&error),"held materials failed"))return false;
        const auto heldColor=heldMaterials->values[0].color;
        for(int i=0;i<12;++i) {
            colors[0][0]=float(i)/16.f;
            if(!compare())return false;
        }
        std::vector<float> channels(cached->materials.size(),1.f);
        draw.probeEmissionChannels=channels;
        if(!compare())return false;
        channels[0]=2.f;
        RecordingBackend channelDraw;GltfSceneDrawStats channelStats;
        if(!check(drawGltfSceneAsset(channelDraw,*cached,current,draw,channelStats,&error),error))return false;
        for(const auto& submission:channelDraw.submissions)
            if(!check(submission.texture.materialTimeSec==
                (submission.geometryKey==cached->primitives[0].geometryKey?2.f:1.f),
                "probe channel mutation was frozen"))return false;
        draw.probeEmissionChannels={};
        auto materialBound=gltfSceneMaterialPreparationStats(*cached);
        if(!check(materialBound.records<=4 && materialBound.retainedBytes<=(1u<<20) &&
            heldMaterials->values[0].color==heldColor,"material cache retention/ownership failed"))return false;
        std::vector<int> omitAll;
        for(std::size_t i=0;i<cached->nodes.size();++i)omitAll.push_back(int(i));
        auto noGeometry=draw;noGeometry.excludedNodes=omitAll;
        RecordingBackend emptyView;GltfSceneDrawStats emptyStats;
        if(!check(drawGltfSceneAsset(emptyView,*cached,current,noGeometry,emptyStats,&error) &&
            emptyView.submissions.empty() &&
            gltfSceneMaterialPreparationStats(*cached).viewMaterialPreparations==materialBound.viewMaterialPreparations,
            "empty selection prepared view material bindings"))return false;
        // Failed inputs cannot hit a previously valid record or partially draw.
        const auto rejected=[&](const GltfScenePose& invalid,const GltfSceneDrawOptions& opts) {
            RecordingBackend recorder;GltfSceneDrawStats result;result.drawCalls=99;
            return check(!drawGltfSceneAsset(recorder,*cached,invalid,opts,result,&error) &&
                recorder.submissions.empty() && result.drawCalls==99,"invalid cached input reached renderer");
        };
        auto bad=current;bad.global[cached->primitives[0].nodeIndex][0][0]=std::numeric_limits<float>::quiet_NaN();
        if(!rejected(bad,draw))return false;
        bad=current;bad.palettes[0][0][0][0]=std::numeric_limits<float>::quiet_NaN();
        if(!rejected(bad,draw))return false;
        auto badOptions=draw;int invalidNode=-1;badOptions.excludedNodes={&invalidNode,1};
        if(!rejected(current,badOptions))return false;
        float channel=0;badOptions=draw;badOptions.probeEmissionChannels={&channel,1};
        if(!rejected(current,badOptions))return false;
        badOptions=draw;badOptions.tint[0]=std::numeric_limits<float>::quiet_NaN();
        if(!rejected(current,badOptions))return false;
        auto invalidColors=colors;invalidColors[0][0]=2;
        badOptions=draw;badOptions.baseColorFactors=invalidColors;
        if(!rejected(current,badOptions))return false;
        auto invalidEmissions=emissions;invalidEmissions[0][0]=65505;
        badOptions=draw;badOptions.emissiveFactors=invalidEmissions;
        if(!rejected(current,badOptions))return false;
        channels[0]=std::numeric_limits<float>::quiet_NaN();
        badOptions=draw;badOptions.probeEmissionChannels=channels;
        if(!rejected(current,badOptions))return false;
        detail::GltfSceneDrawPlanHandle handle;handle.value=plan;
        auto copiedHandle=handle;auto movedHandle=std::move(handle);
        if(!check(!copiedHandle.value && !movedHandle.value && !handle.value,"copy/move inherited owner-specific plan"))return false;
        copiedHandle.value=plan;copiedHandle=movedHandle;
        if(!check(!copiedHandle.value,"assignment retained stale plan"))return false;
        if(!check(!detail::GltfSceneDrawPlanAccess::get(fresh),"copied snapshot retained another owner's bindings"))return false;
        fresh.materials[0].alphaMode=2;fresh.materials[0].baseColor.rgba[0]^=255;
        RecordingBackend changed;GltfSceneDrawStats changedStats;
        draw.cullingViewProjection.reset();draw.instanceTransform=options.instanceTransform;
        if(!check(drawGltfSceneAsset(changed,fresh,current,draw,changedStats,&error) &&
            changed.submissions.size()==2 &&
            changed.submissions.back().geometryKey==fresh.primitives[0].geometryKey &&
            changed.submissions.back().texture.rgba==fresh.materials[0].baseColor.rgba.data(),
            "mutable asset copy reused original order or texture pointers"))return false;
        if(!check(gltfScenePreparationStats(*asset).instanceBuilds!=0 &&
            cached->generation!=asset->generation && !detail::GltfSceneDrawPlanAccess::get(fresh),
            "reload/copy ownership is not isolated"))return false;
    }

    // Collected views own pose/material inputs and survive cache eviction and
    // producer teardown. Preparation must not issue GPU work or publish failures.
    {
        using namespace engine::render::gltf::scene;
        std::shared_ptr<const GltfSceneAsset> collectedAsset;
        if(!check(loadGltfSceneAsset(document.string(),collectedAsset,&error),"collection asset load failed"))return false;
        GltfScenePose current;
        if(!check(sampleGltfScenePose(collectedAsset,0,kBindPoseClip,current,&error),"collection pose failed"))return false;
        GltfSceneDrawOptions draw;draw.surfaceWidth=640;draw.surfaceHeight=360;
        GltfSceneCollectedView held;
        RecordingBackend expected;
        GltfSceneDrawStats drawn;
        {
            const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
            GltfSceneCollectionScheduler scheduler;
            const auto owner=std::this_thread::get_id();bool independent=false,independentJoined=false;
            scheduler.collectIndependent([&]{require(std::this_thread::get_id()==owner,"Collection owner migrated");},
                [&]{independent=true;});
            require(independent,"Independent collection did not join");
            try{scheduler.collectIndependent([]{throw std::runtime_error("owner failure");},[&]{independentJoined=true;});}
            catch(const std::runtime_error&){}
            require(independentJoined,"Owner failure escaped before independent collection joined");
            scheduler.collectIndependent([]{},[]{});
            GltfSceneViewFamily family;std::size_t id=99;
            if(!check(family.collect(collectedAsset,current,draw.instanceTransform,id,&error) && id==0,"collection failed"))return false;
            std::vector<std::array<float,3>> emission(collectedAsset->materials.size(),{.2f,.3f,.4f});
            draw.emissiveFactors=emission;
            std::vector<GltfSceneViewRequest> requests{{id,draw}};
            if(!check(drawGltfSceneAsset(expected,*collectedAsset,current,draw,drawn,&error),"collection reference failed"))return false;
            if(!check(family.prepareView(requests,held,&error) && held.size()==1,"view preparation failed"))return false;
            auto counts=gltfScenePreparationStats(*collectedAsset);
            current.global[0][3][0]+=200;current.palettes[0][0][3][0]+=100;
            emission[0]={10,20,30};draw.emissiveFactors={};
            RecordingBackend replay;
            if(!check(held.draw(replay,0,drawn,&error) && replay.submissions.size()==expected.submissions.size(),
                "collected view lost submissions"))return false;
            for(std::size_t i=0;i<replay.submissions.size();++i) {
                const auto& a=replay.submissions[i];const auto& b=expected.submissions[i];
                if(!check(a.geometryKey==b.geometryKey && a.texture.modelMatrix==b.texture.modelMatrix &&
                    a.palette==b.palette && a.texture.emissiveFactorR==b.texture.emissiveFactorR,
                    "collection borrowed changed pose or material inputs"))return false;
            }
            if(!check(gltfScenePreparationStats(*collectedAsset).instanceReuses==counts.instanceReuses,
                "collected submission repeated instance lookup"))return false;
            GltfSceneCollectedView unchanged=held;
            requests.push_back({999,draw});
            if(!check(!family.prepareView(requests,held,&error) && held.size()==unchanged.size(),
                "invalid complete view was published"))return false;
            requests.resize(1);requests[0].options.instanceTransform[12]=1;
            if(!check(!family.prepareView(requests,held,&error),"changed placement accepted without collection"))return false;
            requests[0].options=draw;requests[0].options.surfaceWidth=0;
            if(!check(!family.prepareView(requests,held,&error),"invalid view dimensions accepted"))return false;
            auto mutableCopy=std::make_shared<GltfSceneAsset>(*collectedAsset);
            if(!check(!family.collect(mutableCopy,current,draw.instanceTransform,id,&error),"mutable asset accepted as immutable"))return false;
            {
                GltfSceneViewFamily plan(&scheduler);
                GltfScenePose fresh;require(sampleGltfScenePose(collectedAsset,0,kBindPoseClip,fresh,&error),"plan pose");
                auto input=draw;input.instanceTransform[12]=311;input.motionObject=77;
                std::size_t object=0;require(plan.collect(collectedAsset,fresh,input.instanceTransform,object,&error),"plan object");
                std::vector<std::array<float,3>> colors(collectedAsset->materials.size(),{.7f,.2f,.1f});
                input.emissiveFactors=colors;
                std::array<GltfSceneViewRequest,1> request{{{object,input}}};
                std::size_t first=99,second=99;
                require(plan.planView(request,first,&error) && plan.planView(request,second,&error),"plan snapshots");
                bool premature=false;try{(void)plan.plannedView(first);}catch(const std::out_of_range&){premature=true;}
                require(premature,"Unprepared view was published");
                colors.assign(colors.size(),{9,8,7});
                bool threw=false;
                try{plan.preparePlannedViews(&error,[]{throw std::runtime_error("selection worker failure");});}
                catch(const std::runtime_error&){threw=true;}
                require(threw,"Independent failure was swallowed");
                premature=false;try{(void)plan.plannedView(first);}catch(const std::out_of_range&){premature=true;}
                require(premature,"Failed view batch was published");
                bool joined=false;require(plan.preparePlannedViews(&error,[&]{joined=true;}),"planned selection failed");
                require(joined && plan.stats().views==2 && plan.stats().selectionBuilds==1 &&
                    plan.stats().selectionReuses==1,"Cross-view duplicate selection was rebuilt");
                RecordingBackend firstDraw,secondDraw;
                require(plan.plannedView(first).drawWithMotion(firstDraw,0,nullptr,drawn,&error) &&
                    plan.plannedView(second).drawWithMotion(secondDraw,0,nullptr,drawn,&error),"planned draw");
                require(!firstDraw.submissions.empty() && firstDraw.submissions.size()==secondDraw.submissions.size() &&
                    firstDraw.submissions[0].texture.emissiveFactorR<1,"Plan borrowed caller material scratch");

                RecordingBackend budgeted;
                std::size_t cursor=0;bool complete=false;unsigned batches=0;
                const auto budgetView=plan.plannedView(first);
                while(!complete){
                    require(budgetView.drawBudgeted(budgeted,0,cursor,1,1,complete,drawn,&error),"budgeted collection draw");
                    require(++batches<=firstDraw.submissions.size()+1,"budgeted draw failed to advance");
                }
                require(budgeted.submissions.size()==firstDraw.submissions.size(),"budgeted draw lost primitives");
                for(std::size_t i=0;i<budgeted.submissions.size();++i)
                    require(budgeted.submissions[i].geometryKey==firstDraw.submissions[i].geometryKey&&
                        budgeted.submissions[i].palette==firstDraw.submissions[i].palette,"budgeted draw reordered material/pose inputs");
                engine::render::WorldMotionHistory inactive;
                require(!plan.plannedView(first).drawWithMotion(firstDraw,0,&inactive,drawn,&error),
                    "Inactive live motion view accepted");
                const auto before=plan.stats();
                require(plan.preparePlannedViews(&error) && plan.stats().selectionBuilds==before.selectionBuilds,
                    "Prepared plan repeated selection work");
            }
            // More than the per-asset instance/selection capacities, using the
            // same asset on both lanes, exercises disjoint joined output records.
            GltfScenePose fresh;
            if(!check(sampleGltfScenePose(collectedAsset,0,kBindPoseClip,fresh,&error),"fresh collection pose failed"))return false;
            constexpr unsigned collectionCount=192;
            std::vector<GltfSceneViewRequest> many;
            for(unsigned i=0;i<collectionCount;++i) {
                auto placed=draw;placed.instanceTransform[12]=float(i);
                if(!check(family.collect(collectedAsset,fresh,placed.instanceTransform,id,&error),"many collection failed"))return false;
                many.push_back({id,placed});
            }
            GltfSceneCollectedView parallel;
            if(!check(family.prepareView(many,parallel,&error) && parallel.size()==collectionCount &&
                family.stats().parallelBatches==1,"joined visibility job coverage missing"))return false;
            RecordingBackend joined;
            for(std::size_t i=0;i<parallel.size();++i)if(!check(parallel.draw(joined,i,drawn,&error),"joined draw failed"))return false;
            if(!check(joined.submissions.size()==collectionCount*collectedAsset->primitives.size(),"joined job dropped draws"))return false;
            for(std::size_t i=0;i<collectionCount;++i) {
                RecordingBackend serial;auto pose=fresh;
                if(!check(drawGltfSceneAsset(serial,*collectedAsset,pose,many[i].options,drawn,&error),"serial joined reference failed"))return false;
                for(std::size_t k=0;k<serial.submissions.size();++k) {
                    const auto& a=joined.submissions[i*serial.submissions.size()+k];const auto& b=serial.submissions[k];
                    if(!check(a.geometryKey==b.geometryKey && a.texture.modelMatrix==b.texture.modelMatrix && a.palette==b.palette,
                        "joined selection changed order/pose"))return false;
                }
            }
        }
        collectedAsset.reset();current={};
        RecordingBackend survivor;
        if(!check(held.draw(survivor,0,drawn,&error) && survivor.submissions.size()==expected.submissions.size() &&
            survivor.submissions[0].palette==expected.submissions[0].palette,
            "view outlived its owned snapshots"))return false;
        const auto previous=drawn.drawCalls;survivor.submissions.clear();
        if(!check(!held.draw(survivor,999,drawn,&error) && survivor.submissions.empty() && drawn.drawCalls==previous,
            "invalid collected draw touched the backend/stats"))return false;
    }

    outFail.clear();
    return true;
}
