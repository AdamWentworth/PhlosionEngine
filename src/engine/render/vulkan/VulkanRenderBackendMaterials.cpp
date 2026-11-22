#include "engine/render/WorldTextureQuality.h"
#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/IWorldDepthControl.h"
#include "engine/render/vulkan/VulkanWorldTextureKey.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

void requireVkMaterial(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result) + ".");
    }
}

std::string materialKey(const std::array<
                        std::string,
                        engine::render::vulkan_backend::
                            kWorldMaterialTextureCount>& textureKeys) {
    std::string key;
    for (const std::string& textureKey : textureKeys) {
        key += '|';
        key += textureKey;
    }
    return key;
}

} // namespace

VulkanRenderBackendImpl::Texture* VulkanRenderBackendImpl::ensureWorldTextureRaw(
    const char* key,
    const char* cacheKey,
    const unsigned char* rgba,
    int width,
    int height,
    int wrapS,
    int wrapT,
    bool srgb,
    std::string* outResolvedKey,
    const IRenderBackend::WorldTextureMipLevel* authoredMipLevels,
    std::uint32_t authoredMipLevelCount, bool linearMipSampling, bool materialImage) {
    if(auto* weather=findWorldRainTexture(key)) {if(outResolvedKey)*outResolvedKey=key;return weather;}
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    engine::render::WorldResourceScopeState::AssetResources assets(worldScopes);
    engine::render::WorldResourceScopeState::AllocationAttempt attempt(worldScopes);
    if (!worldScopes.admitted()) return nullptr;
    const std::string resolved = worldScopes.key(engine::render::vulkan_backend::resolvedWorldTextureKey(
        key,
        cacheKey,
        rgba,
        width,
        height,
        wrapS,
        wrapT,
        srgb,
        authoredMipLevelCount, linearMipSampling)+"|quality="+std::to_string(materialImage?worldMaterialProfile.textureMipDrop:0)+
        "|af="+std::to_string(worldMaterialProfile.maxAnisotropy));
    if (outResolvedKey) *outResolvedKey = resolved;
    auto existing = worldTextures.find(resolved);
    if (existing != worldTextures.end()) { attempt.success(); return &existing->second; }

    engine::render::texture_quality::Selection qualityMips;
    qualityMips.reduce(materialImage?worldMaterialProfile.textureMipDrop:0,rgba,width,height,wrapS,wrapT,srgb,authoredMipLevels,authoredMipLevelCount);
    Texture uploaded;
    try {
        uploaded = createTexture(
        rgba,
        width,
        height,
        srgb,
        wrapS,
        wrapT,
        false,
        authoredMipLevels,
        authoredMipLevelCount, linearMipSampling);
    } catch (...) {
        if (worldScopes.active()) return nullptr;
        throw;
    }
    uploaded.scope = worldScopes.active();
    auto& inserted = worldTextures.emplace(resolved, std::move(uploaded)).first->second;
    engine::render::WorldResourceCounts counts;
    counts.textures = 1;
    counts.payloadBytes = inserted.payloadBytes;
    counts.apiAllocationBytes = inserted.allocationBytes;
    worldScopes.commit(counts);
    attempt.success();
    return &inserted;
}

VulkanRenderBackendImpl::WorldMaterial VulkanRenderBackendImpl::createWorldMaterial(
    Texture& base,
    Texture& normal,
    Texture& metallicRoughness,
    Texture& occlusion,
    Texture& emissive,
    Texture& environment,
    Texture& lightProjection,
    Texture& projectedShadow) {
    const std::array<
        Texture*,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> textures{
        &base,
        &normal,
        &metallicRoughness,
        &occlusion,
        &emissive,
        &environment,
        &lightProjection,
        &projectedShadow};
    WorldMaterial out;
    out.textures = textures;

    VkDescriptorSetAllocateInfo allocateInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = 1u;
    allocateInfo.pSetLayouts = &textureSetLayout;
    requireVkMaterial(vkAllocateDescriptorSets(
                          device, &allocateInfo, &out.descriptorSet),
                      "vkAllocateDescriptorSets(world material)");

    std::array<
        VkDescriptorImageInfo,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> imageInfos{};
    std::array<
        VkWriteDescriptorSet,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> writes{};
    for (std::uint32_t i = 0u; i < textures.size(); ++i) {
        imageInfos[i].sampler = textures[i]->sampler;
        imageInfos[i].imageView = textures[i]->view;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = out.descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1u;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(
        device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0u, nullptr);
    return out;
}

VulkanRenderBackendImpl::WorldMaterial* VulkanRenderBackendImpl::ensureWorldMaterial(
    const IRenderBackend::WorldTextureData* texture) {
    if (!worldScopes.admitted()) return nullptr;
    if (!texture) return &fallbackWorldMaterial;
    const auto binding=engine::render::worldMaterialBindingIdentity(texture,worldScopes.active());
    if(binding&&!(texture&&texture->environment.skyDraw))if(const auto* cached=retainedWorldMaterialBindings.find(*binding)) {
        (void)registerIndexedWorldMaterial(**cached);return *cached;
    }
    engine::render::WorldResourceScopeState::AllocationAttempt attempt(worldScopes);

    std::array<
        std::string,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> keys{
        "__fallback_base__",
        "__fallback_normal__",
        "__fallback_metallic_roughness__",
        "__fallback_occlusion__",
        "__fallback_emissive__",
        "__neutral_pmrem_environment__",
        "__fallback_light_projection__",
        "__fallback_projected_shadow__",
    };

    Texture* base = ensureWorldTextureRaw(
        texture->key,
        texture->cacheKey,
        texture->rgba,
        texture->width,
        texture->height,
        texture->linearMipSampling ? (texture->linearMipSampling == 1u ? 33071 : 10497) : texture->wrapS,
        texture->linearMipSampling ? (texture->linearMipSampling == 1u ? 33071 : 10497) : texture->wrapT,
        texture->textureSrgb != 0u,
        &keys[0],
        texture->mipLevels,
        texture->mipLevelCount, texture->linearMipSampling != 0u, texture->linearMipSampling==0 && texture->materialMode<239);
    if (!base) base = &fallbackWorldTexture;

    Texture* normal = ensureWorldTextureRaw(
        texture->normalKey,
        texture->normalCacheKey,
        texture->normalRgba,
        texture->normalWidth,
        texture->normalHeight,
        texture->normalWrapS,
        texture->normalWrapT,
        texture->normalTextureSrgb != 0u,
        &keys[1],
        texture->normalMipLevels,
        texture->normalMipLevelCount, false, true);
    if (!normal) normal = &fallbackWorldNormalTexture;

    Texture* metallicRoughness = ensureWorldTextureRaw(
        texture->metallicRoughnessKey,
        texture->metallicRoughnessCacheKey,
        texture->metallicRoughnessRgba,
        texture->metallicRoughnessWidth,
        texture->metallicRoughnessHeight,
        texture->metallicRoughnessWrapS,
        texture->metallicRoughnessWrapT,
        texture->metallicRoughnessTextureSrgb != 0u,
        &keys[2],
        texture->metallicRoughnessMipLevels,
        texture->metallicRoughnessMipLevelCount, false, true);
    if (!metallicRoughness) metallicRoughness = &fallbackWorldLinearTexture;

    Texture* occlusion = ensureWorldTextureRaw(
        texture->occlusionKey,
        texture->occlusionCacheKey,
        texture->occlusionRgba,
        texture->occlusionWidth,
        texture->occlusionHeight,
        texture->occlusionWrapS,
        texture->occlusionWrapT,
        texture->occlusionTextureSrgb != 0u,
        &keys[3],
        texture->occlusionMipLevels,
        texture->occlusionMipLevelCount, false, true);
    if (!occlusion) occlusion = &fallbackWorldLinearTexture;

    Texture* emissive = ensureWorldTextureRaw(
        texture->emissiveKey,
        texture->emissiveCacheKey,
        texture->emissiveRgba,
        texture->emissiveWidth,
        texture->emissiveHeight,
        texture->emissiveWrapS,
        texture->emissiveWrapT,
        texture->emissiveTextureSrgb != 0u,
        &keys[4],
        texture->emissiveMipLevels,
        texture->emissiveMipLevelCount, false, true);
    if (!emissive) emissive = &fallbackWorldEmissiveTexture;

    Texture* environment = [&]() {
        engine::render::WorldResourceScopeState::SharedResources shared(worldScopes);
        return ensureWorldTextureRaw(
        texture->environmentKey,
        texture->environmentCacheKey,
        texture->environmentRgba,
        texture->environmentWidth,
        texture->environmentHeight,
        texture->environmentWrapS,
        texture->environmentWrapT,
        texture->environmentTextureSrgb != 0u,
        &keys[5],
        texture->environmentMipLevels,
        texture->environmentMipLevelCount);
    }();
    if (!environment) environment = &neutralPmremTexture;

    Texture* lightProjection = ensureWorldTextureRaw(
        texture->lightProjectionKey,
        texture->lightProjectionCacheKey,
        texture->lightProjectionRgba,
        texture->lightProjectionWidth,
        texture->lightProjectionHeight,
        texture->lightProjectionWrapS,
        texture->lightProjectionWrapT,
        texture->lightProjectionTextureSrgb != 0u,
        &keys[6],
        texture->lightProjectionMipLevels,
        texture->lightProjectionMipLevelCount);
    if (!lightProjection) lightProjection = &fallbackWorldLinearTexture;
    if (IWorldDepthControl::isSnapshotTexture(texture->lightProjectionKey)) {
        if (!worldDepthSnapshot) return nullptr;
        lightProjection = worldDepthSnapshot;
        keys[6] = "scene-depth/" + std::to_string(lightProjection->width) + "x" + std::to_string(lightProjection->height);
    }

    if(texture->environment.skyDraw&&worldSkyModelTexture){
        lightProjection=worldSkyModelTexture;
        keys[6]="sky-model/"+std::to_string(reinterpret_cast<std::uintptr_t>(lightProjection->view));
    }
    Texture* projectedShadow = ensureWorldTextureRaw(
        texture->projectedShadowKey,
        texture->projectedShadowCacheKey,
        texture->projectedShadowRgba,
        texture->projectedShadowWidth,
        texture->projectedShadowHeight,
        texture->projectedShadowWrapS,
        texture->projectedShadowWrapT,
        texture->projectedShadowTextureSrgb != 0u,
        &keys[7],
        nullptr,
        0u);
    if (!projectedShadow) projectedShadow = &fallbackWorldLinearTexture;

    if (!worldScopes.admitted()) return nullptr;
    const std::string key = worldScopes.key(materialKey(keys));
    auto existing = worldMaterials.find(key);
    if (existing != worldMaterials.end()) {
        (void)registerIndexedWorldMaterial(existing->second);
        attempt.success();
        if(binding)retainedWorldMaterialBindings.insert(*binding,&existing->second);
        return &existing->second;
    }
    WorldMaterial material;
    try {
        material = createWorldMaterial(
        *base, *normal, *metallicRoughness, *occlusion, *emissive,
        *environment, *lightProjection, *projectedShadow);
    } catch (...) {
        if (worldScopes.active()) return nullptr;
        throw;
    }
    material.scope = worldScopes.active();
    WorldMaterial& inserted = worldMaterials.emplace(key, material).first->second;
    engine::render::WorldResourceCounts counts;
    counts.materialDescriptors = 8;
    worldScopes.commit(counts);
    attempt.success();
    (void)registerIndexedWorldMaterial(inserted);
    if(binding)retainedWorldMaterialBindings.insert(*binding,&inserted);
    return &inserted;
}

void VulkanRenderBackendImpl::prewarmWorldTexture(
    const IRenderBackend::WorldTextureData* texture) {
    if (!initialized || !texture) return;
    (void)ensureWorldMaterial(texture);
}
