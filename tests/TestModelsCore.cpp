#include "engine/models/PhlosionModelObject.h"
#include "engine/models/ModelBillboards.h"
#include "engine/models/ModelTracks.h"
#include "engine/assets/phlosion/PhlosionCookedModelPackage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    namespace fs = std::filesystem;
    using namespace engine::models;
    const auto root = fs::temp_directory_path() / ("phlosion_models_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); }
    } cleanup{root};
    try {
        MeshData source;
        source.vertices.resize(3);
        source.vertices[1].position.x = 1;
        source.vertices[2].position.y = 1;
        source.indices = {0, 1, 2};
        source.boundsMax = {1, 1, 0};
        source.triangleSubmesh = {0};
        source.submeshIndexOffset = {0};
        source.submeshIndexCount = {3};
        source.submeshBaseColors = {{1, 1, 1, 1}};
        source.submeshMeshIndex = {0};
        source.nodesDefault.resize(1);
        source.nodeNames = {"Root"};
        source.nodeChildren.resize(1);
        source.nodeParent = {-1};
        source.nodeMesh = {0};
        source.nodeSkin = {-1};
        source.sceneRoots = {0};
        source.meshIndexToNode = {0};
        source.animations.resize(1);
        source.animations[0].name = "Pulse";
        source.animations[0].durationSec = 1;
        CachedTextureRgba pixel;
        pixel.width = pixel.height = 1;
        pixel.rgba = {30, 80, 140, 255};
        source.submeshBaseTextures = {pixel};
        pixel.rgba = {0, 0, 0, 255};
        source.submeshEmissionBaseTextures = {pixel};
        source.animationMeshVisibility = {{{0, 30, {0, 0.5f}, {1, 0}}}};
        ContinuousMaterialAnimationTrack emission;
        emission.parameter = MaterialAnimationParameter::EmissiveFactor;
        emission.durationSec = 1;
        emission.components[0].keys = {{0, 0}, {1, 1}};
        source.animationMaterialParameters = {{emission}};
        glm::vec4 sampled{};
        require(sampleClipBoundMaterialAnimation(source, 0, 0,
            MaterialAnimationParameter::EmissiveFactor, 0.5f, sampled) && sampled.x == 0.5f,
            "Fractional emission sample changed");
        MaterialAnimationCurve wrapped{{{0, 0.95f}, {1.0f / 60.0f, 0.05f}}};
        require(std::abs(sampleMaterialCurve(wrapped, 0.5f / 60.0f, 0, true, 60, false) - 1.0f) < 1e-5f,
            "Periodic texture offset crossed the atlas interior");
        require(sampleMaterialCurve(wrapped, 0.5f / 60.0f, 0, true, 60, true) == 0.95f,
            "Discrete texture offset was interpolated");
        AnimatedBillboard billboard;
        billboard.nodeIndex = 0;
        billboard.size = {0.2f, 0.3f};
        billboard.texture = pixel;
        billboard.clipLoops = {false};
        billboard.clipScales = {{{{0, 0}, {1, 1}}}};
        billboard.clipOffsets.resize(1);
        for (auto& axis : billboard.clipOffsets[0]) axis.keys = {{0, 0}, {1, 0.1f}};
        source.animatedBillboards = {billboard};
        ModelCookStats stats;
        std::string error;
        require(cookModelObject("fixtures/animated-lamp", source, root.string(),
            "Model", stats, &error), error);
        const auto object = objectPathForModel("fixtures/animated-lamp", root.string());
        MeshData loaded;
        require(loadModelObject(object, loaded, &error), error);
        require(loaded.vertices.size() == 3 && loaded.indices == source.indices &&
            loaded.vertices[2].position.y == 1, "Geometry changed in round trip");
        require(loaded.animations.size() == 1 && loaded.animations[0].name == "Pulse",
            "Animation changed in round trip");
        require(loaded.submeshBaseTextures[0].rgba == source.submeshBaseTextures[0].rgba &&
            loaded.submeshEmissionBaseTextures[0].rgba == source.submeshEmissionBaseTextures[0].rgba,
            "Texture roles changed in round trip");
        require(loaded.animationMeshVisibility[0][0].values == std::vector<std::uint8_t>{1, 0},
            "Visibility track changed in round trip");
        require(loaded.animationMaterialParameters[0][0].parameter == MaterialAnimationParameter::EmissiveFactor &&
            loaded.animationMaterialParameters[0][0].components[0].keys[1].value == 1,
            "Material track changed in round trip");
        require(loaded.animatedBillboards.size() == 1 &&
            loaded.animatedBillboards[0].clipOffsets[0][1].keys[1].value == 0.1f,
            "Attachment changed in round trip");
        engine::assets::phlosion::CookedModelPackage package;
        require(engine::assets::phlosion::loadCookedModelPackage(object, package, &error), error);
        require(package.vertexCount == 3 && package.animationCount == 1, "Graph validation disagrees");
        std::vector<ModelTextureDependency> dependencies;
        require(listModelObjectTextureDependencies(object, dependencies, &error) &&
            dependencies.size() == 2, "Texture dependencies were lost");
        const auto priorIdentity = loaded.assetCacheIdentity;
        const auto dependency = fs::path(dependencies[0].physicalPath);
        std::ofstream(dependency, std::ios::binary | std::ios::trunc) << "invalid";
        require(!loadModelObject(object, loaded, &error) && !error.empty(),
            "Corrupt dependency was accepted");
        require(loaded.assetCacheIdentity == priorIdentity && loaded.vertices.size() == 3,
            "Failed reload destroyed previously valid data");
        auto records = encodeModelBillboards(source);
        records[0]["node"] = 50;
        bool rejected = false;
        try { decodeModelBillboards(records, loaded); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "Invalid attachment node was accepted");
        std::cout << "Model CPU cook/load/graph/tracks/dependency/reload contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
