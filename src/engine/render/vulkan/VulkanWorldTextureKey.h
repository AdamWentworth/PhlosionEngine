#pragma once

#include <charconv>
#include <cstdint>
#include <string>

namespace engine::render::vulkan_backend {

// Keep the existing texture identity, without per-draw stream/locale setup.
inline std::string resolvedWorldTextureKey(
    const char* key, const char* cacheKey, const unsigned char* rgba,
    int width, int height, int wrapS, int wrapT, bool srgb,
    std::uint32_t authoredMipLevelCount) {
    std::string out;
    const char* identity = cacheKey && cacheKey[0] ? cacheKey : key;
    out.reserve((identity ? std::char_traits<char>::length(identity) : 32u) + 80u);
    const auto number = [&](auto value) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out.append(buffer, result.ptr);
    };
    if (identity && identity[0]) out += identity;
    else { out += "anonymous@"; number(reinterpret_cast<std::uintptr_t>(rgba)); }
    out += '|'; number(width);
    out += 'x'; number(height);
    out += "|ws="; number(wrapS);
    out += "|wt="; number(wrapT);
    out += "|mips="; number(authoredMipLevelCount);
    out += srgb ? "|srgb" : "|linear";
    return out;
}

} // namespace engine::render::vulkan_backend
