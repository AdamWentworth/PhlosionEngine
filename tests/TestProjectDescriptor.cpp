#include "engine/editor/ProjectDescriptor.h"

#include <filesystem>
#include <string>

bool test_project_descriptor_contract(std::string& outFail) {
    constexpr char kProject[] = R"({
        "schema_version": 1,
        "project_id": "test-game",
        "display_name": "Test Game",
        "content_mounts": [
            {
                "id": "cooked",
                "root": "content/phlosion",
                "required": true
            }
        ],
        "startup_scene": {
            "asset_id": "environments/test",
            "mount": "cooked",
            "path": "scenes/test.phscene"
        },
        "scenes": [
            {
                "asset_id": "environments/test",
                "display_name": "Test World",
                "category": "Worlds",
                "mount": "cooked",
                "path": "scenes/test.phscene"
            }
        ],
        "editor_plugin": {
            "library": "TestGameEditorProject",
            "directory": ".phlosion/editor/{config}"
        },
        "play_configurations": [
            {
                "id": "main-menu",
                "display_name": "Main Menu",
                "group": "Frontend",
                "description": "Run the real game frontend.",
                "executable": "build/{config}/TestGame.exe",
                "working_directory": ".",
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
    if (project.schemaVersion != 1u ||
        project.projectId != "test-game" ||
        project.displayName != "Test Game" ||
        project.contentMounts.size() != 1u ||
        project.contentMounts.front().id != "cooked" ||
        project.startupScene.assetId != "environments/test" ||
        project.scenes.size() != 1u ||
        project.scenes.front().displayName != "Test World" ||
        project.playConfigurations.size() != 1u ||
        project.playConfigurations.front().id != "main-menu" ||
        project.playConfigurations.front().arguments.size() != 1u ||
        project.playConfigurations.front().environment.size() != 1u ||
        project.privateAssetDepotEnvironment !=
            "PHLOSION_ASSET_DEPOT") {
        outFail = "descriptor fields changed during parsing";
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
        "schema_version": 1,
        "project_id": "bad",
        "display_name": "Bad",
        "content_mounts": [
            {"id": "cooked", "root": "../outside"}
        ],
        "startup_scene": {
            "asset_id": "bad",
            "mount": "cooked",
            "path": "bad.phscene"
        }
    })";
    if (engine::editor::parseProjectDescriptor(
            kTraversal,
            project,
            nullptr)) {
        outFail = "descriptor accepted a parent-directory content mount";
        return false;
    }
    return true;
}
