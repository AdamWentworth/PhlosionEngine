#include "engine/render/WorldMaterialProfile.h"
#include "engine/render/WorldSurfaceComposition.h"
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
    json["opengl_vertex"] = json["opengl"];
    json["d3d12_vertex"] = json["d3d12"];
    json["vulkan"]["direct_vertex"] = "fragment.spv";
    json["vulkan"]["indirect_vertex"] = "fragment.spv";
    json["modes"]["129"] = {{"pbr_packing", false}, {"opengl_debug_parameter", false}, {"d3d12_debug_parameter", false}, {"occlusion_maximum", 2.0f}};
    json["d3d12_constant_overrides"]["129"]["projectedShadowRowX.0"] = {
        {"terms", {{{"source", "cameraPosX"}, {"minimum", 0.0f}, {"maximum", 3.0f}, {"bias", 1.0f}, {"scale", 10.0f}, {"round", true}}, {{"constant", "materialFlags"}}, 0.25f}},
        {"when", {{"source", "materialFlags"}, {"greater_than", 4.5f}, {"less_than", 5.5f}}}};
    const auto write = [&] { std::ofstream(root / "profile.json") << json.dump(); };
    write();
    const auto profile = loadWorldMaterialProfile(root, "profile.json");
    {
        json["modes"]["238"]={{"shared_local_lighting",true},{"foliage_composition",true}};write();
        const auto shared=loadWorldMaterialProfile(root,"profile.json");
        if(!shared.modes[238].sharedLocalLighting || shared.modes[128].sharedLocalLighting ||
           !shared.modes[238].foliageComposition || shared.modes[128].foliageComposition)
            throw std::runtime_error("Shared local-light enrollment/default contract");
        json["modes"]["238"]["shared_local_lighting"]="true";write();
        if(!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
            throw std::runtime_error("Non-boolean shared-light enrollment accepted");
        json["modes"]["238"]["shared_local_lighting"]=true;
        json["modes"]["238"]["foliage_composition"]="true";write();
        if(!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
            throw std::runtime_error("Non-boolean foliage composition accepted");
        json["modes"].erase("238");write();
    }

    {
        IRenderBackend::WorldTextureData leaf{};
        leaf.materialMode=238;leaf.environment.enabled=true;leaf.alphaMode=1;
        leaf.cameraPosX=0;leaf.cameraPosY=0;leaf.cameraPosZ=0;
        leaf.cameraForwardX=0;leaf.cameraForwardY=0;leaf.cameraForwardZ=-1;
        WorldClusteredLightingView view{};
        view.camera={0,0,0};view.forward={0,0,-1};view.environment=leaf.environment;
        if(worldSurfaceCompositionMaterial(&leaf,view) || !worldSurfaceCompositionMaterial(&leaf,view,true))
            throw std::runtime_error("Leaf composition requires material enrollment");
        view.environment.reflectionResponseWeight=1;
        view.environment.reflectionResponse={1.2f,1.3f,0,1};
        if(!worldSurfaceCompositionMaterial(&leaf,view,true))
            throw std::runtime_error("Unmodified leaf reflection lobe must not inherit surface tuning");
        leaf.environment.reflectionResponseWeight=1;
        if(worldSurfaceCompositionMaterial(&leaf,view,true))
            throw std::runtime_error("Different active leaf reflection response was accepted");
        leaf.environment.reflectionResponseWeight=0;
        view.camera[0]=1;
        if(worldSurfaceCompositionMaterial(&leaf,view,true))
            throw std::runtime_error("Leaf composition accepted another camera");
        view.camera[0]=0;leaf.alphaMode=2;
        if(worldSurfaceCompositionMaterial(&leaf,view,true))
            throw std::runtime_error("Transparent leaf composition accepted");
    }
    {
        auto prepared=profile;
        prepared.d3d12PipelineWarmup={{2,16},{254,64},{2,1024},{2,1280}};
        prepared.validate();
        prepared.d3d12PipelineWarmup.push_back({256,0});
        if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Out-of-range warmup mode accepted");
        prepared.d3d12PipelineWarmup={{2,4096}};
        if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Out-of-range warmup variant accepted");
        prepared.d3d12PipelineWarmup.assign(257,{2,0});
        if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Unbounded warmup plan accepted");
    }

    {
        auto prepared=profile;prepared.vulkanPipelineWarmup={{241,12},{244,13},{2,0}};prepared.validate();
        for(auto bad:std::vector<WorldPipelineWarmup>{{256,12},{241,14},{241,8}}) {
            prepared.vulkanPipelineWarmup={bad};
            if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Invalid Vulkan specialization accepted");
        }
        prepared.vulkanPipelineWarmup={{241,12},{241,12}};
        if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Duplicate Vulkan specialization accepted");
        prepared.vulkanPipelineWarmup.assign(257,{241,12});
        if(!rejects([&]{prepared.validate();}))throw std::runtime_error("Unbounded Vulkan specialization accepted");
        auto unnamed=WorldMaterialProfile{};unnamed.vulkanPipelineWarmup={{241,12}};
        if(!rejects([&]{unnamed.validate();}))throw std::runtime_error("Unnamed Vulkan specialization accepted");
        json["vulkan_pipeline_warmup"]={{{"mode",241u},{"variant",12u}}};write();
        if(loadWorldMaterialProfile(root,"profile.json").vulkanPipelineWarmup!=std::vector<WorldPipelineWarmup>{{241,12}})
            throw std::runtime_error("Vulkan specialization plan not loaded");
        for(auto invalid:nlohmann::json::array({
            {{{"mode",4294967296ull},{"variant",12u}}},
            {{{"mode",241u},{"variant",4294967296ull}}},
            {{{"mode",-1},{"variant",12u}}}})) {
            json["vulkan_pipeline_warmup"]=invalid;write();
            if(!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
                throw std::runtime_error("Invalid JSON Vulkan specialization accepted");
        }
        json.erase("vulkan_pipeline_warmup");write();
    }
    {
        json["version"] = 2; write();
        if (loadWorldMaterialProfile(root,"profile.json") != profile)
            throw std::runtime_error("Version 2 base-profile compatibility changed");
        nlohmann::json family{
            {"id","transparent"}, {"modes",{241u,242u}},
            {"d3d12",json["d3d12"]}, {"d3d12_vertex",json["d3d12_vertex"]},
            {"vulkan",{{"direct","fragment.spv"},{"direct_dual_source","fragment.spv"},{"direct_vertex","fragment.spv"}}}};
        json["transparent_families"] = nlohmann::json::array({family}); write();
        if (!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
            throw std::runtime_error("Version 2 accepted unsupported family schema");
        json["version"] = kWorldMaterialProfileVersion; write();
        const auto loaded = loadWorldMaterialProfile(root,"profile.json");
        if (loaded.transparentFamilies.size()!=1 || loaded.transparentFamilyIndex(241)!=0 ||
                loaded.transparentFamilyIndex(242)!=0 || loaded.transparentFamilyIndex(240)!=-1 ||
                loaded.transparentFamilyIndex(256)!=-1 || loaded==profile)
            throw std::runtime_error("Family ownership/loading/equality failed");
        auto changed=loaded;
        changed.transparentFamilies[0].d3d12.evaluation+="changed";
        if(changed==loaded || changed.d3d12!=loaded.d3d12 || changed.vulkan!=loaded.vulkan)
            throw std::runtime_error("Family edit altered the base program");
        for (auto modes:nlohmann::json::array({nlohmann::json::array(),{241u,241u},{256u},{-1},{4294967296ull}})) {
            json["transparent_families"][0]["modes"]=modes; write();
            if(!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
                throw std::runtime_error("Invalid family mode set accepted");
        }
        for (const auto section:{"d3d12","d3d12_vertex","vulkan"}) {
            json["transparent_families"]=nlohmann::json::array({family});
            const auto field=std::string(section)=="vulkan"?"direct_vertex":"evaluation";
            json["transparent_families"][0][section][field]="../escape"; write();
            if(!rejects([&]{loadWorldMaterialProfile(root,"profile.json");}))
                throw std::runtime_error("Family source escaped the project");
        }
        for (auto mode : {239u,246u,249u,255u}) {
            auto reserved=loaded; reserved.transparentFamilies[0].modes.set(mode);
            if(!rejects([&]{reserved.validate();}))throw std::runtime_error("Engine-owned family mode accepted");
        }
        auto bad=loaded;
        for (std::size_t stage=0;stage<3;++stage) {
            bad=loaded; bad.transparentFamilies[0].vulkan[stage].clear();
            if(!rejects([&]{bad.validate();}))throw std::runtime_error("Incomplete family program accepted");
        }
        bad=loaded; bad.transparentFamilies.push_back(bad.transparentFamilies[0]);
        if(!rejects([&]{bad.validate();}))throw std::runtime_error("Duplicate family accepted");
        bad.transparentFamilies[1].id="other";
        if(!rejects([&]{bad.validate();}))throw std::runtime_error("Overlapping family modes accepted");
        bad.transparentFamilies[1].modes.reset();bad.transparentFamilies[1].modes.set(243);bad.validate();
        bad=loaded;bad.d3d12PipelineWarmup={{241,0}};
        if(!rejects([&]{bad.validate();}))throw std::runtime_error("Opaque family warmup accepted");
        bad.d3d12PipelineWarmup={{241,65}};
        if(!rejects([&]{bad.validate();}))throw std::runtime_error("Surface family warmup accepted");
        bad.d3d12PipelineWarmup={{241,27}};bad.vulkanPipelineWarmup={{241,0}};
        if(!rejects([&]{bad.validate();}))throw std::runtime_error("Vulkan opaque family warmup accepted");
        bad.vulkanPipelineWarmup={{241,12}};bad.validate();
        json.erase("transparent_families");write();
    }
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
    texture.materialMode = 129;
    texture.materialFlags = 5.0f;
    texture.cameraPosX = 9.0f;
    const auto packed = d3d12_internal::makeWorldPsConstants(&texture, 1.0f, false, &profile);
    texture.materialFlags = 4.5f;
    const auto unselected = d3d12_internal::makeWorldPsConstants(&texture, 1.0f, false, &profile);
    if (packed.projectedShadowRowX[0] != 45.25f || unselected.projectedShadowRowX[0] == 45.25f ||
        profile.modes[129].pbrPacking || profile.modes[129].openglDebugParameter ||
        profile.modes[129].d3d12DebugParameter || profile.modes[129].occlusionMaximum != 2.0f ||
        profile.modes[128] != WorldMaterialModeOptions{}) {
        outFail = "Conditional scalar packing or isolated per-mode settings failed.";
        return false;
    }
    auto shadowProfile = profile;
    shadowProfile.vulkan[6] = {1,2,3};
    if (!rejects([&] { shadowProfile.validate(); })) {
        outFail = "Malformed optional directional-shadow shader was accepted.";
        return false;
    }
    shadowProfile.vulkan[6].assign(words.begin(),words.end());
    shadowProfile.validate();
    auto surfaceProfile = profile;
    surfaceProfile.vulkan[7] = {1, 2, 3};
    if (!rejects([&] { surfaceProfile.validate(); })) {
        outFail = "Malformed optional surface-data shader was accepted.";
        return false;
    }
    surfaceProfile.vulkan[7].assign(words.begin(), words.end());
    surfaceProfile.validate();
    json["vulkan"]["surface"] = "fragment.spv";
    write();
    if (loadWorldMaterialProfile(root, "profile.json").vulkan[7] != surfaceProfile.vulkan[7]) {
        outFail = "Surface-data shader was not retained by the material profile.";
        return false;
    }
    json["vulkan"]["surface"] = "../outside.spv";
    write();
    if (!rejects([&] { loadWorldMaterialProfile(root, "profile.json"); })) {
        outFail = "Escaping optional surface shader was accepted.";
        return false;
    }
    json["vulkan"].erase("surface");
    write();
    auto incomplete = profile;
    incomplete.vulkan[2].clear();
    if (!rejects([&] { incomplete.validate(); }) ||
        !rejects([&] { loadWorldMaterialProfile(root, "../outside.json"); })) {
        outFail = "Incomplete or escaping material profiles were accepted.";
        return false;
    }
    json["d3d12_pipeline_warmup"] = {{{"mode",4294967296ull},{"variant",0u}}};
    write();
    if (!rejects([&] { loadWorldMaterialProfile(root, "profile.json"); })) {
        outFail = "Overflowing warmup mode wrapped into a valid pipeline.";
        return false;
    }
    json["d3d12_pipeline_warmup"] = {{{"mode",2u},{"variant",4294967296ull}}};
    write();
    if (!rejects([&] { loadWorldMaterialProfile(root, "profile.json"); })) {
        outFail = "Overflowing warmup variant wrapped into a valid pipeline.";
        return false;
    }
    json.erase("d3d12_pipeline_warmup");
    write();
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
