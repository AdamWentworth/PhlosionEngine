#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace vfx::runtime::authored {
namespace {

std::string toLowerCopyLocal(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

std::uint8_t u8From01(float v) {
    return static_cast<std::uint8_t>(std::clamp<int>(
        static_cast<int>(std::lround(clamp01(v) * 255.0f)), 0, 255));
}

float tevMixU8Scalar(float a, float b, float t) {
    const float a8 = std::floor(clamp01(a) * 255.0f + 0.5f);
    const float b8 = std::floor(clamp01(b) * 255.0f + 0.5f);
    const float t8 = std::floor(clamp01(t) * 255.0f + 0.5f);
    const float tc = t8 + std::floor(t8 / 128.0f);
    const float out8 = std::floor((a8 * 256.0f + (b8 - a8) * tc + 128.0f) / 256.0f);
    return clamp01(out8 / 255.0f);
}

float alpha6bit(float a) {
    return clamp01(std::floor(clamp01(a) * 63.0f + 0.5f) / 63.0f);
}

float computeLocalFade01(float localAge01, float fadeStart) {
    if (localAge01 <= fadeStart) return 1.0f;
    const float t = (localAge01 - fadeStart) / std::max(0.0001f, 1.0f - fadeStart);
    return 1.0f - glm::clamp(t, 0.0f, 1.0f);
}

bool computePassSequenceState(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                              int sequenceCount,
                              float age01,
                              float fadeStart,
                              int sequenceIndex,
                              float &outLocalAge01,
                              float &outFade) {
    if (sequenceCount <= 1) {
        outLocalAge01 = age01;
    } else {
        const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
        const float sequenceStep = std::max(0.0f, pass.sequenceStep);
        const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
        const float sequenceTimelineSpan =
            sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
        const float sequenceAge = age01 * sequenceTimelineSpan;
        outLocalAge01 = (sequenceAge - sequenceStart) / sequenceLife;
        if (outLocalAge01 < 0.0f || outLocalAge01 > 1.0f) return false;
    }

    outFade = computeLocalFade01(outLocalAge01, fadeStart);
    return outFade > 0.001f;
}

bool computeDelayedPassLaunchState(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                   int sequenceCount,
                                   float age01,
                                   int sequenceIndex,
                                   float &outLaunchAge01) {
    if (sequenceCount <= 1) {
        outLaunchAge01 = age01;
        return true;
    }

    const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
    const float sequenceStep = std::max(0.0f, pass.sequenceStep);
    const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
    const float sequenceTimelineSpan =
        sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
    const float sequenceAge = age01 * sequenceTimelineSpan;
    if (sequenceAge < sequenceStart) return false;

    outLaunchAge01 = glm::clamp((sequenceAge - sequenceStart) / sequenceLife, 0.0f, 1.0f);
    return true;
}

float computeSharedDelayedFade(float age01, float fadeStart) {
    const float delayedFadeStart = std::max(fadeStart, 0.92f);
    if (age01 <= delayedFadeStart) return 1.0f;
    const float t = (age01 - delayedFadeStart) / std::max(0.0001f, 1.0f - delayedFadeStart);
    return 1.0f - glm::clamp(t, 0.0f, 1.0f);
}


} // namespace

TevState resolveTevState(const SharedAuthoredBatchVFX::Config &config,
                         const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    TevState tev;
    tev.c0 = pass.overrideTev ? pass.tevC0 : config.tevC0;
    tev.c1 = pass.overrideTev ? pass.tevC1 : config.tevC1;
    tev.k0 = pass.overrideTev ? pass.tevK0 : config.tevK0;
    tev.c0a = pass.overrideTev ? pass.tevC0A : config.tevC0A;
    tev.c1a = pass.overrideTev ? pass.tevC1A : config.tevC1A;
    tev.k1a = pass.overrideTev ? pass.tevK1A : config.tevK1A;
    tev.c0 = glm::clamp(tev.c0, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.c1 = glm::clamp(tev.c1, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.k0 = glm::clamp(tev.k0, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.c0a = clamp01(tev.c0a);
    tev.c1a = clamp01(tev.c1a);
    tev.k1a = clamp01(tev.k1a);
    return tev;
}

std::uint8_t resolveBlendMode(const SharedAuthoredBatchVFX::Config &config,
                              const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    const std::uint8_t resolved = pass.overrideBlendMode ? pass.blendMode : config.blendMode;
    return std::min<std::uint8_t>(2u, resolved);
}

bool isLinePass(const SharedAuthoredBatchVFX::Config &,
                const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return pass.linePass || toLowerCopyLocal(pass.renderMode) == "streak_quad";
}

bool isStreakQuadPass(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return toLowerCopyLocal(pass.renderMode) == "streak_quad";
}

bool usesQuarterTextureBake(const SharedAuthoredBatchVFX::Config &,
                            const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return pass.textureQuarterRing || pass.quarterTextureBake;
}

bool isSparkleMeshPass(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return toLowerCopyLocal(pass.renderMode) == "sparkle_mesh";
}

bool isMeshCornerBillboardPass(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    const std::string mode = toLowerCopyLocal(pass.renderMode);
    return mode == "mesh_corner_billboards" || mode == "corner_billboard_mesh";
}

bool isGlowBillboardPass(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return toLowerCopyLocal(pass.renderMode) == "glow_billboard";
}

bool isQuarterRingPass(const SharedAuthoredBatchVFX::Config &config,
                       const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return usesQuarterTextureBake(config, pass);
}

std::string makeBakedTextureKey(const SharedAuthoredBatchVFX::Config::DrawPass &pass, bool quarterPass) {
    const std::string cacheScope = pass.textureCacheGroup.empty() ? pass.id : pass.textureCacheGroup;
    return std::string("__authored_vfx_baked:") + cacheScope + ":" +
           (quarterPass ? "q:" : "m:") +
           (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath) +
           ":bake=" + toLowerCopyLocal(pass.textureBakeMode) +
           ":alpha=" + toLowerCopyLocal(pass.textureAlphaMode) +
           (pass.preserveGlowAlpha ? "" : ":glow_alpha=tev");
}

std::string makeTextureCacheKey(const SharedAuthoredBatchVFX::Config &config,
                                const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    if (isLinePass(config, pass) || pass.texturePath.empty()) {
        return "__authored_vfx_white__";
    }
    return makeBakedTextureKey(pass, usesQuarterTextureBake(config, pass));
}

bool bakePassTextureRgba(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                         const TevState &tev,
                         bool quarterPass,
                         const std::vector<unsigned char> &rawRgba,
                         std::vector<unsigned char> &outRgba) {
    if (rawRgba.empty() || (rawRgba.size() % 4u) != 0u) {
        outRgba.clear();
        return false;
    }
    outRgba.clear();
    outRgba.resize(rawRgba.size(), 0u);

    const glm::vec3 tint = glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f));
    const bool sparkleMeshPass = quarterPass && isSparkleMeshPass(pass);
    const bool glowBillboardPass = quarterPass && isGlowBillboardPass(pass);
    for (std::size_t i = 0; i + 3u < rawRgba.size(); i += 4u) {
        const float tr = static_cast<float>(rawRgba[i + 0u]) / 255.0f;
        const float tg = static_cast<float>(rawRgba[i + 1u]) / 255.0f;
        const float tb = static_cast<float>(rawRgba[i + 2u]) / 255.0f;
        const float ta = static_cast<float>(rawRgba[i + 3u]) / 255.0f;

        glm::vec3 rgb(1.0f);
        float alpha = ta;
        if (quarterPass) {
            if (sparkleMeshPass || (glowBillboardPass && pass.preserveGlowAlpha)) {
                rgb = glm::vec3(
                    tevMixU8Scalar(tev.c1.r, tev.c0.r, tr),
                    tevMixU8Scalar(tev.c1.g, tev.c0.g, tg),
                    tevMixU8Scalar(tev.c1.b, tev.c0.b, tb));
                rgb *= tint;
                if (sparkleMeshPass) {
                    // Keep the sparkle texture's real shaped falloff, but boost it above the
                    // generic quarter-ring alpha so the cluster remains visible while tuning.
                    alpha = ta > 0.001f ? clamp01(std::pow(ta, 0.6f)) : 0.0f;
                } else {
                    // Preserve the authored radial falloff for billboard glow passes.
                    alpha = ta;
                }
            } else {
                rgb = glm::vec3(
                    tevMixU8Scalar(tev.c1.r, tev.c0.r, tr),
                    tevMixU8Scalar(tev.c1.g, tev.c0.g, tg),
                    tevMixU8Scalar(tev.c1.b, tev.c0.b, tb));
                rgb *= tint;
                alpha = tevMixU8Scalar(tev.c1a, tev.c0a, ta);
            }
        } else {
            const glm::vec3 tevInput = pass.useAlphaMaskForColor
                                           ? glm::vec3(ta, ta, ta)
                                           : glm::vec3(tr, tg, tb);
            const std::string textureBakeMode = toLowerCopyLocal(pass.textureBakeMode);
            if (textureBakeMode == "modulate_c0" ||
                textureBakeMode == "texture_modulate_c0" ||
                textureBakeMode == "raw_modulate_c0") {
                rgb = tint * (tev.c0 * glm::vec3(tr, tg, tb));
            } else if (textureBakeMode == "lerp_c1_c0" ||
                       textureBakeMode == "texture_lerp_c1_c0" ||
                       textureBakeMode == "capture_lerp") {
                rgb = glm::vec3(
                    tevMixU8Scalar(tev.c1.r, tev.c0.r, tr),
                    tevMixU8Scalar(tev.c1.g, tev.c0.g, tg),
                    tevMixU8Scalar(tev.c1.b, tev.c0.b, tb));
                rgb *= tint;
                alpha = tevMixU8Scalar(tev.c1a, tev.c0a, ta);
            } else if (textureBakeMode == "texture" || textureBakeMode == "raw") {
                rgb = tint * glm::vec3(tr, tg, tb);
            } else {
                const glm::vec3 stage1 = glm::mix(tev.c1, tev.k0, tevInput);
                rgb = tint * (tev.c0 * stage1);
            }

            const std::string textureAlphaMode = toLowerCopyLocal(pass.textureAlphaMode);
            const bool preserveBakedAlpha =
                textureAlphaMode == "baked" ||
                textureAlphaMode == "capture" ||
                textureAlphaMode == "capture_lerp" ||
                textureAlphaMode == "tev" ||
                textureAlphaMode == "tev_lerp";
            if (textureAlphaMode == "one" ||
                textureAlphaMode == "opaque" ||
                textureAlphaMode == "vertex") {
                alpha = 1.0f;
            } else if (!preserveBakedAlpha) {
                alpha = ta;
            }
        }

        rgb = glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
        outRgba[i + 0u] = u8From01(rgb.r);
        outRgba[i + 1u] = u8From01(rgb.g);
        outRgba[i + 2u] = u8From01(rgb.b);
        outRgba[i + 3u] = u8From01(alpha);
    }

    return true;
}

std::vector<glm::vec3> resolveGeneratedDirections(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    if (!pass.directionsLocal.empty()) return pass.directionsLocal;

    std::vector<glm::vec3> directions;
    if (pass.generatedDirectionCount > 0) {
        directions.reserve(static_cast<std::size_t>(pass.generatedDirectionCount));
        const int count = std::max(1, pass.generatedDirectionCount);
        const std::string mode = toLowerCopyLocal(pass.generatedDirectionMode);
        if (mode == "sphere" || mode == "spherical") {
            const float startRad = glm::radians(pass.generatedDirectionStartDeg);
            const float arcRad = glm::radians(pass.generatedDirectionArcDeg);
            const float goldenAngle = glm::pi<float>() * (3.0f - std::sqrt(5.0f));
            for (int i = 0; i < count; ++i) {
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
                const float y = 1.0f - 2.0f * t;
                const float radial = std::sqrt(std::max(0.0f, 1.0f - y * y));
                const float theta = (pass.generatedDirectionArcDeg >= 359.9f)
                                        ? (startRad + goldenAngle * static_cast<float>(i))
                                        : (startRad + arcRad * t);
                glm::vec3 dir(
                    std::cos(theta) * radial,
                    y,
                    std::sin(theta) * radial);
                if (std::abs(pass.generatedDirectionForward) > 0.0001f) {
                    dir.z += pass.generatedDirectionForward;
                    if (glm::dot(dir, dir) > 0.000001f) {
                        dir = glm::normalize(dir);
                    }
                }
                directions.push_back(dir);
            }
        } else {
            const float arcDeg = pass.generatedDirectionArcDeg;
            const float stepDeg = (count <= 1) ? 0.0f : (arcDeg / static_cast<float>(count));
            for (int i = 0; i < count; ++i) {
                const float angleDeg = pass.generatedDirectionStartDeg + stepDeg * static_cast<float>(i);
                const float angleRad = glm::radians(angleDeg);
                directions.emplace_back(
                    std::cos(angleRad),
                    std::sin(angleRad),
                    pass.generatedDirectionForward);
            }
        }
        return directions;
    }

    directions.push_back(
        pass.overrideDirection ? pass.directionLocal : glm::vec3(0.0f, 0.0f, 1.0f));
    return directions;
}

const std::vector<SharedAuthoredBatchVFX::Config::AuthoredStreakSegment> &
resolveAuthoredStreakSegments(const SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    return pass.authoredSegmentsLocal;
}

glm::vec3 resolveAuthoredStreakDirection(
    const SharedAuthoredBatchVFX::Config::AuthoredStreakSegment &segment) {
    const glm::vec3 midpoint = 0.5f * (segment.startLocal + segment.endLocal);
    if (glm::dot(midpoint, midpoint) > 0.000001f) {
        return glm::normalize(midpoint);
    }
    const glm::vec3 captureVector = segment.endLocal - segment.startLocal;
    if (glm::dot(captureVector, captureVector) > 0.000001f) {
        return glm::normalize(captureVector);
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

float resolveAuthoredStreakLength(
    const SharedAuthoredBatchVFX::Config::AuthoredStreakSegment &segment) {
    return glm::length(segment.endLocal - segment.startLocal);
}

float resolveAuthoredStreakTravelDistance(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    float localAge01,
    float fallbackLifeSec) {
    const float travelMul = std::max(0.0f, pass.authoredSegmentTravelMul);
    if (travelMul <= 0.0001f) return 0.0f;

    float durationSec = fallbackLifeSec;
    const float explicitStartSec = std::max(0.0f, pass.timeStartSec);
    if (pass.timeEndSec > explicitStartSec + 0.0001f) {
        durationSec = pass.timeEndSec - explicitStartSec;
    }
    durationSec = std::max(durationSec, 0.0001f);

    const float frameRate = std::max(1.0f, pass.authoredSegmentTravelFrameRate);
    const float decayPerFrame = std::clamp(pass.authoredSegmentTravelDecayPerFrame, 0.0f, 1.0f);
    const float elapsedFrames = clamp01(localAge01) * durationSec * frameRate;
    const int wholeFrames = std::max(0, static_cast<int>(std::floor(elapsedFrames)));
    const float fractionalFrame = elapsedFrames - static_cast<float>(wholeFrames);

    float decayedFrameSum = 0.0f;
    if (std::abs(decayPerFrame - 1.0f) <= 0.0001f) {
        decayedFrameSum = elapsedFrames;
    } else {
        decayedFrameSum = (1.0f - std::pow(decayPerFrame, static_cast<float>(wholeFrames))) /
                          std::max(0.0001f, 1.0f - decayPerFrame);
        decayedFrameSum += fractionalFrame * std::pow(decayPerFrame, static_cast<float>(wholeFrames));
    }

    return (travelMul / frameRate) * decayedFrameSum;
}

float resolveAuthoredDecayFactor(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    float localAge01,
    float fallbackLifeSec,
    float decayPerFrame) {
    const float clampedDecay = std::clamp(decayPerFrame, 0.0f, 1.0f);
    if (clampedDecay <= 0.0001f) return 0.0f;
    if (std::abs(clampedDecay - 1.0f) <= 0.0001f) return 1.0f;

    float durationSec = fallbackLifeSec;
    const float explicitStartSec = std::max(0.0f, pass.timeStartSec);
    if (pass.timeEndSec > explicitStartSec + 0.0001f) {
        durationSec = pass.timeEndSec - explicitStartSec;
    }
    durationSec = std::max(durationSec, 0.0001f);

    const float frameRate = std::max(1.0f, pass.authoredSegmentTravelFrameRate);
    const float elapsedFrames = clamp01(localAge01) * durationSec * frameRate;
    return std::pow(clampedDecay, elapsedFrames);
}

float resolveAuthoredStreakVisibilityFade(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    const glm::vec3 &localStart) {
    const float maxVisibleDistance = pass.authoredSegmentMaxVisibleDistance;
    if (maxVisibleDistance <= 0.0001f) return 1.0f;

    const float startDistance = glm::length(localStart);
    // Clamp spark visibility by the spark's travel anchor, not the furthest tip of the
    // streak. Using the tip makes long early-frame streaks disappear too soon and flattens
    // the perceived displacement curve.
    const float anchorDistance = startDistance;
    if (anchorDistance >= maxVisibleDistance) return 0.0f;

    const float fadeStartDistance = maxVisibleDistance * 0.85f;
    if (anchorDistance <= fadeStartDistance) return 1.0f;

    const float t =
        (anchorDistance - fadeStartDistance) /
        std::max(0.0001f, maxVisibleDistance - fadeStartDistance);
    return 1.0f - clamp01(t);
}

float quantizeLineVertexAlpha(float srcAlpha, float lineTevK1A, float colorAlpha) {
    const float alpha255 =
        std::clamp(clamp01(srcAlpha) * clamp01(lineTevK1A) * 255.0f, 0.0f, 255.0f);
    const float quantized = std::floor(alpha255 * 0.25f) / 63.0f;
    return std::clamp(clamp01(colorAlpha) * quantized, 0.0f, 1.0f);
}

float resolvePassAnimatedAlphaMul(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                  float globalAgeSec) {
    if (pass.passAlphaFps <= 0.0f || pass.passAlphaFrames.empty()) {
        return 1.0f;
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!pass.passAlphaUseGlobalTime && pass.timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, pass.timeEndSec - pass.timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - pass.timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * pass.passAlphaFps;
    const auto &frames = pass.passAlphaFrames;
    if (frames.size() == 1u) {
        return clamp01(frames.front().alphaMul);
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return clamp01(frames.front().alphaMul);
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return clamp01(frames.back().alphaMul);
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return clamp01(glm::mix(a.alphaMul, b.alphaMul, t));
    }

    return 1.0f;
}

glm::vec3 resolvePassAnimatedScaleMul(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                      float globalAgeSec) {
    if (pass.passScaleFps <= 0.0f || pass.passScaleFrames.empty()) {
        return glm::vec3(1.0f, 1.0f, 1.0f);
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!pass.passScaleUseGlobalTime && pass.timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, pass.timeEndSec - pass.timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - pass.timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * pass.passScaleFps;
    const auto &frames = pass.passScaleFrames;
    if (frames.size() == 1u) {
        return glm::max(frames.front().scaleMul, glm::vec3(0.0f, 0.0f, 0.0f));
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return glm::max(frames.front().scaleMul, glm::vec3(0.0f, 0.0f, 0.0f));
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return glm::max(frames.back().scaleMul, glm::vec3(0.0f, 0.0f, 0.0f));
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return glm::max(glm::mix(a.scaleMul, b.scaleMul, t), glm::vec3(0.0f, 0.0f, 0.0f));
    }

    return glm::vec3(1.0f, 1.0f, 1.0f);
}

namespace {

glm::vec3 resolveAnimatedOffsetFrames(const std::vector<SharedAuthoredBatchVFX::Config::PassOffsetFrame> &frames,
                                      float fps,
                                      bool useGlobalTime,
                                      float timeStartSec,
                                      float timeEndSec,
                                      float globalAgeSec) {
    if (fps <= 0.0f || frames.empty()) {
        return glm::vec3(0.0f);
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!useGlobalTime && timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, timeEndSec - timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * fps;
    if (frames.size() == 1u) {
        return frames.front().offset;
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return frames.front().offset;
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return frames.back().offset;
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return glm::mix(a.offset, b.offset, t);
    }

    return glm::vec3(0.0f);
}

glm::vec2 resolveAnimatedUvScaleFrames(
    const std::vector<SharedAuthoredBatchVFX::Config::PassUvScaleFrame> &frames,
    float fps,
    bool useGlobalTime,
    float timeStartSec,
    float timeEndSec,
    float globalAgeSec) {
    if (fps <= 0.0f || frames.empty()) {
        return glm::vec2(1.0f, 1.0f);
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!useGlobalTime && timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, timeEndSec - timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * fps;
    if (frames.size() == 1u) {
        return frames.front().uvScale;
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return frames.front().uvScale;
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return frames.back().uvScale;
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return glm::mix(a.uvScale, b.uvScale, t);
    }

    return glm::vec2(1.0f, 1.0f);
}

glm::vec2 resolveAnimatedUvOffsetFrames(
    const std::vector<SharedAuthoredBatchVFX::Config::PassUvOffsetFrame> &frames,
    float fps,
    bool useGlobalTime,
    float timeStartSec,
    float timeEndSec,
    float globalAgeSec) {
    if (fps <= 0.0f || frames.empty()) {
        return glm::vec2(0.0f, 0.0f);
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!useGlobalTime && timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, timeEndSec - timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * fps;
    if (frames.size() == 1u) {
        return frames.front().uvOffset;
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return frames.front().uvOffset;
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return frames.back().uvOffset;
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return glm::mix(a.uvOffset, b.uvOffset, t);
    }

    return glm::vec2(0.0f, 0.0f);
}

glm::quat resolveAnimatedRotationFrames(
    const std::vector<SharedAuthoredBatchVFX::Config::PassRotationFrame> &frames,
    float fps,
    bool useGlobalTime,
    float timeStartSec,
    float timeEndSec,
    float globalAgeSec) {
    if (fps <= 0.0f || frames.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    float sampleAgeSec = std::max(0.0f, globalAgeSec);
    if (!useGlobalTime && timeEndSec >= 0.0f) {
        const float window = std::max(0.0f, timeEndSec - timeStartSec);
        sampleAgeSec =
            (window > 0.0001f) ? glm::clamp(globalAgeSec - timeStartSec, 0.0f, window) : 0.0f;
    }

    const float frameF = sampleAgeSec * fps;
    if (frames.size() == 1u) {
        return glm::normalize(frames.front().rotationQuat);
    }

    const int firstFrame = frames.front().frameIndex;
    const int lastFrame = frames.back().frameIndex;
    if (frameF <= static_cast<float>(firstFrame)) {
        return glm::normalize(frames.front().rotationQuat);
    }
    if (frameF >= static_cast<float>(lastFrame)) {
        return glm::normalize(frames.back().rotationQuat);
    }

    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto &a = frames[i];
        const auto &b = frames[i + 1];
        if (frameF < static_cast<float>(a.frameIndex) ||
            frameF > static_cast<float>(b.frameIndex)) {
            continue;
        }
        const float span = std::max(1.0f, static_cast<float>(b.frameIndex - a.frameIndex));
        const float t = glm::clamp((frameF - static_cast<float>(a.frameIndex)) / span, 0.0f, 1.0f);
        return glm::normalize(glm::slerp(a.rotationQuat, b.rotationQuat, t));
    }

    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

} // namespace

glm::vec3 resolvePassAnimatedPositionLocalOffset(
    const SharedAuthoredBatchVFX::Config::DrawPass &pass,
    float globalAgeSec) {
    return resolveAnimatedOffsetFrames(pass.passPositionOffsetFrames,
                                       pass.passPositionOffsetFps,
                                       pass.passPositionOffsetUseGlobalTime,
                                       pass.timeStartSec,
                                       pass.timeEndSec,
                                       globalAgeSec);
}

glm::vec3 resolvePassAnimatedMeshLocalOffset(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                             float globalAgeSec) {
    return resolveAnimatedOffsetFrames(pass.passMeshOffsetFrames,
                                       pass.passMeshOffsetFps,
                                       pass.passMeshOffsetUseGlobalTime,
                                       pass.timeStartSec,
                                       pass.timeEndSec,
                                       globalAgeSec);
}

glm::quat resolvePassAnimatedMeshRotationQuat(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                              float globalAgeSec) {
    return resolveAnimatedRotationFrames(pass.passMeshRotationFrames,
                                         pass.passMeshRotationFps,
                                         pass.passMeshRotationUseGlobalTime,
                                         pass.timeStartSec,
                                         pass.timeEndSec,
                                         globalAgeSec);
}

glm::vec2 resolvePassAnimatedUvScale(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                     float globalAgeSec) {
    return resolveAnimatedUvScaleFrames(pass.passUvScaleFrames,
                                        pass.passUvScaleFps,
                                        pass.passUvScaleUseGlobalTime,
                                        pass.timeStartSec,
                                        pass.timeEndSec,
                                        globalAgeSec);
}

glm::vec2 resolvePassAnimatedUvOffset(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                      float globalAgeSec) {
    return resolveAnimatedUvOffsetFrames(pass.passUvOffsetFrames,
                                         pass.passUvOffsetFps,
                                         pass.passUvOffsetUseGlobalTime,
                                         pass.timeStartSec,
                                         pass.timeEndSec,
                                         globalAgeSec);
}

float resolveTimeFadeStart(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                           float defaultFadeStart) {
    if (pass.timeFadeStart >= 0.0f) {
        return glm::clamp(pass.timeFadeStart, 0.0f, 1.0f);
    }
    return glm::clamp(defaultFadeStart, 0.0f, 1.0f);
}

float resolveLocalScaleMul(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                           float localAge01,
                           float lifeSec) {
    float t = glm::clamp(localAge01, 0.0f, 1.0f);
    const float rampSec = std::max(0.0f, pass.localScaleRampSec);
    if (rampSec > 0.0001f) {
        float durationSec = std::max(0.0001f, lifeSec);
        const float explicitStartSec = std::max(0.0f, pass.timeStartSec);
        if (pass.timeEndSec > explicitStartSec + 0.0001f) {
            durationSec = pass.timeEndSec - explicitStartSec;
        }
        const float rampAge01 = glm::clamp(rampSec / durationSec, 0.0001f, 1.0f);
        t = glm::clamp(t / rampAge01, 0.0f, 1.0f);
    }
    return glm::mix(
        std::max(0.0f, pass.localScaleStartMul),
        std::max(0.0f, pass.localScaleEndMul),
        t);
}

PassTimingPlan planPassTiming(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                              bool allowRepeatedSequence) {
    PassTimingPlan plan;
    plan.explicitTimeWindow =
        (pass.timeStartSec > 0.0001f) || (pass.timeEndSec > pass.timeStartSec + 0.0001f);
    if (plan.explicitTimeWindow) {
        return plan;
    }

    plan.rawSequenceCount = std::max(1, pass.sequenceCount);
    plan.delayedSequenceIndex =
        (plan.rawSequenceCount > 1)
            ? std::clamp(pass.sequenceIndex, -1, plan.rawSequenceCount - 1)
            : -1;
    plan.delayedSinglePass = plan.delayedSequenceIndex >= 0;
    plan.repeatedSequencePass =
        allowRepeatedSequence && plan.rawSequenceCount > 1 && !plan.delayedSinglePass;
    plan.sequenceLoopCount = plan.repeatedSequencePass ? plan.rawSequenceCount : 1;
    return plan;
}

bool evaluatePassTiming(const SharedAuthoredBatchVFX::Config::DrawPass &pass,
                        float ageSec,
                        float lifeSec,
                        float fadeStart,
                        const PassTimingPlan &plan,
                        int sequenceOrdinal,
                        PassTimingState &outState) {
    if (ageSec < 0.0f) return false;
    const float safeLifeSec = std::max(0.0001f, lifeSec);
    const float age01 = glm::clamp(ageSec / safeLifeSec, 0.0f, 1.0f);
    const float localFadeStart = resolveTimeFadeStart(pass, fadeStart);
    outState.globalAge01 = age01;
    outState.localAge01 = age01;
    outState.fade = 1.0f;

    if (plan.explicitTimeWindow) {
        const float startSec = std::max(0.0f, pass.timeStartSec);
        const float endSec =
            (pass.timeEndSec > startSec + 0.0001f) ? pass.timeEndSec : safeLifeSec;
        if (ageSec < startSec || ageSec > endSec) return false;
        const float durationSec = std::max(0.0001f, endSec - startSec);
        outState.localAge01 = glm::clamp((ageSec - startSec) / durationSec, 0.0f, 1.0f);
        outState.fade = pass.timeFadeLocal ? computeLocalFade01(outState.localAge01, localFadeStart) : 1.0f;
        return outState.fade > 0.001f;
    }

    if (plan.repeatedSequencePass) {
        return computePassSequenceState(
            pass,
            plan.rawSequenceCount,
            age01,
            localFadeStart,
            sequenceOrdinal,
            outState.localAge01,
            outState.fade);
    }

    if (plan.delayedSinglePass) {
        if (!computeDelayedPassLaunchState(
                pass,
                plan.rawSequenceCount,
                age01,
                plan.delayedSequenceIndex,
                outState.localAge01)) {
            return false;
        }
        outState.fade = pass.sequenceFadeLocal
                            ? computeLocalFade01(outState.localAge01, localFadeStart)
                            : computeSharedDelayedFade(age01, localFadeStart);
        return outState.fade > 0.001f;
    }

    outState.fade = computeLocalFade01(age01, localFadeStart);
    return outState.fade > 0.001f;
}

} // namespace vfx::runtime::authored
