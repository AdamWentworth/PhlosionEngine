#include "engine/models/PhlosionTextureDependencyStore.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <chrono>
#include <fstream>
#include <system_error>
#include <utility>

namespace engine::models::texture_dependency_store {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSharedTexturePrefix = "dependencies/ktx2/";

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

std::string lowerHex64(std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (std::size_t index = 0u; index < out.size(); ++index) {
        const std::size_t reverseIndex = out.size() - 1u - index;
        out[reverseIndex] = kDigits[value & 0x0full];
        value >>= 4u;
    }
    return out;
}

bool readFile(
    const fs::path& path,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return fail(
            outError,
            "Could not open Phlosion resource: " + path.string());
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        return fail(
            outError,
            "Could not measure Phlosion resource: " + path.string());
    }
    out.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    }
    if (!input) {
        return fail(
            outError,
            "Could not read Phlosion resource: " + path.string());
    }
    return true;
}

bool writeFile(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string* outError) {
    std::error_code errorCode;
    fs::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        return fail(
            outError,
            "Could not create Phlosion output directory: " +
                errorCode.message());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail(
            outError,
            "Could not create Phlosion resource: " + path.string());
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        return fail(
            outError,
            "Could not write Phlosion resource: " + path.string());
    }
    return true;
}

bool isSafeRelativeAssetId(std::string_view assetId) {
    if (assetId.empty()) return false;
    const fs::path path(assetId);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const fs::path& component : path) {
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

bool isLowerHex(char value) {
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f');
}

bool isSharedTextureAssetId(std::string_view assetId) {
    if (!assetId.starts_with(kSharedTexturePrefix)) return false;
    const std::string_view fileName = assetId.substr(kSharedTexturePrefix.size());
    constexpr std::size_t kIdentityLength = 16u + 1u + 16u + 5u;
    if (fileName.size() != kIdentityLength ||
        fileName[16u] != '-' ||
        fileName.substr(fileName.size() - 5u) != ".ktx2") {
        return false;
    }
    for (std::size_t index = 0u; index < 16u; ++index) {
        if (!isLowerHex(fileName[index]) ||
            !isLowerHex(fileName[17u + index])) {
            return false;
        }
    }
    return true;
}

bool resolve(
    const fs::path& materialDirectory,
    std::string_view assetId,
    fs::path& outPath,
    std::string* outError) {
    if (!isSafeRelativeAssetId(assetId)) {
        return fail(
            outError,
            "PHMAT texture dependency has an unsafe asset ID: " +
                std::string(assetId));
    }
    if (assetId.starts_with(kSharedTexturePrefix)) {
        if (!isSharedTextureAssetId(assetId) ||
            materialDirectory.parent_path().filename() != "objects") {
            return fail(
                outError,
                "PHMAT shared texture dependency has an invalid identity: " +
                    std::string(assetId));
        }
        outPath =
            materialDirectory.parent_path().parent_path() /
            fs::path(assetId);
        return true;
    }
    const fs::path relative(assetId);
    if (relative.begin() == relative.end() ||
        *relative.begin() != "textures") {
        return fail(
            outError,
            "Legacy PHMAT texture dependency is outside its texture directory: " +
                std::string(assetId));
    }
    outPath = materialDirectory / relative;
    return true;
}

bool publishImmutableFile(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string* outError) {
    std::error_code errorCode;
    if (fs::exists(path, errorCode)) {
        if (errorCode) {
            return fail(
                outError,
                "Could not inspect shared Phlosion dependency: " +
                    errorCode.message());
        }
        std::vector<std::uint8_t> existing;
        if (!readFile(path, existing, outError)) return false;
        if (existing != bytes) {
            return fail(
                outError,
                "Shared Phlosion dependency identity collision: " +
                    path.string());
        }
        return true;
    }
    if (errorCode) {
        return fail(
            outError,
            "Could not inspect shared Phlosion dependency: " +
                errorCode.message());
    }

    const auto nonce = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const fs::path partial =
        path.string() + ".partial." + std::to_string(nonce);
    const auto cleanup = [&]() {
        std::error_code ignored;
        fs::remove(partial, ignored);
    };
    if (!writeFile(partial, bytes, outError)) {
        cleanup();
        return false;
    }
    std::vector<std::uint8_t> verification;
    if (!readFile(partial, verification, outError) || verification != bytes) {
        cleanup();
        if (outError && outError->empty()) {
            *outError =
                "Shared Phlosion dependency changed during publication: " +
                path.string();
        }
        return false;
    }
    fs::rename(partial, path, errorCode);
    if (!errorCode) return true;

    // Another cooker may have won the immutable publication race. Accept the
    // winner only when it is exactly the payload this identity names.
    std::vector<std::uint8_t> winner;
    if (readFile(path, winner, nullptr) && winner == bytes) {
        cleanup();
        return true;
    }
    const std::string publishFailure = errorCode.message();
    cleanup();
    return fail(
        outError,
        "Could not publish shared Phlosion dependency " + path.string() +
            ": " + publishFailure);
}

} // namespace

std::string sharedAssetId(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t semanticHash) {
    return
        std::string(kSharedTexturePrefix) +
        lowerHex64(engine::assets::phrc::contentHash64(bytes)) +
        "-" + lowerHex64(semanticHash) + ".ktx2";
}

bool publishShared(
    const fs::path& cookedRoot,
    std::string_view assetId,
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t semanticHash,
    std::string* outError) {
    if (!isSharedTextureAssetId(assetId) ||
        !isSafeRelativeAssetId(assetId) ||
        assetId != sharedAssetId(bytes, semanticHash)) {
        return fail(
            outError,
            "Shared Phlosion texture dependency has an invalid identity: " +
                std::string(assetId));
    }
    return publishImmutableFile(cookedRoot / fs::path(assetId), bytes, outError);
}

bool readDependency(
    const fs::path& materialDirectory,
    std::string_view assetId,
    fs::path& outPhysicalPath,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) {
    outPhysicalPath.clear();
    outBytes.clear();
    return
        resolve(materialDirectory, assetId, outPhysicalPath, outError) &&
        readFile(outPhysicalPath, outBytes, outError);
}

} // namespace engine::models::texture_dependency_store
