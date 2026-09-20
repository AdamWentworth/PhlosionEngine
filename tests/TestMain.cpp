#include <iostream>
#include <string>

bool test_phlosion_resource_container_contract(std::string& outFail);
bool test_phlosion_mesh_asset_contract(std::string& outFail);
bool test_phlosion_authored_scene_contract(std::string& outFail);
bool test_phlosion_environment_patch_contract(std::string& outFail);
bool test_phlosion_scene_archive_contract(std::string& outFail);
bool test_async_asset_loader_contract(std::string& outFail);
bool test_phlosion_asset_graph_loader_contract(std::string& outFail);
bool test_phlosion_archive_session_contract(std::string& outFail);
bool test_public_asset_fixture_contract(std::string& outFail);
bool test_public_asset_fixture_rendering_contract(std::string& outFail);
bool test_project_descriptor_contract(std::string& outFail);
bool test_editor_renderer_preference_contract(std::string& outFail);
bool test_camera_view_plane_pan_contract(std::string& outFail);
bool test_editor_project_plugin_contract(std::string& outFail);
bool test_editor_game_preview_routing_contract(std::string& outFail);
bool test_editor_gameplay_reload(std::string& outFail);
bool test_editor_performance_contract(std::string& outFail);
bool test_world_material_profile_contract(std::string &outFail);

int main(int argc, char** argv) {
    // A real child process for reload launcher tests; avoids shell-specific
    // argument parsing and verifies ordinary executable argument boundaries.
    if (argc == 4 && std::string(argv[1]) == "--reload-child") {
        std::cout << argv[3] << '\n';
        return std::stoi(argv[2]);
    }
    struct TestCase {
        const char* name;
        bool (*run)(std::string&);
    };

    const TestCase tests[] = {
        {"world_material_profile_contract", &test_world_material_profile_contract},
        {"phlosion_resource_container_contract", &test_phlosion_resource_container_contract},
        {"phlosion_mesh_asset_contract", &test_phlosion_mesh_asset_contract},
        {"phlosion_authored_scene_contract", &test_phlosion_authored_scene_contract},
        {"phlosion_environment_patch_contract", &test_phlosion_environment_patch_contract},
        {"phlosion_scene_archive_contract", &test_phlosion_scene_archive_contract},
        {"async_asset_loader_contract", &test_async_asset_loader_contract},
        {"phlosion_asset_graph_loader_contract", &test_phlosion_asset_graph_loader_contract},
        {"phlosion_archive_session_contract", &test_phlosion_archive_session_contract},
        {"public_asset_fixture_contract", &test_public_asset_fixture_contract},
        {"public_asset_fixture_rendering_contract",
         &test_public_asset_fixture_rendering_contract},
        {"project_descriptor_contract", &test_project_descriptor_contract},
        {"editor_renderer_preference_contract", &test_editor_renderer_preference_contract},
        {"camera_view_plane_pan_contract", &test_camera_view_plane_pan_contract},
        {"editor_project_plugin_contract", &test_editor_project_plugin_contract},
        {"editor_game_preview_routing_contract", &test_editor_game_preview_routing_contract},
        {"editor_gameplay_reload", &test_editor_gameplay_reload},
        {"editor_performance_contract", &test_editor_performance_contract},
    };

    bool passed = true;
    for (const TestCase& test : tests) {
        std::string failure;
        if (test.run(failure)) {
            std::cout << "[PASS] " << test.name << '\n';
            continue;
        }
        passed = false;
        std::cerr << "[FAIL] " << test.name << ": " << failure << '\n';
    }
    return passed ? 0 : 1;
}
