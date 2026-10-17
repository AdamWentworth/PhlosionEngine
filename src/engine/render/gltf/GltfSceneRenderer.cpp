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

bool outsideView(const GltfScenePrimitive& primitive,
                 const std::array<float, 16>& clipFromLocal) {
    if (!primitive.boundsValid || primitive.skinIndex >= 0) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(primitive.boundsMin[axis]) ||
            !std::isfinite(primitive.boundsMax[axis]) ||
            primitive.boundsMin[axis] > primitive.boundsMax[axis]) return false;
    }
    // A convex box is outside only if every corner lies beyond the SAME plane.
    // Homogeneous tests also retain boxes crossing the eye/near plane.
    unsigned commonOutside = 63u;
    for (int corner = 0; corner < 8; ++corner) {
        std::array<double, 4> clip{};
        for (int row = 0; row < 4; ++row) {
            clip[row] = clipFromLocal[12 + row];
            for (int axis = 0; axis < 3; ++axis) {
                const float position = (corner & (1 << axis))
                    ? primitive.boundsMax[axis] : primitive.boundsMin[axis];
                clip[row] += static_cast<double>(clipFromLocal[axis * 4 + row]) * position;
            }
            if (!std::isfinite(clip[row])) return false;
        }
        unsigned outside = 0u;
        for (int axis = 0; axis < 3; ++axis) {
            const double slack = 1e-5 * std::max({1.0, std::abs(clip[axis]), std::abs(clip[3])});
            if (clip[3] + clip[axis] < -slack) outside |= 1u << (axis * 2);
            if (clip[3] - clip[axis] < -slack) outside |= 1u << (axis * 2 + 1);
        }
        commonOutside &= outside;
        if (!commonOutside) return false;
    }
    return commonOutside != 0u;
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

    texture.materialMode = material.unlit ? 255u : options.materialMode;
    texture.alphaMode = options.translucent ? 2u : material.alphaMode;
    texture.alphaCutoff = material.alphaCutoff;
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
    texture.vertexColorMulR = material.baseColorFactor[0] * options.tint[0];
    texture.vertexColorMulG = material.baseColorFactor[1] * options.tint[1];
    texture.vertexColorMulB = material.baseColorFactor[2] * options.tint[2];
    texture.vertexColorMulA = material.baseColorFactor[3] * options.tint[3];
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
    texture.environment = options.environment;
    texture.environment.surfaceAbsorption = std::clamp(material.roughnessFactor,0.f,1.f)*
        (1.f-std::clamp(material.metallicFactor,0.f,1.f));
    if (material.unlit || material.alphaMode!=0) texture.environment.surfaceWeatherEnabled=false;
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
    for (const int node : options.excludedNodes) {
        if (node < 0 || static_cast<std::size_t>(node) >= asset.nodes.size())
            return fail("glTF excluded node is out of range");
    }
    for (const float value : options.tint) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return fail("glTF scene tint must be finite and normalized");
    }
    if (asset.materials.empty() && !asset.primitives.empty()) {
        return fail("the asset has primitives but no materials");
    }
    if (!options.baseColorFactors.empty()) {
        if (options.baseColorFactors.size() != asset.materials.size())
            return fail("glTF material factor count does not match the asset");
        for (const auto& color : options.baseColorFactors)
            for (float value : color)
                if (!std::isfinite(value) || value < 0 || value > 1)
                    return fail("glTF material factors must be finite and normalized");
    }
    if (!options.emissiveFactors.empty()) {
        if (options.emissiveFactors.size() != asset.materials.size())
            return fail("glTF emissive factor count does not match the asset");
        for (const auto& color : options.emissiveFactors)
            for (float value : color)
                if (!std::isfinite(value) || value < 0 || value > 65504)
                    return fail("glTF emissive factors must be finite nonnegative radiance within 65504");
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
    if (options.cullingViewProjection && !finiteMatrix(*options.cullingViewProjection))
        return fail("glTF culling matrix must be finite");
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
    if (!options.prewarmOnly) renderer.beginWorldIndexedBatchSubmission();
    struct EndBatch {
        IRenderBackend& renderer;
        bool active;
        ~EndBatch() { if (active) renderer.endWorldIndexedBatchSubmission(); }
    } endBatch{renderer,!options.prewarmOnly};
    GltfSceneDrawStats collected;
    // Establish opaque depth before drawing transparent surface overlays.
    // Preserve authored primitive order within each pass.
    for (int transparentPass = 0; transparentPass < 2; ++transparentPass) {
    for (std::size_t index = 0u; index < asset.primitives.size(); ++index) {
        const GltfScenePrimitive& primitive = asset.primitives[index];
        if (std::find(options.excludedNodes.begin(), options.excludedNodes.end(),
                      primitive.nodeIndex) != options.excludedNodes.end()) continue;
        const bool transparent = options.translucent ||
            asset.materials[primitive.materialIndex].alphaMode == 2u;
        if (transparent != (transparentPass != 0)) continue;
        const std::vector<glm::mat4>& palette = pose.palettes[index];

        const std::array<float, 16> nodeGlobal =
            toArray(pose.global[static_cast<std::size_t>(primitive.nodeIndex)]);
        const std::array<float, 16> modelMatrix =
            multiplyColumnMajor(options.instanceTransform, nodeGlobal);
        if (!options.prewarmOnly && options.cullingViewProjection &&
            options.materialMode == 2u && outsideView(primitive,
                multiplyColumnMajor(*options.cullingViewProjection, modelMatrix))) {
            ++collected.culledDraws;
            collected.culledTriangles += primitive.indices.size() / 3u;
            continue;
        }
        WorldTextureData texture = makeTextureData(
            asset.materials[static_cast<std::size_t>(primitive.materialIndex)],
            modelMatrix,
            options);
        // All backends upload/copy texture bytes during submission. Supplying
        // a one-level authored chain prevents automatic mip generation.
        const auto& material = asset.materials[primitive.materialIndex];
        const GltfSceneTexture* roles[] = {&material.baseColor, &material.normal,
            &material.metallicRoughness, &material.occlusion, &material.emissive};
        engine::render::backend::WorldTextureMipLevel levels[5];
        const engine::render::backend::WorldTextureMipLevel** pointers[] = {
            &texture.mipLevels, &texture.normalMipLevels, &texture.metallicRoughnessMipLevels,
            &texture.occlusionMipLevels, &texture.emissiveMipLevels};
        std::uint32_t* counts[] = {&texture.mipLevelCount, &texture.normalMipLevelCount,
            &texture.metallicRoughnessMipLevelCount, &texture.occlusionMipLevelCount,
            &texture.emissiveMipLevelCount};
        for (int role = 0; role < 5; ++role) if (roles[role]->linearWithoutMipmaps) {
            levels[role] = {roles[role]->rgba.data(), roles[role]->width, roles[role]->height};
            *pointers[role] = &levels[role];
            *counts[role] = 1;
        }

        if (!options.baseColorFactors.empty()) {
            const auto& color = options.baseColorFactors[primitive.materialIndex];
            texture.vertexColorMulR = color[0] * options.tint[0];
            texture.vertexColorMulG = color[1] * options.tint[1];
            texture.vertexColorMulB = color[2] * options.tint[2];
            texture.vertexColorMulA = color[3] * options.tint[3];
        }
        if (!options.emissiveFactors.empty()) {
            const auto& color = options.emissiveFactors[primitive.materialIndex];
            texture.emissiveFactorR = color[0];
            texture.emissiveFactorG = color[1];
            texture.emissiveFactorB = color[2];
        }

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

        if (options.prewarmOnly) {
            renderer.prewarmWorldTextureData(&texture);
            renderer.prewarmWorldIndexedMeshCached(primitive.geometryKey.c_str(),
                primitive.vertices.data(), primitive.vertices.size(),
                primitive.indices.data(), primitive.indices.size());
        } else renderer.drawWorldIndexedMeshTexturedCached(
            primitive.geometryKey.c_str(),
            primitive.vertices.data(),
            primitive.vertices.size(),
            primitive.indices.data(),
            primitive.indices.size(),
            &texture,
            options.viewProjectionMatrix.data(),
            options.surfaceWidth,
            options.surfaceHeight);
        if (!options.prewarmOnly) ++collected.drawCalls;
        collected.triangles +=
            static_cast<std::uint64_t>(primitive.indices.size() / 3u);
        ++collected.materialBinds;
        collected.textureRoles += 5u;
    }
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
