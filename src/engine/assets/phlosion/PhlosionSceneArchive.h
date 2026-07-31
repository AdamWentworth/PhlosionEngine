#pragma once

#include "engine/core/IAssetStore.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

struct SceneArchiveFile {
    std::string virtualPath;
    std::vector<std::uint8_t> bytes;
};

bool encodeSceneArchive(
    const std::string& sceneId,
    std::vector<SceneArchiveFile> files,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError = nullptr);

bool encodePrefabArchive(
    const std::string& prefabId,
    const std::string& prefabKind,
    const std::string& metadataJson,
    std::vector<SceneArchiveFile> files,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError = nullptr);

class SceneArchiveStore final : public IAssetStore {
public:
    bool load(
        const IAssetStore& hostStore,
        const std::string& archiveVirtualPath,
        std::string* outError = nullptr);

    bool loadBytes(
        const std::vector<std::uint8_t>& archiveBytes,
        std::string* outError = nullptr);

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override;

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override;

    bool exists(const std::string& virtualPath) const override;

    const std::string& sceneId() const noexcept {
        return sceneId_;
    }

    std::size_t fileCount() const noexcept {
        return files_.size();
    }

private:
    std::string sceneId_;
    std::map<std::string, std::vector<std::uint8_t>> files_;
};

class PrefabArchiveStore final : public IAssetStore {
public:
    bool load(
        const IAssetStore& hostStore,
        const std::string& archiveVirtualPath,
        std::string* outError = nullptr);

    bool loadBytes(
        const std::vector<std::uint8_t>& archiveBytes,
        std::string* outError = nullptr);

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override;

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override;

    bool exists(const std::string& virtualPath) const override;

    const std::string& prefabId() const noexcept {
        return prefabId_;
    }

    const std::string& prefabKind() const noexcept {
        return prefabKind_;
    }

    const std::string& metadataJson() const noexcept {
        return metadataJson_;
    }

    std::size_t fileCount() const noexcept {
        return files_.size();
    }

private:
    std::string prefabId_;
    std::string prefabKind_;
    std::string metadataJson_;
    std::map<std::string, std::vector<std::uint8_t>> files_;
};

} // namespace engine::assets::phlosion
