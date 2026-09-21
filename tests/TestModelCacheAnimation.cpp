// CPU contracts for the model cache's narrow animation codec seam
// (src/engine/render/ModelCacheAnimation.*). The production cache uses exactly
// these functions for real AnimationSampler I/O and for the packed-header
// identity check, so exercising them here exercises the shipped codec instead
// of a test-only copy. No graphics context, Model instance or cache file is
// involved.

#include "engine/render/ModelCacheAnimation.h"
#include "engine/render/ModelPoseSampler.h"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {

using engine::render::animation::PoseSource;
using engine::render::animation::samplePose;
using engine::render::model_cache::cacheIdentityValid;
using engine::render::model_cache::kModelCacheMagic;
using engine::render::model_cache::kModelCacheVersion;
using engine::render::model_cache::readAnimationSampler;
using engine::render::model_cache::writeAnimationSampler;
using engine::render::model_types::AnimationChannel;
using engine::render::model_types::AnimationClip;
using engine::render::model_types::AnimationSampler;
using engine::render::model_types::ChannelPath;
using engine::render::model_types::NodeTRS;
using engine::render::model_types::SkinData;

constexpr float kEpsilon = 1.0e-5f;

bool near(float actual, float expected) {
    return std::abs(actual - expected) <= kEpsilon;
}

bool nearVec3(const glm::vec3& actual, const glm::vec3& expected) {
    return near(actual.x, expected.x) && near(actual.y, expected.y) &&
        near(actual.z, expected.z);
}

bool exactVec4(const glm::vec4& left, const glm::vec4& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z &&
        left.w == right.w;
}

std::string serialize(const AnimationSampler& sampler) {
    std::ostringstream out(std::ios::binary);
    if (!writeAnimationSampler(out, sampler)) {
        return std::string{};
    }
    return out.str();
}

// A CUBICSPLINE translation channel with a hand-computed midpoint.
//
// Keys 0 s and 2 s; values (1, 2, 3) and (5, 8, 11), so the chord is
// (4, 6, 8). The out-tangent of key 0 is (1, 1.5, 2) and the in-tangent of
// key 1 is (-0.5, -0.75, -1) per second, so the interval-scaled tangents are
// 0.5 * chord and -0.25 * chord.
//
// At a = 0.5 the Hermite weights are h01 = 0.5, h10 = 0.125 and h11 = -0.125,
// so the curve weight is h01 + 0.5*h10 - 0.25*h11 = 0.5 + 0.0625 + 0.03125
// = 0.59375 and the value is (1, 2, 3) + 0.59375 * (4, 6, 8)
// = (3.375, 5.5625, 7.75).
AnimationSampler cubicTranslationSampler() {
    AnimationSampler sampler;
    sampler.interpolation = "CUBICSPLINE";
    sampler.isVec4 = false;
    sampler.inputs = {0.0f, 2.0f};
    sampler.outputs = {
        glm::vec4(9.0f, 9.0f, 9.0f, 9.0f),       // key 0 in-tangent: unused
        glm::vec4(1.0f, 2.0f, 3.0f, 0.0f),       // key 0 value
        glm::vec4(1.0f, 1.5f, 2.0f, 0.0f),       // key 0 out-tangent
        glm::vec4(-0.5f, -0.75f, -1.0f, 0.0f),   // key 1 in-tangent
        glm::vec4(5.0f, 8.0f, 11.0f, 0.0f),      // key 1 value
        glm::vec4(7.0f, 7.0f, 7.0f, 7.0f),       // key 1 out-tangent: unused
    };
    return sampler;
}

bool checkCubicRoundtripPreservesRawTangents(std::string& fail) {
    const AnimationSampler authored = cubicTranslationSampler();
    const std::string bytes = serialize(authored);
    if (bytes.empty()) {
        fail = "writing a cubic sampler through the production helper failed";
        return false;
    }

    AnimationSampler decoded;
    std::istringstream in(bytes, std::ios::binary);
    if (!readAnimationSampler(in, decoded)) {
        fail = "reading back a cubic sampler through the production helper failed";
        return false;
    }
    if (decoded.interpolation != "CUBICSPLINE" || decoded.isVec4) {
        fail = "cubic sampler identity fields changed during the roundtrip";
        return false;
    }
    if (decoded.inputs != authored.inputs) {
        fail = "cubic sampler key times changed during the roundtrip";
        return false;
    }
    if (decoded.outputs.size() != authored.outputs.size() ||
        decoded.outputs.size() != 6u) {
        fail = "cubic sampler did not keep exactly three outputs per key";
        return false;
    }
    for (std::size_t index = 0; index < authored.outputs.size(); ++index) {
        if (!exactVec4(decoded.outputs[index], authored.outputs[index])) {
            fail = "cubic sampler outputs changed during the roundtrip";
            return false;
        }
    }
    return true;
}

bool checkSamplingAfterRoundtrip(std::string& fail) {
    const std::string bytes = serialize(cubicTranslationSampler());
    if (bytes.empty()) {
        fail = "writing a cubic sampler through the production helper failed";
        return false;
    }
    AnimationSampler decoded;
    std::istringstream in(bytes, std::ios::binary);
    if (!readAnimationSampler(in, decoded)) {
        fail = "reading back a cubic sampler through the production helper failed";
        return false;
    }

    std::vector<NodeTRS> nodes(1);
    nodes[0].t = glm::vec3(-1.0f, -1.0f, -1.0f);
    std::vector<std::vector<int>> children(1);
    std::vector<int> roots{0};
    std::vector<SkinData> skins;
    std::vector<std::string> names{"node"};
    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;

    auto sampleAt = [&](float time, float duration, glm::vec3& out) {
        AnimationClip clip;
        clip.name = "cubic";
        clip.durationSec = duration;
        clip.samplers.push_back(decoded);
        clip.channels.push_back(AnimationChannel{0, 0, ChannelPath::Translation});
        std::vector<AnimationClip> animations{clip};
        const PoseSource source{nodes, children, roots, skins, animations, names};
        samplePose(source, time, 0, locals, globals);
        out = locals[0].t;
    };

    glm::vec3 value(0.0f);
    sampleAt(1.0f, 2.0f, value);
    if (!nearVec3(value, glm::vec3(3.375f, 5.5625f, 7.75f))) {
        fail = "sampling a roundtripped cubic channel did not match the "
               "hand-computed midpoint";
        return false;
    }

    // The first key is an exact sample.
    sampleAt(0.0f, 2.0f, value);
    if (!nearVec3(value, glm::vec3(1.0f, 2.0f, 3.0f))) {
        fail = "an exact first key did not return the authored cubic value";
        return false;
    }
    // A sample time that is an exact positive multiple of the clip duration
    // keeps the established nextafter policy, so this is an interpolated sample
    // just inside the last segment, not an exact last-key sample. It must still
    // land on the authored final value.
    sampleAt(2.0f, 2.0f, value);
    if (!nearVec3(value, glm::vec3(5.0f, 8.0f, 11.0f))) {
        fail = "a sample just inside the end of an exact-duration clip did not "
               "land on the authored final cubic value";
        return false;
    }

    // Keys end at 2 s inside a 4 s clip: the wrapped sample time clamps to the
    // channel's last key instead of interpolating across the tail.
    sampleAt(3.0f, 4.0f, value);
    if (!nearVec3(value, glm::vec3(5.0f, 8.0f, 11.0f))) {
        fail = "a cubic sample past the last key was not clamped to it";
        return false;
    }
    return true;
}

bool checkLinearAndStepLayoutRoundtrip(std::string& fail) {
    AnimationSampler linear;
    linear.interpolation = "LINEAR";
    linear.isVec4 = false;
    linear.inputs = {0.0f, 1.5f};
    linear.outputs = {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                      glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)};

    AnimationSampler step;
    step.interpolation = "STEP";
    step.isVec4 = true;
    step.inputs = {0.25f};
    step.outputs = {glm::vec4(0.0f, 0.0f, 0.5f, 0.5f)};

    for (const AnimationSampler& authored : {linear, step}) {
        const std::string bytes = serialize(authored);
        if (bytes.empty()) {
            fail = "writing a " + authored.interpolation +
                " sampler through the production helper failed";
            return false;
        }
        AnimationSampler decoded;
        std::istringstream in(bytes, std::ios::binary);
        if (!readAnimationSampler(in, decoded)) {
            fail = "reading a " + authored.interpolation +
                " sampler through the production helper failed";
            return false;
        }
        if (decoded.interpolation != authored.interpolation ||
            decoded.isVec4 != authored.isVec4 ||
            decoded.inputs != authored.inputs) {
            fail = authored.interpolation +
                " sampler identity fields changed during the roundtrip";
            return false;
        }
        // LINEAR/STEP keep exactly one output per key: the layout is unchanged
        // by this pass.
        if (decoded.outputs.size() != authored.inputs.size()) {
            fail = authored.interpolation +
                " sampler no longer stores one output per key";
            return false;
        }
        for (std::size_t index = 0; index < authored.outputs.size(); ++index) {
            if (!exactVec4(decoded.outputs[index], authored.outputs[index])) {
                fail = authored.interpolation +
                    " sampler outputs changed during the roundtrip";
                return false;
            }
        }
    }
    return true;
}

bool checkTruncatedReadDoesNotPublish(std::string& fail) {
    const std::string bytes = serialize(cubicTranslationSampler());
    if (bytes.size() < 4u) {
        fail = "serialized cubic sampler is unexpectedly short";
        return false;
    }
    // Drop the final output vector entry so the stream ends inside the outputs.
    const std::string truncated = bytes.substr(0u, bytes.size() - sizeof(float) * 4u);

    AnimationSampler sentinel;
    sentinel.interpolation = "SENTINEL";
    sentinel.isVec4 = true;
    sentinel.inputs = {42.0f};
    sentinel.outputs = {glm::vec4(42.0f)};

    std::istringstream in(truncated, std::ios::binary);
    if (readAnimationSampler(in, sentinel)) {
        fail = "a truncated sampler stream was accepted";
        return false;
    }
    if (sentinel.interpolation != "SENTINEL" || !sentinel.isVec4 ||
        sentinel.inputs != std::vector<float>({42.0f}) ||
        sentinel.outputs.size() != 1u ||
        !exactVec4(sentinel.outputs[0], glm::vec4(42.0f))) {
        fail = "a failed read published a partial sampler over the caller";
        return false;
    }
    return true;
}

bool checkCacheIdentity(std::string& fail) {
    if (kModelCacheVersion != 10u) {
        fail = "the model cache version is not the expected v10";
        return false;
    }
    if (!cacheIdentityValid(kModelCacheMagic, 10u)) {
        fail = "the production identity check rejected the current header";
        return false;
    }
    if (cacheIdentityValid(kModelCacheMagic, 9u)) {
        fail = "the production identity check accepted the old v9 header";
        return false;
    }
    if (cacheIdentityValid(kModelCacheMagic + 1u, 10u)) {
        fail = "the production identity check accepted a wrong magic";
        return false;
    }
    if (cacheIdentityValid(0u, 10u) || cacheIdentityValid(kModelCacheMagic, 0u)) {
        fail = "the production identity check accepted an empty header";
        return false;
    }
    return true;
}

using Check = bool (*)(std::string&);

const Check kChecks[] = {
    &checkCubicRoundtripPreservesRawTangents,
    &checkSamplingAfterRoundtrip,
    &checkLinearAndStepLayoutRoundtrip,
    &checkTruncatedReadDoesNotPublish,
    &checkCacheIdentity,
};

} // namespace

bool test_model_cache_animation_contract(std::string& outFail) {
    for (const Check check : kChecks) {
        std::string failure;
        if (!check(failure)) {
            outFail = failure;
            return false;
        }
    }
    return true;
}
