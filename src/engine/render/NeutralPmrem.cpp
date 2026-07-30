#include "engine/render/NeutralPmrem.h"

#include "engine/core/Environment.h"
#include "engine/core/Paths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace engine::render::neutral_pmrem {
namespace {

namespace fs = std::filesystem;

constexpr int kLodMin = 4;
constexpr std::array<float, 6> kExtraLodSigma = {0.125f, 0.215f, 0.35f, 0.446f, 0.526f, 0.582f};
constexpr int kMaxBlurSamples = 20;
constexpr float kRgbmRange = 16.0f;
constexpr std::uint32_t kNeutralPmremCacheMagic = 0x504d524du; // PMRM
constexpr std::uint32_t kNeutralPmremCacheVersion = 2u;

struct CacheHeader {
    std::uint32_t magic = 0u;
    std::uint32_t version = 0u;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t cubeSize = 0;
    std::int32_t lodMax = 0;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    float maxMip = 0.0f;
    float rgbmRange = 0.0f;
    std::uint64_t rgbaSize = 0u;
    std::uint64_t rgba16fSize = 0u;
};

struct LodData {
    int lodMax = 0;
    std::vector<int> sizeLods;
    std::vector<float> sigmas;
};

struct Layout {
    int x = 0;
    int y = 0;
    int size = 0;
};

struct SceneBox {
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
    float rotY = 0.0f;
    bool emissive = false;
    float emissiveIntensity = 0.0f;
    bool interior = false;
};

struct HitInfo {
    bool hit = false;
    float t = std::numeric_limits<float>::max();
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool emissive = false;
    float emissiveIntensity = 0.0f;
};

template <typename T>
bool writePod(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return out.good();
}

template <typename T>
bool readPod(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

bool neutralPmremDiskCacheEnabled() {
    return !engine::env::flagEnabled("PAC_DISABLE_NEUTRAL_PMREM_CACHE");
}

bool neutralPmremForceRebuild() {
    return engine::env::flagEnabled("PAC_REBUILD_NEUTRAL_PMREM_CACHE");
}

fs::path neutralPmremCachePath() {
    return fs::path(engine::paths::data("cache/neutral_pmrem")) / "neutral_room_pmrem_v2.bin";
}

bool validateCacheHeader(const CacheHeader& header) {
    if (header.magic != kNeutralPmremCacheMagic) return false;
    if (header.version != kNeutralPmremCacheVersion) return false;
    if (header.width <= 0 || header.height <= 0) return false;
    if (header.cubeSize <= 0 || header.lodMax < 0) return false;

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(header.width) * static_cast<std::uint64_t>(header.height);
    if (header.rgbaSize != pixelCount * 4u) return false;
    if (header.rgba16fSize != pixelCount * 4u) return false;
    return true;
}

bool tryLoadNeutralRoomPmremAtlasFromCache(Atlas& out) {
    if (!neutralPmremDiskCacheEnabled() || neutralPmremForceRebuild()) return false;

    std::ifstream in(neutralPmremCachePath(), std::ios::binary);
    if (!in.is_open()) return false;

    CacheHeader header{};
    if (!readPod(in, header) || !validateCacheHeader(header)) return false;

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(header.rgbaSize), 0u);
    std::vector<std::uint16_t> rgba16f(static_cast<std::size_t>(header.rgba16fSize), 0u);
    in.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    if (!in.good()) return false;
    in.read(
        reinterpret_cast<char*>(rgba16f.data()),
        static_cast<std::streamsize>(rgba16f.size() * sizeof(std::uint16_t)));
    if (!in.good()) return false;

    out.width = header.width;
    out.height = header.height;
    out.cubeSize = header.cubeSize;
    out.lodMax = header.lodMax;
    out.texelWidth = header.texelWidth;
    out.texelHeight = header.texelHeight;
    out.maxMip = header.maxMip;
    out.rgbmRange = header.rgbmRange;
    out.rgba = std::move(rgba);
    out.rgba16f = std::move(rgba16f);
    return true;
}

void tryStoreNeutralRoomPmremAtlasToCache(const Atlas& atlas) {
    if (!neutralPmremDiskCacheEnabled()) return;

    const fs::path cachePath = neutralPmremCachePath();
    std::error_code ec;
    fs::create_directories(cachePath.parent_path(), ec);
    if (ec) return;

    std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;

    CacheHeader header{};
    header.magic = kNeutralPmremCacheMagic;
    header.version = kNeutralPmremCacheVersion;
    header.width = atlas.width;
    header.height = atlas.height;
    header.cubeSize = atlas.cubeSize;
    header.lodMax = atlas.lodMax;
    header.texelWidth = atlas.texelWidth;
    header.texelHeight = atlas.texelHeight;
    header.maxMip = atlas.maxMip;
    header.rgbmRange = atlas.rgbmRange;
    header.rgbaSize = static_cast<std::uint64_t>(atlas.rgba.size());
    header.rgba16fSize = static_cast<std::uint64_t>(atlas.rgba16f.size());

    if (!writePod(out, header)) return;
    out.write(reinterpret_cast<const char*>(atlas.rgba.data()), static_cast<std::streamsize>(atlas.rgba.size()));
    if (!out.good()) return;
    out.write(
        reinterpret_cast<const char*>(atlas.rgba16f.data()),
        static_cast<std::streamsize>(atlas.rgba16f.size() * sizeof(std::uint16_t)));
}

glm::vec3 rotateY(const glm::vec3& v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return glm::vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

glm::vec3 inverseRotateY(const glm::vec3& v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return glm::vec3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}

LodData buildLodData(int cubeSize) {
    LodData data;
    data.lodMax = static_cast<int>(std::floor(std::log2(static_cast<float>(cubeSize))));
    int lod = data.lodMax;
    const int totalLods = data.lodMax - kLodMin + 1 + static_cast<int>(kExtraLodSigma.size());
    data.sizeLods.reserve(totalLods);
    data.sigmas.reserve(totalLods);
    for (int i = 0; i < totalLods; ++i) {
        const int size = 1 << lod;
        data.sizeLods.push_back(size);
        float sigma = 1.0f / static_cast<float>(size);
        if (i > data.lodMax - kLodMin) {
            sigma = kExtraLodSigma[static_cast<std::size_t>(i - data.lodMax + kLodMin - 1)];
        } else if (i == 0) {
            sigma = 0.0f;
        }
        data.sigmas.push_back(sigma);
        if (lod > kLodMin) {
            --lod;
        }
    }
    return data;
}

Layout layoutForLod(const LodData& lodData, int cubeSize, int lodIndex) {
    Layout layout;
    layout.size = lodData.sizeLods[static_cast<std::size_t>(lodIndex)];
    const int extraOffset =
        (lodIndex > lodData.lodMax - kLodMin) ? (lodIndex - lodData.lodMax + kLodMin) : 0;
    layout.x = 3 * layout.size * extraOffset;
    layout.y = 4 * (cubeSize - layout.size);
    return layout;
}

int getFace(const glm::vec3& direction) {
    const glm::vec3 ad = glm::abs(direction);
    if (ad.x > ad.z) {
        if (ad.x > ad.y) return (direction.x > 0.0f) ? 0 : 3;
        return (direction.y > 0.0f) ? 1 : 4;
    }
    if (ad.z > ad.y) return (direction.z > 0.0f) ? 2 : 5;
    return (direction.y > 0.0f) ? 1 : 4;
}

glm::vec2 getUv(const glm::vec3& direction, int face) {
    if (face == 0) return glm::vec2(direction.z, direction.y) / std::abs(direction.x);
    if (face == 1) return glm::vec2(-direction.x, -direction.z) / std::abs(direction.y);
    if (face == 2) return glm::vec2(-direction.x, direction.y) / std::abs(direction.z);
    if (face == 3) return glm::vec2(-direction.z, direction.y) / std::abs(direction.x);
    if (face == 4) return glm::vec2(-direction.x, direction.z) / std::abs(direction.y);
    return glm::vec2(direction.x, direction.y) / std::abs(direction.z);
}

glm::vec3 getDirection(glm::vec2 uv, int face) {
    uv = 2.0f * uv - glm::vec2(1.0f);
    glm::vec3 direction(uv, 1.0f);
    if (face == 0) {
        direction = glm::vec3(direction.z, direction.y, direction.x); // pos x
    } else if (face == 1) {
        direction = glm::vec3(direction.x, direction.z, direction.y);
        direction.x *= -1.0f;
        direction.z *= -1.0f; // pos y
    } else if (face == 2) {
        direction.x *= -1.0f; // pos z
    } else if (face == 3) {
        direction = glm::vec3(direction.z, direction.y, direction.x);
        direction.x *= -1.0f;
        direction.z *= -1.0f; // neg x
    } else if (face == 4) {
        direction = glm::vec3(direction.x, direction.z, direction.y);
        direction.x *= -1.0f;
        direction.y *= -1.0f; // neg y
    } else {
        direction.z *= -1.0f; // neg z
    }
    return direction;
}

glm::vec3 rotateAroundAxis(const glm::vec3& value, const glm::vec3& axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return value * c + glm::cross(axis, value) * s + axis * glm::dot(axis, value) * (1.0f - c);
}

std::size_t atlasIndex(int width, int x, int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

std::uint16_t floatToHalf(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));

    const std::uint32_t sign = (bits >> 16u) & 0x8000u;
    std::int32_t exp = static_cast<std::int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x007fffffu;

    if (exp <= 0) {
        if (exp < -10) return static_cast<std::uint16_t>(sign);
        mantissa |= 0x00800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exp);
        std::uint32_t halfMantissa = mantissa >> shift;
        const std::uint32_t roundBit = (mantissa >> (shift - 1u)) & 1u;
        halfMantissa += roundBit;
        return static_cast<std::uint16_t>(sign | (halfMantissa & 0x03ffu));
    }

    if (exp >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }

    mantissa += 0x00001000u; // round to nearest
    if (mantissa & 0x00800000u) {
        mantissa = 0u;
        ++exp;
        if (exp >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7c00u);
        }
    }

    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(exp) << 10u) | (mantissa >> 13u));
}

bool intersectAabbLocal(const glm::vec3& ro,
                        const glm::vec3& rd,
                        const glm::vec3& half,
                        float& tNear,
                        float& tFar,
                        glm::vec3& nNear,
                        glm::vec3& nFar) {
    tNear = -std::numeric_limits<float>::infinity();
    tFar = std::numeric_limits<float>::infinity();
    nNear = glm::vec3(0.0f);
    nFar = glm::vec3(0.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const float roA = ro[axis];
        const float rdA = rd[axis];
        const float h = half[axis];
        if (std::abs(rdA) < 1e-7f) {
            if (roA < -h || roA > h) return false;
            continue;
        }
        float t1 = (-h - roA) / rdA;
        float t2 = ( h - roA) / rdA;
        glm::vec3 n1(0.0f);
        glm::vec3 n2(0.0f);
        n1[axis] = -1.0f;
        n2[axis] = 1.0f;
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(n1, n2);
        }
        if (t1 > tNear) {
            tNear = t1;
            nNear = n1;
        }
        if (t2 < tFar) {
            tFar = t2;
            nFar = n2;
        }
        if (tNear > tFar) return false;
    }
    return tFar > 0.0f;
}

bool intersectEmissivePanelLocal(const glm::vec3& ro,
                                 const glm::vec3& rd,
                                 const glm::vec3& half,
                                 float& tHit,
                                 glm::vec3& nLocal) {
    int thinAxis = 0;
    if (half.y < half[thinAxis]) thinAxis = 1;
    if (half.z < half[thinAxis]) thinAxis = 2;
    if (std::abs(rd[thinAxis]) <= 1e-6f) return false;

    tHit = std::numeric_limits<float>::max();
    nLocal = glm::vec3(0.0f, 1.0f, 0.0f);
    bool hit = false;
    const int axisA = (thinAxis + 1) % 3;
    const int axisB = (thinAxis + 2) % 3;
    for (float sign : {-1.0f, 1.0f}) {
        const float planeCoord = sign * half[thinAxis];
        const float t = (planeCoord - ro[thinAxis]) / rd[thinAxis];
        if (t <= 1e-4f || t >= tHit) continue;

        const glm::vec3 p = ro + rd * t;
        if (std::abs(p[axisA]) > half[axisA] || std::abs(p[axisB]) > half[axisB]) continue;

        hit = true;
        tHit = t;
        nLocal = glm::vec3(0.0f);
        nLocal[thinAxis] = sign;
    }
    return hit;
}

HitInfo traceRoomEnvironment(const glm::vec3& direction, const std::vector<SceneBox>& scene) {
    const glm::vec3 rayOrigin(0.0f);
    const glm::vec3 rayDir = glm::normalize(direction);
    HitInfo best;

    for (const SceneBox& box : scene) {
        const glm::vec3 localRo = inverseRotateY(rayOrigin - box.center, box.rotY);
        const glm::vec3 localRd = inverseRotateY(rayDir, box.rotY);
        float tHit = std::numeric_limits<float>::max();
        glm::vec3 nLocal(0.0f, 1.0f, 0.0f);
        if (box.emissive) {
            if (!intersectEmissivePanelLocal(localRo, localRd, box.half, tHit, nLocal)) continue;
        } else {
            float tNear = 0.0f;
            float tFar = 0.0f;
            glm::vec3 nNear(0.0f);
            glm::vec3 nFar(0.0f);
            if (!intersectAabbLocal(localRo, localRd, box.half, tNear, tFar, nNear, nFar)) continue;

            if (box.interior) {
                if (tFar <= 1e-4f) continue;
                tHit = tFar;
                nLocal = -nFar;
            } else {
                if (tNear > 1e-4f) {
                    tHit = tNear;
                    nLocal = nNear;
                } else if (tFar > 1e-4f) {
                    tHit = tFar;
                    nLocal = nFar;
                } else {
                    continue;
                }
            }
        }

        if (tHit >= best.t) continue;
        best.hit = true;
        best.t = tHit;
        best.position = rayOrigin + rayDir * tHit;
        best.normal = glm::normalize(rotateY(nLocal, box.rotY));
        best.emissive = box.emissive;
        best.emissiveIntensity = box.emissiveIntensity;
    }

    return best;
}

glm::vec3 roomRadiance(const glm::vec3& direction, const std::vector<SceneBox>& scene) {
    const HitInfo hit = traceRoomEnvironment(direction, scene);
    if (!hit.hit) return glm::vec3(0.0f);
    if (hit.emissive) {
        return glm::vec3(hit.emissiveIntensity);
    }

    const glm::vec3 pointLightPos(0.418f, 16.199f, 0.300f);
    const glm::vec3 toLight = pointLightPos - hit.position;
    const float dist2 = glm::dot(toLight, toLight);
    if (dist2 <= 1e-8f) return glm::vec3(0.0f);
    const float dist = std::sqrt(dist2);
    const glm::vec3 l = toLight / dist;
    const float nDotL = std::max(glm::dot(hit.normal, l), 0.0f);
    if (nDotL <= 0.0f) return glm::vec3(0.0f);

    float attenuation = 1.0f / std::max(std::pow(dist, 2.0f), 0.01f);
    const float cutoffDistance = 28.0f;
    const float ratio = dist / cutoffDistance;
    const float cutoff = std::max(1.0f - std::pow(ratio, 4.0f), 0.0f);
    attenuation *= cutoff * cutoff;
    const float irradiance = 900.0f * attenuation * nDotL;
    const float lambert = 1.0f / glm::pi<float>();
    const float bounce = 0.02f;
    return glm::vec3(irradiance * lambert + bounce);
}

void writeLodPixel(std::vector<glm::vec3>& atlas,
                   int width,
                   const Layout& layout,
                   int face,
                   int px,
                   int py,
                   const glm::vec3& value) {
    const int faceCol = face % 3;
    const int faceRow = face / 3;
    const int x = layout.x + faceCol * layout.size + px;
    const int y = layout.y + faceRow * layout.size + py;
    atlas[atlasIndex(width, x, y)] = value;
}

glm::vec3 readAtlasLinear(const std::vector<glm::vec3>& atlas, int width, int height, float x, float y) {
    const float fx = x - 0.5f;
    const float fy = y - 0.5f;
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, height - 1);
    const int x1 = std::clamp(x0 + 1, 0, width - 1);
    const int y1 = std::clamp(y0 + 1, 0, height - 1);
    const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);

    const glm::vec3 c00 = atlas[atlasIndex(width, x0, y0)];
    const glm::vec3 c10 = atlas[atlasIndex(width, x1, y0)];
    const glm::vec3 c01 = atlas[atlasIndex(width, x0, y1)];
    const glm::vec3 c11 = atlas[atlasIndex(width, x1, y1)];
    const glm::vec3 cx0 = c00 + (c10 - c00) * tx;
    const glm::vec3 cx1 = c01 + (c11 - c01) * tx;
    return cx0 + (cx1 - cx0) * ty;
}

glm::vec3 sampleAtlasCubeUv(const std::vector<glm::vec3>& atlas,
                            int width,
                            int height,
                            int lodMax,
                            const glm::vec3& sampleDir,
                            float mipIntIn) {
    const float cubeUvMinMipLevel = 4.0f;
    const float cubeUvMinTileSize = 16.0f;
    int face = getFace(sampleDir);
    float mipInt = mipIntIn;
    float filterInt = std::max(cubeUvMinMipLevel - mipInt, 0.0f);
    mipInt = std::max(mipInt, cubeUvMinMipLevel);
    const float faceSize = std::exp2(mipInt);
    glm::vec2 uv = getUv(sampleDir, face) * (faceSize - 2.0f) + 1.0f;
    if (face > 2) {
        uv.y += faceSize;
        face -= 3;
    }
    uv.x += static_cast<float>(face) * faceSize;
    uv.x += filterInt * 3.0f * cubeUvMinTileSize;
    uv.y += 4.0f * (std::exp2(static_cast<float>(lodMax)) - faceSize);
    return readAtlasLinear(atlas, width, height, uv.x, uv.y);
}

glm::vec3 directionForFacePixel(int face, int px, int py, int faceSize) {
    const float denom = std::max(static_cast<float>(faceSize - 2), 1.0f);
    const float u = (static_cast<float>(px) + 0.5f - 1.0f) / denom;
    const float v = (static_cast<float>(py) + 0.5f - 1.0f) / denom;
    return glm::normalize(getDirection(glm::vec2(u, v), face));
}

void halfBlur(const std::vector<glm::vec3>& src,
              std::vector<glm::vec3>& dst,
              int width,
              int height,
              const LodData& lodData,
              int cubeSize,
              int lodIn,
              int lodOut,
              float sigmaRadians,
              bool latitudinal,
              const glm::vec3& poleAxisIn) {
    const Layout outLayout = layoutForLod(lodData, cubeSize, lodOut);
    const float pixels = static_cast<float>(lodData.sizeLods[static_cast<std::size_t>(lodIn)] - 1);
    const float radiansPerPixel = std::isfinite(sigmaRadians)
        ? glm::pi<float>() / (2.0f * std::max(pixels, 1.0f))
        : (2.0f * glm::pi<float>()) / static_cast<float>(2 * kMaxBlurSamples - 1);
    const float sigmaPixels = std::isfinite(sigmaRadians) ? sigmaRadians / std::max(radiansPerPixel, 1e-6f) : 0.0f;
    int samples = std::isfinite(sigmaRadians)
        ? (1 + static_cast<int>(std::floor(3.0f * sigmaPixels)))
        : kMaxBlurSamples;
    samples = std::clamp(samples, 1, kMaxBlurSamples);

    std::array<float, kMaxBlurSamples> weights{};
    float weightSum = 0.0f;
    for (int i = 0; i < kMaxBlurSamples; ++i) {
        const float x = (sigmaPixels > 1e-6f) ? (static_cast<float>(i) / sigmaPixels) : 0.0f;
        const float w = std::exp(-0.5f * x * x);
        weights[static_cast<std::size_t>(i)] = w;
        if (i == 0) {
            weightSum += w;
        } else if (i < samples) {
            weightSum += 2.0f * w;
        }
    }
    const float invWeightSum = (weightSum > 1e-6f) ? (1.0f / weightSum) : 1.0f;
    for (float& w : weights) {
        w *= invWeightSum;
    }

    const glm::vec3 poleAxis = glm::normalize(poleAxisIn);
    const float mipInt = static_cast<float>(lodData.lodMax - lodIn);

    for (int face = 0; face < 6; ++face) {
        for (int py = 0; py < outLayout.size; ++py) {
            for (int px = 0; px < outLayout.size; ++px) {
                const glm::vec3 dir = directionForFacePixel(face, px, py, outLayout.size);
                glm::vec3 axis = latitudinal ? poleAxis : glm::cross(poleAxis, dir);
                if (glm::dot(axis, axis) < 1e-10f) {
                    axis = glm::vec3(dir.z, 0.0f, -dir.x);
                }
                if (glm::dot(axis, axis) < 1e-10f) {
                    axis = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                axis = glm::normalize(axis);

                glm::vec3 color = weights[0] * sampleAtlasCubeUv(src, width, height, lodData.lodMax, dir, mipInt);
                for (int i = 1; i < samples; ++i) {
                    const float theta = radiansPerPixel * static_cast<float>(i);
                    const glm::vec3 d0 = rotateAroundAxis(dir, axis, -theta);
                    const glm::vec3 d1 = rotateAroundAxis(dir, axis, theta);
                    const float w = weights[static_cast<std::size_t>(i)];
                    color += w * sampleAtlasCubeUv(src, width, height, lodData.lodMax, d0, mipInt);
                    color += w * sampleAtlasCubeUv(src, width, height, lodData.lodMax, d1, mipInt);
                }
                writeLodPixel(dst, width, outLayout, face, px, py, color);
            }
        }
    }
}

std::vector<SceneBox> buildRoomEnvironmentScene() {
    std::vector<SceneBox> scene;
    scene.reserve(13);

    SceneBox room{};
    room.center = glm::vec3(-0.757f, 13.219f, 0.717f);
    room.half = glm::vec3(31.713f, 28.305f, 28.591f) * 0.5f;
    room.rotY = 0.0f;
    room.emissive = false;
    room.interior = true;
    scene.push_back(room);

    // RoomEnvironment diffuse boxes.
    const std::array<glm::vec3, 6> boxPos = {
        glm::vec3(-10.906f,  2.009f,  1.846f),
        glm::vec3( -5.607f, -0.754f, -0.758f),
        glm::vec3(  6.167f,  0.857f,  7.803f),
        glm::vec3( -2.017f,  0.018f,  6.124f),
        glm::vec3(  2.291f, -0.756f, -2.621f),
        glm::vec3( -2.193f, -0.369f, -5.547f),
    };
    const std::array<glm::vec3, 6> boxScale = {
        glm::vec3(2.328f, 7.905f, 4.651f),
        glm::vec3(1.970f, 1.534f, 3.955f),
        glm::vec3(3.927f, 6.285f, 3.687f),
        glm::vec3(2.002f, 4.566f, 2.064f),
        glm::vec3(1.546f, 1.552f, 1.496f),
        glm::vec3(3.875f, 3.487f, 2.986f),
    };
    const std::array<float, 6> boxRotY = {-0.195f, 0.994f, 0.561f, 0.333f, -0.286f, 0.516f};
    for (std::size_t i = 0; i < boxPos.size(); ++i) {
        SceneBox b{};
        b.center = boxPos[i];
        b.half = boxScale[i] * 0.5f;
        b.rotY = boxRotY[i];
        b.emissive = false;
        b.interior = false;
        scene.push_back(b);
    }

    // RoomEnvironment emissive panels.
    const std::array<glm::vec3, 6> lightPos = {
        glm::vec3(-16.116f, 14.370f,  8.208f),
        glm::vec3(-16.109f, 18.021f, -8.207f),
        glm::vec3( 14.904f, 12.198f, -1.832f),
        glm::vec3( -0.462f,  8.890f, 14.520f),
        glm::vec3(  3.235f, 11.486f,-12.541f),
        glm::vec3(  0.000f, 20.000f,  0.000f),
    };
    const std::array<glm::vec3, 6> lightScale = {
        glm::vec3(0.1f, 2.428f, 2.739f),
        glm::vec3(0.1f, 2.425f, 2.751f),
        glm::vec3(0.15f, 4.265f, 6.331f),
        glm::vec3(4.38f, 5.441f, 0.088f),
        glm::vec3(2.5f, 2.0f, 0.1f),
        glm::vec3(1.0f, 0.1f, 1.0f),
    };
    const std::array<float, 6> lightIntensity = {50.0f, 50.0f, 17.0f, 43.0f, 20.0f, 100.0f};
    for (std::size_t i = 0; i < lightPos.size(); ++i) {
        SceneBox l{};
        l.center = lightPos[i];
        l.half = lightScale[i] * 0.5f;
        l.rotY = 0.0f;
        l.emissive = true;
        l.emissiveIntensity = lightIntensity[i];
        l.interior = false;
        scene.push_back(l);
    }

    return scene;
}

Atlas buildNeutralRoomPmremAtlas() {
    Atlas out;
    out.cubeSize = 256;
    const LodData lodData = buildLodData(out.cubeSize);
    out.lodMax = lodData.lodMax;
    out.maxMip = static_cast<float>(lodData.lodMax);
    out.width = 3 * std::max(out.cubeSize, 16 * 7);
    out.height = 4 * out.cubeSize;
    out.texelWidth = 1.0f / static_cast<float>(out.width);
    out.texelHeight = 1.0f / static_cast<float>(out.height);
    out.rgbmRange = kRgbmRange;

    std::vector<glm::vec3> atlas;
    atlas.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), glm::vec3(0.0f));
    std::vector<glm::vec3> ping = atlas;
    const std::vector<SceneBox> scene = buildRoomEnvironmentScene();

    // Base cube capture (lod 0).
    const Layout baseLayout = layoutForLod(lodData, out.cubeSize, 0);
    for (int face = 0; face < 6; ++face) {
        for (int py = 0; py < baseLayout.size; ++py) {
            for (int px = 0; px < baseLayout.size; ++px) {
                const glm::vec3 dir = directionForFacePixel(face, px, py, baseLayout.size);
                const glm::vec3 color = roomRadiance(dir, scene);
                writeLodPixel(atlas, out.width, baseLayout, face, px, py, color);
            }
        }
    }

    // PMREM-style separable blur chain.
    const std::array<glm::vec3, 10> axisDirections = {
        glm::normalize(glm::vec3(-1.61803398875f, 0.61803398875f, 0.0f)),
        glm::normalize(glm::vec3( 1.61803398875f, 0.61803398875f, 0.0f)),
        glm::normalize(glm::vec3(-0.61803398875f, 0.0f, 1.61803398875f)),
        glm::normalize(glm::vec3( 0.61803398875f, 0.0f, 1.61803398875f)),
        glm::normalize(glm::vec3(0.0f, 1.61803398875f, -0.61803398875f)),
        glm::normalize(glm::vec3(0.0f, 1.61803398875f, 0.61803398875f)),
        glm::normalize(glm::vec3(-1.0f, 1.0f, -1.0f)),
        glm::normalize(glm::vec3( 1.0f, 1.0f, -1.0f)),
        glm::normalize(glm::vec3(-1.0f, 1.0f, 1.0f)),
        glm::normalize(glm::vec3( 1.0f, 1.0f, 1.0f)),
    };

    const int n = static_cast<int>(lodData.sizeLods.size());
    for (int i = 1; i < n; ++i) {
        const float sigmaPrev = lodData.sigmas[static_cast<std::size_t>(i - 1)];
        const float sigmaCurr = lodData.sigmas[static_cast<std::size_t>(i)];
        const float sigma = std::sqrt(std::max(0.0f, sigmaCurr * sigmaCurr - sigmaPrev * sigmaPrev));
        const glm::vec3 poleAxis = axisDirections[static_cast<std::size_t>((n - i - 1) % static_cast<int>(axisDirections.size()))];
        halfBlur(atlas, ping, out.width, out.height, lodData, out.cubeSize, i - 1, i, sigma, true, poleAxis);
        halfBlur(ping, atlas, out.width, out.height, lodData, out.cubeSize, i, i, sigma, false, poleAxis);
    }

    // Encode to RGBM so we keep HDR range in RGBA8 world texture path.
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4u, 0u);
    out.rgba16f.resize(
        static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4u, 0u);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            glm::vec3 c = glm::max(atlas[atlasIndex(out.width, x, y)], glm::vec3(0.0f));
            const std::size_t dst = (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                                     static_cast<std::size_t>(x)) * 4u;
            out.rgba16f[dst + 0u] = floatToHalf(c.r);
            out.rgba16f[dst + 1u] = floatToHalf(c.g);
            out.rgba16f[dst + 2u] = floatToHalf(c.b);
            out.rgba16f[dst + 3u] = floatToHalf(1.0f);

            const float peak = std::max(c.r, std::max(c.g, c.b));
            float m = peak / kRgbmRange;
            m = std::clamp(m, 0.0f, 1.0f);
            if (m > 0.0f) {
                m = std::ceil(m * 255.0f) / 255.0f;
                c /= (m * kRgbmRange);
            } else {
                c = glm::vec3(0.0f);
            }
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            out.rgba[dst + 0u] = static_cast<std::uint8_t>(std::lround(c.r * 255.0f));
            out.rgba[dst + 1u] = static_cast<std::uint8_t>(std::lround(c.g * 255.0f));
            out.rgba[dst + 2u] = static_cast<std::uint8_t>(std::lround(c.b * 255.0f));
            out.rgba[dst + 3u] = static_cast<std::uint8_t>(std::lround(m * 255.0f));
        }
    }

    return out;
}

} // namespace

const Atlas& getNeutralRoomPmremAtlas() {
    static const Atlas kAtlas = []() {
        Atlas atlas;
        if (tryLoadNeutralRoomPmremAtlasFromCache(atlas)) {
            return atlas;
        }
        atlas = buildNeutralRoomPmremAtlas();
        tryStoreNeutralRoomPmremAtlasToCache(atlas);
        return atlas;
    }();
    return kAtlas;
}

} // namespace engine::render::neutral_pmrem
