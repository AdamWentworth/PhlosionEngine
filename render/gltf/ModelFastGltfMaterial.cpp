#include "ModelFastGltfMaterial.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

extern bool isMipmapMinFilter(GLint minF);

namespace pac::model_fastgltf {

MaterialRenderInfo resolveMaterialRenderInfo(const fastgltf::Asset& asset,
                                             int materialIndex,
                                             const Model::CPUTexture& baseCPU,
                                             bool dbgThisModel) {
    MaterialRenderInfo info{};

    if (materialIndex < 0 || materialIndex >= static_cast<int>(asset.materials.size())) {
        return info;
    }

    const auto& mat = asset.materials[static_cast<size_t>(materialIndex)];

    // emissiveFactor is always present in glTF (defaults to (0,0,0)).
    info.emissiveFactor = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);
    info.metallicFactor = std::clamp(static_cast<float>(mat.pbrData.metallicFactor), 0.0f, 1.0f);
    info.roughnessFactor = std::clamp(static_cast<float>(mat.pbrData.roughnessFactor), 0.0f, 1.0f);
    if (mat.normalTexture.has_value()) {
        info.normalScale = std::max(0.0f, static_cast<float>(mat.normalTexture->scale));
    }
    if (mat.occlusionTexture.has_value()) {
        info.occlusionStrength = std::clamp(static_cast<float>(mat.occlusionTexture->strength), 0.0f, 1.0f);
    }

    // Apply emissive strength ONCE.
    info.emissiveFactor *= static_cast<float>(mat.emissiveStrength);

    // Boost ONLY the tail fire, without affecting the rest of the model.
    const std::string matName(mat.name.begin(), mat.name.end());
    if (matName == "fire") {
        constexpr float kTailFireBoost = 1.35f;
        info.emissiveFactor *= kTailFireBoost;
    }

    switch (mat.alphaMode) {
        case fastgltf::AlphaMode::Mask:
            info.alphaMode = 1;
            break;
        case fastgltf::AlphaMode::Blend:
            info.alphaMode = 2;
            break;
        default:
            info.alphaMode = 0;
            break;
    }
    info.alphaCutoff = static_cast<float>(mat.alphaCutoff);
    info.doubleSided = mat.doubleSided;

    // Normalize BLEND materials that are effectively opaque or binary cutout.
    if (info.alphaMode == 2 && !baseCPU.rgba.empty()) {
        const size_t pixelCount = baseCPU.rgba.size() / 4u;
        if (pixelCount > 0u) {
            std::uint8_t minA = 255u;
            std::uint8_t maxA = 0u;
            size_t zeroA = 0u;
            size_t midA = 0u;

            for (size_t i = 3; i < baseCPU.rgba.size(); i += 4u) {
                const std::uint8_t a = baseCPU.rgba[i];
                minA = (std::min)(minA, a);
                maxA = (std::max)(maxA, a);
                if (a == 0u) ++zeroA;
                else if (a < 255u) ++midA;
            }

            const float midFrac = static_cast<float>(midA) / static_cast<float>(pixelCount);
            const bool effectivelyOpaque = (minA >= 250u) && (midFrac <= 0.001f);
            const bool mostlyBinaryCutout = (zeroA > 0u) && (midFrac <= 0.015f);

            if (effectivelyOpaque) {
                info.alphaMode = 0;
            } else if (mostlyBinaryCutout) {
                info.alphaMode = 1;
                info.alphaCutoff = std::clamp(info.alphaCutoff, 0.1f, 0.9f);
            }

            if (dbgThisModel && info.alphaMode != 2) {
                std::cerr << "[gltf][MAT] normalized BLEND material '" << matName
                          << "' -> " << (info.alphaMode == 0 ? "OPAQUE" : "MASK")
                          << " (minA=" << static_cast<int>(minA)
                          << " maxA=" << static_cast<int>(maxA)
                          << " zero=" << zeroA
                          << " mid=" << midA
                          << " px=" << pixelCount << ")\n";
            }
        }
    }

    return info;
}

GLuint uploadTexture2D(const Model::CPUTexture& cpuTexture,
                       bool dbgThisModel,
                       const char* debugLabel) {
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const std::uint32_t w = (cpuTexture.width == 0 ? 1u : cpuTexture.width);
    const std::uint32_t h = (cpuTexture.height == 0 ? 1u : cpuTexture.height);
    const void* pixels = cpuTexture.rgba.empty() ? nullptr : cpuTexture.rgba.data();

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (dbgThisModel) {
        const GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "[gltf][GL] " << debugLabel
                      << " glTexImage2D error=0x" << std::hex << static_cast<unsigned>(err) << std::dec << "\n";
        }
        GLint wq = 0;
        GLint hq = 0;
        GLint ifmt = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &wq);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &hq);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
        std::cerr << "[gltf][GL] " << debugLabel << " uploaded size=" << wq << "x" << hq
                  << " ifmt=" << ifmt << "\n";
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(cpuTexture.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(cpuTexture.wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(cpuTexture.minF));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(cpuTexture.magF));

    if (isMipmapMinFilter(static_cast<GLint>(cpuTexture.minF))) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    return textureId;
}

}  // namespace pac::model_fastgltf
