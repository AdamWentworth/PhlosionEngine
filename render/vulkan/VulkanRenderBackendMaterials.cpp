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
                               bool srgb) {
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
        << (srgb ? "|srgb" : "|linear");
    return out.str();
}

std::string materialKey(const std::array<std::string, 5>& textureKeys) {
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
    std::string* outResolvedKey) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    const std::string resolved = resolvedTextureKey(
        key, cacheKey, rgba, width, height, wrapS, wrapT, srgb);
    if (outResolvedKey) *outResolvedKey = resolved;
    auto existing = worldTextures.find(resolved);
    if (existing != worldTextures.end()) return &existing->second;

    Texture uploaded = createTexture(
        rgba, width, height, srgb, wrapS, wrapT, false);
    return &worldTextures.emplace(resolved, std::move(uploaded)).first->second;
}

VulkanRenderBackendImpl::WorldMaterial VulkanRenderBackendImpl::createWorldMaterial(
    Texture& base,
    Texture& normal,
    Texture& metallicRoughness,
    Texture& occlusion,
    Texture& emissive) {
    const std::array<
        Texture*,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> textures{
        &base,
        &normal,
        &metallicRoughness,
        &occlusion,
        &emissive,
        &neutralPmremTexture};
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

    std::array<std::string, 5> keys{
        "__fallback_base__",
        "__fallback_normal__",
        "__fallback_metallic_roughness__",
        "__fallback_occlusion__",
        "__fallback_emissive__",
    };

    Texture* base = ensureWorldTextureRaw(
        texture->key,
        texture->cacheKey,
        texture->rgba,
        texture->width,
        texture->height,
        texture->wrapS,
        texture->wrapT,
        true,
        &keys[0]);
    if (!base) base = &fallbackWorldTexture;

    Texture* normal = ensureWorldTextureRaw(
        texture->normalKey,
        texture->normalCacheKey,
        texture->normalRgba,
        texture->normalWidth,
        texture->normalHeight,
        texture->normalWrapS,
        texture->normalWrapT,
        false,
        &keys[1]);
    if (!normal) normal = &fallbackWorldNormalTexture;

    Texture* metallicRoughness = ensureWorldTextureRaw(
        texture->metallicRoughnessKey,
        texture->metallicRoughnessCacheKey,
        texture->metallicRoughnessRgba,
        texture->metallicRoughnessWidth,
        texture->metallicRoughnessHeight,
        texture->metallicRoughnessWrapS,
        texture->metallicRoughnessWrapT,
        false,
        &keys[2]);
    if (!metallicRoughness) metallicRoughness = &fallbackWorldLinearTexture;

    Texture* occlusion = ensureWorldTextureRaw(
        texture->occlusionKey,
        texture->occlusionCacheKey,
        texture->occlusionRgba,
        texture->occlusionWidth,
        texture->occlusionHeight,
        texture->occlusionWrapS,
        texture->occlusionWrapT,
        false,
        &keys[3]);
    if (!occlusion) occlusion = &fallbackWorldLinearTexture;

    Texture* emissive = ensureWorldTextureRaw(
        texture->emissiveKey,
        texture->emissiveCacheKey,
        texture->emissiveRgba,
        texture->emissiveWidth,
        texture->emissiveHeight,
        texture->emissiveWrapS,
        texture->emissiveWrapT,
        true,
        &keys[4]);
    if (!emissive) emissive = &fallbackWorldEmissiveTexture;

    const std::string key = materialKey(keys);
    auto existing = worldMaterials.find(key);
    if (existing != worldMaterials.end()) {
        (void)registerIndexedWorldMaterial(existing->second);
        return &existing->second;
    }
    WorldMaterial material = createWorldMaterial(
        *base, *normal, *metallicRoughness, *occlusion, *emissive);
    WorldMaterial& inserted = worldMaterials.emplace(key, material).first->second;
    (void)registerIndexedWorldMaterial(inserted);
    return &inserted;
}

void VulkanRenderBackendImpl::prewarmWorldTexture(
    const IRenderBackend::WorldTextureData* texture) {
    if (!initialized || !texture) return;
    (void)ensureWorldMaterial(texture);
}
