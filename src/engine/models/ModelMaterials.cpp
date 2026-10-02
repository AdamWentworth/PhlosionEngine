#include "engine/models/ModelMaterials.h"
#include "engine/models/ModelTracks.h"
#include <algorithm>
namespace engine::models {
namespace {
constexpr unsigned char kFallbackWhiteRgba[4] = {255u,255u,255u,255u};
std::string buildWorldTextureCacheKey(const std::string& key,
                                      int width,
                                      int height,
                                      int wrapS,
                                      int wrapT,
                                      bool srgb) {
    if (key.empty() || width <= 0 || height <= 0) return {};
    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += srgb ? "|srgb" : "|lin";
    return cacheKey;
}

}
std::vector<batches::WorldIndexedBatch> buildMaterialTemplates(const MeshData& source,
    const std::string& keyPrefix, std::size_t batchCount) {
    const auto* mesh = &source;
    std::vector<batches::WorldIndexedBatch> result(batchCount);
    for (std::size_t si = 0; si < batchCount; ++si) {
        auto& batch = result[si];
        batch.geometryCacheKey = keyPrefix + "#submesh_geom:" + std::to_string(si);
        if (si < mesh->submeshBaseTextures.size()) {
            const auto& tex = mesh->submeshBaseTextures[si];
            if (tex.hasPixels()) {
                batch.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
                batch.textureCacheKey = buildWorldTextureCacheKey(
                    batch.textureKey,
                    tex.width,
                    tex.height,
                    tex.wrapS,
                    tex.wrapT,
                    true);
                batch.textureRgba = tex.rgba.data();
                batch.textureWidth = tex.width;
                batch.textureHeight = tex.height;
                batch.textureWrapS = tex.wrapS;
                batch.textureWrapT = tex.wrapT;
                if (si < mesh->submeshNormalTextures.size()) {
                    const auto& normalTex = mesh->submeshNormalTextures[si];
                    if (normalTex.hasPixels()) {
                        batch.normalTextureKey =
                            keyPrefix + "#submesh_normal:" + std::to_string(si);
                        batch.normalTextureCacheKey = buildWorldTextureCacheKey(
                            batch.normalTextureKey,
                            normalTex.width,
                            normalTex.height,
                            normalTex.wrapS,
                            normalTex.wrapT,
                            false);
                        batch.normalTextureRgba = normalTex.rgba.data();
                        batch.normalTextureWidth = normalTex.width;
                        batch.normalTextureHeight = normalTex.height;
                        batch.normalTextureWrapS = normalTex.wrapS;
                        batch.normalTextureWrapT = normalTex.wrapT;
                    }
                }
                if (si < mesh->submeshMetallicRoughnessTextures.size()) {
                    const auto& metallicRoughnessTex = mesh->submeshMetallicRoughnessTextures[si];
                    if (metallicRoughnessTex.hasPixels()) {
                        batch.metallicRoughnessTextureKey =
                            keyPrefix + "#submesh_mr:" + std::to_string(si);
                        batch.metallicRoughnessTextureCacheKey = buildWorldTextureCacheKey(
                            batch.metallicRoughnessTextureKey,
                            metallicRoughnessTex.width,
                            metallicRoughnessTex.height,
                            metallicRoughnessTex.wrapS,
                            metallicRoughnessTex.wrapT,
                            false);
                        batch.metallicRoughnessTextureRgba = metallicRoughnessTex.rgba.data();
                        batch.metallicRoughnessTextureWidth = metallicRoughnessTex.width;
                        batch.metallicRoughnessTextureHeight = metallicRoughnessTex.height;
                        batch.metallicRoughnessTextureWrapS = metallicRoughnessTex.wrapS;
                        batch.metallicRoughnessTextureWrapT = metallicRoughnessTex.wrapT;
                    }
                }
                if (si < mesh->submeshOcclusionTextures.size()) {
                    const auto& occlusionTex = mesh->submeshOcclusionTextures[si];
                    if (occlusionTex.hasPixels()) {
                        batch.occlusionTextureKey =
                            keyPrefix + "#submesh_occ:" + std::to_string(si);
                        batch.occlusionTextureCacheKey = buildWorldTextureCacheKey(
                            batch.occlusionTextureKey,
                            occlusionTex.width,
                            occlusionTex.height,
                            occlusionTex.wrapS,
                            occlusionTex.wrapT,
                            false);
                        batch.occlusionTextureRgba = occlusionTex.rgba.data();
                        batch.occlusionTextureWidth = occlusionTex.width;
                        batch.occlusionTextureHeight = occlusionTex.height;
                        batch.occlusionTextureWrapS = occlusionTex.wrapS;
                        batch.occlusionTextureWrapT = occlusionTex.wrapT;
                    }
                }
                if (si < mesh->submeshEmissiveTextures.size()) {
                    const auto& emissiveTex = mesh->submeshEmissiveTextures[si];
                    if (emissiveTex.hasPixels()) {
                        batch.emissiveTextureKey =
                            keyPrefix + "#submesh_emissive:" + std::to_string(si);
                        batch.emissiveTextureCacheKey = buildWorldTextureCacheKey(
                            batch.emissiveTextureKey,
                            emissiveTex.width,
                            emissiveTex.height,
                            emissiveTex.wrapS,
                            emissiveTex.wrapT,
                            true);
                        batch.emissiveTextureRgba = emissiveTex.rgba.data();
                        batch.emissiveTextureWidth = emissiveTex.width;
                        batch.emissiveTextureHeight = emissiveTex.height;
                        batch.emissiveTextureWrapS = emissiveTex.wrapS;
                        batch.emissiveTextureWrapT = emissiveTex.wrapT;
                    }
                }
                if (si < mesh->submeshEnvironmentTextures.size()) {
                    const auto& environmentTex =
                        mesh->submeshEnvironmentTextures[si];
                    if (environmentTex.hasPixels()) {
                        batch.environmentTextureKey =
                            keyPrefix + "#submesh_environment:" +
                            std::to_string(si);
                        batch.environmentTextureCacheKey =
                            buildWorldTextureCacheKey(
                                batch.environmentTextureKey,
                                environmentTex.width,
                                environmentTex.height,
                                environmentTex.wrapS,
                                environmentTex.wrapT,
                                false);
                        batch.environmentTextureRgba =
                            environmentTex.rgba.data();
                        batch.environmentTextureWidth = environmentTex.width;
                        batch.environmentTextureHeight = environmentTex.height;
                        batch.environmentTextureWrapS = environmentTex.wrapS;
                        batch.environmentTextureWrapT = environmentTex.wrapT;
                    }
                }
            }
        }
        if (!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0) {
            batch.textureKey = "__fallback_white_1x1__";
            batch.textureCacheKey = buildWorldTextureCacheKey(
                batch.textureKey, 1, 1, 33071, 33071, true);
            batch.textureRgba = kFallbackWhiteRgba;
            batch.textureWidth = 1;
            batch.textureHeight = 1;
            batch.textureWrapS = 33071; // GL_CLAMP_TO_EDGE
            batch.textureWrapT = 33071; // GL_CLAMP_TO_EDGE
        }
        if (si < mesh->submeshAlphaMode.size()) {
            batch.alphaMode = mesh->submeshAlphaMode[si];
        }
        if (si < mesh->submeshAlphaCutoff.size()) {
            batch.alphaCutoff = mesh->submeshAlphaCutoff[si];
        }
        if (si < mesh->submeshNormalScale.size()) {
            batch.normalScale = std::max(0.0f, mesh->submeshNormalScale[si]);
        }
        if (si < mesh->submeshMetallicFactor.size()) {
            batch.metallicFactor = std::clamp(mesh->submeshMetallicFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshRoughnessFactor.size()) {
            batch.roughnessFactor = std::clamp(mesh->submeshRoughnessFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshOcclusionStrength.size()) {
            batch.occlusionStrength = std::clamp(mesh->submeshOcclusionStrength[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshEmissiveFactors.size()) {
            const glm::vec3& e = mesh->submeshEmissiveFactors[si];
            batch.emissiveFactorR = std::max(0.0f, e.r);
            batch.emissiveFactorG = std::max(0.0f, e.g);
            batch.emissiveFactorB = std::max(0.0f, e.b);
        }
        batch.materialMode = si < mesh->submeshMaterialModes.size() ? mesh->submeshMaterialModes[si] : 2u;
    }
    return result;
}
bool applyEmissionAnimation(const batches::WorldIndexedBatch& src,
    batches::WorldIndexedBatch& dst, const MeshData& mesh, std::size_t submeshIndex,
    int materialAnimationIndex, float materialAnimationTimeSec) {
    glm::vec4 emission{};
    if (sampleClipBoundMaterialAnimation(
            mesh, materialAnimationIndex, submeshIndex,
            MaterialAnimationParameter::EmissiveFactor,
            materialAnimationTimeSec, emission)) {
        dst = src;
        dst.sharedTemplate = nullptr;
        dst.emissiveFactorR = std::max(0.0f, emission.x);
        dst.emissiveFactorG = std::max(0.0f, emission.y);
        dst.emissiveFactorB = std::max(0.0f, emission.z);
        if (submeshIndex < mesh.submeshEmissionBaseTextures.size()) {
            const auto& texture = mesh.submeshEmissionBaseTextures[submeshIndex];
            if (texture.hasPixels()) {
                dst.textureKey = src.textureKey + "#clip-emission-base";
                // Empty key lets the backend derive its cache identity from
                // the distinct texture key and the sampler below.
                dst.textureCacheKey.clear();
                dst.textureRgba = texture.rgba.data();
                dst.textureWidth = texture.width;
                dst.textureHeight = texture.height;
                dst.textureWrapS = texture.wrapS;
                dst.textureWrapT = texture.wrapT;
                dst.textureMipLevels = nullptr;
                dst.textureMipLevelCount = 0u;
            }
        }
        return true;
    }
    return false;
}
}
