#pragma once

// Generic world-renderer adapter for a CPU-owned glTF scene snapshot.
//
// The adapter binds the asset's owned vertices, indices, five decoded map roles
// and scalar factors through the existing cached textured world-mesh draw path.
// It never constructs Model, never creates its own GPU resources and retains no
// renderer pointer. Borrowed pointers are valid for the duration of the call;
// the caller owns the asset and pose until it returns. Owner-thread use only.

#include "GltfSceneAsset.h"

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldResourceScope.h"

#include <array>
#include <cstdint>
#include <string>
#include <span>

namespace engine::render::gltf::scene {

struct GltfSceneDrawOptions {
    std::array<float, 16> viewProjectionMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    // Whole-scene instance placement; composed after each node's global
    // transform.
    std::array<float, 16> instanceTransform{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    std::array<float, 3> cameraPosition{0.0f, 7.0f, 9.0f};
    std::array<float, 3> cameraForward{0.0f, -0.6139406f, -0.7893522f};
    std::array<float, 3> cameraTarget{0.0f, -1.0f, 0.0f};
    // The shared world material path; 2 selects the composed PBR material.
    std::uint8_t materialMode = 2u;
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
    bool translucent = false;
    // Upload the same geometry/materials without submitting a visible draw.
    // May be called outside a frame on the renderer's owner thread.
    bool prewarmOnly = false;
    // Omit primitives attached directly to these nodes. Asset data, hierarchy
    // and animation palettes remain intact; the span is borrowed for this call.
    std::span<const int> excludedNodes;
    // Optional per-material factors for animated material inputs. Empty uses
    // authored factors; otherwise supply one linear RGBA value per material.
    std::span<const std::array<float, 4>> baseColorFactors;
    // Optional normalized linear RGB emission per material, replacing authored
    // factors while retaining emissive textures. Empty preserves authored values.
    std::span<const std::array<float, 3>> emissiveFactors;
    // Optional explicit GPU ownership. Create/release this renderer-owned scope
    // through IWorldResourceScope. Zero retains the legacy unowned cache path.
    engine::render::WorldResourceScopeId resourceScope = 0u;
    engine::render::WorldEnvironmentLighting environment;
};

struct GltfSceneDrawStats {
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t skinnedDraws = 0u;
    std::uint32_t paletteDraws = 0u;
    std::uint32_t paletteMatrices = 0u;
    std::uint32_t materialBinds = 0u;
    std::uint32_t textureRoles = 0u;
};

// Submits opaque/masked primitives, then blended ones, in authored order within
// each pass. Returns false with a diagnostic
// before submitting anything when the pose does not belong to this asset
// generation or any submission input is inconsistent; the caller's statistics
// are only written on success. Native allocation failures in a managed scope
// are reported after submission; they are not transactional draw rollback.
bool drawGltfSceneAsset(IRenderBackend& renderer,
                        const GltfSceneAsset& asset,
                        const GltfScenePose& pose,
                        const GltfSceneDrawOptions& options,
                        GltfSceneDrawStats& stats,
                        std::string* error = nullptr);

}  // namespace engine::render::gltf::scene
