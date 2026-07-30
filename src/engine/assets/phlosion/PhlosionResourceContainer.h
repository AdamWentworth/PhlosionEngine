#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets::phrc {

inline constexpr std::uint16_t kContainerVersion = 1u;
inline constexpr std::uint16_t kHeaderBytes = 96u;
inline constexpr std::uint32_t kDependencyRequired = 1u << 0u;

using Magic = std::array<char, 4>;

struct Dependency {
    std::string assetId;
    std::uint64_t expectedContentHash = 0u;
    std::uint32_t flags = kDependencyRequired;
};

struct Chunk {
    std::array<char, 4> type{'D', 'A', 'T', 'A'};
    std::uint32_t flags = 0u;
    std::uint32_t alignment = 16u;
    std::vector<std::uint8_t> bytes;
};

struct Document {
    Magic magic{'P', 'H', 'R', 'C'};
    std::uint32_t schemaVersion = 1u;
    std::uint32_t flags = 0u;
    std::string manifestJson;
    std::vector<Dependency> dependencies;
    std::vector<Chunk> chunks;
    std::uint64_t contentHash = 0u;
};

Magic magic(std::string_view fourCharacters);
std::string magicString(const Magic& value);
std::uint64_t contentHash64(
    const void* data,
    std::size_t byteCount,
    std::uint64_t seed = 14695981039346656037ull);
std::uint64_t contentHash64(const std::vector<std::uint8_t>& bytes);

bool encode(
    const Document& document,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError = nullptr);

bool decode(
    const std::vector<std::uint8_t>& bytes,
    Document& outDocument,
    std::string* outError = nullptr);

const Chunk* findChunk(
    const Document& document,
    std::string_view fourCharacterType);

} // namespace engine::assets::phrc
