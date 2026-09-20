// tests/TestGltfMaterialImport.cpp
//
// CPU contract for the existing glTF CPU material resolution path. The test
// authors a tiny public glTF file, loads it through the production loader
// boundary (engine::render::gltf::loader::tryLoad) and inspects the production
// resolver (engine::render::gltf::model::resolveMaterialRenderInfo). It never
// constructs a Model, uploads a texture or touches a graphics context.

#include "gltf_import/GltfImportTestSupport.h"

#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfMaterial.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using engine::render::gltf::model::MaterialRenderInfo;
using engine::render::gltf::model::resolveMaterialRenderInfo;
using gltf_import_contract::FixtureDirectory;
using gltf_import_contract::expect;
using gltf_import_contract::floatText;
using gltf_import_contract::nearlyEqual;
using gltf_import_contract::vecText;

// Authored material indices in the fixture below.
constexpr int kDefaultOpaque = 0;
constexpr int kAuthoredPbr = 1;
constexpr int kCutout = 2;
constexpr int kGlass = 3;
constexpr int kWideCutoffBlend = 4;

const char* kMaterialsJson = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_emissive_strength" ],
  "buffers": [ { "uri": "materials.bin", "byteLength": 8 } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 8 } ],
  "images": [ { "name": "swatch", "bufferView": 0, "mimeType": "image/png" } ],
  "textures": [ { "source": 0 } ],
  "materials": [
    { "name": "DefaultOpaque" },
    {
      "name": "AuthoredPbr",
      "pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 0.5, 0.25, 1.0],
        "metallicFactor": 0.3,
        "roughnessFactor": 0.6
      },
      "normalTexture": { "index": 0, "scale": 2.5 },
      "occlusionTexture": { "index": 0, "strength": 0.4 },
      "emissiveFactor": [0.25, 0.5, 0.125],
      "extensions": {
        "KHR_materials_emissive_strength": { "emissiveStrength": 2.0 }
      },
      "doubleSided": true
    },
    { "name": "Cutout", "alphaMode": "MASK", "alphaCutoff": 0.35 },
    { "name": "Glass", "alphaMode": "BLEND", "alphaCutoff": 0.25 },
    { "name": "WideCutoffBlend", "alphaMode": "BLEND", "alphaCutoff": 5.0 }
  ]
}
)";

// A PNG signature is enough: the loader reads image bytes without decoding
// them, and this contract never uploads a texture.
std::vector<std::uint8_t> materialsBinary() {
    return {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
}

Model::CPUTexture makeTexture(std::vector<std::uint8_t> rgba) {
    Model::CPUTexture texture;
    texture.width = 2u;
    texture.height = 2u;
    texture.rgba = std::move(rgba);
    return texture;
}

// 2x2 textures driving the inspected blend-normalization policy.
Model::CPUTexture opaqueAlphaTexture() {
    return makeTexture({255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u,
                        255u, 255u, 255u, 255u, 255u, 255u});
}

Model::CPUTexture binaryAlphaTexture() {
    return makeTexture({255u, 255u, 255u, 255u, 64u, 32u, 16u, 0u, 32u, 64u, 96u,
                        255u, 0u, 0u, 0u, 0u});
}

Model::CPUTexture intermediateAlphaTexture() {
    return makeTexture({255u, 255u, 255u, 128u, 200u, 200u, 200u, 128u, 100u, 100u,
                        100u, 128u, 0u, 0u, 0u, 128u});
}

MaterialRenderInfo defaultRenderInfo() {
    MaterialRenderInfo expected;
    expected.emissiveFactor = glm::vec3(0.0f, 0.0f, 0.0f);
    expected.normalScale = 1.0f;
    expected.metallicFactor = 1.0f;
    expected.roughnessFactor = 1.0f;
    expected.occlusionStrength = 1.0f;
    expected.alphaMode = 0;
    expected.alphaCutoff = 0.5f;
    expected.doubleSided = false;
    return expected;
}

bool checkInfo(const MaterialRenderInfo& actual,
               const MaterialRenderInfo& expected,
               const std::string& context,
               std::string& outFail) {
    if (!expect(nearlyEqual(actual.emissiveFactor, expected.emissiveFactor),
                context + ": emissive factor " + vecText(actual.emissiveFactor) +
                    " != " + vecText(expected.emissiveFactor),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(actual.normalScale, expected.normalScale),
                context + ": normal scale " + floatText(actual.normalScale) + " != " +
                    floatText(expected.normalScale),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(actual.metallicFactor, expected.metallicFactor),
                context + ": metallic factor " + floatText(actual.metallicFactor) +
                    " != " + floatText(expected.metallicFactor),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(actual.roughnessFactor, expected.roughnessFactor),
                context + ": roughness factor " + floatText(actual.roughnessFactor) +
                    " != " + floatText(expected.roughnessFactor),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(actual.occlusionStrength, expected.occlusionStrength),
                context + ": occlusion strength " + floatText(actual.occlusionStrength) +
                    " != " + floatText(expected.occlusionStrength),
                outFail)) {
        return false;
    }
    if (!expect(actual.alphaMode == expected.alphaMode,
                context + ": alpha mode " + std::to_string(actual.alphaMode) + " != " +
                    std::to_string(expected.alphaMode),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(actual.alphaCutoff, expected.alphaCutoff),
                context + ": alpha cutoff " + floatText(actual.alphaCutoff) + " != " +
                    floatText(expected.alphaCutoff),
                outFail)) {
        return false;
    }
    if (!expect(actual.doubleSided == expected.doubleSided,
                context + ": double sided " +
                    std::string(actual.doubleSided ? "true" : "false") + " != " +
                    std::string(expected.doubleSided ? "true" : "false"),
                outFail)) {
        return false;
    }
    return true;
}

bool writeFixture(const FixtureDirectory& directory, std::string& outFail) {
    if (!expect(directory.writeText("materials.gltf", kMaterialsJson),
                "could not write materials.gltf", outFail)) {
        return false;
    }
    if (!expect(directory.writeBytes("materials.bin", materialsBinary()),
                "could not write materials.bin", outFail)) {
        return false;
    }
    return true;
}

bool caseInvalidMaterialIndex(const fastgltf::Asset& asset, std::string& outFail) {
    const MaterialRenderInfo expected = defaultRenderInfo();
    const Model::CPUTexture noTexture;
    if (!checkInfo(resolveMaterialRenderInfo(asset, -1, noTexture, false), expected,
                   "material index -1", outFail)) {
        return false;
    }
    if (!checkInfo(resolveMaterialRenderInfo(asset, static_cast<int>(asset.materials.size()),
                                             noTexture, false),
                   expected, "material index == material count", outFail)) {
        return false;
    }
    if (!checkInfo(resolveMaterialRenderInfo(asset, 999, noTexture, false), expected,
                   "material index 999", outFail)) {
        return false;
    }
    return true;
}

bool caseDefaultsAndAuthoredPbr(const fastgltf::Asset& asset, std::string& outFail) {
    const Model::CPUTexture noTexture;
    if (!checkInfo(resolveMaterialRenderInfo(asset, kDefaultOpaque, noTexture, false),
                   defaultRenderInfo(), "DefaultOpaque", outFail)) {
        return false;
    }
    // The resolver must not treat a binary alpha channel on an OPAQUE material
    // as a request to change the authored alpha mode.
    if (!checkInfo(resolveMaterialRenderInfo(asset, kDefaultOpaque,
                                             binaryAlphaTexture(), false),
                   defaultRenderInfo(), "DefaultOpaque with binary alpha", outFail)) {
        return false;
    }

    MaterialRenderInfo expected;
    expected.emissiveFactor = glm::vec3(0.5f, 1.0f, 0.25f);  // * emissiveStrength 2
    expected.normalScale = 2.5f;
    expected.metallicFactor = 0.3f;
    expected.roughnessFactor = 0.6f;
    expected.occlusionStrength = 0.4f;
    expected.alphaMode = 0;
    expected.alphaCutoff = 0.5f;
    expected.doubleSided = true;
    return checkInfo(resolveMaterialRenderInfo(asset, kAuthoredPbr, noTexture, false),
                     expected, "AuthoredPbr", outFail);
}

bool caseMaskedMaterial(const fastgltf::Asset& asset, std::string& outFail) {
    MaterialRenderInfo expected;
    expected.emissiveFactor = glm::vec3(0.0f, 0.0f, 0.0f);
    expected.normalScale = 1.0f;
    expected.metallicFactor = 1.0f;
    expected.roughnessFactor = 1.0f;
    expected.occlusionStrength = 1.0f;
    expected.alphaMode = 1;
    expected.alphaCutoff = 0.35f;
    expected.doubleSided = false;
    if (!checkInfo(resolveMaterialRenderInfo(asset, kCutout, Model::CPUTexture{}, false),
                   expected, "Cutout without a base texture", outFail)) {
        return false;
    }
    return checkInfo(
        resolveMaterialRenderInfo(asset, kCutout, opaqueAlphaTexture(), false), expected,
        "Cutout with an opaque base texture", outFail);
}

// Records the current engine policy: BLEND materials backed by a base texture
// whose alpha is effectively opaque or mostly binary are normalized to OPAQUE or
// MASK. This is an engine behavior, not a general glTF guarantee.
bool caseBlendNormalizationPolicy(const fastgltf::Asset& asset, std::string& outFail) {
    MaterialRenderInfo blend = defaultRenderInfo();
    blend.alphaMode = 2;
    blend.alphaCutoff = 0.25f;

    if (!checkInfo(resolveMaterialRenderInfo(asset, kGlass, Model::CPUTexture{}, false),
                   blend, "Glass without a base texture", outFail)) {
        return false;
    }

    MaterialRenderInfo opaque = blend;
    opaque.alphaMode = 0;
    if (!checkInfo(resolveMaterialRenderInfo(asset, kGlass, opaqueAlphaTexture(), false),
                   opaque, "Glass with an opaque base texture", outFail)) {
        return false;
    }

    MaterialRenderInfo mask = blend;
    mask.alphaMode = 1;
    if (!checkInfo(resolveMaterialRenderInfo(asset, kGlass, binaryAlphaTexture(), false),
                   mask, "Glass with a binary base texture", outFail)) {
        return false;
    }
    if (!checkInfo(
            resolveMaterialRenderInfo(asset, kGlass, intermediateAlphaTexture(), false),
            blend, "Glass with an intermediate base texture", outFail)) {
        return false;
    }

    // The binary-cutout normalization clamps the authored cutoff into [0.1, 0.9].
    MaterialRenderInfo clamped = blend;
    clamped.alphaCutoff = 0.9f;
    clamped.alphaMode = 1;
    return checkInfo(
        resolveMaterialRenderInfo(asset, kWideCutoffBlend, binaryAlphaTexture(), false),
        clamped, "WideCutoffBlend with a binary base texture", outFail);
}

}  // namespace

bool test_gltf_material_import_contract(std::string& outFail) {
    outFail.clear();
    FixtureDirectory directory("phlosion-gltf-material-import-contract");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeFixture(directory, outFail)) {
        return false;
    }
    const auto loaded = engine::render::gltf::loader::tryLoad(
        directory.file("materials.gltf").string());
    if (!expect(loaded.has_value(),
                "loader::tryLoad rejected the authored materials fixture", outFail)) {
        return false;
    }
    // fastgltf's own stricter structural validation of the parsed asset. It is
    // a parse-level check, not proof of appearance or of backend parity.
    const fastgltf::Error validation = fastgltf::validate(loaded->asset);
    if (!expect(validation == fastgltf::Error::None,
                "fastgltf::validate rejected the authored materials fixture: " +
                    std::string(engine::render::gltf::loader::errorName(validation)),
                outFail)) {
        return false;
    }
    const fastgltf::Asset& asset = loaded->asset;
    if (!expect(asset.materials.size() == 5u,
                "authored material count " + std::to_string(asset.materials.size()) +
                    " != 5",
                outFail)) {
        return false;
    }
    if (!caseInvalidMaterialIndex(asset, outFail)) {
        return false;
    }
    if (!caseDefaultsAndAuthoredPbr(asset, outFail)) {
        return false;
    }
    if (!caseMaskedMaterial(asset, outFail)) {
        return false;
    }
    if (!caseBlendNormalizationPolicy(asset, outFail)) {
        return false;
    }
    return true;
}
