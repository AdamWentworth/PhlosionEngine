// src/engine/core/IAssetStore.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// Minimal asset store abstraction.
// Backends may read from loose files (dev) or packed bundles (release).
class IAssetStore {
public:
    virtual ~IAssetStore() = default;

    // Returns true on success, false on error (optional error message).
    virtual bool readText(const std::string& virtualPath, std::string& outText, std::string* outError = nullptr) const = 0;
    virtual bool readBytes(const std::string& virtualPath, std::vector<std::uint8_t>& outBytes, std::string* outError = nullptr) const = 0;

    virtual bool exists(const std::string& virtualPath) const = 0;
};

} // namespace engine
