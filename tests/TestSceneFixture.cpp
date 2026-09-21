// tests/TestSceneFixture.cpp
//
// CPU contracts for the authored glTF scene fixture: case catalog, authored
// analytic rig, staged document import through the production scene loader,
// lifecycle preparation/transition, and the verification oracle (including a
// negative case). No graphics context or screenshot is involved.

#include "SceneFixtureAssets.h"
#include "SceneFixtureCase.h"
#include "SceneFixtureGuard.h"
#include "SceneFixturePresentation.h"

#include "gltf_import/GltfSceneTestSupport.h"

#include "engine/render/gltf/GltfSceneAsset.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

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
using engine::render::gltf::scene::GltfScenePose;
using engine::render::gltf::scene::loadGltfSceneAsset;
using engine::render::gltf::scene::sampleGltfScenePose;
using engine::scene_fixture::FixtureCase;
using engine::scene_fixture::SceneVariant;
using gltf_import_contract::nearlyEqual;

}  // namespace

bool test_scene_fixture_contract(std::string& outFail) {
    const gltf_scene_contract::ScopedDecoderDiagnostics diagnostics;
    const auto check = [&outFail](bool condition, std::string message) {
        return gltf_import_contract::expect(
            condition, std::move(message), outFail);
    };

    // Case catalog: six authored cases with the documented capture frames.
    const std::array<const char*, 6> kExpectedNames{
        "a-bind", "a-pose", "failed-replacement",
        "replacement-b", "emission-off", "unloaded"};
    if (!check(engine::scene_fixture::fixtureCaseCatalog().size() ==
                   static_cast<std::size_t>(
                       engine::scene_fixture::kFixtureCaseCount),
               "the scene fixture catalog is not the authored six cases")) {
        return false;
    }
    for (int index = 0; index < engine::scene_fixture::kFixtureCaseCount; ++index) {
        const auto fixtureCase = static_cast<FixtureCase>(index);
        const auto& info = engine::scene_fixture::fixtureCaseInfo(fixtureCase);
        if (!check(std::string(info.name) == kExpectedNames[static_cast<std::size_t>(index)],
                   "scene fixture case name mismatch at index " +
                       std::to_string(index))) {
            return false;
        }
        const auto parsed = engine::scene_fixture::parseFixtureCase(info.name);
        if (!check(parsed.has_value() && *parsed == fixtureCase,
                   "scene fixture case parse round trip failed")) {
            return false;
        }
    }
    if (!check(engine::scene_fixture::fixtureCaseLogicalFrame(FixtureCase::ABind) == 0 &&
                   engine::scene_fixture::fixtureCaseLogicalFrame(
                       FixtureCase::Unloaded) == 150,
               "scene fixture logical frames are not the authored values")) {
        return false;
    }
    if (!check(engine::scene_fixture::fixtureCaseInfo(FixtureCase::ABind).bindPose &&
                   !engine::scene_fixture::fixtureCaseInfo(FixtureCase::APose).bindPose &&
                   engine::scene_fixture::fixtureCaseInfo(
                       FixtureCase::EmissionOff).emissionOff,
               "scene fixture pose/emission flags are not the authored values")) {
        return false;
    }

    // Authored analytic rig: the inverse bind cancelation is exact at the bind
    // pose and the animated palette differs from it.
    for (const SceneVariant variant : {SceneVariant::A, SceneVariant::B}) {
        for (int joint = 0; joint < 2; ++joint) {
            if (!check(nearlyEqual(
                           engine::scene_fixture::authoredJointPalette(variant,
                                                                       joint, 0.0f),
                           glm::mat4(1.0f), 1e-4f),
                       "the authored bind palette is not the identity")) {
                return false;
            }
        }
        if (!check(!nearlyEqual(
                       engine::scene_fixture::authoredJointPalette(variant, 1, 1.0f),
                       glm::mat4(1.0f), 1e-3f),
                   "the authored animated palette is still the bind pose")) {
            return false;
        }
        const std::vector<glm::vec3> bindPoints =
            engine::scene_fixture::authoredLimbPoints(variant, 0.0f);
        const std::vector<glm::vec3> posedPoints =
            engine::scene_fixture::authoredLimbPoints(variant, 1.0f);
        if (!check(bindPoints.size() == posedPoints.size() &&
                       !nearlyEqual(bindPoints.back(), posedPoints.back(), 1e-3f),
                   "the authored weighted limb does not deform")) {
            return false;
        }
    }

    // Staged document import through the production loader.
    engine::scene_fixture::SceneStagingDirectory staging(
        "phlosion-scene-fixture-contract");
    if (!check(staging.valid(), "could not create the fixture staging directory: " +
                                    staging.error())) {
        return false;
    }
    std::string error;
    if (!check(engine::scene_fixture::stageSceneDocument(
                   SceneVariant::A, false, staging.path(), &error),
               "could not stage the authored scene fixture: " + error)) {
        return false;
    }
    const std::filesystem::path document =
        staging.file(engine::scene_fixture::sceneDocumentFileName());
    std::shared_ptr<const GltfSceneAsset> asset;
    const bool loaded = loadGltfSceneAsset(document.string(), asset, &error);
    if (!check(loaded, "the authored scene fixture did not load: " + error)) {
        return false;
    }
    if (!check(asset->nodes.size() == 10u && asset->primitives.size() == 7u &&
                   asset->materials.size() == 5u && asset->skins.size() == 1u &&
                   asset->animations.size() == 1u,
               "the authored scene fixture imported an unexpected structure")) {
        return false;
    }
    {
        bool foundSkinned = false;
        for (const auto& primitive : asset->primitives) {
            if (primitive.skinIndex >= 0) {
                foundSkinned = true;
                if (!check(primitive.meshIndex == 3 && primitive.skinIndex == 0 &&
                               primitive.materialIndex == 0,
                           "the weighted limb primitive binding is wrong")) {
                    return false;
                }
            }
        }
        if (!check(foundSkinned, "the authored scene has no skinned primitive")) {
            return false;
        }
    }
    {
        const auto& head = asset->materials[1];
        if (!check(!head.hasEmissiveMap && !head.emissive.authored &&
                       nearlyEqual(head.emissiveFactor[0], 0.90f, 1e-4f) &&
                       head.emissive.rgba.size() == 4u &&
                       head.emissive.rgba[0] == 255u,
                   "the emissive-factor-only surface is not authored as "
                   "expected")) {
            return false;
        }
        const auto& body = asset->materials[0];
        const std::vector<std::uint8_t> expectedPixels =
            engine::scene_fixture::authoredMapPixels(
                engine::scene_fixture::authoredMap(SceneVariant::A, "body.png"));
        if (!check(body.hasBaseColorMap && body.baseColor.rgba == expectedPixels,
                   "the authored base map was not preserved exactly")) {
            return false;
        }
    }

    // The imported animated palette matches the authored analytic palette.
    GltfScenePose pose;
    const bool posed = sampleGltfScenePose(asset, 1.0f, 0, pose, &error);
    if (!check(posed, "the authored scene pose did not sample: " + error)) {
        return false;
    }
    {
        std::size_t skinnedIndex = asset->primitives.size();
        for (std::size_t index = 0u; index < asset->primitives.size(); ++index) {
            if (asset->primitives[index].skinIndex >= 0) {
                skinnedIndex = index;
                break;
            }
        }
        if (!check(skinnedIndex < asset->primitives.size() &&
                       pose.palettes[skinnedIndex].size() == 2u,
                   "the skinned primitive produced no two-joint palette")) {
            return false;
        }
        for (int joint = 0; joint < 2; ++joint) {
            // The submitted palette is inverse(mesh node) * authored palette.
            if (!check(nearlyEqual(
                           pose.palettes[skinnedIndex][static_cast<std::size_t>(joint)],
                           glm::inverse(
                               engine::scene_fixture::authoredArmMeshNodeMatrix()) *
                               engine::scene_fixture::authoredJointPalette(
                                   SceneVariant::A, joint, 1.0f),
                           1e-4f),
                       "the imported palette does not match the authored "
                       "analytic palette for joint " +
                           std::to_string(joint))) {
                return false;
            }
        }
    }

    // The emission-off variant changes only the authored emissive factor.
    {
        engine::scene_fixture::SceneStagingDirectory offStaging(
            "phlosion-scene-fixture-emission-off");
        if (!check(offStaging.valid() &&
                       engine::scene_fixture::stageSceneDocument(
                           SceneVariant::A, true, offStaging.path(), &error),
                   "could not stage the emission-off variant: " + error)) {
            return false;
        }
        std::shared_ptr<const GltfSceneAsset> offAsset;
        const bool offLoaded = loadGltfSceneAsset(
            offStaging
                .file(engine::scene_fixture::sceneDocumentFileName())
                .string(),
            offAsset, &error);
        if (!check(offLoaded, "the emission-off variant did not load: " + error)) {
            return false;
        }
        if (!check(nearlyEqual(offAsset->materials[1].emissiveFactor[0], 0.0f) &&
                       offAsset->materials[1].emissive.rgba ==
                           asset->materials[1].emissive.rgba,
                   "the emission-off variant must keep every other authored "
                   "material input identical")) {
            return false;
        }
    }

    {
        engine::scene_fixture::FixtureSession session;
        if (!check(engine::scene_fixture::prepareFixtureSession(FixtureCase::APose, session, &error), error)) return false;
        const auto* identity = session.asset.get();
        const auto bind = session.pose.palettes;
        if (!check(session.pose.clipIndex == engine::render::gltf::scene::kBindPoseClip &&
                   engine::scene_fixture::advanceFixtureSession(session, 2, &error) &&
                   session.pose.clipIndex == engine::render::gltf::scene::kBindPoseClip &&
                   engine::scene_fixture::advanceFixtureSession(session, 3, &error) &&
                   session.asset.get() == identity && session.pose.clipIndex == 0 &&
                   session.pose.timeSec == engine::scene_fixture::fixturePoseTimeSec() &&
                   session.pose.palettes != bind,
                   "a-pose must change the same asset from bind to animated pose at frame 3")) return false;
    }

    // Lifecycle: failed replacement preserves the active asset, replacement B
    // publishes a new one, unload releases it.
    {
        engine::scene_fixture::FixtureSession session;
        if (!check(engine::scene_fixture::prepareFixtureSession(
                       FixtureCase::FailedReplacement, session, &error),
                   "failed-replacement session did not prepare: " + error)) {
            return false;
        }
        const std::string identity = session.asset->sourceIdentity;
        if (!check(engine::scene_fixture::advanceFixtureSession(session, 1, &error) &&
                       !session.replacementAttempted,
                   "the failed-replacement transition must not apply early")) {
            return false;
        }
        if (!check(engine::scene_fixture::advanceFixtureSession(
                       session, engine::scene_fixture::fixtureTransitionFrame(),
                       &error),
                   "failed-replacement transition failed: " + error)) {
            return false;
        }
        if (!check(session.replacementAttempted && session.replacementFailed &&
                       !session.replacementSucceeded &&
                       !session.replacementDiagnostic.empty() &&
                       session.asset != nullptr &&
                       session.asset->sourceIdentity == identity,
                   "a failed replacement must preserve the active asset with a "
                   "diagnostic")) {
            return false;
        }

        engine::scene_fixture::FixtureSession replacement;
        if (!check(engine::scene_fixture::prepareFixtureSession(
                       FixtureCase::ReplacementB, replacement, &error),
                   "replacement session did not prepare: " + error)) {
            return false;
        }
        const std::string originalIdentity = replacement.asset->sourceIdentity;
        if (!check(engine::scene_fixture::advanceFixtureSession(
                       replacement,
                       engine::scene_fixture::fixtureTransitionFrame(), &error),
                   "replacement transition failed: " + error)) {
            return false;
        }
        if (!check(replacement.replacementSucceeded &&
                       replacement.variant == SceneVariant::B &&
                       replacement.asset != nullptr &&
                       replacement.asset->sourceIdentity != originalIdentity,
                   "a successful replacement must publish the new content")) {
            return false;
        }
        if (!check(engine::render::gltf::scene::gltfScenePoseMatchesAsset(
                       *replacement.asset, replacement.pose),
                   "the replacement pose must belong to the replacement asset")) {
            return false;
        }

        engine::scene_fixture::FixtureSession unloaded;
        if (!check(engine::scene_fixture::prepareFixtureSession(
                       FixtureCase::Unloaded, unloaded, &error),
                   "unload session did not prepare: " + error)) {
            return false;
        }
        if (!check(engine::scene_fixture::advanceFixtureSession(
                       unloaded, engine::scene_fixture::fixtureTransitionFrame(),
                       &error),
                   "unload transition failed: " + error)) {
            return false;
        }
        if (!check(unloaded.unloaded && unloaded.asset == nullptr,
                   "the unloaded case must release the active CPU scene")) {
            return false;
        }
    }

    // Verification oracle: every authored case passes its own synthetic
    // expectation, and a background-only capture is rejected for a content case.
    for (int index = 0; index < engine::scene_fixture::kFixtureCaseCount; ++index) {
        const auto fixtureCase = static_cast<FixtureCase>(index);
        engine::fixture::FixtureImage image;
        if (!check(engine::scene_fixture::verification::buildSyntheticCapture(
                       fixtureCase, 320, 180, image, &error),
                   "synthetic capture failed: " + error)) {
            return false;
        }
        engine::scene_fixture::verification::GuardResult result;
        if (!check(engine::scene_fixture::verification::validateCaptureContent(
                       image, fixtureCase, result, &error),
                   "content validation failed: " + error)) {
            return false;
        }
        if (!check(result.ok, "the synthetic capture for " +
                                  std::string(engine::scene_fixture::fixtureCaseName(
                                      fixtureCase)) +
                                  " was rejected: " +
                                  (result.failures.empty()
                                       ? std::string("no diagnostic")
                                       : result.failures.front()))) {
            return false;
        }
    }
    {
        engine::fixture::FixtureImage blank;
        std::string blankError;
        if (!check(engine::scene_fixture::verification::buildSyntheticCapture(
                       FixtureCase::Unloaded, 320, 180, blank, &blankError),
                   "blank capture failed: " + blankError)) {
            return false;
        }
        engine::scene_fixture::verification::GuardResult result;
        if (!check(engine::scene_fixture::verification::validateCaptureContent(
                       blank, FixtureCase::APose, result, &error) &&
                       !result.ok && !result.failures.empty(),
                   "a background-only capture must be rejected for a content "
                   "case")) {
            return false;
        }
    }

    outFail.clear();
    return true;
}
