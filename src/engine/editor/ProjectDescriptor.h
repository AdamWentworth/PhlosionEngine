#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor {

struct ContentMount {
    std::string id;
    std::filesystem::path root;
    std::vector<std::string>
        assetBrowserExcludePatterns;
    bool required = true;
};

struct ProjectEnvironment {
    std::string assetId;
    std::string displayName;
    std::string kind;
    std::string mountId;
    std::filesystem::path path;
};

struct ProjectScene {
    std::string sceneId;
    std::string displayName;
    std::string category;
    std::string environmentAssetId;
    std::filesystem::path runtimePath;
    std::string status;
};

struct EditorPlugin {
    std::string library;
    std::filesystem::path directory;
};

struct PlayEnvironmentVariable {
    std::string name;
    std::string value;
};

struct PlayConfiguration {
    std::string id;
    std::string displayName;
    std::string group;
    std::string description;
    std::filesystem::path executable;
    std::filesystem::path workingDirectory = ".";
    std::vector<std::string> arguments;
    std::vector<PlayEnvironmentVariable> environment;
};

struct ProjectDescriptor {
    std::uint32_t schemaVersion = 0u;
    std::string projectId;
    std::string displayName;
    std::vector<ContentMount> contentMounts;
    std::string startupSceneId;
    std::vector<ProjectEnvironment> environments;
    std::vector<ProjectScene> scenes;
    EditorPlugin editorPlugin;
    std::vector<PlayConfiguration> playConfigurations;
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

bool resolveScenePath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    const ProjectScene& scene,
    std::filesystem::path& out,
    std::string* outError = nullptr);

bool resolveEditorPluginPath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError = nullptr);

bool resolvePlayExecutablePath(
    const std::filesystem::path& descriptorPath,
    const PlayConfiguration& configuration,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError = nullptr);

bool resolvePlayWorkingDirectory(
    const std::filesystem::path& descriptorPath,
    const PlayConfiguration& configuration,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError = nullptr);

} // namespace engine::editor
