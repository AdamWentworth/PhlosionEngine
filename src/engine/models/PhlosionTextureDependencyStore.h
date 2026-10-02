#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::models::texture_dependency_store {

// Shared texture identities partition byte-identical KTX2 payloads when their
// sampling or material semantics are incompatible.
std::string sharedAssetId(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t semanticHash);

bool publishShared(
    const std::filesystem::path& cookedRoot,
    std::string_view assetId,
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t semanticHash,
    std::string* outError = nullptr);

// Reads both the shared layout and the schema-1 private texture layout so old
// cooked objects remain loadable while runtime GLB compatibility is retired.
bool readDependency(
    const std::filesystem::path& materialDirectory,
    std::string_view assetId,
    std::filesystem::path& outPhysicalPath,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError = nullptr);

} // namespace engine::models::texture_dependency_store
