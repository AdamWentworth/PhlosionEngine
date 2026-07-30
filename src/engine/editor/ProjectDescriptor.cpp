#include "engine/editor/ProjectDescriptor.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

namespace engine::editor {

namespace {

bool fail(std::string message, std::string* outError) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

bool isPortableRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool isPortableLibraryName(std::string_view name) {
    return !name.empty() &&
           name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos &&
           name != "." &&
           name != "..";
}

std::string replaceConfigurationToken(
    std::string value,
    std::string_view buildConfiguration) {
    constexpr std::string_view token = "{config}";
    std::size_t offset = 0u;
    while ((offset = value.find(token, offset)) != std::string::npos) {
        value.replace(offset, token.size(), buildConfiguration);
        offset += buildConfiguration.size();
    }
    return value;
}

} // namespace

bool parseProjectDescriptor(
    const std::string& jsonText,
    ProjectDescriptor& out,
    std::string* outError) {
    try {
        const nlohmann::json root = nlohmann::json::parse(jsonText);
        ProjectDescriptor parsed;
        parsed.schemaVersion = root.at("schema_version").get<std::uint32_t>();
        parsed.projectId = root.at("project_id").get<std::string>();
        parsed.displayName = root.at("display_name").get<std::string>();
        if (parsed.schemaVersion != 1u) {
            return fail(
                "Unsupported Phlosion project schema version: " +
                    std::to_string(parsed.schemaVersion),
                outError);
        }
        if (parsed.projectId.empty() || parsed.displayName.empty()) {
            return fail(
                "Project id and display name must not be empty.",
                outError);
        }

        for (const auto& mountJson : root.at("content_mounts")) {
            ContentMount mount;
            mount.id = mountJson.at("id").get<std::string>();
            mount.root = mountJson.at("root").get<std::string>();
            mount.required = mountJson.value("required", true);
            if (mount.id.empty() || !isPortableRelativePath(mount.root)) {
                return fail(
                    "Content mounts require an id and a portable relative root.",
                    outError);
            }
            const auto duplicate = std::find_if(
                parsed.contentMounts.begin(),
                parsed.contentMounts.end(),
                [&](const ContentMount& candidate) {
                    return candidate.id == mount.id;
                });
            if (duplicate != parsed.contentMounts.end()) {
                return fail(
                    "Duplicate content mount id: " + mount.id,
                    outError);
            }
            parsed.contentMounts.push_back(std::move(mount));
        }
        if (parsed.contentMounts.empty()) {
            return fail("At least one content mount is required.", outError);
        }

        const auto& sceneJson = root.at("startup_scene");
        parsed.startupScene.assetId =
            sceneJson.at("asset_id").get<std::string>();
        parsed.startupScene.mountId =
            sceneJson.at("mount").get<std::string>();
        parsed.startupScene.path =
            sceneJson.at("path").get<std::string>();
        if (parsed.startupScene.assetId.empty() ||
            parsed.startupScene.mountId.empty() ||
            !isPortableRelativePath(parsed.startupScene.path)) {
            return fail(
                "Startup scene requires an asset id, mount id, and portable relative path.",
                outError);
        }

        if (root.contains("editor_plugin")) {
            const auto& pluginJson = root.at("editor_plugin");
            parsed.editorPlugin.library =
                pluginJson.at("library").get<std::string>();
            parsed.editorPlugin.directory =
                pluginJson.at("directory").get<std::string>();
            if (!isPortableLibraryName(parsed.editorPlugin.library) ||
                !isPortableRelativePath(parsed.editorPlugin.directory)) {
                return fail(
                    "Editor plugin requires a portable library name and relative directory.",
                    outError);
            }
        }

        if (root.contains("private_asset_depot")) {
            parsed.privateAssetDepotEnvironment =
                root.at("private_asset_depot")
                    .value("environment", std::string{});
        }

        out = std::move(parsed);
        if (outError) {
            outError->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        return fail(
            std::string("Invalid Phlosion project descriptor: ") +
                exception.what(),
            outError);
    }
}

bool loadProjectDescriptor(
    const std::filesystem::path& descriptorPath,
    ProjectDescriptor& out,
    std::string* outError) {
    std::ifstream input(descriptorPath, std::ios::binary);
    if (!input) {
        return fail(
            "Could not open project descriptor: " +
                descriptorPath.string(),
            outError);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseProjectDescriptor(buffer.str(), out, outError);
}

bool resolveStartupScenePath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    std::filesystem::path& out,
    std::string* outError) {
    const auto mount = std::find_if(
        descriptor.contentMounts.begin(),
        descriptor.contentMounts.end(),
        [&](const ContentMount& candidate) {
            return candidate.id == descriptor.startupScene.mountId;
        });
    if (mount == descriptor.contentMounts.end()) {
        return fail(
            "Startup scene references unknown mount: " +
                descriptor.startupScene.mountId,
            outError);
    }

    std::error_code error;
    const auto descriptorDirectory =
        std::filesystem::absolute(descriptorPath, error).parent_path();
    if (error) {
        return fail(
            "Could not resolve project descriptor directory: " +
                error.message(),
            outError);
    }
    out = (descriptorDirectory / mount->root /
           descriptor.startupScene.path)
              .lexically_normal();
    if (outError) {
        outError->clear();
    }
    return true;
}

bool resolveEditorPluginPath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError) {
    if (descriptor.editorPlugin.library.empty() ||
        descriptor.editorPlugin.directory.empty()) {
        return fail(
            "Project does not declare an editor_plugin.",
            outError);
    }
    if (buildConfiguration.empty()) {
        return fail(
            "Editor build configuration must not be empty.",
            outError);
    }

    std::error_code error;
    const auto descriptorDirectory =
        std::filesystem::absolute(descriptorPath, error).parent_path();
    if (error) {
        return fail(
            "Could not resolve project descriptor directory: " +
                error.message(),
            outError);
    }

    const std::filesystem::path pluginDirectory =
        replaceConfigurationToken(
            descriptor.editorPlugin.directory.generic_string(),
            buildConfiguration);
#if defined(_WIN32)
    const std::string libraryFile =
        descriptor.editorPlugin.library + ".dll";
#elif defined(__APPLE__)
    const std::string libraryFile =
        "lib" + descriptor.editorPlugin.library + ".dylib";
#else
    const std::string libraryFile =
        "lib" + descriptor.editorPlugin.library + ".so";
#endif
    out = (descriptorDirectory / pluginDirectory / libraryFile)
              .lexically_normal();
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace engine::editor
