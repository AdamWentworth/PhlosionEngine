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
            mount.assetBrowserExcludePatterns =
                mountJson.value(
                    "asset_browser_exclude",
                    std::vector<std::string>{});
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
            for (const auto& pattern :
                 mount.assetBrowserExcludePatterns) {
                if (!isPortableRelativePath(pattern)) {
                    return fail(
                        "Asset-browser exclusions must be portable paths relative to their content mount.",
                        outError);
                }
            }
            parsed.contentMounts.push_back(std::move(mount));
        }
        if (parsed.contentMounts.empty()) {
            return fail("At least one content mount is required.", outError);
        }

        const auto& startupSceneJson = root.at("startup_scene");
        parsed.startupScene.assetId =
            startupSceneJson.at("asset_id").get<std::string>();
        parsed.startupScene.mountId =
            startupSceneJson.at("mount").get<std::string>();
        parsed.startupScene.path =
            startupSceneJson.at("path").get<std::string>();
        if (parsed.startupScene.assetId.empty() ||
            parsed.startupScene.mountId.empty() ||
            !isPortableRelativePath(parsed.startupScene.path)) {
            return fail(
                "Startup scene requires an asset id, mount id, and portable relative path.",
                outError);
        }

        if (root.contains("scenes")) {
            for (const auto& sceneJson : root.at("scenes")) {
                ProjectScene scene;
                scene.assetId =
                    sceneJson.at("asset_id").get<std::string>();
                scene.displayName =
                    sceneJson.at("display_name").get<std::string>();
                scene.category =
                    sceneJson.value("category", std::string{});
                const std::string kind =
                    sceneJson.value(
                        "kind",
                        std::string("cooked_world"));
                scene.mountId =
                    sceneJson.value("mount", std::string{});
                scene.path =
                    sceneJson.at("path").get<std::string>();
                if (scene.assetId.empty() ||
                    scene.displayName.empty() ||
                    kind != "cooked_world" ||
                    scene.mountId.empty() ||
                    !isPortableRelativePath(scene.path)) {
                    return fail(
                        "Project scenes must be cooked-world documents with an asset id, display name, mount, and portable relative path. Runtime states belong in the project plugin's Game Preview catalog.",
                        outError);
                }
                const auto duplicate = std::find_if(
                    parsed.scenes.begin(),
                    parsed.scenes.end(),
                    [&](const ProjectScene& candidate) {
                        return candidate.assetId == scene.assetId;
                    });
                if (duplicate != parsed.scenes.end()) {
                    return fail(
                        "Duplicate project scene asset id: " +
                            scene.assetId,
                        outError);
                }
                parsed.scenes.push_back(std::move(scene));
            }
        }
        if (parsed.scenes.empty()) {
            parsed.scenes.push_back(ProjectScene{
                .assetId = parsed.startupScene.assetId,
                .displayName = parsed.startupScene.assetId,
                .category = "Scenes",
                .mountId = parsed.startupScene.mountId,
                .path = parsed.startupScene.path});
        }
        const auto startupCatalogEntry = std::find_if(
            parsed.scenes.begin(),
            parsed.scenes.end(),
            [&](const ProjectScene& scene) {
                return scene.assetId ==
                       parsed.startupScene.assetId;
            });
        if (startupCatalogEntry == parsed.scenes.end()) {
            return fail(
                "The startup scene must also appear in the cooked scene catalog.",
                outError);
        }
        if (startupCatalogEntry->mountId !=
                parsed.startupScene.mountId ||
            startupCatalogEntry->path !=
                parsed.startupScene.path) {
            return fail(
                "The startup scene and its cooked scene catalog entry must resolve to the same mount and path.",
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

        if (root.contains("play_configurations")) {
            for (const auto& playJson :
                 root.at("play_configurations")) {
                PlayConfiguration configuration;
                configuration.id =
                    playJson.at("id").get<std::string>();
                configuration.displayName =
                    playJson.at("display_name").get<std::string>();
                configuration.group =
                    playJson.value("group", std::string{});
                configuration.description =
                    playJson.value("description", std::string{});
                configuration.executable =
                    playJson.at("executable").get<std::string>();
                configuration.workingDirectory =
                    playJson.value(
                        "working_directory",
                        std::string("."));
                configuration.arguments =
                    playJson.value(
                        "arguments",
                        std::vector<std::string>{});

                if (configuration.id.empty() ||
                    configuration.displayName.empty() ||
                    !isPortableRelativePath(
                        configuration.executable) ||
                    !isPortableRelativePath(
                        configuration.workingDirectory)) {
                    return fail(
                        "Play configurations require an id, display name, portable relative executable, and portable relative working directory.",
                        outError);
                }
                const auto duplicate = std::find_if(
                    parsed.playConfigurations.begin(),
                    parsed.playConfigurations.end(),
                    [&](const PlayConfiguration& candidate) {
                        return candidate.id == configuration.id;
                    });
                if (duplicate !=
                    parsed.playConfigurations.end()) {
                    return fail(
                        "Duplicate play configuration id: " +
                            configuration.id,
                        outError);
                }

                if (playJson.contains("environment")) {
                    const auto& environmentJson =
                        playJson.at("environment");
                    if (!environmentJson.is_object()) {
                        return fail(
                            "Play configuration environment must be an object.",
                            outError);
                    }
                    for (auto it = environmentJson.begin();
                         it != environmentJson.end();
                         ++it) {
                        if (it.key().empty() ||
                            it.key().find('=') !=
                                std::string::npos ||
                            !it.value().is_string()) {
                            return fail(
                                "Play configuration environment names must be non-empty, exclude '=', and map to string values.",
                                outError);
                        }
                        configuration.environment.push_back(
                            PlayEnvironmentVariable{
                                .name = it.key(),
                                .value =
                                    it.value().get<std::string>()});
                    }
                }
                parsed.playConfigurations.push_back(
                    std::move(configuration));
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

bool resolveScenePath(
    const std::filesystem::path& descriptorPath,
    const ProjectDescriptor& descriptor,
    const ProjectScene& scene,
    std::filesystem::path& out,
    std::string* outError) {
    std::error_code error;
    const auto descriptorDirectory =
        std::filesystem::absolute(descriptorPath, error)
            .parent_path();
    if (error) {
        return fail(
            "Could not resolve project descriptor directory: " +
                error.message(),
            outError);
    }
    const auto mount = std::find_if(
        descriptor.contentMounts.begin(),
        descriptor.contentMounts.end(),
        [&](const ContentMount& candidate) {
            return candidate.id == scene.mountId;
        });
    if (mount == descriptor.contentMounts.end()) {
        return fail(
            "Scene references unknown mount: " +
                scene.mountId,
            outError);
    }
    out = (descriptorDirectory / mount->root /
           scene.path)
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

bool resolvePlayExecutablePath(
    const std::filesystem::path& descriptorPath,
    const PlayConfiguration& configuration,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError) {
    if (!isPortableRelativePath(configuration.executable)) {
        return fail(
            "Play configuration executable must be a portable relative path.",
            outError);
    }
    if (buildConfiguration.empty()) {
        return fail(
            "Editor build configuration must not be empty.",
            outError);
    }

    std::error_code error;
    const auto descriptorDirectory =
        std::filesystem::absolute(descriptorPath, error)
            .parent_path();
    if (error) {
        return fail(
            "Could not resolve project descriptor directory: " +
                error.message(),
            outError);
    }
    out = (
        descriptorDirectory /
        replaceConfigurationToken(
            configuration.executable.generic_string(),
            buildConfiguration))
              .lexically_normal();
    if (outError) {
        outError->clear();
    }
    return true;
}

bool resolvePlayWorkingDirectory(
    const std::filesystem::path& descriptorPath,
    const PlayConfiguration& configuration,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError) {
    if (!isPortableRelativePath(
            configuration.workingDirectory)) {
        return fail(
            "Play configuration working directory must be a portable relative path.",
            outError);
    }
    if (buildConfiguration.empty()) {
        return fail(
            "Editor build configuration must not be empty.",
            outError);
    }

    std::error_code error;
    const auto descriptorDirectory =
        std::filesystem::absolute(descriptorPath, error)
            .parent_path();
    if (error) {
        return fail(
            "Could not resolve project descriptor directory: " +
                error.message(),
            outError);
    }
    out = (
        descriptorDirectory /
        replaceConfigurationToken(
            configuration.workingDirectory.generic_string(),
            buildConfiguration))
              .lexically_normal();
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace engine::editor
