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
        project.privateAssetDepotEnvironment !=
            "PHLOSION_ASSET_DEPOT") {
        outFail = "descriptor fields changed during parsing";
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
