// tests/TestGltfSceneAsset.cpp
//
// CPU contracts for the reusable glTF scene path introduced for the
// Real-file integration: per-node primitive
// bindings, material source preservation and factor separation, bind and
// animated palettes, unsupported-subset diagnostics, bounded arithmetic,
// failure-preserved publication and retained ownership after the source is
// removed or replaced. No graphics context or renderer is created here.

#include "gltf_import/GltfSceneTestSupport.h"

#include "engine/render/gltf/GltfSceneAsset.h"

#include <glm/gtc/matrix_transform.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using engine::render::gltf::scene::GltfSceneAsset;
using engine::render::gltf::scene::GltfSceneLoadOptions;
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
    if (document.empty()) {
        return {};
    }
    if (!writeText(document, source.document.dump(2))) {
        return {};
    }
    if (!writeBytes(directory.file("scene.bin"), source.binary)) {
        return {};
    }
    if (!writeBytes(directory.file("base_color.png"),
                    gltf_scene_contract::authoredBaseColorPngBytes())) {
        return {};
    }
    return document;
}

void patchU16(SceneSource& source, std::size_t offset, std::uint16_t value) {
    source.binary[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    source.binary[offset + 1u] =
        static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void patchF32(SceneSource& source, std::size_t offset, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
        source.binary[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((bits >> shift) & 0xFFu);
    }
}

GltfSceneLoadOptions defaultOptions() { return GltfSceneLoadOptions{}; }

}  // namespace

bool test_gltf_scene_asset_contract(std::string& outFail) {
    const gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
    const auto check = [&outFail](bool condition, std::string message) {
        return gltf_import_contract::expect(
            condition, std::move(message), outFail);
    };

    FixtureDirectory directory("phlosion-gltf-scene-asset");
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
    if (!check(asset != nullptr, "a successful load published no asset")) {
        return false;
    }

    if (!check(asset->nodes.size() == 5u,
               "expected five imported nodes, found " +
                   std::to_string(asset->nodes.size()))) {
        return false;
    }
    if (!check(asset->primitives.size() == 2u,
               "expected two imported primitives, found " +
                   std::to_string(asset->primitives.size()))) {
        return false;
    }
    if (!check(asset->materials.size() == 2u, "expected two materials")) {
        return false;
    }
    if (!check(asset->skins.size() == 1u, "expected one skin")) {
        return false;
    }
    if (!check(asset->animations.size() == 1u, "expected one clip")) {
        return false;
    }
    if (!check(asset->stats.vertexCount == 8u && asset->stats.indexCount == 12u,
               "decoded statistics expected 8 vertices and 12 indices, found " +
                   std::to_string(asset->stats.vertexCount) + " and " +
                   std::to_string(asset->stats.indexCount))) {
        return false;
    }
    if (!check(asset->stats.retainedTextureBytes > 0u &&
                   asset->stats.retainedAssetBytes >=
                       asset->stats.retainedGeometryBytes,
               "decoded payload statistics were not recorded")) {
        return false;
    }
    if (!check(!asset->sourceIdentity.empty() && asset->generation != 0u,
               "the asset identity or generation was not recorded")) {
        return false;
    }

    // Per-node bindings and hierarchy.
    if (!check(asset->nodes[1].parent == AuthoredRig::kArmatureNode &&
                   asset->nodes[2].parent == AuthoredRig::kShoulderNode,
               "node parenting was not imported")) {
        return false;
    }
    const glm::mat4 expectedMeshNode =
        glm::translate(glm::mat4(1.0f),
                       glm::vec3(AuthoredRig::kMeshNodeOffsetX, 0.0f, 0.0f));
    if (!check(nearlyEqual(asset->nodes[AuthoredRig::kBlockMeshNode].defaultGlobal,
                           expectedMeshNode),
               "the non-identity mesh node transform was not composed")) {
        return false;
    }
    const auto& skinned = asset->primitives[0];
    const auto& prop = asset->primitives[1];
    for (const auto* primitive : {&skinned, &prop}) {
        for (std::size_t i = 0; i < primitive->indices.size(); i += 3) {
            const auto& a = primitive->vertices[primitive->indices[i]];
            const auto& b = primitive->vertices[primitive->indices[i + 1]];
            const auto& c = primitive->vertices[primitive->indices[i + 2]];
            const glm::vec3 cross = glm::cross(
                glm::vec3(b.x - a.x, b.y - a.y, b.z - a.z),
                glm::vec3(c.x - a.x, c.y - a.y, c.z - a.z));
            if (!check(glm::dot(cross, glm::vec3(a.nx, a.ny, a.nz)) < 0,
                       "glTF exterior must be clockwise in the world pipeline"))
                return false;
        }
    }
    if (!check(skinned.nodeIndex == AuthoredRig::kBlockMeshNode &&
                   skinned.meshIndex == 0 && skinned.primitiveIndex == 0 &&
                   skinned.materialIndex == 0 && skinned.skinIndex == 0,
               "the skinned primitive's node/mesh/material/skin binding is wrong")) {
        return false;
    }
    if (!check(prop.nodeIndex == AuthoredRig::kPropNode && prop.meshIndex == 1 &&
                   prop.materialIndex == 1 && prop.skinIndex == -1,
               "the static primitive's binding is wrong")) {
        return false;
    }
    if (!check(skinned.triangleCount == 2u && prop.triangleCount == 2u,
               "primitive triangle counts are wrong")) {
        return false;
    }
    if (!check(skinned.vertices.size() == 4u &&
                   skinned.vertices[2].weight0 == 1.0f &&
                   skinned.vertices[2].joint0 == 1.0f &&
                   skinned.vertices[0].joint0 == 0.0f,
               "joint indices or weights were not imported per vertex")) {
        return false;
    }
    if (!check(skinned.geometryKey.find(asset->sourceIdentity) !=
                   std::string::npos,
               "the geometry cache identity is missing the source identity")) {
        return false;
    }

    // Material source preservation and factor separation.
    const auto& blockMaterial = asset->materials[0];
    if (!check(blockMaterial.hasBaseColorMap && blockMaterial.baseColor.authored,
               "the authored base-color map is not marked present")) {
        return false;
    }
    const std::vector<std::uint8_t> authoredPixels =
        gltf_scene_contract::authoredBaseColorDecodedPixels();
    if (!check(blockMaterial.baseColor.width == 2 &&
                   blockMaterial.baseColor.height == 2 &&
                   blockMaterial.baseColor.rgba == authoredPixels,
               "the PreserveSource base map was not returned unchanged "
               "(source pixels must not have the factor baked in)")) {
        return false;
    }
    if (!check(blockMaterial.baseColor.textureSrgb == 1u,
               "the base-color role is not submitted as sRGB")) {
        return false;
    }
    const auto& factors = blockMaterial.baseColorFactor;
    if (!check(nearlyEqual(factors[0], gltf_scene_contract::kAuthoredBaseColorFactor[0]) &&
                   nearlyEqual(factors[1],
                               gltf_scene_contract::kAuthoredBaseColorFactor[1]) &&
                   nearlyEqual(factors[2],
                               gltf_scene_contract::kAuthoredBaseColorFactor[2]) &&
                   nearlyEqual(factors[3],
                               gltf_scene_contract::kAuthoredBaseColorFactor[3]),
               "the base-color factor was not kept separate from the source "
               "pixels")) {
        return false;
    }
    if (!check(nearlyEqual(blockMaterial.metallicFactor,
                           gltf_scene_contract::kAuthoredMetallicFactor) &&
                   nearlyEqual(blockMaterial.roughnessFactor,
                               gltf_scene_contract::kAuthoredRoughnessFactor),
               "the metallic/roughness factors were not imported")) {
        return false;
    }
    if (!check(!blockMaterial.hasEmissiveMap &&
                   !blockMaterial.emissive.authored &&
                   blockMaterial.emissive.width == 1 &&
                   blockMaterial.emissive.height == 1 &&
                   blockMaterial.emissive.rgba.size() == 4u &&
                   blockMaterial.emissive.rgba[0] == 255u &&
                   blockMaterial.emissive.rgba[1] == 255u &&
                   blockMaterial.emissive.rgba[2] == 255u,
               "a genuinely absent emissive map must bind implicit white while "
               "the emissive factor stays separate")) {
        return false;
    }
    if (!check(nearlyEqual(blockMaterial.emissiveFactor[0],
                           gltf_scene_contract::kAuthoredEmissiveFactor[0]),
               "the emissive factor was not imported")) {
        return false;
    }
    if (!check(blockMaterial.normal.width == 1 &&
                   blockMaterial.normal.rgba.size() == 4u &&
                   blockMaterial.normal.rgba[0] == 128u &&
                   blockMaterial.normal.rgba[2] == 255u,
               "the absent normal map did not use the flat-normal fallback")) {
        return false;
    }
    const auto& propMaterial = asset->materials[1];
    if (!check(!propMaterial.hasBaseColorMap &&
                   propMaterial.baseColor.rgba.size() == 4u &&
                   propMaterial.baseColor.rgba[0] == 255u &&
                   nearlyEqual(propMaterial.baseColorFactor[0], 1.0f),
               "the default material did not resolve to implicit white with "
               "neutral factors")) {
        return false;
    }
    if (!check(blockMaterial.baseColor.key != blockMaterial.normal.key &&
                   blockMaterial.baseColor.key != blockMaterial.emissive.key &&
                   blockMaterial.baseColor.key.find(asset->sourceIdentity) !=
                       std::string::npos,
               "material cache identities are not role- and source-distinct")) {
        return false;
    }

    // Bind pose and animated palette composition.
    GltfScenePose bindPose;
    const bool bindSampled =
        sampleGltfScenePose(asset, 0.0f, kBindPoseClip, bindPose, &error);
    if (!check(bindSampled,
               "bind-pose sampling failed: " + error)) {
        return false;
    }
    if (!check(bindPose.palettes.size() == asset->primitives.size() &&
                   bindPose.palettes[0].size() == 2u &&
                   bindPose.palettes[1].empty(),
               "bind-pose palettes are not per-primitive")) {
        return false;
    }
    if (!check(nearlyEqual(bindPose.palettes[0][0],
                           gltf_scene_contract::authoredPalette(0, 0.0f)) &&
                   nearlyEqual(bindPose.palettes[0][1],
                               gltf_scene_contract::authoredPalette(1, 0.0f)),
               "the bind palette does not match the authored analytic result")) {
        return false;
    }
    if (!check(nearlyEqual(bindPose.global[AuthoredRig::kPropNode],
                           glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f))),
               "the static node transform was not composed")) {
        return false;
    }

    GltfScenePose animatedPose;
    const bool animatedSampled = sampleGltfScenePose(
        asset, AuthoredRig::kAnimatedTimeSec, 0, animatedPose, &error);
    if (!check(animatedSampled,
               "animated pose sampling failed: " + error)) {
        return false;
    }
    if (!check(nearlyEqual(animatedPose.palettes[0][1],
                           gltf_scene_contract::authoredPalette(
                               1, AuthoredRig::kAnimatedTimeSec), 1e-4f) &&
                   nearlyEqual(animatedPose.palettes[0][0],
                               gltf_scene_contract::authoredPalette(0, 0.0f),
                               1e-4f),
               "the animated palette does not match the authored analytic "
               "result")) {
        return false;
    }
    if (!check(!nearlyEqual(animatedPose.palettes[0][1],
                            bindPose.palettes[0][1], 1e-3f),
               "the animated palette did not differ from the bind palette")) {
        return false;
    }
    if (!check(engine::render::gltf::scene::gltfScenePoseMatchesAsset(
                   *asset, animatedPose),
               "the pose does not report the asset it was sampled from")) {
        return false;
    }
    if (!check(animatedPose.clipIndex == 0 &&
                   nearlyEqual(animatedPose.timeSec,
                               AuthoredRig::kAnimatedTimeSec),
               "the pose did not record its clip and time")) {
        return false;
    }
    engine::render::animation::PoseBlendRequest blend;
    blend.baseClipIndex = kBindPoseClip;
    blend.targetClipIndex = 0;
    blend.targetTimeSec = AuthoredRig::kAnimatedTimeSec;
    blend.crossfadeWeight = 0.5f;
    GltfScenePose blendedPose;
    if (!check(engine::render::gltf::scene::sampleGltfSceneBlendedPose(
                   asset, blend, blendedPose, &error) &&
                   nearlyEqual(blendedPose.palettes[0][1],
                               gltf_scene_contract::authoredPalette(1, 0.5f),
                               1e-4f) &&
                   engine::render::gltf::scene::gltfScenePoseMatchesAsset(
                       *asset, blendedPose),
               "blended pose did not produce the authored half-turn palette: " +
                   error)) {
        return false;
    }
    // Failed sampling preserves the caller's pose.
    GltfScenePose preserved = animatedPose;
    std::string sampleError;
    if (!check(!sampleGltfScenePose(asset, 0.5f, 7, preserved, &sampleError) &&
                   !sampleError.empty() &&
                   preserved.timeSec == animatedPose.timeSec &&
                   preserved.palettes.size() == animatedPose.palettes.size(),
               "a failed pose sample must leave the previous pose unchanged")) {
        return false;
    }

    // Bounded arithmetic: each admission limit rejects with a diagnostic.
    struct BoundedCase {
        GltfSceneLoadOptions options;
        const char* expected;
    };
    std::vector<BoundedCase> bounded;
    {
        GltfSceneLoadOptions nodes = defaultOptions();
        nodes.maxNodes = 4u;
        bounded.push_back({nodes, "nodes"});
        GltfSceneLoadOptions primitives = defaultOptions();
        primitives.maxPrimitives = 1u;
        bounded.push_back({primitives, "primitives"});
        GltfSceneLoadOptions vertices = defaultOptions();
        vertices.maxVertices = 7u;
        bounded.push_back({vertices, "vertices"});
        GltfSceneLoadOptions indices = defaultOptions();
        indices.maxIndices = 11u;
        bounded.push_back({indices, "indices"});
        GltfSceneLoadOptions textures = defaultOptions();
        textures.maxTextureBytes = 8u;
        bounded.push_back({textures, "texture bytes"});
    }
    for (const BoundedCase& boundedCase : bounded) {
        std::shared_ptr<const GltfSceneAsset> rejected = asset;
        std::string boundedError;
        if (!check(!loadGltfSceneAsset(document.string(), rejected,
                                       &boundedError, boundedCase.options) &&
                       boundedError.find(boundedCase.expected) !=
                           std::string::npos,
                   std::string("a bounded import should reject on ") +
                       boundedCase.expected + " (error: " + boundedError + ")")) {
            return false;
        }
        if (!check(rejected.get() == asset.get(),
                   "a bounded rejection must preserve the previous output")) {
            return false;
        }
    }

    // Unsupported subset and malformed-input diagnostics. Each entry mutates a
    // copy of the authored document and expects a rejection that names the
    // offending feature.
    struct RejectionCase {
        std::string name;
        SceneSource source;
        const char* expected;
    };
    std::vector<RejectionCase> rejections;
    const auto addRejection = [&rejections](
                                  std::string name,
                                  SceneSource mutated,
                                  const char* expected) {
        rejections.push_back({std::move(name), std::move(mutated), expected});
    };
    {
        SceneSource mutated = source;
        mutated.document["meshes"][0]["primitives"][0]["mode"] = 1;
        addRejection("lines-topology", std::move(mutated), "TRIANGLES");

        mutated = source;
        mutated.document["meshes"][0]["primitives"][0]["targets"] =
            nlohmann::json::array(
                {nlohmann::json{{"POSITION", 0}}});
        addRejection("morph-targets", std::move(mutated), "morph targets");

        mutated = source;
        mutated.document["meshes"][0]["primitives"][0]["attributes"]
                        ["JOINTS_1"] = 3;
        mutated.document["meshes"][0]["primitives"][0]["attributes"]
                        ["WEIGHTS_1"] = 4;
        addRejection("extra-joint-set", std::move(mutated), "extra joint set");

        mutated = source;
        mutated.document["meshes"][0]["primitives"][0]["attributes"]
                        ["TEXCOORD_1"] = 2;
        addRejection("extra-uv-set", std::move(mutated), "TEXCOORD_1");

        mutated = source;
        mutated.document["accessors"][2]["count"] = 3;
        addRejection("attribute-count-mismatch", std::move(mutated),
                     "element count");

        mutated = source;
        const std::size_t indexView =
            mutated.document["accessors"][5]["bufferView"];
        const std::size_t indexOffset =
            mutated.document["bufferViews"][indexView]["byteOffset"];
        patchU16(mutated, indexOffset, 9u);
        addRejection("out-of-range-index", std::move(mutated), "out of range");

        mutated = source;
        const std::size_t weightView =
            mutated.document["accessors"][4]["bufferView"];
        const std::size_t weightOffset =
            mutated.document["bufferViews"][weightView]["byteOffset"];
        patchF32(mutated, weightOffset,
                 std::numeric_limits<float>::quiet_NaN());
        addRejection("non-finite-weight", std::move(mutated), "non-finite");

        mutated = source;
        mutated.document["nodes"][0]["children"] = {1};
        mutated.document["nodes"][1]["children"] = {2};
        mutated.document["nodes"][2]["children"] = {0};
        addRejection("hierarchy-cycle", std::move(mutated), "cycle");

        mutated = source;
        mutated.document["nodes"][2]["children"] = {3};
        mutated.document["nodes"][4]["children"] = {3};
        addRejection("double-parent", std::move(mutated), "single-parent");

        mutated = source;
        mutated.document["skins"][0]["inverseBindMatrices"] = 6;
        mutated.document["accessors"][6]["count"] = 1;
        addRejection("inverse-bind-count", std::move(mutated),
                     "inverse bind matrices");

        mutated = source;
        mutated.document["materials"][0]["alphaCutoff"] = -0.1;
        addRejection("invalid-alpha-cutoff", std::move(mutated), "alpha cutoff");

        mutated = source;
        mutated.document["materials"][0]["doubleSided"] = true;
        addRejection("double-sided", std::move(mutated), "double-sided");

        mutated = source;
        mutated.document["materials"][0]["pbrMetallicRoughness"]
                        ["baseColorTexture"]["texCoord"] = 1;
        addRejection("uv1-material", std::move(mutated), "UV0");

        mutated = source;
        mutated.document["extensionsUsed"] =
            nlohmann::json::array({"KHR_texture_transform"});
        mutated.document["materials"][0]["pbrMetallicRoughness"]
                        ["baseColorTexture"]["extensions"]
                        ["KHR_texture_transform"] = {
                            {"offset", {0.25, 0.0}}};
        addRejection("texture-transform", std::move(mutated), "KHR_texture_transform");

        mutated = source;
        mutated.document["extensionsUsed"] =
            nlohmann::json::array({"KHR_materials_clearcoat"});
        mutated.document["materials"][0]["extensions"]["KHR_materials_clearcoat"] =
            nlohmann::json::object();
        addRejection("clearcoat-material", std::move(mutated), "clearcoat");

        mutated = source;
        mutated.document["meshes"][0]["primitives"][0]["attributes"].erase(
            "TEXCOORD_0");
        addRejection("missing-uv-with-map", std::move(mutated), "no TEXCOORD_0");

        mutated = source;
        mutated.document["nodes"][3].erase("skin");
        addRejection("joints-without-skin", std::move(mutated), "no skin");
    }
    {
        SceneSource bad = source;
        bad.document["meshes"][0]["primitives"][0]["material"] = 4294967296ull;
        addRejection("oversized-material-index", std::move(bad), "material index");
        bad = source;
        bad.document["nodes"][3]["skin"] = 999;
        addRejection("skin-reference", std::move(bad), "out-of-range skin");
        bad = source; bad.document["scene"] = 99;
        addRejection("scene-reference", std::move(bad), "default scene");
        bad = source; bad.document["scenes"][0]["nodes"] = {99};
        addRejection("scene-root", std::move(bad), "root node");
        bad = source; bad.document["bufferViews"][0]["byteLength"] = 4;
        addRejection("truncated-position-view", std::move(bad), "buffer view");
        bad = source; bad.document["accessors"][0]["byteOffset"] = 4294967295ull;
        addRejection("accessor-offset", std::move(bad), "offset");
        bad = source; bad.document["accessors"][6]["type"] = "VEC4";
        addRejection("inverse-bind-shape", std::move(bad), "shape");
        bad = source; bad.document["nodes"][0]["rotation"] = {0, 0, 0, 0};
        addRejection("zero-quaternion", std::move(bad), "rotation");
        bad = source; bad.document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {1, 1, 1, 0.5};
        addRejection("alpha-factor", std::move(bad), "alpha");
        bad = source; bad.document["samplers"][0]["magFilter"] = 9728;
        addRejection("nearest-sampling", std::move(bad), "magnification");
        bad = source; bad.document["samplers"][0]["minFilter"] = 9728;
        addRejection("explicit-minification", std::move(bad), "minification");
        bad = source; bad.document["animations"][0]["channels"][0]["target"]["path"] = "weights";
        addRejection("animation-weights", std::move(bad), "channel path");
        bad = source;
        const std::size_t outputIndex = bad.document["animations"][0]["samplers"][0]["output"];
        bad.document["accessors"][outputIndex]["count"] = 1;
        addRejection("animation-output-count", std::move(bad), "output elements");
        bad = source; bad.document["accessors"][outputIndex]["type"] = "VEC3";
        addRejection("animation-output-shape", std::move(bad), "output shape");
    }
    for (const RejectionCase& rejection : rejections) {
        const std::filesystem::path rejectionDocument =
            stageScene(directory, rejection.source);
        std::shared_ptr<const GltfSceneAsset> rejected = asset;
        std::string rejectionError;
        const bool loadedRejected =
            loadGltfSceneAsset(rejectionDocument.string(), rejected,
                               &rejectionError);
        if (!check(!loadedRejected,
                   "the " + rejection.name +
                       " import should have been rejected (error dump: " +
                       rejectionError + ")")) {
            return false;
        }
        if (!check(!rejectionError.empty(),
                   "the " + rejection.name + " rejection has no diagnostic")) {
            return false;
        }
        if (!check(rejectionError.find(rejection.expected) != std::string::npos,
                   "the " + rejection.name + " diagnostic does not name '" +
                       rejection.expected + "': " + rejectionError)) {
            return false;
        }
        if (!check(rejected.get() == asset.get(),
                   "the " + rejection.name +
                       " rejection replaced the previous asset")) {
            return false;
        }
    }

    // Only selected-scene meshes are imported; the legal default material works.
    {
        SceneSource selected = source;
        selected.document["nodes"].push_back({{"mesh", 1}});
        selected.document["scenes"].push_back({{"nodes", {5}}});
        selected.document["meshes"][1]["primitives"][0].erase("material");
        std::shared_ptr<const GltfSceneAsset> imported;
        const auto selectedPath = stageScene(directory, selected);
        if (!check(loadGltfSceneAsset(selectedPath.string(), imported, &error) &&
                       imported->primitives.size() == 2u &&
                       imported->primitives[1].materialIndex == 2,
                   "selected scene/default material import failed: " + error)) return false;
        selected.document["scene"] = 1;
        const auto otherPath = stageScene(directory, selected);
        if (!check(loadGltfSceneAsset(otherPath.string(), imported, &error) &&
                       imported->primitives.size() == 1u && imported->primitives[0].nodeIndex == 5,
                   "a second scene must not import the first scene's meshes")) return false;
    }

    // A missing or malformed file leaves the caller's output untouched.
    {
        std::shared_ptr<const GltfSceneAsset> untouched = asset;
        std::string missingError;
        if (!check(!loadGltfSceneAsset(
                       (directory.path() / "does-not-exist.gltf").string(),
                       untouched, &missingError) &&
                       !missingError.empty() &&
                       untouched.get() == asset.get(),
                   "a missing source must fail with a diagnostic and preserve "
                   "the previous asset")) {
            return false;
        }
        const std::filesystem::path malformed = directory.file("malformed.gltf");
        writeText(malformed, "{ this is not glTF }");
        std::shared_ptr<const GltfSceneAsset> stillUntouched = asset;
        std::string malformedError;
        if (!check(!loadGltfSceneAsset(malformed.string(), stillUntouched,
                                       &malformedError) &&
                       !malformedError.empty() &&
                       stillUntouched.get() == asset.get(),
                   "a malformed source must fail with a diagnostic and preserve "
                   "the previous asset")) {
            return false;
        }
        std::shared_ptr<const GltfSceneAsset> empty;
        std::string emptyError;
        if (!check(!loadGltfSceneAsset("", empty, &emptyError) && empty == nullptr,
                   "an empty path must fail without publishing an asset")) {
            return false;
        }
    }

    // Retained ownership: removing the staged inputs keeps the published asset
    // and its decoded payload usable, and a changed source reload produces a
    // new generation with new cache identities.
    const std::string originalIdentity = asset->sourceIdentity;
    const std::uint64_t originalGeneration = asset->generation;
    const std::string originalGeometryKey = asset->primitives[0].geometryKey;
    const std::string originalTextureKey = asset->materials[0].baseColor.key;
    std::error_code removalError;
    std::filesystem::remove(document, removalError);
    std::filesystem::remove(directory.file("scene.bin"), removalError);
    std::filesystem::remove(directory.file("base_color.png"), removalError);
    if (!check(!std::filesystem::exists(document),
               "the staged glTF source was not removed for the ownership test")) {
        return false;
    }
    GltfScenePose retainedPose;
    const bool retainedSampled = sampleGltfScenePose(
        asset, AuthoredRig::kAnimatedTimeSec, 0, retainedPose, &error);
    if (!check(retainedSampled &&
                   retainedPose.palettes[0].size() == 2u &&
                   asset->materials[0].baseColor.rgba == authoredPixels,
               "the published asset must stay readable after its source is "
               "removed: " + error)) {
        return false;
    }

    SceneSource replacement = gltf_scene_contract::makeValidSceneSource();
    replacement.document["materials"][1]["name"] = "ReplacedPropSkin";
    replacement.document["meshes"][1]["name"] = "ReplacedProp";
    // A different index order changes the decoded geometry content while the
    // accessor layout stays valid.
    {
        const std::size_t propIndexView =
            replacement.document["accessors"][10]["bufferView"];
        const std::size_t propIndexOffset =
            replacement.document["bufferViews"][propIndexView]["byteOffset"];
        patchU16(replacement, propIndexOffset, 1u);
    }
    const std::filesystem::path replacementDocument =
        stageScene(directory, replacement);
    std::shared_ptr<const GltfSceneAsset> replaced;
    const bool replacedLoaded = loadGltfSceneAsset(
        replacementDocument.string(), replaced, &error);
    if (!check(replacedLoaded,
               "the replacement glTF scene did not load: " + error)) {
        return false;
    }
    if (!check(replaced->sourceIdentity != originalIdentity &&
                   replaced->generation != originalGeneration,
               "a successful reload of the same path did not publish a new "
               "generation")) {
        return false;
    }
    if (!check(replaced->primitives[0].geometryKey != originalGeometryKey &&
                   replaced->materials[0].baseColor.key != originalTextureKey,
               "a successful reload did not change the geometry/texture cache "
               "identities")) {
        return false;
    }
    if (!check(!engine::render::gltf::scene::gltfScenePoseMatchesAsset(
                   *replaced, retainedPose),
               "a pose from the previous generation must not match the reloaded "
               "asset")) {
        return false;
    }

    // The retained asset still reads correctly after the replacement.
    if (!check(asset->sourceIdentity == originalIdentity &&
                   asset->primitives[0].geometryKey == originalGeometryKey &&
                   asset->materials[0].baseColor.rgba == authoredPixels,
               "the previous snapshot must not be modified by a reload")) {
        return false;
    }

    outFail.clear();
    return true;
}
