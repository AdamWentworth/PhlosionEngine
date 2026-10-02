#include "engine/vfx/runtime/shared/SharedAuthoredVfxBridge.h"

namespace vfx::runtime::authored_bridge {

bool appendBatches(const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                   std::vector<authored_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture) {
    if (snapshot.drawPasses.empty() || snapshot.rings.empty()) return false;

    bool appendedAny = false;
    for (const auto& pass : snapshot.drawPasses) {
        if (!pass.enabled) continue;

        const authored::TevState passTev = authored::resolveTevState(snapshot.config, pass);

        const bool drawQuarterRing = pass.textureQuarterRing;
        const bool glowBillboardPass = authored::isGlowBillboardPass(pass);
        const bool streakQuadPass = authored::isStreakQuadPass(pass);
        authored_batches::MeshData* passMesh = nullptr;
        if (!drawQuarterRing && !glowBillboardPass && !streakQuadPass) {
            if (pass.meshPath.empty()) continue;
            if (!resolveMesh) continue;
            passMesh = resolveMesh(pass.meshPath);
            if (!passMesh) continue;
        }

        if (!resolveTexture) continue;
        authored_batches::TextureView texture;
        if (!resolveTexture(pass, passTev, texture)) continue;

        appendedAny =
            authored_batches::appendPassBatch(outBatches,
                                              snapshot,
                                              pass,
                                              passTev,
                                              passMesh,
                                              texture,
                                              cameraWorldPos) ||
            appendedAny;
    }

    return appendedAny;
}

} // namespace vfx::runtime::authored_bridge
