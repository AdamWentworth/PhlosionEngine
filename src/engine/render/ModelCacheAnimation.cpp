// src/engine/render/ModelCacheAnimation.cpp

#include "ModelCacheAnimation.h"

#include <utility>

#include <glm/glm.hpp>

namespace engine::render::model_cache {
namespace {

template <typename T>
bool readPod(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
bool writePod(std::ostream& out, const T& value) {
    return static_cast<bool>(
        out.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

bool readString(std::istream& in, std::string& text) {
    uint32_t size = 0u;
    if (!readPod(in, size)) return false;
    text.clear();
    if (size == 0u) return true;
    text.resize(size);
    return static_cast<bool>(in.read(text.data(), size));
}

bool writeString(std::ostream& out, const std::string& text) {
    const uint32_t size = static_cast<uint32_t>(text.size());
    if (!writePod(out, size)) return false;
    if (size == 0u) return true;
    return static_cast<bool>(out.write(text.data(), size));
}

} // namespace

bool cacheIdentityValid(std::uint64_t magic, std::uint32_t version) noexcept {
    return magic == kModelCacheMagic && version == kModelCacheVersion;
}

bool writeAnimationSampler(std::ostream& out,
                           const model_types::AnimationSampler& sampler) {
    if (!writeString(out, sampler.interpolation)) return false;
    const uint8_t isVec4 = sampler.isVec4 ? 1u : 0u;
    if (!writePod(out, isVec4)) return false;

    const uint32_t inputCount = static_cast<uint32_t>(sampler.inputs.size());
    if (!writePod(out, inputCount)) return false;
    for (float value : sampler.inputs) {
        if (!writePod(out, value)) return false;
    }

    const uint32_t outputCount = static_cast<uint32_t>(sampler.outputs.size());
    if (!writePod(out, outputCount)) return false;
    for (const glm::vec4& value : sampler.outputs) {
        if (!writePod(out, value)) return false;
    }
    return true;
}

bool readAnimationSampler(std::istream& in,
                          model_types::AnimationSampler& sampler) {
    // Decode into a local value: a truncated stream must not publish a
    // partially filled sampler over the caller's state.
    model_types::AnimationSampler decoded;
    if (!readString(in, decoded.interpolation)) return false;

    uint8_t isVec4 = 0u;
    if (!readPod(in, isVec4)) return false;
    decoded.isVec4 = (isVec4 != 0u);

    uint32_t inputCount = 0u;
    if (!readPod(in, inputCount)) return false;
    decoded.inputs.resize(inputCount);
    for (uint32_t k = 0u; k < inputCount; ++k) {
        if (!readPod(in, decoded.inputs[k])) return false;
    }

    uint32_t outputCount = 0u;
    if (!readPod(in, outputCount)) return false;
    decoded.outputs.resize(outputCount);
    for (uint32_t k = 0u; k < outputCount; ++k) {
        if (!readPod(in, decoded.outputs[k])) return false;
    }

    sampler = std::move(decoded);
    return true;
}

} // namespace engine::render::model_cache
