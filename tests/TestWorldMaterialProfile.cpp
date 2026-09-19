#include "engine/render/WorldMaterialProfile.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <fstream>
#include <nlohmann/json.hpp>

bool test_world_material_profile_contract(std::string &outFail) {
    using namespace engine::render;
    const auto rejects = [](auto action) {
        try {
            action();
        } catch (const std::exception &) {
            return true;
        }
        return false;
    };
    const std::string shader = "begin\n__PHLOSION_PROJECT_MATERIAL_DECLARATIONS__\nmain {\n__PHLOSION_PROJECT_MATERIAL_EVALUATION__\n}";
    if (injectWorldMaterialProfile(shader, {}).find("PHLOSION_PROJECT") != std::string::npos ||
        !rejects([&] { injectWorldMaterialProfile("incompatible", {}); })) {
        outFail = "Default profile or shader interface validation failed.";
        return false;
    }

    const auto root = std::filesystem::temp_directory_path() / "phlosion-material-profile-contract";
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    std::ofstream(root / "declarations.glsl") << "float customValue() { return 0.4; }";
    std::ofstream(root / "evaluation.glsl") << "customValue();";
    // Header-only fixture exercises loading/validation; GPU compilation is a
    // separate qualification of real project shaders on each native backend.
    const std::array<std::uint32_t, 5> words{0x07230203u, 0x00010300u, 0u, 1u, 0u};
    std::ofstream binary(root / "fragment.spv", std::ios::binary);
    binary.write(reinterpret_cast<const char *>(words.data()), sizeof(words));
    binary.close();
    nlohmann::json json{
        {"version", kWorldMaterialProfileVersion}, {"id", "test.surface"}, {"opengl", {{"declarations", "declarations.glsl"}, {"evaluation", "evaluation.glsl"}}}, {"d3d12", {{"declarations", "declarations.glsl"}, {"evaluation", "evaluation.glsl"}}}, {"vulkan", {{"direct", "fragment.spv"}, {"direct_dual_source", "fragment.spv"}, {"indirect", "fragment.spv"}, {"indirect_dual_source", "fragment.spv"}}}, {"d3d12_constant_overrides", {{"128", {{"materialTimeSec", "cameraPosX"}, {"materialFlags", 0.75f}}}}}};
    const auto write = [&] { std::ofstream(root / "profile.json") << json.dump(); };
    write();
    const auto profile = loadWorldMaterialProfile(root, "profile.json");
    auto independent = profile;
    independent.opengl.declarations = "different";
    if (profile.id != "test.surface" || profile == independent ||
        injectWorldMaterialProfile(shader, profile.opengl).find("customValue") == std::string::npos) {
        outFail = "Material source ownership or composition failed.";
        return false;
    }
    IRenderBackend::WorldTextureData texture;
    texture.materialMode = 128;
    texture.cameraPosX = 2.25f;
    const auto before = d3d12_internal::makeWorldPsConstants(&texture, 1.0f);
    const auto selected = d3d12_internal::makeWorldPsConstants(&texture, 1.0f, false, &profile);
    texture.cameraPosX = 3.5f;
    const auto changed = d3d12_internal::makeWorldPsConstants(&texture, 1.0f, false, &profile);
    const auto after = d3d12_internal::makeWorldPsConstants(&texture, 1.0f);
    if (selected.materialTimeSec != 2.25f || selected.materialFlags != 0.75f ||
        changed.materialTimeSec != 3.5f || before.materialTimeSec != after.materialTimeSec) {
        outFail = "Project mappings must use current draw data without changing engine defaults.";
        return false;
    }
    auto incomplete = profile;
    incomplete.vulkan[2].clear();
    if (!rejects([&] { incomplete.validate(); }) ||
        !rejects([&] { loadWorldMaterialProfile(root, "../outside.json"); })) {
        outFail = "Incomplete or escaping material profiles were accepted.";
        return false;
    }
    json["version"] = kWorldMaterialProfileVersion + 1;
    write();
    if (!rejects([&] { loadWorldMaterialProfile(root, "profile.json"); })) {
        outFail = "Incompatible material shader interface was accepted.";
        return false;
    }
    json["version"] = kWorldMaterialProfileVersion;
    json["d3d12_constant_overrides"]["128"]["unknownConstant"] = 1;
    write();
    if (!rejects([&] { loadWorldMaterialProfile(root, "profile.json"); })) {
        outFail = "Unknown material constant was accepted.";
        return false;
    }
    return true;
}
