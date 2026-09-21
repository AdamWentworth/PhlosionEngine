// src/engine/render/ModelCacheAnimation.h
//
// Narrow internal seam shared by the model cache and its CPU contracts: the
// packed-header identity constants plus the AnimationSampler read/write that
// the production cache uses for real sampler I/O. It depends only on standard
// streams and the animation types (no GL, no Model constructor), so the exact
// production codec can be exercised directly by the CPU contracts.
#pragma once

#include "ModelAnimationTypes.h"

#include <cstdint>
#include <istream>
#include <ostream>

namespace engine::render::model_cache {

// Cache identity. Version 10 invalidates v9 and older caches written before
// CUBICSPLINE tangents were preserved, so those models re-import instead of
// playing back tangent-stripped animation.
inline constexpr std::uint64_t kModelCacheMagic = 0x4C444D434150554FULL; // "PACMDML" in little-endian-ish
inline constexpr std::uint32_t kModelCacheVersion = 10u;

// True only for the exact magic/version pair this build understands.
bool cacheIdentityValid(std::uint64_t magic, std::uint32_t version) noexcept;

// Writes one AnimationSampler in the existing packed layout (interpolation
// string, isVec4 flag, key times, then the outputs vector, which stores the raw
// per-key [in-tangent, value, out-tangent] triplets for CUBICSPLINE).
bool writeAnimationSampler(std::ostream& out,
                           const model_types::AnimationSampler& sampler);

// Reads one AnimationSampler. A failed read returns false and leaves `sampler`
// untouched, so a truncated cache cannot publish a partially decoded sampler.
bool readAnimationSampler(std::istream& in,
                          model_types::AnimationSampler& sampler);

} // namespace engine::render::model_cache
