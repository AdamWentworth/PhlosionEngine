// AssetPackFormat.h
#pragma once

#include <cstdint>

namespace engine::assets {

// Pack file format (v1)
// Header:
//   magic[4] = "PACD"
//   uint32_t version
//   uint32_t entryCount
//   uint64_t indexOffset
// Index entries (entryCount times):
//   uint32_t pathLength
//   uint64_t offset
//   uint64_t size
//   char[pathLength] path (UTF-8, forward slashes)
//
// Data region precedes the index and stores raw bytes per entry.
constexpr char kPackMagic[4] = {'P', 'A', 'C', 'D'};
constexpr std::uint32_t kPackVersion = 1;
constexpr std::uint32_t kPackMaxPathLength = 64 * 1024; // 64 KB safety cap

} // namespace engine::assets
