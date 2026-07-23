#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/Paths.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace engine::render::sprite_card_art {

inline constexpr const char* kProxyPrefix = "__backend_card_art__:";

inline bool isProxyPath(std::string_view path) {
    constexpr std::size_t kPrefixLen = 21u;
    return path.size() >= kPrefixLen &&
           path.compare(0u, kPrefixLen, kProxyPrefix) == 0;
}

inline std::string makeProxyPath(std::string_view sourcePath) {
    if (sourcePath.empty()) return {};
    if (isProxyPath(sourcePath)) return std::string(sourcePath);
    std::string out(kProxyPrefix);
    out.append(sourcePath.data(), sourcePath.size());
    return out;
}

inline std::string sourcePathFromProxy(std::string_view path) {
    if (!isProxyPath(path)) return std::string(path);
    constexpr std::size_t kPrefixLen = 21u;
    return std::string(path.substr(kPrefixLen));
}

inline std::string hexHash64(std::uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

inline std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 14695981039346656037ull;
    for (const unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

inline std::filesystem::path resolveExistingPath(std::string_view sourcePath) {
    std::error_code ec;
    std::filesystem::path direct{std::string(sourcePath)};
    if (std::filesystem::exists(direct, ec) && !ec) return direct;
    std::string alt(sourcePath);
    std::replace(alt.begin(), alt.end(), '\\', '/');
    std::filesystem::path normalized{alt};
    ec.clear();
    if (std::filesystem::exists(normalized, ec) && !ec) return normalized;
    return direct;
}

inline std::string proxyCacheFingerprint(const std::filesystem::path& sourcePath, int maxDim) {
    std::error_code ec;
    std::string fingerprint;
    fingerprint.reserve(192u);
    fingerprint += "backend_card_art|cap=";
    fingerprint += std::to_string(maxDim);
    fingerprint += "|";
    fingerprint += sourcePath.generic_string();
    fingerprint += "|size=";
    fingerprint +=
        std::to_string(std::filesystem::exists(sourcePath, ec) && !ec
                           ? std::filesystem::file_size(sourcePath, ec)
                           : 0ull);
    fingerprint += "|mtime=";
    ec.clear();
    if (std::filesystem::exists(sourcePath, ec) && !ec) {
        fingerprint +=
            std::to_string(std::filesystem::last_write_time(sourcePath, ec).time_since_epoch().count());
    } else {
        fingerprint += "0";
    }
    return fingerprint;
}

inline std::filesystem::path proxyCacheBasePath(std::string_view sourcePath, int maxDim) {
    const std::filesystem::path resolved = resolveExistingPath(sourcePath);
    const std::string fingerprint = proxyCacheFingerprint(resolved, maxDim);
    return std::filesystem::path(engine::paths::data("cache/card_art")) /
           ("proxy_" + hexHash64(fnv1a64(fingerprint)));
}

inline std::filesystem::path proxyCachePath(std::string_view sourcePath, int maxDim) {
    auto path = proxyCacheBasePath(sourcePath, maxDim);
    path += ".tga";
    return path;
}

inline std::filesystem::path legacyProxyCachePath(std::string_view sourcePath, int maxDim) {
    auto path = proxyCacheBasePath(sourcePath, maxDim);
    path += ".png";
    return path;
}

inline int scaledDimension(int sourceWidth, int sourceHeight, int maxDim, bool widthAxis) {
    if (sourceWidth <= 0 || sourceHeight <= 0) return 0;
    if (maxDim <= 0) return widthAxis ? sourceWidth : sourceHeight;
    const int longest = std::max(sourceWidth, sourceHeight);
    if (longest <= maxDim) return widthAxis ? sourceWidth : sourceHeight;
    const float scale = static_cast<float>(maxDim) / static_cast<float>(longest);
    const int scaled = static_cast<int>(std::lround(
        static_cast<float>(widthAxis ? sourceWidth : sourceHeight) * scale));
    return std::max(1, scaled);
}

inline std::vector<unsigned char> resizeRgbaBilinear(const unsigned char* src,
                                                     int srcWidth,
                                                     int srcHeight,
                                                     int dstWidth,
                                                     int dstHeight) {
    std::vector<unsigned char> out;
    if (!src || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
        return out;
    }
    out.resize(static_cast<std::size_t>(dstWidth) * static_cast<std::size_t>(dstHeight) * 4u);

    const float scaleX = static_cast<float>(srcWidth) / static_cast<float>(dstWidth);
    const float scaleY = static_cast<float>(srcHeight) / static_cast<float>(dstHeight);

    const auto sample = [&](int x, int y, int c) -> float {
        x = std::clamp(x, 0, srcWidth - 1);
        y = std::clamp(y, 0, srcHeight - 1);
        const std::size_t idx =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(srcWidth) +
             static_cast<std::size_t>(x)) * 4u +
            static_cast<std::size_t>(c);
        return static_cast<float>(src[idx]);
    };

    for (int y = 0; y < dstHeight; ++y) {
        const float srcY = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const int y0 = static_cast<int>(std::floor(srcY));
        const int y1 = y0 + 1;
        const float fy = srcY - static_cast<float>(y0);
        for (int x = 0; x < dstWidth; ++x) {
            const float srcX = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const int x0 = static_cast<int>(std::floor(srcX));
            const int x1 = x0 + 1;
            const float fx = srcX - static_cast<float>(x0);
            const std::size_t dstIdx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(dstWidth) +
                 static_cast<std::size_t>(x)) * 4u;
            for (int c = 0; c < 4; ++c) {
                const float top = sample(x0, y0, c) + (sample(x1, y0, c) - sample(x0, y0, c)) * fx;
                const float bottom = sample(x0, y1, c) + (sample(x1, y1, c) - sample(x0, y1, c)) * fx;
                const float value = top + (bottom - top) * fy;
                out[dstIdx + static_cast<std::size_t>(c)] =
                    static_cast<unsigned char>(std::clamp(std::lround(value), 0l, 255l));
            }
        }
    }

    return out;
}

} // namespace engine::render::sprite_card_art
