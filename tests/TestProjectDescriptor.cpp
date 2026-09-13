#include "engine/editor/ProjectDescriptor.h"

#include <filesystem>
#include <string>

bool test_project_descriptor_contract(std::string& outFail) {
    constexpr char kProject[] = R"({
        "schema_version": 2,
        "project_id": "test-game",
        "display_name": "Test Game",
        "content_mounts": [
            {
                "id": "cooked",
                "root": "content/phlosion",
                "asset_browser_exclude": [
                    "objects/internal-*/*.phlo"
                ],
                "required": true
            }
        ],
        "startup_scene": {
            "scene_id": "routes/test"
        },
        "environments": [
            {
                "asset_id": "environments/test",
                "display_name": "Test Environment",
                "kind": "cooked",
                "mount": "cooked",
                "path": "scenes/test.phscene"
            },
            {
                "asset_id": "environments/procedural",
                "display_name": "Procedural Environment",
                "kind": "runtime_generated"
            }
        ],
        "scenes": [
            {
                "scene_id": "routes/test",
                "display_name": "Test Route",
                "category": "Routes",
                "environment_asset_id": "environments/test",
                "authored_scene_path": "scenes/test.scene.json",
                "status": "implemented"
            },
            {
                "scene_id": "routes/test-variant",
                "display_name": "Test Route Variant",
                "category": "Routes",
                "environment_asset_id": "environments/test",
                "runtime_path": "scripts/states/test_variant.lua",
                "status": "in_progress"
            },
            {
                "scene_id": "routes/procedural",
                "display_name": "Procedural Route",
                "category": "Routes",
                "environment_asset_id": "environments/procedural",
                "runtime_path": "scripts/states/procedural.lua",
                "status": "in_progress"
            }
        ],
        "editor_plugin": {
            "library": "TestGameEditorProject",
            "directory": ".phlosion/editor/{config}"
        },
        "editor_packages": [
            {
                "id": "phlosion.tile-tools",
                "version": "0.1.0",
                "library": "PhlosionTileTools",
                "directory": ".phlosion/packages/{config}/phlosion.tile-tools"
            }
        ],
        "play_configurations": [
            {
                "id": "main-menu",
                "display_name": "Main Menu",
                "group": "Frontend",
                "description": "Run the real game frontend.",
                "executable": "build/{config}/TestGame.exe",
                "working_directory": ".",
                "build_directory": "build",
                "build_target": "TestGame",
                "arguments": ["--editor-preview"],
                "environment": {
                    "TEST_GAME_VIEW": "main-menu"
                }
            }
        ],
        "private_asset_depot": {
            "environment": "PHLOSION_ASSET_DEPOT"
        }
    })";

    engine::editor::ProjectDescriptor project;
    std::string error;
    if (!engine::editor::parseProjectDescriptor(kProject, project, &error)) {
        outFail = "valid descriptor failed: " + error;
        return false;
    }
    if (project.schemaVersion != 2u ||
        project.projectId != "test-game" ||
        project.displayName != "Test Game" ||
        project.contentMounts.size() != 1u ||
        project.contentMounts.front().id != "cooked" ||
        project.contentMounts.front().
                assetBrowserExcludePatterns.size() !=
            1u ||
        project.contentMounts.front().
                assetBrowserExcludePatterns.front() !=
            "objects/internal-*/*.phlo" ||
        project.startupSceneId != "routes/test" ||
        project.environments.size() != 2u ||
        project.scenes.size() != 3u ||
        project.scenes.front().displayName != "Test Route" ||
        project.scenes.front().authoredScenePath !=
            "scenes/test.scene.json" ||
        project.scenes[0].environmentAssetId !=
            project.scenes[1].environmentAssetId ||
        project.playConfigurations.size() != 1u ||
        project.editorPackages.size() != 1u ||
        project.editorPackages.front().id !=
            "phlosion.tile-tools" ||
        project.editorPackages.front().version != "0.1.0" ||
        project.playConfigurations.front().id != "main-menu" ||
        project.playConfigurations.front().arguments.size() != 1u ||
        project.playConfigurations.front().buildDirectory != "build" ||
        project.playConfigurations.front().buildTarget != "TestGame" ||
        project.playConfigurations.front().environment.size() != 1u ||
        project.privateAssetDepotEnvironment !=
            "PHLOSION_ASSET_DEPOT") {
        outFail = "descriptor fields changed during parsing";
        return false;
    }

    std::filesystem::path authoredScenePath;
    if (!engine::editor::resolveAuthoredScenePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project.scenes.front(),
            authoredScenePath,
            &error) ||
        authoredScenePath.generic_string() !=
            "D:/Projects/TestGame/scenes/test.scene.json") {
        outFail =
            "authored scene path resolution failed: " + error +
            " path=" + authoredScenePath.generic_string();
        return false;
    }

    std::filesystem::path catalogScenePath;
    if (!engine::editor::resolveScenePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project,
            project.scenes.front(),
            catalogScenePath,
            &error) ||
        catalogScenePath.generic_string() !=
            "D:/Projects/TestGame/content/phlosion/scenes/test.phscene") {
        outFail =
            "scene catalog path resolution failed: " + error +
            " path=" + catalogScenePath.generic_string();
        return false;
    }

    std::filesystem::path sharedScenePath;
    if (!engine::editor::resolveScenePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project,
            project.scenes[1],
            sharedScenePath,
            &error) ||
        sharedScenePath != catalogScenePath) {
        outFail =
            "shared environment path resolution failed: " + error +
            " path=" + sharedScenePath.generic_string();
        return false;
    }

    std::filesystem::path proceduralScenePath;
    if (!engine::editor::resolveScenePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project,
            project.scenes[2],
            proceduralScenePath,
            &error) ||
        !proceduralScenePath.empty()) {
        outFail =
            "runtime-generated environment should not resolve a cooked path: " +
            error;
        return false;
    }

    std::filesystem::path scenePath;
    if (!engine::editor::resolveStartupScenePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project,
            scenePath,
            &error) ||
        scenePath.generic_string() !=
            "D:/Projects/TestGame/content/phlosion/scenes/test.phscene") {
        outFail = "startup scene resolution failed: " + error +
                  " path=" + scenePath.generic_string();
        return false;
    }

    std::filesystem::path pluginPath;
    if (!engine::editor::resolveEditorPluginPath(
            "D:/Projects/TestGame/phlosion.project.json",
            project,
            "Debug",
            pluginPath,
            &error)) {
        outFail = "editor plugin path resolution failed: " + error;
        return false;
    }
#if defined(_WIN32)
    const std::string expectedPluginPath =
        "D:/Projects/TestGame/.phlosion/editor/Debug/TestGameEditorProject.dll";
#elif defined(__APPLE__)
    const std::string expectedPluginPath =
        "D:/Projects/TestGame/.phlosion/editor/Debug/libTestGameEditorProject.dylib";
#else
    const std::string expectedPluginPath =
        "D:/Projects/TestGame/.phlosion/editor/Debug/libTestGameEditorProject.so";
#endif
    if (pluginPath.generic_string() != expectedPluginPath) {
        outFail =
            "unexpected editor plugin path: " +
            pluginPath.generic_string();
        return false;
    }

    std::filesystem::path packagePath;
    if (!engine::editor::resolveEditorPackagePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project.editorPackages.front(),
            "Debug",
            packagePath,
            &error)) {
        outFail = "editor package path resolution failed: " + error;
        return false;
    }
#if defined(_WIN32)
    const std::string expectedPackagePath =
        "D:/Projects/TestGame/.phlosion/packages/Debug/phlosion.tile-tools/PhlosionTileTools.dll";
#elif defined(__APPLE__)
    const std::string expectedPackagePath =
        "D:/Projects/TestGame/.phlosion/packages/Debug/phlosion.tile-tools/libPhlosionTileTools.dylib";
#else
    const std::string expectedPackagePath =
        "D:/Projects/TestGame/.phlosion/packages/Debug/phlosion.tile-tools/libPhlosionTileTools.so";
#endif
    if (packagePath.generic_string() != expectedPackagePath) {
        outFail =
            "unexpected editor package path: " +
            packagePath.generic_string();
        return false;
    }

    std::filesystem::path executablePath;
    if (!engine::editor::resolvePlayExecutablePath(
            "D:/Projects/TestGame/phlosion.project.json",
            project.playConfigurations.front(),
            "Debug",
            executablePath,
            &error) ||
        executablePath.generic_string() !=
            "D:/Projects/TestGame/build/Debug/TestGame.exe") {
        outFail =
            "play executable resolution failed: " + error +
            " path=" + executablePath.generic_string();
        return false;
    }

    std::filesystem::path workingDirectory;
    if (!engine::editor::resolvePlayWorkingDirectory(
            "D:/Projects/TestGame/phlosion.project.json",
            project.playConfigurations.front(),
            "Debug",
            workingDirectory,
            &error) ||
        workingDirectory.generic_string() !=
            "D:/Projects/TestGame/") {
        outFail =
            "play working directory resolution failed: " + error +
            " path=" + workingDirectory.generic_string();
        return false;
    }

    constexpr char kTraversal[] = R"({
        "schema_version": 2,
        "project_id": "bad",
        "display_name": "Bad",
        "content_mounts": [
            {"id": "cooked", "root": "../outside"}
        ],
        "startup_scene": {
            "scene_id": "bad"
        },
        "environments": [],
        "scenes": []
    })";
    if (engine::editor::parseProjectDescriptor(
            kTraversal,
            project,
            nullptr)) {
        outFail = "descriptor accepted a parent-directory content mount";
        return false;
    }
    std::string invalidPackageVersion(kProject);
    const std::string validVersion =
        "\"version\": \"0.1.0\"";
    const auto packageVersionOffset =
        invalidPackageVersion.find(validVersion);
    if (packageVersionOffset == std::string::npos) {
        outFail = "test descriptor lost its package version";
        return false;
    }
    invalidPackageVersion.replace(
        packageVersionOffset,
        validVersion.size(),
        "\"version\": \"^0.1\"");
    if (engine::editor::parseProjectDescriptor(
            invalidPackageVersion,
            project,
            nullptr)) {
        outFail = "descriptor accepted a non-locked package version";
        return false;
    }
    for (const auto& replacement : {std::string("\"build_directory\": \"../outside\""), std::string("\"build_directory\": \"\" ")}) {
        std::string invalidBuild(kProject);
        const std::string original = "\"build_directory\": \"build\"";
        invalidBuild.replace(invalidBuild.find(original), original.size(), replacement);
        if (engine::editor::parseProjectDescriptor(invalidBuild, project, nullptr)) {
            outFail = "Standalone build accepted an escaping path or missing build directory.";
            return false;
        }
    }
    std::string projectWithoutPackages(kProject);
    const auto packageBlockStart =
        projectWithoutPackages.find("        \"editor_packages\": [");
    const auto packageBlockEnd =
        projectWithoutPackages.find(
            "        \"play_configurations\": [",
            packageBlockStart);
    if (packageBlockStart == std::string::npos ||
        packageBlockEnd == std::string::npos) {
        outFail = "test descriptor lost its optional package block";
        return false;
    }
    projectWithoutPackages.erase(
        packageBlockStart,
        packageBlockEnd - packageBlockStart);
    if (!engine::editor::parseProjectDescriptor(
            projectWithoutPackages,
            project,
            &error) ||
        !project.editorPackages.empty()) {
        outFail =
            "descriptor without editor packages must remain valid: " +
            error;
        return false;
    }
    return true;
}
