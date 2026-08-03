#include "engine/editor/ProjectDescriptor.h"

#include <algorithm>
#include <cctype>
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

bool isPackageId(std::string_view id) {
    if (id.empty() || id.front() == '.' || id.back() == '.') {
        return false;
    }
    bool segmentHasCharacter = false;
    for (const unsigned char character : id) {
        if (character == '.') {
            if (!segmentHasCharacter) {
                return false;
            }
            segmentHasCharacter = false;
            continue;
        }
        if (!(std::islower(character) || std::isdigit(character) ||
              character == '-')) {
            return false;
        }
        segmentHasCharacter = true;
    }
    return segmentHasCharacter;
}

bool isPackageVersion(std::string_view version) {
    // Package resolution currently accepts stable MAJOR.MINOR.PATCH versions.
    // Keeping this strict makes the lock/install format deterministic while a
    // future registry can add prerelease ranges explicitly.
    unsigned int component = 0u;
    bool hasDigit = false;
    for (const unsigned char character : version) {
        if (std::isdigit(character)) {
            hasDigit = true;
        } else if (character == '.' && hasDigit && component < 2u) {
            ++component;
            hasDigit = false;
        } else {
            return false;
        }
    }
    return component == 2u && hasDigit;
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
        if (parsed.schemaVersion != 2u) {
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

        parsed.startupSceneId =
            root.at("startup_scene")
                .at("scene_id")
                .get<std::string>();
        if (parsed.startupSceneId.empty()) {
            return fail(
                "Startup scene requires a scene id.",
                outError);
        }

        for (const auto& environmentJson :
             root.at("environments")) {
            ProjectEnvironment environment;
            environment.assetId =
                environmentJson.at("asset_id")
                    .get<std::string>();
            environment.displayName =
                environmentJson.value(
                    "display_name",
                    environment.assetId);
            environment.kind =
                environmentJson.value(
                    "kind",
                    std::string("cooked"));
            environment.mountId =
                environmentJson.value(
                    "mount",
                    std::string{});
            environment.path =
                environmentJson.value(
                    "path",
                    std::string{});
            const bool cooked =
                environment.kind == "cooked";
            const bool structural =
                environment.kind ==
                    "runtime_generated" ||
                environment.kind == "placeholder";
            if (environment.assetId.empty() ||
                environment.displayName.empty() ||
                (!cooked && !structural) ||
                (cooked &&
                 (environment.mountId.empty() ||
                  !isPortableRelativePath(
                      environment.path))) ||
                (structural &&
                 (!environment.mountId.empty() ||
                  !environment.path.empty()))) {
                return fail(
                    "Project environments require an id, display name, and supported kind. Cooked environments require a mount and path; runtime-generated and placeholder environments must not declare cooked backing.",
                    outError);
            }
            const auto duplicate = std::find_if(
                parsed.environments.begin(),
                parsed.environments.end(),
                [&](const ProjectEnvironment& candidate) {
                    return candidate.assetId ==
                           environment.assetId;
                });
            if (duplicate != parsed.environments.end()) {
                return fail(
                    "Duplicate project environment asset id: " +
                        environment.assetId,
                    outError);
            }
            parsed.environments.push_back(
                std::move(environment));
        }
        if (parsed.environments.empty()) {
            return fail(
                "At least one project environment is required.",
                outError);
        }

        for (const auto& sceneJson : root.at("scenes")) {
            ProjectScene scene;
            scene.sceneId =
                sceneJson.at("scene_id").get<std::string>();
            scene.displayName =
                sceneJson.at("display_name")
                    .get<std::string>();
            scene.category =
                sceneJson.value(
                    "category",
                    std::string{});
            scene.environmentAssetId =
                sceneJson.at("environment_asset_id")
                    .get<std::string>();
            scene.authoredScenePath =
                sceneJson.value(
                    "authored_scene_path",
                    std::string{});
            scene.runtimePath =
                sceneJson.value(
                    "runtime_path",
                    std::string{});
            scene.status =
                sceneJson.value(
                    "status",
                    std::string("in_progress"));
            if (scene.sceneId.empty() ||
                scene.displayName.empty() ||
                scene.environmentAssetId.empty() ||
                scene.status.empty() ||
                (!scene.authoredScenePath.empty() &&
                 !isPortableRelativePath(
                     scene.authoredScenePath)) ||
                (!scene.runtimePath.empty() &&
                 !isPortableRelativePath(
                     scene.runtimePath))) {
                return fail(
                    "Project scenes require a scene id, display name, environment asset id, status, and optional portable authored-scene and runtime paths.",
                    outError);
            }
            const auto environment = std::find_if(
                parsed.environments.begin(),
                parsed.environments.end(),
                [&](const ProjectEnvironment& candidate) {
                    return candidate.assetId ==
                           scene.environmentAssetId;
                });
            if (environment == parsed.environments.end()) {
                return fail(
                    "Project scene references unknown environment: " +
                        scene.environmentAssetId,
                    outError);
            }
            const auto duplicate = std::find_if(
                parsed.scenes.begin(),
                parsed.scenes.end(),
                [&](const ProjectScene& candidate) {
                    return candidate.sceneId ==
                           scene.sceneId;
                });
            if (duplicate != parsed.scenes.end()) {
                return fail(
                    "Duplicate project scene id: " +
                        scene.sceneId,
                    outError);
            }
            parsed.scenes.push_back(std::move(scene));
        }
        if (parsed.scenes.empty()) {
            return fail(
                "At least one project scene is required.",
                outError);
        }
        const auto startupScene = std::find_if(
            parsed.scenes.begin(),
            parsed.scenes.end(),
            [&](const ProjectScene& scene) {
                return scene.sceneId ==
                       parsed.startupSceneId;
            });
        if (startupScene == parsed.scenes.end()) {
            return fail(
                "The startup scene must appear in the project scene catalog.",
                outError);
        }
        const auto startupEnvironment = std::find_if(
            parsed.environments.begin(),
            parsed.environments.end(),
            [&](const ProjectEnvironment& environment) {
                return environment.assetId ==
                       startupScene->environmentAssetId;
            });
        if (startupEnvironment ==
                parsed.environments.end() ||
            startupEnvironment->kind != "cooked") {
            return fail(
                "The startup scene must reference a cooked environment so the editor can initialize its Scene view.",
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

        if (root.contains("editor_packages")) {
            const auto& packagesJson = root.at("editor_packages");
            if (!packagesJson.is_array()) {
                return fail(
                    "editor_packages must be an array.",
                    outError);
            }
            for (const auto& packageJson : packagesJson) {
                EditorPackageDependency package;
                package.id = packageJson.at("id").get<std::string>();
                package.version =
                    packageJson.at("version").get<std::string>();
                package.library =
                    packageJson.at("library").get<std::string>();
                package.directory =
                    packageJson.at("directory").get<std::string>();
                package.required =
                    packageJson.value("required", true);
                if (!isPackageId(package.id) ||
                    !isPackageVersion(package.version) ||
                    !isPortableLibraryName(package.library) ||
                    !isPortableRelativePath(package.directory)) {
                    return fail(
                        "Editor packages require a lowercase dotted id, MAJOR.MINOR.PATCH version, portable library name, and relative generated directory.",
                        outError);
                }
                const auto duplicate = std::find_if(
                    parsed.editorPackages.begin(),
                    parsed.editorPackages.end(),
                    [&](const EditorPackageDependency& candidate) {
                        return candidate.id == package.id;
                    });
                if (duplicate != parsed.editorPackages.end()) {
                    return fail(
                        "Duplicate editor package id: " + package.id,
                        outError);
                }
                parsed.editorPackages.push_back(std::move(package));
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
    const auto scene = std::find_if(
        descriptor.scenes.begin(),
        descriptor.scenes.end(),
        [&](const ProjectScene& candidate) {
            return candidate.sceneId ==
                   descriptor.startupSceneId;
        });
    if (scene == descriptor.scenes.end()) {
        return fail(
            "Startup scene is missing from the scene catalog: " +
                descriptor.startupSceneId,
            outError);
    }
    if (!resolveScenePath(
            descriptorPath,
            descriptor,
            *scene,
            out,
            outError)) {
        return false;
    }
    if (out.empty()) {
        return fail(
            "Startup scene does not have a cooked environment backing: " +
                descriptor.startupSceneId,
            outError);
    }
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
    out.clear();
    const auto environment = std::find_if(
        descriptor.environments.begin(),
        descriptor.environments.end(),
        [&](const ProjectEnvironment& candidate) {
            return candidate.assetId ==
                   scene.environmentAssetId;
        });
    if (environment == descriptor.environments.end()) {
        return fail(
            "Scene references unknown environment: " +
                scene.environmentAssetId,
            outError);
    }
    if (environment->kind != "cooked") {
        if (outError) {
            outError->clear();
        }
        return true;
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
    const auto mount = std::find_if(
        descriptor.contentMounts.begin(),
        descriptor.contentMounts.end(),
        [&](const ContentMount& candidate) {
            return candidate.id ==
                   environment->mountId;
        });
    if (mount == descriptor.contentMounts.end()) {
        return fail(
            "Environment references unknown mount: " +
                environment->mountId,
            outError);
    }
    out = (descriptorDirectory / mount->root /
           environment->path)
              .lexically_normal();
    if (outError) {
        outError->clear();
    }
    return true;
}

bool resolveAuthoredScenePath(
    const std::filesystem::path& descriptorPath,
    const ProjectScene& scene,
    std::filesystem::path& out,
    std::string* outError) {
    out.clear();
    if (scene.authoredScenePath.empty()) {
        if (outError) {
            outError->clear();
        }
        return true;
    }
    if (!isPortableRelativePath(scene.authoredScenePath)) {
        return fail(
            "Authored scene path must stay inside the project.",
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
    out = (descriptorDirectory / scene.authoredScenePath)
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

bool resolveEditorPackagePath(
    const std::filesystem::path& descriptorPath,
    const EditorPackageDependency& package,
    std::string_view buildConfiguration,
    std::filesystem::path& out,
    std::string* outError) {
    if (!isPackageId(package.id) ||
        !isPackageVersion(package.version) ||
        !isPortableLibraryName(package.library) ||
        !isPortableRelativePath(package.directory)) {
        return fail(
            "Editor package declaration is invalid: " + package.id,
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
    const std::filesystem::path packageDirectory =
        replaceConfigurationToken(
            package.directory.generic_string(),
            buildConfiguration);
#if defined(_WIN32)
    const std::string libraryFile = package.library + ".dll";
#elif defined(__APPLE__)
    const std::string libraryFile =
        "lib" + package.library + ".dylib";
#else
    const std::string libraryFile =
        "lib" + package.library + ".so";
#endif
    out = (descriptorDirectory / packageDirectory / libraryFile)
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
