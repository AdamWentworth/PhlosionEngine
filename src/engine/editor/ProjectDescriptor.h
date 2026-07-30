#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::editor {

struct ContentMount {
    std::string id;
    std::filesystem::path root;
    bool required = true;
};

struct StartupScene {
    std::string assetId;
    std::string mountId;
    std::filesystem::path path;
};

struct ProjectDescriptor {
    std::uint32_t schemaVersion = 0u;
    std::string projectId;
    std::string displayName;
    std::vector<ContentMount> contentMounts;
    StartupScene startupScene;
    std::string privateAssetDepotEnvironment;
};

bool parseProjectDescriptor(
    const std::string& jsonText,
    ProjectDescriptor& out,
    std::string* outError = nullptr);

bool loadProjectDescriptor(
    const std::filesystem::path& descriptorPath,
    ProjectDescriptor& out,
    std::string* outError = nullptr);

bool resolveStartupScenePath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    std::filesystem::path& out,
    std::string* outError = nullptr);

} // namespace engine::editor
