#include "engine/render/vulkan/VulkanWorldTextureKey.h"

#include <array>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

using engine::render::vulkan_backend::resolvedWorldTextureKey;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct GroupedNumbers : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};
}

int main() try {
    const unsigned char pixel = 255;
    const auto key = [&](const char* source, const char* cache) {
        return resolvedWorldTextureKey(source, cache, &pixel, 1024, 512, 10497, 33071, true, 4);
    };
    check(key("source", "cache") == "cache|1024x512|ws=10497|wt=33071|mips=4|srgb",
          "Explicit cache identity did not override source name");
    check(key("source", nullptr) == key("source", ""), "Empty cache identity did not use source name");
    check(key(nullptr, "cache") == key("ignored", "cache"), "Source name changed explicit cache identity");
    const auto anonymous = "anonymous@" + std::to_string(reinterpret_cast<std::uintptr_t>(&pixel)) +
        "|1024x512|ws=10497|wt=33071|mips=4|srgb";
    check(key(nullptr, nullptr) == anonymous && key("", "") == anonymous, "Anonymous identity lost pixel address");
    check(resolvedWorldTextureKey(nullptr, nullptr, nullptr, 0, 0, 0, 0, false, 0) ==
        "anonymous@0|0x0|ws=0|wt=0|mips=0|linear", "Null anonymous or linear identity changed");
    // Preserve the complete legacy spelling at integer limits and for punctuation
    // in authored names. Every sampling/format field must remain part of identity.
    constexpr std::array<int, 5> values{0, 1, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    for (int width : values) for (int height : values) for (int wrapS : values) for (int wrapT : values)
        for (bool srgb : {false, true}) for (std::uint32_t mips : {0u, 12u, UINT32_MAX}) {
            std::ostringstream legacy;
            legacy.imbue(std::locale::classic());
            legacy << "asset/texture|variant:1" << '|' << width << 'x' << height
                   << "|ws=" << wrapS << "|wt=" << wrapT << "|mips=" << mips << (srgb ? "|srgb" : "|linear");
            check(resolvedWorldTextureKey("asset/texture|variant:1", nullptr, &pixel, width, height, wrapS, wrapT, srgb, mips) ==
                legacy.str(), "Legacy texture identity changed");
        }
    const auto previous = std::locale::global(std::locale(std::locale::classic(), new GroupedNumbers));
    const auto localized = key("source", "cache");
    std::locale::global(previous);
    check(localized == "cache|1024x512|ws=10497|wt=33071|mips=4|srgb", "Cache identity depends on global number formatting");
    std::cout << "PASS Vulkan texture identity precedence, anonymous addresses, sampling fields, numeric limits and locale independence\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
