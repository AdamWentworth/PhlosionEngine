#include "engine/vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxBridge.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) try {
    using namespace vfx::runtime::authored;
    check(argc == 2, "Expected a fixture output directory");
    SharedAuthoredBatchVFX::Config config;
    check(config.drawPasses.empty() && config.drawManifestPath.empty() &&
          config.vertShaderPath.empty() && config.fragShaderPath.empty(),
          "Default effects must not select project content");
    SharedAuthoredBatchVFX::Config::DrawPass pass;
    check(pass.meshPath.empty() && pass.texturePath.empty(), "Default passes must not load assets");
    pass.fragShaderPath = "arbitrary/material.frag";
    check(!isLinePass(config, pass) && !usesQuarterTextureBake(config, pass),
          "Asset names must not select material behavior");
    pass.linePass = true;
    check(isLinePass(config, pass), "Explicit line material was ignored");
    pass.linePass = false;
    pass.quarterTextureBake = true;
    check(usesQuarterTextureBake(config, pass), "Explicit texture bake was ignored");
    pass.renderMode = "glow_billboard";
    pass.tintColor = glm::vec3(1.0f);
    TevState tev;
    tev.c0 = glm::vec3(1.0f);
    tev.c1 = glm::vec3(0.0f);
    tev.k0 = glm::vec3(1.0f);
    tev.c0a = 0.5f;
    tev.c1a = 0.0f;
    tev.k1a = 0.5f;
    const std::vector<unsigned char> input{128, 64, 32, 128};
    std::vector<unsigned char> preserved, modulated;
    check(bakePassTextureRgba(pass, tev, true, input, preserved), "Texture bake failed");
    const auto preservedKey = makeBakedTextureKey(pass, true);
    pass.preserveGlowAlpha = false;
    check(makeBakedTextureKey(pass, true) != preservedKey, "Alpha policies must not share baked textures");
    check(bakePassTextureRgba(pass, tev, true, input, modulated), "Alpha bake failed");
    check(preserved[3] == 128 && modulated[3] != preserved[3],
          "Glow alpha policy must be explicit");

    SharedAuthoredBatchVFX::RenderSnapshot snapshot;
    SharedAuthoredBatchVFX::Config::DrawPass streak;
    streak.renderMode = "streak_quad";
    snapshot.drawPasses.push_back(streak);
    SharedAuthoredBatchVFX::RenderRing ring;
    ring.lifeSec = 1.0f;
    ring.ageSec = 0.1f;
    ring.startScale = ring.endScale = 1.0f;
    snapshot.rings.push_back(ring);
    bool meshRequested = false;
    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> batches;
    const unsigned char white[4] = {255, 255, 255, 255};
    const bool appended = vfx::runtime::authored_bridge::appendBatches(
        snapshot, batches, {0.0f, 1.0f, 4.0f},
        [&](const std::string&) -> vfx::runtime::authored_batches::MeshData* {
            meshRequested = true;
            return nullptr;
        },
        [&](const auto&, const auto&, vfx::runtime::authored_batches::TextureView& texture) {
            texture.rgba = white;
            texture.width = texture.height = 1;
            return true;
        });
    check(appended && !batches.empty() && !meshRequested,
          "Procedural streaks must render without an unrelated mesh asset");

    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    const auto manifest = root / "authored.json";
    std::ofstream(manifest) << R"({"draw_passes":[{
        "id":"test-pass", "render_mode":"glow_billboard",
        "frag_shader":"arbitrary/material.frag", "line_pass":true,
        "quarter_texture_bake":true, "preserve_glow_alpha":false
    }]})";
    config.drawManifestPath = manifest.string();
    SharedAuthoredBatchVFX effect;
    effect.setConfig(config);
    const auto& parsed = effect.getConfig().drawPasses;
    check(parsed.size() == 1 && parsed[0].linePass && parsed[0].quarterTextureBake &&
          !parsed[0].preserveGlowAlpha, "Manifest lost explicit material behavior");
    std::filesystem::remove(manifest);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
