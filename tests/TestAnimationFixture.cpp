// CPU contracts for the public animation fixture and its verification guard.
//
// These checks exercise the authored rig, the caller-owned palette space, the
// analytic expectations and the guard's image validation with hermetic
// synthetic captures. No graphics context, host binary or GPU capture is
// involved; the one stub backend below only names itself for a handshake.

#include "AnimationFixtureCase.h"
#include "AnimationFixtureGuard.h"
#include "AnimationFixturePresentation.h"
#include "AnimationFixtureRig.h"

#include "gltf_import/GltfImportTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

namespace fixture = engine::animation_fixture;
namespace verification = engine::animation_fixture::verification;

using fixture::FixtureCase;
using fixture::FixtureCaseInfo;
using fixture::FixturePose;
using fixture::FixtureRig;
using verification::ExpectedContentResult;
using verification::FixtureCrop;
using verification::FixtureImage;
using verification::HandshakeCheckResult;
using verification::ImageCompareMetrics;
using verification::SynthDefect;

bool near(double actual, double expected, double epsilon = 1.0e-4) {
    return std::abs(actual - expected) <= epsilon;
}

using Check = bool (*)(std::string&);

bool checkCaseCatalog(std::string& fail) {
    const auto& catalog = fixture::fixtureCaseCatalog();
    if (catalog.size() != 7u) {
        fail = "the animation fixture catalog must have seven authored cases";
        return false;
    }
    std::vector<std::string> names;
    std::vector<int> frames;
    for (const FixtureCaseInfo& info : catalog) {
        names.emplace_back(info.name);
        frames.push_back(info.logicalFrame);
        const std::string clip(info.clipName);
        if (clip != "linear" && clip != "step" && clip != "cubic") {
            fail = "case clip is not one of the authored clips";
            return false;
        }
    }
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        fail = "authored case names are not unique";
        return false;
    }
    std::sort(frames.begin(), frames.end());
    if (std::adjacent_find(frames.begin(), frames.end()) != frames.end()) {
        fail = "authored logical frames are not unique";
        return false;
    }

    // The catalog owns clip/time/frame values; every authored angle is
    // independently cross-checked against the shared CPU sampler.
    for (const FixtureCaseInfo& info : catalog) {
        FixturePose pose;
        std::string error;
        if (!fixture::buildFixturePose(info.fixtureCase, pose, &error)) {
            fail = "case pose build failed: " + error;
            return false;
        }
        if (pose.nodeGlobals.size() != 2u ||
            !near(static_cast<double>(pose.sampleTimeSec),
                  static_cast<double>(info.timeSec),
                  1.0e-6)) {
            fail = "case pose did not use the catalog sample time";
            return false;
        }
        const glm::mat4& tipGlobal = pose.nodeGlobals[1];
        const double sampledAngle =
            std::atan2(
                static_cast<double>(tipGlobal[0][1]),
                static_cast<double>(tipGlobal[0][0])) *
            180.0 / 3.14159265358979323846;
        if (!near(sampledAngle, static_cast<double>(info.expectedAngleDegrees), 1.0e-2)) {
            fail = std::string(
                       "sampled tip angle does not match the authored angle for case '") +
                info.name + "'";
            return false;
        }
        // The guard's linearized-cubic defect is only distinguishable where the
        // authored cubic pose differs from a LINEAR interpolation of the same
        // keys; the other cases must stay identical to their fallback.
        const double authored = static_cast<double>(info.expectedAngleDegrees);
        const double fallback =
            static_cast<double>(info.linearFallbackAngleDegrees);
        if (std::string(info.clipName) == "cubic") {
            if (std::abs(authored - fallback) <= 10.0) {
                fail = std::string("cubic case '") + info.name +
                    "' is not visibly different from its linear fallback";
                return false;
            }
        } else if (!near(authored, fallback, 1.0e-4)) {
            fail = std::string("non-cubic case '") + info.name +
                "' does not share its linear fallback angle";
            return false;
        }
    }
    return true;
}

bool checkRigStructure(std::string& fail) {
    const FixtureRig& rig = fixture::fixtureRig();
    if (rig.nodesDefault.size() != 2u || rig.nodeChildren.size() != 2u) {
        fail = "the fixture rig must author exactly two joints";
        return false;
    }
    if (rig.nodeNames.size() != 2u || rig.nodeNames[0] != "base" ||
        rig.nodeNames[1] != "tip") {
        fail = "the fixture rig joint names are not the authored names";
        return false;
    }
    if (rig.sceneRoots.size() != 1u || rig.sceneRoots[0] != 0) {
        fail = "the fixture rig scene root is not the base joint";
        return false;
    }
    if (rig.skins.size() != 1u || rig.skins[0].joints.size() != 2u ||
        rig.skins[0].inverseBind.size() != 2u) {
        fail = "the fixture rig skin must reference both joints";
        return false;
    }
    const glm::vec3 inverseBindTranslation0(rig.skins[0].inverseBind[0][3]);
    const glm::vec3 inverseBindTranslation1(rig.skins[0].inverseBind[1][3]);
    if (glm::length(inverseBindTranslation0) < 0.25f ||
        glm::length(inverseBindTranslation1) < 0.25f) {
        fail = "the fixture rig inverse binds are not both nonidentity";
        return false;
    }
    if (near(static_cast<double>(rig.meshTransform[0][0]), 1.0) &&
        near(static_cast<double>(rig.meshTransform[3][2]), 0.0) &&
        near(static_cast<double>(rig.meshTransform[3][0]), 0.0)) {
        fail = "the fixture rig mesh transform must be nonidentity";
        return false;
    }
    if (rig.animations.size() != 3u || rig.linearClipIndex != 0 ||
        rig.stepClipIndex != 1 || rig.cubicClipIndex != 2) {
        fail = "the fixture rig must author the LINEAR, STEP and cubic clips";
        return false;
    }
    // The cubic clip keeps the raw per-key [in-tangent, value, out-tangent]
    // triplets with nonzero tangents; a tangent-stripped or linearized sampler
    // could not produce the authored -120 degree midpoint.
    {
        const fixture::AnimationClip& cubic =
            rig.animations[static_cast<std::size_t>(rig.cubicClipIndex)];
        if (cubic.samplers.size() != 1u ||
            cubic.samplers[0].interpolation != "CUBICSPLINE" ||
            !cubic.samplers[0].isVec4 ||
            cubic.samplers[0].inputs != std::vector<float>({0.0f, 2.0f}) ||
            cubic.samplers[0].outputs.size() != 6u) {
            fail = "the fixture cubic clip is not the authored CUBICSPLINE sampler";
            return false;
        }
        const glm::vec4 outTangent0 = cubic.samplers[0].outputs[2];
        const glm::vec4 inTangent1 = cubic.samplers[0].outputs[3];
        if (glm::length(outTangent0) < 0.1f || glm::length(inTangent1) < 0.1f) {
            fail = "the fixture cubic clip must carry nonzero tangents";
            return false;
        }
    }
    if (rig.vertices.size() != 10u || rig.indices.size() != 24u) {
        fail = "the fixture rig strip geometry is not the authored quad strip";
        return false;
    }
    double minimumTipWeight = 1.0;
    double maximumTipWeight = 0.0;
    std::size_t blendedVertices = 0u;
    for (const auto& vertex : rig.vertices) {
        const double weightSum = vertex.weights[0] + vertex.weights[1] +
            vertex.weights[2] + vertex.weights[3];
        if (!near(weightSum, 1.0, 1.0e-6)) {
            fail = "fixture rig vertex weights do not sum to one";
            return false;
        }
        if (vertex.joints[0] != 0 || vertex.joints[1] != 1) {
            fail = "fixture rig vertices must reference both joints";
            return false;
        }
        minimumTipWeight = std::min(
            minimumTipWeight, static_cast<double>(vertex.weights[1]));
        maximumTipWeight = std::max(
            maximumTipWeight, static_cast<double>(vertex.weights[1]));
        if (vertex.weights[0] > 0.0f && vertex.weights[1] > 0.0f) {
            ++blendedVertices;
        }
    }
    if (!near(minimumTipWeight, 0.0) || !near(maximumTipWeight, 1.0)) {
        fail = "fixture rig weights do not span the fixed and moving joints";
        return false;
    }
    if (blendedVertices < 6u) {
        fail = "fixture rig does not blend a visible share of its vertices";
        return false;
    }
    return true;
}

bool checkPaletteSpaceAndDeformation(std::string& fail) {
    const FixtureRig& rig = fixture::fixtureRig();
    for (const FixtureCaseInfo& info : fixture::fixtureCaseCatalog()) {
        FixturePose pose;
        std::string error;
        if (!fixture::buildFixturePose(info.fixtureCase, pose, &error)) {
            fail = error;
            return false;
        }
        if (pose.skinMatrices.size() != 32u) {
            fail = "fixture pose palette is not two 4x4 matrices";
            return false;
        }
        const glm::mat4 inverseMesh = glm::inverse(rig.meshTransform);
        std::vector<glm::mat4> palette;
        for (std::size_t joint = 0; joint < 2u; ++joint) {
            const glm::mat4 expected =
                inverseMesh *
                pose.nodeGlobals[static_cast<std::size_t>(
                    rig.skins[0].joints[joint])] *
                rig.skins[0].inverseBind[joint];
            for (std::size_t index = 0; index < 16u; ++index) {
                if (!near(
                        static_cast<double>(pose.skinMatrices[joint * 16u + index]),
                        static_cast<double>(glm::value_ptr(expected)[index]),
                        1.0e-5)) {
                    fail = std::string("palette entry ") + std::to_string(index) +
                        " of joint " + std::to_string(joint) +
                        " is not inverse(mesh) * jointGlobal * inverseBind";
                    return false;
                }
            }
            palette.push_back(expected);
        }

        // Applying the caller-owned palette exactly like the renderer does must
        // reproduce the authored analytic blend for every bind vertex.
        double maximumError = 0.0;
        for (const auto& vertex : rig.vertices) {
            const glm::vec4 bind(vertex.position, 1.0f);
            glm::vec4 skinned(0.0f);
            for (std::size_t influence = 0; influence < 4u; ++influence) {
                const float weight = vertex.weights[influence];
                if (weight <= 0.0f) {
                    continue;
                }
                const std::size_t joint =
                    static_cast<std::size_t>(vertex.joints[influence]);
                skinned += (palette[joint] * bind) * weight;
            }
            const glm::vec3 world = glm::vec3(
                rig.meshTransform * glm::vec4(glm::vec3(skinned), 1.0f));
            const glm::vec3 expected = fixture::fixtureAnalyticBlendPosition(
                vertex, info.expectedAngleDegrees);
            maximumError = std::max(
                maximumError,
                static_cast<double>(glm::distance(world, expected)));
        }
        if (maximumError > 1.0e-4) {
            fail = std::string(
                       "mesh * palette * bindVertex does not match the analytic blend for case '") +
                info.name + "'";
            return false;
        }

        // Landmarks sit on the band's centre line, which is not a bind vertex:
        // they are authored here from the same weight rule as the strip.
        const auto landmark = [](float station) {
            fixture::FixtureVertexSpec vertex;
            vertex.position = glm::vec3(station, 0.0f, 0.0f);
            vertex.joints = {0, 1, 0, 0};
            const float tipWeight = station / 2.0f;
            vertex.weights = {1.0f - tipWeight, tipWeight, 0.0f, 0.0f};
            return vertex;
        };
        const auto baseVertex = landmark(0.0f);
        const auto middleVertex = landmark(1.0f);
        const auto tipVertex = landmark(2.0f);
        const glm::vec3 basePosition =
            fixture::fixtureAnalyticBlendPosition(baseVertex, info.expectedAngleDegrees);
        const glm::vec3 middlePosition = fixture::fixtureAnalyticBlendPosition(
            middleVertex, info.expectedAngleDegrees);
        const glm::vec3 tipPosition =
            fixture::fixtureAnalyticBlendPosition(tipVertex, info.expectedAngleDegrees);
        const glm::vec3 rigidTipMiddle = fixture::fixtureAnalyticRigidTipPosition(
            middleVertex, info.expectedAngleDegrees);
        const glm::vec3 rigidTipTip = fixture::fixtureAnalyticRigidTipPosition(
            tipVertex, info.expectedAngleDegrees);
        const glm::vec3 rigidTipBase = fixture::fixtureAnalyticRigidTipPosition(
            baseVertex, info.expectedAngleDegrees);
        if (glm::distance(basePosition, baseVertex.position) > 1.0e-5f) {
            fail = "the fully base-weighted vertex is not anchored to its bind position";
            return false;
        }
        if (glm::distance(tipPosition, rigidTipTip) > 1.0e-4f) {
            fail = "the fully tip-weighted vertex does not follow the tip joint";
            return false;
        }
        if (std::abs(info.expectedAngleDegrees) > 1.0f) {
            const float middleGap = std::min(
                glm::distance(middlePosition, middleVertex.position),
                glm::distance(middlePosition, rigidTipMiddle));
            const float rigidBaseGap = glm::distance(rigidTipBase, basePosition);
            if (middleGap < 0.2f) {
                fail = "the blended middle vertex is not distinct from either rigid alternative";
                return false;
            }
            if (rigidBaseGap < 0.03f) {
                fail = "the rigid base alternative is not measurably separated";
                return false;
            }
        }
    }
    return true;
}

std::string handshakeTextForCase(const FixtureCaseInfo& info,
                                 double paletteBias) {
    const verification::FixtureExpectation expectation =
        verification::computeFixtureExpectation(info.fixtureCase, 640, 360);
    const verification::FixtureExpectation bindExpectation =
        verification::computeFixtureExpectation(FixtureCase::Bind, 640, 360);
    std::string text = "{\n";
    text += "  \"schema\": \"phlosion-animation-fixture-handshake-v1\",\n";
    text += "  \"host\": \"runtime\",\n";
    text += "  \"case\": \"" + expectation.caseName + "\",\n";
    text += "  \"requested_backend\": \"opengl\",\n";
    text += "  \"actual_backend\": \"opengl\",\n";
    text += "  \"gpu\": \"cpu-contract\",\n";
    text += "  \"gpu_discrete\": false,\n";
    text += "  \"surface\": {\"width\": 640, \"height\": 360},\n";
    text += "  \"animation\": {\"clip\": \"" + expectation.clipName +
        "\", \"clip_index\": " + std::to_string(expectation.clipIndex) +
        ", \"sample_time_sec\": " +
        std::to_string(expectation.sampleTimeSec) +
        ", \"logical_frame\": " + std::to_string(expectation.logicalFrame) +
        ", \"expected_angle_degrees\": " +
        std::to_string(expectation.angleDegrees) + "},\n";
    const bool posed =
        std::abs(static_cast<double>(expectation.angleDegrees)) > 1.0;
    text +=
        "  \"presentation\": {\"bind_frames\": 2, \"pose_frames\": 4, "
        "\"geometry_key\": \"fixture/anim/strip\", \"gpu_skinning\": true, "
        "\"joint_count\": 2, \"skin_matrix_count\": 2, "
        "\"palette_address\": \"0x20000\", "
        "\"bind_draw_palette_address\": \"0x20000\", "
        "\"pose_draw_palette_address\": \"0x20000\", "
        "\"palette_contents_changed\": " +
        std::string(posed ? "true" : "false") + "},\n";
    const auto floatList = [](const float* values, std::size_t count) {
        std::string out = "[";
        for (std::size_t index = 0; index < count; ++index) {
            if (index != 0u) {
                out += ", ";
            }
            out += std::to_string(static_cast<double>(values[index]));
        }
        return out + "]";
    };
    text += "  \"mesh_transform\": " +
        floatList(expectation.meshTransform.data(), 16u) + ",\n";
    std::vector<float> palette(expectation.palette.begin(), expectation.palette.end());
    palette[0] = static_cast<float>(palette[0] + paletteBias);
    text += "  \"palette\": " + floatList(palette.data(), palette.size()) + ",\n";
    text += "  \"bind_palette\": " +
        floatList(bindExpectation.palette.data(), 32u) + ",\n";
    text += "  \"node_globals\": " +
        floatList(bindExpectation.palette.data(), 32u) + ",\n";
    text += "  \"joint_bind_origin\": [[-0.5, 0.0, 0.0], [3.0, 0.0, 0.0]],\n";
    text += "  \"vertices\": [\n";
    const std::vector<verification::AuthoredVertex> authored =
        verification::authoredFixtureVertices();
    for (std::size_t index = 0; index < authored.size(); ++index) {
        const auto& vertex = authored[index];
        text += "    {\"position\": [" + std::to_string(vertex.x) + ", " +
            std::to_string(vertex.y) + ", 0.0], \"joints\": [" +
            std::to_string(vertex.joints[0]) + ", " +
            std::to_string(vertex.joints[1]) + ", " +
            std::to_string(vertex.joints[2]) + ", " +
            std::to_string(vertex.joints[3]) + "], \"weights\": [" +
            std::to_string(vertex.weights[0]) + ", " +
            std::to_string(vertex.weights[1]) + ", " +
            std::to_string(vertex.weights[2]) + ", " +
            std::to_string(vertex.weights[3]) + "]}";
        if (index + 1u != authored.size()) {
            text += ",";
        }
        text += "\n";
    }
    text += "  ]\n}\n";
    return text;
}

bool checkHandshakeValidation(std::string& fail) {
    gltf_import_contract::FixtureDirectory directory(
        "phlosion-animation-handshake");
    if (!directory.ready()) {
        fail = directory.error();
        return false;
    }
    for (const FixtureCaseInfo& info : fixture::fixtureCaseCatalog()) {
        const std::string name = std::string("handshake-") + info.name + ".json";
        if (!directory.writeText(name, handshakeTextForCase(info, 0.0))) {
            fail = "could not write the contract handshake";
            return false;
        }
        HandshakeCheckResult result;
        std::string error;
        if (!verification::validateFixtureHandshake(
                directory.file(name).string(),
                info.fixtureCase,
                640,
                360,
                result,
                &error,
                std::string("opengl"),
                std::string("runtime"))) {
            fail = error;
            return false;
        }
        if (!result.ok) {
            fail = std::string("authored handshake was rejected for case '") +
                info.name + "': " +
                (result.failures.empty() ? std::string("unknown")
                                         : result.failures.front());
            return false;
        }

        // A palette in the wrong space and a mismatched sample time are both
        // observable to the guard.
        const std::string wrongPaletteName =
            std::string("handshake-wrong-space-") + info.name + ".json";
        if (!directory.writeText(
                wrongPaletteName, handshakeTextForCase(info, -1.0))) {
            fail = "could not write the tampered handshake";
            return false;
        }
        HandshakeCheckResult tampered;
        if (!verification::validateFixtureHandshake(
                directory.file(wrongPaletteName).string(),
                info.fixtureCase,
                640,
                360,
                tampered,
                &error,
                std::string("opengl"),
                std::string("runtime"))) {
            fail = error;
            return false;
        }
        if (tampered.ok) {
            fail = "the guard accepted a palette that is not in mesh space";
            return false;
        }

        std::string wrongTime = handshakeTextForCase(info, 0.0);
        const std::string timeKey = "\"sample_time_sec\": ";
        const std::size_t timeStart = wrongTime.find(timeKey);
        if (timeStart == std::string::npos) {
            fail = "the authored handshake lost its sample time field";
            return false;
        }
        const std::size_t timeEnd = wrongTime.find(',', timeStart);
        wrongTime.replace(
            timeStart + timeKey.size(),
            timeEnd - (timeStart + timeKey.size()),
            std::to_string(static_cast<double>(info.timeSec) + 0.25));
        const std::string wrongTimeName =
            std::string("handshake-wrong-time-") + info.name + ".json";
        if (!directory.writeText(wrongTimeName, wrongTime)) {
            fail = "could not write the time-tampered handshake";
            return false;
        }
        HandshakeCheckResult timeResult;
        if (!verification::validateFixtureHandshake(
                directory.file(wrongTimeName).string(),
                info.fixtureCase,
                640,
                360,
                timeResult,
                &error,
                std::string("opengl"),
                std::string("runtime"))) {
            fail = error;
            return false;
        }
        if (timeResult.ok) {
            fail = "the guard accepted a sample time outside the case catalog";
            return false;
        }
    }
    return true;
}

bool checkGuardRejectsAuthoredDefects(std::string& fail) {
    // At zero degrees a stale bind palette and the rigid tip transform are the
    // identity, so they are indistinguishable from the authored bind pose.
    // Disabled skinning, a mesh-space palette and a blank frame still differ
    // because the renderer keeps applying the nonidentity mesh transform to the
    // raw bind vertices. Every posed case must reject every defect.
    const auto mustFail = [](FixtureCase fixtureCase, SynthDefect defect) {
        if (defect == SynthDefect::None) {
            return false;
        }
        if (defect == SynthDefect::LinearizedCubic) {
            // Only the cubic cases author a pose that differs from a LINEAR
            // interpolation of the same keys; on the LINEAR and STEP cases the
            // fallback is the authored pose, so the image is indistinguishable.
            return std::string(fixture::fixtureCaseInfo(fixtureCase).clipName) ==
                "cubic";
        }
        if (fixtureCase != FixtureCase::Bind) {
            return true;
        }
        return defect == SynthDefect::DisabledSkinning ||
            defect == SynthDefect::WrongSpace ||
            defect == SynthDefect::Blank;
    };
    for (const FixtureCaseInfo& info : fixture::fixtureCaseCatalog()) {
        for (const SynthDefect defect : {
                 SynthDefect::None,
                 SynthDefect::DisabledSkinning,
                 SynthDefect::StalePalette,
                 SynthDefect::WrongSpace,
                 SynthDefect::WrongJoint,
                 SynthDefect::Blank,
                 SynthDefect::LinearizedCubic}) {
            FixtureImage image;
            std::string error;
            if (!verification::buildSyntheticFixtureCapture(
                    info.fixtureCase, defect, 320, 180, image, &error)) {
                fail = error;
                return false;
            }
            const verification::FixtureExpectation expectation =
                verification::computeFixtureExpectation(
                    info.fixtureCase, 320, 180);
            ExpectedContentResult result;
            if (!verification::validateFixtureExpectedContent(
                    image, expectation, result, &error)) {
                fail = error;
                return false;
            }
            if (mustFail(info.fixtureCase, defect) == result.ok) {
                fail = std::string("guard verdict is wrong for defect '") +
                    verification::synthDefectName(defect) + "' on case '" +
                    info.name + "'";
                return false;
            }
        }
    }
    return true;
}

bool checkImagePrimitives(std::string& fail) {
    FixtureImage bindImage;
    FixtureImage bindCopy;
    FixtureImage posedImage;
    std::string error;
    if (!verification::buildSyntheticFixtureCapture(
            FixtureCase::Bind, SynthDefect::None, 320, 180, bindImage, &error) ||
        !verification::buildSyntheticFixtureCapture(
            FixtureCase::Bind, SynthDefect::None, 320, 180, bindCopy, &error) ||
        !verification::buildSyntheticFixtureCapture(
            FixtureCase::LinearNearEndpoint,
            SynthDefect::None,
            320,
            180,
            posedImage,
            &error)) {
        fail = error;
        return false;
    }
    FixtureCrop crop;
    if (!verification::detectFixtureViewport(bindImage, crop, &error)) {
        fail = error;
        return false;
    }
    if (crop.x != 0 || crop.y != 0 || crop.width != 320 || crop.height != 180) {
        fail = "a full-frame synthetic capture did not resolve to the full viewport";
        return false;
    }
    ImageCompareMetrics identical;
    if (!verification::compareFixtureImages(
            bindImage, bindCopy, 8, identical, &error)) {
        fail = error;
        return false;
    }
    if (identical.maxChannelDiff != 0 || identical.changedPixelRatio != 0.0) {
        fail = "identical images did not compare equal";
        return false;
    }
    ImageCompareMetrics different;
    if (!verification::compareFixtureImages(
            bindImage, posedImage, 8, different, &error)) {
        fail = error;
        return false;
    }
    if (different.changedPixelRatio < 0.005 ||
        different.maxChannelDiff <= 8) {
        fail = "the bind and posed cases did not differ measurably";
        return false;
    }
    bindCopy.rgba[0] = static_cast<std::uint8_t>(bindCopy.rgba[0] ^ 0xffu);
    ImageCompareMetrics singlePixel;
    if (!verification::compareFixtureImages(
            bindImage, bindCopy, 8, singlePixel, &error)) {
        fail = error;
        return false;
    }
    const double expectedRatio = 1.0 / (320.0 * 180.0);
    if (!near(singlePixel.changedPixelRatio, expectedRatio, 1.0e-9)) {
        fail = "a single changed channel did not report one changed pixel";
        return false;
    }
    const FixtureImage sheet = verification::buildFixtureContactSheet(
        {{"bind", bindImage}, {"posed", posedImage}}, 2, 160, 90);
    if (sheet.empty() || sheet.width != 320 || sheet.height != 90) {
        fail = "contact sheet dimensions do not match the requested grid";
        return false;
    }
    gltf_import_contract::FixtureDirectory directory("phlosion-animation-png");
    if (!directory.ready()) {
        fail = directory.error();
        return false;
    }
    const auto pngPath = directory.file("round-trip.png");
    if (!verification::saveFixturePng(pngPath.string(), bindImage, &error)) {
        fail = error;
        return false;
    }
    FixtureImage reloaded;
    if (!verification::loadFixturePng(pngPath.string(), reloaded, &error)) {
        fail = error;
        return false;
    }
    ImageCompareMetrics roundTrip;
    if (!verification::compareFixtureImages(
            bindImage, reloaded, 0, roundTrip, &error)) {
        fail = error;
        return false;
    }
    if (roundTrip.maxChannelDiff != 0 || roundTrip.changedPixelRatio != 0.0) {
        fail = "PNG save and load did not round-trip the pixels exactly";
        return false;
    }
    return true;
}

bool checkSha256Vectors(std::string& fail) {
    if (verification::sha256Hex("") !=
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
        fail = "SHA-256 of the empty string does not match the published vector";
        return false;
    }
    if (verification::sha256Hex("abc") !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        fail = "SHA-256 of 'abc' does not match the published vector";
        return false;
    }
    gltf_import_contract::FixtureDirectory directory("phlosion-animation-hash");
    if (!directory.ready() || !directory.writeText("payload.bin", "abc")) {
        fail = "could not create the hash contract fixture";
        return false;
    }
    std::string fileHash;
    std::string error;
    if (!verification::sha256FileHex(
            directory.file("payload.bin").string(), fileHash, &error)) {
        fail = error;
        return false;
    }
    if (fileHash != verification::sha256Hex("abc")) {
        fail = "file hashing does not agree with in-memory hashing";
        return false;
    }
    return true;
}

const Check kChecks[] = {
    &checkCaseCatalog,
    &checkRigStructure,
    &checkPaletteSpaceAndDeformation,
    &checkHandshakeValidation,
    &checkGuardRejectsAuthoredDefects,
    &checkImagePrimitives,
    &checkSha256Vectors,
};

} // namespace

bool test_animation_fixture_contract(std::string& outFail) {
    for (const Check check : kChecks) {
        std::string failure;
        if (!check(failure)) {
            outFail = failure;
            return false;
        }
    }
    return true;
}
