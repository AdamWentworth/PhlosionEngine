#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace engine::assets::phlosion {
namespace {

constexpr std::uint32_t kSchemaVersion = 1u;
constexpr std::size_t kMaxArchiveFiles = 1'000'000u;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

bool normalizedVirtualPath(
    std::string_view input,
    std::string& out) {
    out.assign(input);
    std::replace(out.begin(), out.end(), '\\', '/');
    while (out.starts_with("./")) {
        out.erase(0u, 2u);
    }
    if (out.empty() ||
        out.front() == '/' ||
        out.find(':') != std::string::npos) {
        return false;
    }
    std::size_t begin = 0u;
    while (begin <= out.size()) {
        const std::size_t end = out.find('/', begin);
        const std::string_view part(
            out.data() + begin,
            (end == std::string::npos ? out.size() : end) - begin);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1u;
    }
    return true;
}

} // namespace

bool encodeSceneArchive(
    const std::string& sceneId,
    std::vector<SceneArchiveFile> files,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) {
    outBytes.clear();
    if (sceneId.empty()) {
        return fail(outError, "PHSC scene ID must not be empty.");
    }
    if (files.empty() || files.size() > kMaxArchiveFiles) {
        return fail(outError, "PHSC file count is invalid.");
    }
    for (SceneArchiveFile& file : files) {
        std::string path;
        if (!normalizedVirtualPath(file.virtualPath, path)) {
            return fail(
                outError,
                "PHSC contains an invalid virtual path: " +
                    file.virtualPath);
        }
        file.virtualPath = std::move(path);
    }
    std::sort(
        files.begin(),
        files.end(),
        [](const SceneArchiveFile& left, const SceneArchiveFile& right) {
            return left.virtualPath < right.virtualPath;
        });

    phrc::Document archive;
    archive.magic = phrc::magic("PHSC");
    archive.schemaVersion = kSchemaVersion;
    nlohmann::json manifest{
        {"schema_version", kSchemaVersion},
        {"container", "PHRC-1"},
        {"cooker", "PhlosionForge"},
        {"target_profile", "desktop-canonical-scene"},
        {"root_type", "Scene"},
        {"scene_id", sceneId},
        {"files", nlohmann::json::array()}};
    std::string previousPath;
    for (std::size_t index = 0u; index < files.size(); ++index) {
        const std::string& path = files[index].virtualPath;
        if (path == previousPath) {
            return fail(
                outError,
                "PHSC contains a duplicate virtual path: " + path);
        }
        previousPath = path;
        phrc::Chunk chunk;
        chunk.type = phrc::magic("FILE");
        chunk.alignment = 16u;
        chunk.bytes = std::move(files[index].bytes);
        manifest["files"].push_back({
            {"path", path},
            {"chunk", index},
            {"bytes", chunk.bytes.size()},
            {"content_hash", phrc::contentHash64(chunk.bytes)}});
        archive.chunks.push_back(std::move(chunk));
    }
    archive.manifestJson = manifest.dump();
    return phrc::encode(archive, outBytes, outError);
}

bool SceneArchiveStore::load(
    const IAssetStore& hostStore,
    const std::string& archiveVirtualPath,
    std::string* outError) {
    std::vector<std::uint8_t> bytes;
    if (!hostStore.readBytes(archiveVirtualPath, bytes, outError)) {
        return false;
    }
    return loadBytes(bytes, outError);
}

bool SceneArchiveStore::loadBytes(
    const std::vector<std::uint8_t>& archiveBytes,
    std::string* outError) {
    phrc::Document archive;
    if (!phrc::decode(archiveBytes, archive, outError)) {
        return false;
    }
    if (archive.magic != phrc::magic("PHSC") ||
        archive.schemaVersion != kSchemaVersion) {
        return fail(outError, "PHSC type or schema is unsupported.");
    }
    try {
        const nlohmann::json manifest =
            nlohmann::json::parse(archive.manifestJson);
        if (manifest.at("schema_version").get<std::uint32_t>() !=
                kSchemaVersion ||
            manifest.at("root_type") != "Scene") {
            return fail(outError, "PHSC manifest contract is unsupported.");
        }
        const auto& records = manifest.at("files");
        if (!records.is_array() ||
            records.empty() ||
            records.size() > kMaxArchiveFiles) {
            return fail(outError, "PHSC manifest file count is invalid.");
        }
        std::map<std::string, std::vector<std::uint8_t>> loadedFiles;
        for (const auto& record : records) {
            std::string path;
            const std::string storedPath =
                record.at("path").get<std::string>();
            const std::size_t chunkIndex =
                record.at("chunk").get<std::size_t>();
            const std::uint64_t expectedBytes =
                record.at("bytes").get<std::uint64_t>();
            const std::uint64_t expectedHash =
                record.at("content_hash").get<std::uint64_t>();
            if (!normalizedVirtualPath(storedPath, path) ||
                chunkIndex >= archive.chunks.size() ||
                archive.chunks[chunkIndex].type != phrc::magic("FILE") ||
                archive.chunks[chunkIndex].bytes.size() != expectedBytes ||
                phrc::contentHash64(
                    archive.chunks[chunkIndex].bytes) != expectedHash ||
                loadedFiles.contains(path)) {
                return fail(
                    outError,
                    "PHSC manifest contains an invalid file record.");
            }
            loadedFiles.emplace(
                std::move(path),
                archive.chunks[chunkIndex].bytes);
        }
        sceneId_ = manifest.at("scene_id").get<std::string>();
        if (sceneId_.empty()) {
            return fail(outError, "PHSC scene ID is empty.");
        }
        files_ = std::move(loadedFiles);
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Could not decode PHSC manifest: " +
                std::string(exception.what()));
    }
}

bool SceneArchiveStore::readText(
    const std::string& virtualPath,
    std::string& outText,
    std::string* outError) const {
    std::vector<std::uint8_t> bytes;
    if (!readBytes(virtualPath, bytes, outError)) {
        return false;
    }
    if (bytes.empty()) {
        outText.clear();
    } else {
        outText.assign(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
    }
    return true;
}

bool SceneArchiveStore::readBytes(
    const std::string& virtualPath,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) const {
    std::string path;
    if (!normalizedVirtualPath(virtualPath, path)) {
        return fail(outError, "Invalid PHSC virtual path.");
    }
    const auto found = files_.find(path);
    if (found == files_.end()) {
        return fail(
            outError,
            "PHSC asset does not exist: " + path);
    }
    outBytes = found->second;
    return true;
}

bool SceneArchiveStore::exists(
    const std::string& virtualPath) const {
    std::string path;
    return normalizedVirtualPath(virtualPath, path) &&
        files_.contains(path);
}

} // namespace engine::assets::phlosion
