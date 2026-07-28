#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void requireVkMaterial(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result) + ".");
    }
}

std::string resolvedTextureKey(const char* key,
                               const char* cacheKey,
                               const unsigned char* rgba,
                               int width,
                               int height,
                               int wrapS,
                               int wrapT,
                               bool srgb,
                               std::uint32_t authoredMipLevelCount) {
    std::ostringstream out;
    if (cacheKey && cacheKey[0] != '\0') {
        out << cacheKey;
    } else if (key && key[0] != '\0') {
        out << key;
    } else {
        out << "anonymous@" << reinterpret_cast<std::uintptr_t>(rgba);
    }
    out << '|' << width << 'x' << height
        << "|ws=" << wrapS
        << "|wt=" << wrapT
        << "|mips=" << authoredMipLevelCount
        << (srgb ? "|srgb" : "|linear");
    return out.str();
}

std::string materialKey(const std::array<std::string, 6>& textureKeys) {
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
    std::uint32_t authoredMipLevelCount) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    const std::string resolved = resolvedTextureKey(
        key,
        cacheKey,
        rgba,
        width,
        height,
        wrapS,
        wrapT,
        srgb,
        authoredMipLevelCount);
    if (outResolvedKey) *outResolvedKey = resolved;
    auto existing = worldTextures.find(resolved);
    if (existing != worldTextures.end()) return &existing->second;

    Texture uploaded = createTexture(
        rgba,
        width,
        height,
        srgb,
        wrapS,
        wrapT,
        false,
        authoredMipLevels,
        authoredMipLevelCount);
    return &worldTextures.emplace(resolved, std::move(uploaded)).first->second;
}

VulkanRenderBackendImpl::WorldMaterial VulkanRenderBackendImpl::createWorldMaterial(
    Texture& base,
    Texture& normal,
    Texture& metallicRoughness,
    Texture& occlusion,
    Texture& emissive,
    Texture& environment) {
    const std::array<
        Texture*,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> textures{
        &base,
        &normal,
        &metallicRoughness,
        &occlusion,
        &emissive,
        &environment};
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
    if (!texture) return &fallbackWorldMaterial;

    std::array<std::string, 6> keys{
        "__fallback_base__",
        "__fallback_normal__",
        "__fallback_metallic_roughness__",
        "__fallback_occlusion__",
        "__fallback_emissive__",
        "__neutral_pmrem_environment__",
    };

    Texture* base = ensureWorldTextureRaw(
        texture->key,
        texture->cacheKey,
        texture->rgba,
        texture->width,
        texture->height,
        texture->wrapS,
        texture->wrapT,
        texture->textureSrgb != 0u,
        &keys[0],
        texture->mipLevels,
        texture->mipLevelCount);
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
        texture->normalMipLevelCount);
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
        texture->metallicRoughnessMipLevelCount);
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
        texture->occlusionMipLevelCount);
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
        texture->emissiveMipLevelCount);
    if (!emissive) emissive = &fallbackWorldEmissiveTexture;

    Texture* environment = ensureWorldTextureRaw(
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
    if (!environment) environment = &neutralPmremTexture;

    const std::string key = materialKey(keys);
    auto existing = worldMaterials.find(key);
    if (existing != worldMaterials.end()) {
        (void)registerIndexedWorldMaterial(existing->second);
        return &existing->second;
    }
    WorldMaterial material = createWorldMaterial(
        *base, *normal, *metallicRoughness, *occlusion, *emissive,
        *environment);
    WorldMaterial& inserted = worldMaterials.emplace(key, material).first->second;
    (void)registerIndexedWorldMaterial(inserted);
    return &inserted;
}

void VulkanRenderBackendImpl::prewarmWorldTexture(
    const IRenderBackend::WorldTextureData* texture) {
    if (!initialized || !texture) return;
    (void)ensureWorldMaterial(texture);
}
