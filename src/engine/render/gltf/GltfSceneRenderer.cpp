#include "GltfSceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::gltf::scene {
namespace {

using engine::render::backend::WorldTextureData;

std::array<float, 16> multiplyColumnMajor(const std::array<float, 16>& left,
                                          const std::array<float, 16>& right) {
    std::array<float, 16> out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += left[static_cast<std::size_t>(k) * 4u +
                            static_cast<std::size_t>(row)] *
                       right[static_cast<std::size_t>(column) * 4u +
                             static_cast<std::size_t>(k)];
            }
            out[static_cast<std::size_t>(column) * 4u +
                static_cast<std::size_t>(row)] = sum;
        }
    }
    return out;
}

std::array<float, 16> toArray(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    const float* source = &matrix[0][0];
    std::copy(source, source + 16, out.begin());
    return out;
}

WorldTextureData makeTextureData(const GltfSceneMaterial& material,
                                 const std::array<float, 16>& modelMatrix,
                                 const GltfSceneDrawOptions& options) {
    WorldTextureData texture;

    texture.key = material.baseColor.key.c_str();
    texture.cacheKey = material.baseColor.cacheKey.c_str();
    texture.rgba = material.baseColor.rgba.data();
    texture.width = material.baseColor.width;
    texture.height = material.baseColor.height;
    texture.wrapS = material.baseColor.wrapS;
    texture.wrapT = material.baseColor.wrapT;
    texture.textureSrgb = material.baseColor.textureSrgb;

    texture.normalKey = material.normal.key.c_str();
    texture.normalCacheKey = material.normal.cacheKey.c_str();
    texture.normalRgba = material.normal.rgba.data();
    texture.normalWidth = material.normal.width;
    texture.normalHeight = material.normal.height;
    texture.normalWrapS = material.normal.wrapS;
    texture.normalWrapT = material.normal.wrapT;
    texture.normalTextureSrgb = material.normal.textureSrgb;

    texture.metallicRoughnessKey = material.metallicRoughness.key.c_str();
    texture.metallicRoughnessCacheKey =
        material.metallicRoughness.cacheKey.c_str();
    texture.metallicRoughnessRgba = material.metallicRoughness.rgba.data();
    texture.metallicRoughnessWidth = material.metallicRoughness.width;
    texture.metallicRoughnessHeight = material.metallicRoughness.height;
    texture.metallicRoughnessWrapS = material.metallicRoughness.wrapS;
    texture.metallicRoughnessWrapT = material.metallicRoughness.wrapT;
    texture.metallicRoughnessTextureSrgb =
        material.metallicRoughness.textureSrgb;

    texture.occlusionKey = material.occlusion.key.c_str();
    texture.occlusionCacheKey = material.occlusion.cacheKey.c_str();
    texture.occlusionRgba = material.occlusion.rgba.data();
    texture.occlusionWidth = material.occlusion.width;
    texture.occlusionHeight = material.occlusion.height;
    texture.occlusionWrapS = material.occlusion.wrapS;
    texture.occlusionWrapT = material.occlusion.wrapT;
    texture.occlusionTextureSrgb = material.occlusion.textureSrgb;

    texture.emissiveKey = material.emissive.key.c_str();
    texture.emissiveCacheKey = material.emissive.cacheKey.c_str();
    texture.emissiveRgba = material.emissive.rgba.data();
    texture.emissiveWidth = material.emissive.width;
    texture.emissiveHeight = material.emissive.height;
    texture.emissiveWrapS = material.emissive.wrapS;
    texture.emissiveWrapT = material.emissive.wrapT;
    texture.emissiveTextureSrgb = material.emissive.textureSrgb;

    texture.materialMode = options.materialMode;
    texture.alphaMode = 0u;
    texture.depthTestEnabled = 1u;
    texture.normalScale = material.normalScale;
    texture.metallicFactor = material.metallicFactor;
    texture.roughnessFactor = material.roughnessFactor;
    texture.occlusionStrength = material.occlusionStrength;
    texture.emissiveFactorR = material.emissiveFactor[0];
    texture.emissiveFactorG = material.emissiveFactor[1];
    texture.emissiveFactorB = material.emissiveFactor[2];
    // The base color factor is applied exactly once, in the linear shader
    // field the shared world material multiplies with the sampled texture.
    texture.vertexColorMulR = material.baseColorFactor[0];
    texture.vertexColorMulG = material.baseColorFactor[1];
    texture.vertexColorMulB = material.baseColorFactor[2];
    texture.vertexColorMulA = material.baseColorFactor[3];
    texture.modelMatrix = modelMatrix;
    texture.cameraPosX = options.cameraPosition[0];
    texture.cameraPosY = options.cameraPosition[1];
    texture.cameraPosZ = options.cameraPosition[2];
    texture.cameraForwardX = options.cameraForward[0];
    texture.cameraForwardY = options.cameraForward[1];
    texture.cameraForwardZ = options.cameraForward[2];
    texture.cameraTargetX = options.cameraTarget[0];
    texture.cameraTargetY = options.cameraTarget[1];
    texture.cameraTargetZ = options.cameraTarget[2];
    texture.materialTimeSec = 0.0f;
    return texture;
}

}  // namespace

bool drawGltfSceneAsset(IRenderBackend& renderer,
                        const GltfSceneAsset& asset,
                        const GltfScenePose& pose,
                        const GltfSceneDrawOptions& options,
                        GltfSceneDrawStats& stats,
                        std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (!gltfScenePoseMatchesAsset(asset, pose)) {
        return fail(
            "the pose was sampled from a different glTF scene asset "
            "generation; reload the pose before drawing");
    }
    if (pose.global.size() != asset.nodes.size()) {
        return fail("the pose has " + std::to_string(pose.global.size()) +
                    " global transforms for " +
                    std::to_string(asset.nodes.size()) + " nodes");
    }
    if (pose.palettes.size() != asset.primitives.size()) {
        return fail("the pose has " + std::to_string(pose.palettes.size()) +
                    " palettes for " +
                    std::to_string(asset.primitives.size()) + " primitives");
    }
    if (options.surfaceWidth <= 0 || options.surfaceHeight <= 0) {
        return fail("glTF scene draw needs positive surface dimensions");
    }
    if (asset.materials.empty() && !asset.primitives.empty()) {
        return fail("the asset has primitives but no materials");
    }
    const auto finiteMatrix = [](const std::array<float, 16>& matrix) {
        for (const float value : matrix) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    };
    if (!finiteMatrix(options.viewProjectionMatrix) ||
        !finiteMatrix(options.instanceTransform)) {
        return fail("glTF scene draw matrices must be finite");
    }
    for (const auto& vector : {options.cameraPosition, options.cameraForward, options.cameraTarget}) {
        for (const float value : vector) {
            if (!std::isfinite(value)) return fail("glTF scene camera must be finite");
        }
    }

    // Preflight every submission before the first draw is issued, so a failure
    // can never leave a partially drawn frame.
    for (std::size_t index = 0u; index < asset.primitives.size(); ++index) {
        const GltfScenePrimitive& primitive = asset.primitives[index];
        if (primitive.materialIndex < 0 ||
            static_cast<std::size_t>(primitive.materialIndex) >=
                asset.materials.size()) {
            return fail("primitive " + std::to_string(index) +
                        " has no supported material binding");
        }
        if (primitive.nodeIndex < 0 ||
            static_cast<std::size_t>(primitive.nodeIndex) >= pose.global.size()) {
            return fail("primitive " + std::to_string(index) +
                        " references an out-of-range node");
        }
        if (primitive.vertices.empty() || primitive.indices.empty()) {
            return fail("primitive " + std::to_string(index) +
                        " has no submittable geometry");
        }
        const auto composed = multiplyColumnMajor(options.instanceTransform,
            toArray(pose.global[static_cast<std::size_t>(primitive.nodeIndex)]));
        if (!finiteMatrix(composed)) return fail("composed scene transform is not finite");
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(pose.global[static_cast<std::size_t>(
                        primitive.nodeIndex)][column][row])) {
                    return fail("primitive " + std::to_string(index) +
                                " has a non-finite node transform");
                }
            }
        }
        if (primitive.skinIndex >= 0) {
            if (static_cast<std::size_t>(primitive.skinIndex) >=
                asset.skins.size()) {
                return fail("primitive " + std::to_string(index) +
                            " references an out-of-range skin");
            }
            const std::size_t expected =
                asset.skins[static_cast<std::size_t>(primitive.skinIndex)]
                    .joints.size();
            const std::vector<glm::mat4>& palette = pose.palettes[index];
            if (expected == 0u || expected > kMaxSkinJoints || palette.size() != expected) {
                return fail("primitive " + std::to_string(index) +
                            " needs " + std::to_string(expected) +
                            " palette matrices but the pose has " +
                            std::to_string(palette.size()));
            }
            for (const glm::mat4& matrix : palette) {
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        if (!std::isfinite(matrix[column][row])) {
                            return fail("primitive " + std::to_string(index) +
                                        " has a non-finite palette matrix");
                        }
                    }
                }
            }
        } else if (!pose.palettes[index].empty()) {
            return fail("primitive " + std::to_string(index) +
                        " is not skinned but the pose carries a palette");
        }
    }

    IWorldResourceScope* scopeApi = nullptr;
    if (options.resourceScope != 0u) {
        scopeApi = dynamic_cast<IWorldResourceScope*>(&renderer);
        if (!scopeApi || !scopeApi->beginWorldResourceScope(options.resourceScope))
            return fail("renderer rejected the glTF scene resource scope");
    }
    struct EndScope {
        IWorldResourceScope* api;
        ~EndScope() { if (api) api->endWorldResourceScope(); }
    } endScope{scopeApi};
    // Preserve backend state once for this ordered group of primitives.
    renderer.beginWorldIndexedBatchSubmission();
    struct EndBatch {
        IRenderBackend& renderer;
        ~EndBatch() { renderer.endWorldIndexedBatchSubmission(); }
    } endBatch{renderer};
    GltfSceneDrawStats collected;
    for (std::size_t index = 0u; index < asset.primitives.size(); ++index) {
        const GltfScenePrimitive& primitive = asset.primitives[index];
        const std::vector<glm::mat4>& palette = pose.palettes[index];

        const std::array<float, 16> nodeGlobal =
            toArray(pose.global[static_cast<std::size_t>(primitive.nodeIndex)]);
        const std::array<float, 16> modelMatrix =
            multiplyColumnMajor(options.instanceTransform, nodeGlobal);
        WorldTextureData texture = makeTextureData(
            asset.materials[static_cast<std::size_t>(primitive.materialIndex)],
            modelMatrix,
            options);

        if (!palette.empty()) {
            texture.gpuSkinning = 1u;
            // Mode 0 binds one matrix per joint; the palette already contains
            // the inverse(meshGlobal) * jointGlobal * inverseBind product.
            texture.gpuSkinningMode = 0u;
            texture.skinMatrixCount =
                static_cast<std::uint32_t>(palette.size());
            texture.skinMatrices =
                reinterpret_cast<const float*>(palette.data());
            ++collected.paletteDraws;
            ++collected.skinnedDraws;
            collected.paletteMatrices +=
                static_cast<std::uint32_t>(palette.size());
        }

        renderer.drawWorldIndexedMeshTexturedCached(
            primitive.geometryKey.c_str(),
            primitive.vertices.data(),
            primitive.vertices.size(),
            primitive.indices.data(),
            primitive.indices.size(),
            &texture,
            options.viewProjectionMatrix.data(),
            options.surfaceWidth,
            options.surfaceHeight);
        ++collected.drawCalls;
        collected.triangles +=
            static_cast<std::uint64_t>(primitive.indices.size() / 3u);
        ++collected.materialBinds;
        collected.textureRoles += 5u;
    }

    if (scopeApi) {
        engine::render::WorldResourceScopeStats scope;
        if (!scopeApi->getWorldResourceScopeStats(options.resourceScope, scope) ||
            scope.status != engine::render::WorldResourceScopeStatus::Ready)
            return fail("glTF scene GPU allocation failed: " + scope.error);
    }
    stats = collected;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace engine::render::gltf::scene
