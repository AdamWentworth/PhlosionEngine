// tests/TestGltfTextureImport.cpp
//
// CPU contract for the existing glTF texture decoding path. The test authors
// tiny public fixtures (a glTF document, an external binary buffer with image
// bufferViews, one external PNG file and a valid GLB) in a uniquely owned
// temporary directory, loads them through the production loader boundary
// (engine::render::gltf::loader::tryLoad) and inspects the five existing CPU
// decode entry points in
// engine/render/gltf/ModelFastGltfTextures.h. It never constructs a Model,
// uploads a texture or creates a graphics context.
//
// The encoded PNG/JPEG payloads live in gltf_import/GltfTextureFixtures.h and
// were produced once by an independent encoder; every expected pixel value here
// is a literal authored value, not the output of the decoder under test.
//
// This contract reports current behavior. It does not claim GPU sampler or UV
// binding behavior, rendered PBR appearance, linear-light base color
// multiplication, or that the recorded fallback inconsistencies are desired.

#include "gltf_import/GltfImportTestSupport.h"
#include "gltf_import/GltfTextureFixtures.h"

#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfMaterial.h"
#include "engine/render/gltf/ModelFastGltfTextures.h"
#include "engine/core/Environment.h"

#include <fastgltf/tools.hpp>
#include <glad/glad.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using engine::render::gltf::loader::LoadResult;
using engine::render::gltf::model::CPUTexture;
using engine::render::gltf::model::MaterialRenderInfo;
using engine::render::gltf::model::decodeBaseColorTextureFast;
using engine::render::gltf::model::decodeEmissiveTextureFast;
using engine::render::gltf::model::decodeMetallicRoughnessTextureFast;
using engine::render::gltf::model::decodeNormalTextureFast;
using engine::render::gltf::model::decodeOcclusionTextureFast;
using engine::render::gltf::model::resolveMaterialRenderInfo;
using gltf_import_contract::FixtureDirectory;
using gltf_import_contract::expect;
using gltf_texture_contract::BufferSlice;
using gltf_texture_contract::ByteWriter;
using gltf_texture_contract::ImageFixture;
using gltf_texture_contract::kJpegFlatBytes;
using gltf_texture_contract::kPngABytes;
using gltf_texture_contract::kPngAFixture;
using gltf_texture_contract::kPngBBytes;
using gltf_texture_contract::kPngBFixture;
using gltf_texture_contract::kPngCBytes;
using gltf_texture_contract::kPngCFixture;
using gltf_texture_contract::kPngDBytes;
using gltf_texture_contract::kPngDFixture;
using gltf_texture_contract::kPngEBytes;
using gltf_texture_contract::kPngEFixture;

// Process-local environment override for the duration of one scope. The decoder
// can write debug PNGs relative to the process working directory and can emit
// extra logging when these flags are set, so the contract forces them off and
// restores whatever the caller had, including on early returns.
class ScopedEnvValue {
public:
    ScopedEnvValue(const char* name, const char* value) : name_(name) {
        if (const auto previous = engine::env::get(name)) previous_ = *previous;
        applied_ = set(name, value);
    }

    ScopedEnvValue(const ScopedEnvValue&) = delete;
    ScopedEnvValue& operator=(const ScopedEnvValue&) = delete;

    ~ScopedEnvValue() { restore(); }

    bool applied() const { return applied_; }

private:
    static bool set(const char* name, const char* value) {
#if defined(_MSC_VER)
        return _putenv_s(name, value) == 0;
#else
        return ::setenv(name, value, 1) == 0;
#endif
    }

    static bool unset(const char* name) {
#if defined(_MSC_VER)
        return _putenv_s(name, "") == 0;
#else
        return ::unsetenv(name) == 0;
#endif
    }

    void restore() {
        if (!applied_) return;
        applied_ = false;
        if (previous_.has_value()) {
            set(name_.c_str(), previous_->c_str());
        } else {
            unset(name_.c_str());
        }
    }

    std::string name_;
    std::optional<std::string> previous_;
    bool applied_ = false;
};

// ---------------------------------------------------------------------------
// Authored fixture indices. The document below is written to match exactly.
// ---------------------------------------------------------------------------

constexpr int kMatRouteExternalUri = 0;
constexpr int kMatRouteDataUri = 1;
constexpr int kMatRouteBufferView = 2;
constexpr int kMatRoles = 3;
constexpr int kMatFactorOnly = 4;
constexpr int kMatFactorLegacyMultiply = 5;
constexpr int kMatRawDataFactors = 6;
constexpr int kMatNoTextures = 7;
constexpr int kMatBlendOpaque = 8;
constexpr int kMatBlendBinary = 9;
constexpr int kMatBlendIntermediate = 10;
constexpr int kMatBlendAlphaFactor = 11;
constexpr int kMatSignatureOnly = 12;
constexpr int kMatTruncatedPng = 13;
constexpr int kMatTruncatedJpeg = 14;
constexpr int kMatSamplerDefault = 15;
constexpr int kMatSamplerEmpty = 16;
constexpr int kMatSamplerClampNearest = 17;
constexpr int kMatSamplerMirrorLinear = 18;
constexpr int kMatSamplerMin9984 = 19;
constexpr int kMatSamplerMin9985 = 20;
constexpr int kMatSamplerMin9986 = 21;
constexpr int kMatSamplerMin9987 = 22;
constexpr int kMatJpegFlat = 23;
constexpr int kMatAllRolesBufferViewA = 24;

// Texture indices in the fixture document: 0..9 select images 0..9 in order
// (external PNG A, data-URI PNG A, bufferView images A..E, PNG signature only,
// truncated PNG, truncated JPEG); 10..17 select bufferView image A with,
// respectively, no sampler and samplers 0..6; texture 18 selects image 10, the
// flat JPEG.

// ---------------------------------------------------------------------------
// Fixture authoring
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> firstBytes(const std::vector<std::uint8_t>& bytes,
                                     std::size_t count) {
    return std::vector<std::uint8_t>(bytes.begin(),
                                     bytes.begin() +
                                         static_cast<std::ptrdiff_t>(count));
}

BufferSlice addSlice(ByteWriter& writer,
                     const std::vector<std::uint8_t>& bytes,
                     std::size_t paddingBefore) {
    writer.appendRaw(paddingBefore, 0x55u);
    const std::size_t offset = writer.size();
    writer.append(bytes);
    return BufferSlice{offset, bytes.size()};
}

// The external buffer places the image payloads at nonzero, non-contiguous
// offsets with explicit padding before, between and after the slices.
struct TextureBuffer {
    BufferSlice pngA;
    BufferSlice pngB;
    BufferSlice pngC;
    BufferSlice pngD;
    BufferSlice pngE;
    BufferSlice signatureOnly;
    BufferSlice truncatedPng;
    BufferSlice truncatedJpeg;
    BufferSlice jpegFlat;
    std::vector<std::uint8_t> bytes;
};

TextureBuffer buildTextureBuffer() {
    TextureBuffer layout;
    ByteWriter writer;
    writer.appendRaw(16u, 0xAAu);
    layout.pngA = addSlice(writer, kPngABytes, 0u);
    layout.pngB = addSlice(writer, kPngBBytes, 8u);
    layout.pngC = addSlice(writer, kPngCBytes, 4u);
    layout.pngD = addSlice(writer, kPngDBytes, 4u);
    layout.pngE = addSlice(writer, kPngEBytes, 4u);
    layout.signatureOnly = addSlice(writer, firstBytes(kPngABytes, 8u), 4u);
    layout.truncatedPng = addSlice(writer, firstBytes(kPngABytes, 40u), 4u);
    layout.truncatedJpeg = addSlice(writer, firstBytes(kJpegFlatBytes, 40u), 4u);
    layout.jpegFlat = addSlice(writer, kJpegFlatBytes, 4u);
    writer.appendRaw(8u, 0x00u);
    layout.bytes = writer.bytes();
    return layout;
}

std::string bufferViewsJson(const TextureBuffer& layout) {
    const BufferSlice slices[9] = {layout.pngA,
                                   layout.pngB,
                                   layout.pngC,
                                   layout.pngD,
                                   layout.pngE,
                                   layout.signatureOnly,
                                   layout.truncatedPng,
                                   layout.truncatedJpeg,
                                   layout.jpegFlat};
    std::string text = "[";
    for (std::size_t i = 0; i < 9u; ++i) {
        if (i != 0u) text += ", ";
        text += "{ \"buffer\": 0, \"byteOffset\": " +
                std::to_string(slices[i].offset) + ", \"byteLength\": " +
                std::to_string(slices[i].length) + " }";
    }
    return text + "]";
}

std::string textureFixtureJson(const TextureBuffer& layout) {
    std::string json = R"j({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_emissive_strength" ],
  "buffers": [ { "uri": "textures.bin", "byteLength": )j" +
                     std::to_string(layout.bytes.size()) + R"j( } ],
  "bufferViews": )j" +
                     bufferViewsJson(layout) + R"j(,
  "images": [
    { "name": "externalA", "uri": "fixture_a.png" },
    { "name": "dataUriA", "uri": ")j" +
                     std::string("data:image/png;base64,") +
                     gltf_texture_contract::encodeBase64(kPngABytes) + R"j(" },
    { "name": "bufferViewA", "bufferView": 0, "mimeType": "image/png" },
    { "name": "bufferViewB", "bufferView": 1, "mimeType": "image/png" },
    { "name": "bufferViewC", "bufferView": 2, "mimeType": "image/png" },
    { "name": "bufferViewD", "bufferView": 3, "mimeType": "image/png" },
    { "name": "bufferViewE", "bufferView": 4, "mimeType": "image/png" },
    { "name": "signatureOnlyPng", "bufferView": 5, "mimeType": "image/png" },
    { "name": "truncatedPng", "bufferView": 6, "mimeType": "image/png" },
    { "name": "truncatedJpeg", "bufferView": 7, "mimeType": "image/jpeg" },
    { "name": "jpegFlat", "bufferView": 8, "mimeType": "image/jpeg" }
  ],
  "samplers": [
    { },
    { "wrapS": 33071, "wrapT": 33648, "minFilter": 9728, "magFilter": 9728 },
    { "wrapS": 33648, "wrapT": 33071, "minFilter": 9729, "magFilter": 9729 },
    { "minFilter": 9984, "magFilter": 9729 },
    { "minFilter": 9985, "magFilter": 9729 },
    { "minFilter": 9986, "magFilter": 9728 },
    { "minFilter": 9987, "magFilter": 9729 }
  ],
  "textures": [
    { "source": 0, "sampler": 0 },
    { "source": 1, "sampler": 0 },
    { "source": 2 },
    { "source": 3 },
    { "source": 4 },
    { "source": 5 },
    { "source": 6 },
    { "source": 7 },
    { "source": 8 },
    { "source": 9 },
    { "source": 2 },
    { "source": 2, "sampler": 0 },
    { "source": 2, "sampler": 1 },
    { "source": 2, "sampler": 2 },
    { "source": 2, "sampler": 3 },
    { "source": 2, "sampler": 4 },
    { "source": 2, "sampler": 5 },
    { "source": 2, "sampler": 6 },
    { "source": 10 }
  ],
  "materials": [
    { "name": "RouteExternalUri", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
    { "name": "RouteDataUri", "pbrMetallicRoughness": { "baseColorTexture": { "index": 1 } } },
    { "name": "RouteBufferView", "pbrMetallicRoughness": { "baseColorTexture": { "index": 2 } } },
    {
      "name": "Roles",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 2, "texCoord": 1 },
        "metallicRoughnessTexture": { "index": 5, "texCoord": 0 }
      },
      "emissiveTexture": { "index": 3, "texCoord": 0 },
      "normalTexture": { "index": 4, "texCoord": 1, "scale": 1.0 },
      "occlusionTexture": { "index": 6, "texCoord": 1 }
    },
    {
      "name": "FactorOnly",
      "pbrMetallicRoughness": { "baseColorFactor": [0.5, 0.25, 0.75, 0.0] }
    },
    {
      "name": "FactorLegacyMultiply",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 2 },
        "baseColorFactor": [0.5, 0.5, 0.5, 0.5]
      }
    },
    {
      "name": "RawDataFactors",
      "emissiveTexture": { "index": 3 },
      "emissiveFactor": [0.25, 0.5, 1.0],
      "extensions": { "KHR_materials_emissive_strength": { "emissiveStrength": 2.0 } },
      "normalTexture": { "index": 4, "scale": 2.0 },
      "pbrMetallicRoughness": {
        "metallicRoughnessTexture": { "index": 5 },
        "metallicFactor": 0.25,
        "roughnessFactor": 0.75
      },
      "occlusionTexture": { "index": 6, "strength": 0.5 }
    },
    { "name": "NoTextures" },
    {
      "name": "BlendOpaque",
      "alphaMode": "BLEND",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 4 } }
    },
    {
      "name": "BlendBinary",
      "alphaMode": "BLEND",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 5 } }
    },
    {
      "name": "BlendIntermediate",
      "alphaMode": "BLEND",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 2 } }
    },
    {
      "name": "BlendAlphaFactor",
      "alphaMode": "BLEND",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 4 },
        "baseColorFactor": [1.0, 1.0, 1.0, 0.5]
      }
    },
    {
      "name": "SignatureOnly",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 7 },
        "metallicRoughnessTexture": { "index": 7 }
      },
      "emissiveTexture": { "index": 7 },
      "normalTexture": { "index": 7 },
      "occlusionTexture": { "index": 7 }
    },
    {
      "name": "TruncatedPng",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 8 },
        "metallicRoughnessTexture": { "index": 8 }
      },
      "emissiveTexture": { "index": 8 },
      "normalTexture": { "index": 8 },
      "occlusionTexture": { "index": 8 }
    },
    {
      "name": "TruncatedJpeg",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 9 },
        "metallicRoughnessTexture": { "index": 9 }
      },
      "emissiveTexture": { "index": 9 },
      "normalTexture": { "index": 9 },
      "occlusionTexture": { "index": 9 }
    },
    { "name": "SamplerDefault", "pbrMetallicRoughness": { "baseColorTexture": { "index": 10 } } },
    { "name": "SamplerEmpty", "pbrMetallicRoughness": { "baseColorTexture": { "index": 11 } } },
    { "name": "SamplerClampNearest", "pbrMetallicRoughness": { "baseColorTexture": { "index": 12 } } },
    { "name": "SamplerMirrorLinear", "pbrMetallicRoughness": { "baseColorTexture": { "index": 13 } } },
    { "name": "SamplerMin9984", "pbrMetallicRoughness": { "baseColorTexture": { "index": 14 } } },
    { "name": "SamplerMin9985", "pbrMetallicRoughness": { "baseColorTexture": { "index": 15 } } },
    { "name": "SamplerMin9986", "pbrMetallicRoughness": { "baseColorTexture": { "index": 16 } } },
    { "name": "SamplerMin9987", "pbrMetallicRoughness": { "baseColorTexture": { "index": 17 } } },
    { "name": "JpegFlat", "pbrMetallicRoughness": { "baseColorTexture": { "index": 18 } } },
    {
      "name": "AllRolesBufferViewA",
      "pbrMetallicRoughness": {
        "baseColorTexture": { "index": 2 },
        "metallicRoughnessTexture": { "index": 2 }
      },
      "emissiveTexture": { "index": 2 },
      "normalTexture": { "index": 2 },
      "occlusionTexture": { "index": 2 }
    }
  ]
}
)j";
    return json;
}

std::string glbFixtureJson(const TextureBuffer& layout) {
    return std::string(R"j({
  "asset": { "version": "2.0" },
  "buffers": [ { "byteLength": )j") +
           std::to_string(layout.bytes.size()) + R"j( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": )j" +
           std::to_string(layout.pngA.offset) + R"j(, "byteLength": )j" +
           std::to_string(layout.pngA.length) + R"j( },
    { "buffer": 0, "byteOffset": )j" +
           std::to_string(layout.pngB.offset) + R"j(, "byteLength": )j" +
           std::to_string(layout.pngB.length) + R"j( }
  ],
  "images": [
    { "name": "glbA", "bufferView": 0, "mimeType": "image/png" },
    { "name": "glbB", "bufferView": 1, "mimeType": "image/png" }
  ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "materials": [
    { "name": "GlbBase", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
    { "name": "GlbEmissive", "emissiveTexture": { "index": 1 } }
  ]
}
)j";
}

bool writeTextureFixture(const FixtureDirectory& directory,
                         const TextureBuffer& layout,
                         std::string& outFail) {
    if (!expect(directory.writeBytes("textures.bin", layout.bytes),
                "could not write textures.bin", outFail)) {
        return false;
    }
    if (!expect(directory.writeBytes("fixture_a.png", kPngABytes),
                "could not write fixture_a.png", outFail)) {
        return false;
    }
    if (!expect(directory.writeText("textures.gltf", textureFixtureJson(layout)),
                "could not write textures.gltf", outFail)) {
        return false;
    }
    const std::vector<std::uint8_t> glb =
        gltf_texture_contract::makeGlb(glbFixtureJson(layout), layout.bytes);
    if (!expect(directory.writeBytes("textures.glb", glb),
                "could not write textures.glb", outFail)) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Decode helpers
// ---------------------------------------------------------------------------

enum class Role { BaseColor, Emissive, Normal, MetallicRoughness, Occlusion };

const char* roleName(Role role) {
    switch (role) {
        case Role::BaseColor: return "baseColor";
        case Role::Emissive: return "emissive";
        case Role::Normal: return "normal";
        case Role::MetallicRoughness: return "metallicRoughness";
        case Role::Occlusion: return "occlusion";
        default: return "unknown";
    }
}

CPUTexture decodeRole(Role role,
                      const fastgltf::Asset& asset,
                      const std::filesystem::path& baseDir,
                      int materialIndex,
                      int* outTexCoordIndex) {
    switch (role) {
        case Role::BaseColor:
            return decodeBaseColorTextureFast(asset, baseDir, materialIndex, false,
                                              "test.gltf", outTexCoordIndex);
        case Role::Emissive:
            return decodeEmissiveTextureFast(asset, baseDir, materialIndex, false,
                                             "test.gltf", outTexCoordIndex);
        case Role::Normal:
            return decodeNormalTextureFast(asset, baseDir, materialIndex, false,
                                           "test.gltf", outTexCoordIndex);
        case Role::MetallicRoughness:
            return decodeMetallicRoughnessTextureFast(asset, baseDir, materialIndex,
                                                      false, "test.gltf",
                                                      outTexCoordIndex);
        case Role::Occlusion:
            return decodeOcclusionTextureFast(asset, baseDir, materialIndex, false,
                                              "test.gltf", outTexCoordIndex);
        default:
            return CPUTexture{};
    }
}

// Literal placeholder values the decoder returns when no texture is available.
const ImageFixture kWhite1x1{1u, 1u, {255u, 255u, 255u, 255u}};
const ImageFixture kBlack1x1{1u, 1u, {0u, 0u, 0u, 255u}};
const ImageFixture kFlatNormal1x1{1u, 1u, {128u, 128u, 255u, 255u}};
const ImageFixture kFactoredPngA{
    2u, 2u, {128u, 0u, 0u, 128u, 0u, 128u, 0u, 96u, 0u, 0u, 128u, 48u, 128u, 128u,
             0u, 0u}};
// PNG C with only the base-color alpha factor (0.5) applied: RGB factors are
// 1.0, so the RGB bytes stay authored while alpha 255 * 0.5 -> 128.
const ImageFixture kFactoredPngCAlpha{
    2u, 2u, {10u, 20u, 30u, 128u, 40u, 50u, 60u, 128u, 70u, 80u, 90u, 128u, 100u,
             110u, 120u, 128u}};

// Fallback used when a role has no usable texture reference at all.
ImageFixture roleFallback(Role role) {
    switch (role) {
        case Role::Emissive: return kBlack1x1;
        case Role::Normal: return kFlatNormal1x1;
        default: return kWhite1x1;
    }
}

bool expectTexture(const CPUTexture& actual,
                   const ImageFixture& expected,
                   const std::string& context,
                   std::string& outFail) {
    return gltf_texture_contract::expectTexturePixels(actual.rgba, actual.width,
                                                      actual.height, expected,
                                                      context, outFail);
}

bool expectFallbackEnums(const CPUTexture& actual,
                         const std::string& context,
                         std::string& outFail) {
    return gltf_texture_contract::expectTextureEnums(
        actual.wrapS, actual.wrapT, actual.minF, actual.magF, GL_REPEAT, GL_REPEAT,
        GL_LINEAR, GL_LINEAR, context, outFail);
}

bool expectDecodedDefaultEnums(const CPUTexture& actual,
                               const std::string& context,
                               std::string& outFail) {
    return gltf_texture_contract::expectTextureEnums(
        actual.wrapS, actual.wrapT, actual.minF, actual.magF, GL_REPEAT, GL_REPEAT,
        GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, context, outFail);
}

bool loadFixture(const std::filesystem::path& path,
                 std::optional<LoadResult>& out,
                 std::string& outFail) {
    out = engine::render::gltf::loader::tryLoad(path.string());
    if (!expect(out.has_value(),
                "loader::tryLoad rejected the authored fixture " + path.string(),
                outFail)) {
        return false;
    }
    const fastgltf::Error validation = fastgltf::validate(out->asset);
    if (!expect(validation == fastgltf::Error::None,
                "fastgltf::validate rejected the authored fixture " + path.string() +
                    ": " + engine::render::gltf::loader::errorName(validation),
                outFail)) {
        return false;
    }
    return true;
}

// For the deliberately invalid probe documents: the production loader boundary
// is `tryLoad`, while `fastgltf::validate` is a stricter structural check that
// rejects them. Both observations are recorded so the boundary is explicit.
bool loadProbeFixture(const std::filesystem::path& path,
                      std::optional<LoadResult>& out,
                      std::string& outFail) {
    out = engine::render::gltf::loader::tryLoad(path.string());
    if (!expect(out.has_value(),
                "loader::tryLoad rejected the probe fixture " + path.string(),
                outFail)) {
        return false;
    }
    const fastgltf::Error validation = fastgltf::validate(out->asset);
    std::cout << "[gltf-texture] probe " << path.filename().string()
              << ": tryLoad=accepted fastgltf::validate="
              << engine::render::gltf::loader::errorName(validation) << "\n";
    return true;
}

std::string imageSourceName(const fastgltf::DataSource& data) {
    if (std::holds_alternative<fastgltf::sources::BufferView>(data)) return "BufferView";
    if (std::holds_alternative<fastgltf::sources::URI>(data)) return "URI";
    if (std::holds_alternative<fastgltf::sources::Array>(data)) return "Array";
    if (std::holds_alternative<fastgltf::sources::Vector>(data)) return "Vector";
    if (std::holds_alternative<fastgltf::sources::ByteView>(data)) return "ByteView";
    if (std::holds_alternative<fastgltf::sources::Fallback>(data)) return "Fallback";
    if (std::holds_alternative<std::monostate>(data)) return "monostate";
    return "other";
}

std::string textureSourceContext(const fastgltf::Asset& asset,
                                 const std::string& route,
                                 std::size_t imageIndex) {
    return "route " + route + " image[" + std::to_string(imageIndex) +
           "] source=" + imageSourceName(asset.images[imageIndex].data);
}

// Reads the authored role slot the fixture is supposed to have bound. This is a
// fixture assertion helper: it checks which image the material's role texture
// selects before the decoder is called, and does not reimplement any decoding.
bool authoredRoleImage(const fastgltf::Asset& asset,
                       int materialIndex,
                       Role role,
                       std::size_t& outTextureIndex,
                       std::size_t& outImageIndex) {
    if (materialIndex < 0 ||
        materialIndex >= static_cast<int>(asset.materials.size())) {
        return false;
    }
    const auto& material = asset.materials[static_cast<std::size_t>(materialIndex)];
    const fastgltf::TextureInfo* info = nullptr;
    switch (role) {
        case Role::BaseColor:
            if (material.pbrData.baseColorTexture.has_value()) {
                info = &material.pbrData.baseColorTexture.value();
            }
            break;
        case Role::Emissive:
            if (material.emissiveTexture.has_value()) {
                info = &material.emissiveTexture.value();
            }
            break;
        case Role::Normal:
            if (material.normalTexture.has_value()) {
                info = &material.normalTexture.value();
            }
            break;
        case Role::MetallicRoughness:
            if (material.pbrData.metallicRoughnessTexture.has_value()) {
                info = &material.pbrData.metallicRoughnessTexture.value();
            }
            break;
        case Role::Occlusion:
            if (material.occlusionTexture.has_value()) {
                info = &material.occlusionTexture.value();
            }
            break;
        default:
            return false;
    }
    if (info == nullptr || info->textureIndex >= asset.textures.size()) return false;
    const auto& texture = asset.textures[info->textureIndex];
    if (!texture.imageIndex.has_value()) return false;
    outTextureIndex = info->textureIndex;
    outImageIndex = texture.imageIndex.value();
    return outImageIndex < asset.images.size();
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

// The same authored PNG must decode identically through the external relative
// URI, the data URI and the bufferView-in-external-buffer routes, and the same
// bufferView route inside a valid GLB container.
bool caseRoutesReproducePng(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;
    // Observed route mapping: which fastgltf data source each authored route
    // ends up as under the engine loader's fixed option set. Recorded as
    // evidence, not asserted, so the guide can state the coverage precisely.
    std::cout << "[gltf-texture] observed sources: external-uri="
              << imageSourceName(asset.images[0].data)
              << " data-uri=" << imageSourceName(asset.images[1].data)
              << " buffer-view=" << imageSourceName(asset.images[2].data) << "\n";

    const struct {
        int material;
        std::size_t imageIndex;
        const char* route;
    } routes[] = {
        {kMatRouteExternalUri, 0u, "external-relative-uri"},
        {kMatRouteDataUri, 1u, "image-data-uri"},
        {kMatRouteBufferView, 2u, "external-buffer-view"},
    };
    for (const auto& route : routes) {
        int texCoord = -1;
        const CPUTexture decoded =
            decodeRole(Role::BaseColor, asset, gltf->baseDir, route.material, &texCoord);
        const std::string context = textureSourceContext(asset, route.route,
                                                         route.imageIndex);
        if (!expectTexture(decoded, kPngAFixture, context, outFail)) return false;
        if (!expectDecodedDefaultEnums(decoded, context, outFail)) return false;
        if (!expect(texCoord == 0,
                    context + ": reported texCoord " + std::to_string(texCoord) +
                        " != 0",
                    outFail)) {
            return false;
        }
    }

    std::optional<LoadResult> glb;
    if (!loadFixture(directory.file("textures.glb"), glb, outFail)) return false;
    std::cout << "[gltf-texture] observed glb source: buffer-view="
              << imageSourceName(glb->asset.images[0].data) << "\n";
    int texCoord = -1;
    const CPUTexture glbBase =
        decodeRole(Role::BaseColor, glb->asset, glb->baseDir, 0, &texCoord);
    if (!expectTexture(glbBase, kPngAFixture,
                       textureSourceContext(glb->asset, "glb-buffer-view", 0u) +
                           " role baseColor",
                       outFail)) {
        return false;
    }
    const CPUTexture glbEmissive =
        decodeRole(Role::Emissive, glb->asset, glb->baseDir, 1, nullptr);
    return expectTexture(glbEmissive, kPngBFixture,
                         textureSourceContext(glb->asset, "glb-buffer-view", 1u) +
                             " role emissive",
                         outFail);
}

// All five decode roles must select their own authored image and report their
// own texCoord index. A null outTexCoordIndex must be accepted.
bool caseRoleSelection(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    const struct {
        Role role;
        const ImageFixture* expected;
        int expectedTexCoord;
    } roles[] = {
        {Role::BaseColor, &kPngAFixture, 1},
        {Role::Emissive, &kPngBFixture, 0},
        {Role::Normal, &kPngCFixture, 1},
        {Role::MetallicRoughness, &kPngDFixture, 0},
        {Role::Occlusion, &kPngEFixture, 1},
    };
    for (const auto& entry : roles) {
        int texCoord = -1;
        const CPUTexture decoded =
            decodeRole(entry.role, asset, gltf->baseDir, kMatRoles, &texCoord);
        const std::string context =
            std::string("material Roles role ") + roleName(entry.role);
        if (!expectTexture(decoded, *entry.expected, context, outFail)) return false;
        if (!expect(texCoord == entry.expectedTexCoord,
                    context + ": texCoord " + std::to_string(texCoord) +
                        " != expected " + std::to_string(entry.expectedTexCoord),
                    outFail)) {
            return false;
        }
    }

    // A null outTexCoordIndex must not change the decoded payload.
    const CPUTexture nullCoord =
        decodeRole(Role::BaseColor, asset, gltf->baseDir, kMatRoles, nullptr);
    if (!expectTexture(nullCoord, kPngAFixture,
                       "material Roles role baseColor with null outTexCoordIndex",
                       outFail)) {
        return false;
    }

    // One material whose five roles all select the same image must return that
    // image for every role.
    for (const Role role : {Role::BaseColor, Role::Emissive, Role::Normal,
                            Role::MetallicRoughness, Role::Occlusion}) {
        const CPUTexture shared = decodeRole(role, asset, gltf->baseDir,
                                             kMatAllRolesBufferViewA, nullptr);
        if (!expectTexture(shared, kPngAFixture,
                           std::string("material AllRolesBufferViewA role ") +
                               roleName(role),
                           outFail)) {
            return false;
        }
    }
    return true;
}

// Sampler metadata only: wrapping, mag/min filters, an absent sampler and an
// explicit empty sampler. This does not prove GPU sampler or UV binding behavior.
bool caseSamplerMetadata(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    const struct {
        int material;
        const char* label;
        int wrapS;
        int wrapT;
        int minF;
        int magF;
    } cases[] = {
        {kMatSamplerDefault, "no sampler index", GL_REPEAT, GL_REPEAT,
         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR},
        {kMatSamplerEmpty, "empty sampler object", GL_REPEAT, GL_REPEAT,
         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR},
        {kMatSamplerClampNearest, "clamp/mirrored nearest", GL_CLAMP_TO_EDGE,
         GL_MIRRORED_REPEAT, GL_NEAREST, GL_NEAREST},
        {kMatSamplerMirrorLinear, "mirrored/clamp linear", GL_MIRRORED_REPEAT,
         GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR},
        {kMatSamplerMin9984, "min 9984", GL_REPEAT, GL_REPEAT,
         GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR},
        {kMatSamplerMin9985, "min 9985", GL_REPEAT, GL_REPEAT,
         GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
        {kMatSamplerMin9986, "min 9986", GL_REPEAT, GL_REPEAT,
         GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
        {kMatSamplerMin9987, "min 9987", GL_REPEAT, GL_REPEAT,
         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR},
    };
    for (const auto& entry : cases) {
        const CPUTexture decoded =
            decodeRole(Role::BaseColor, asset, gltf->baseDir, entry.material, nullptr);
        const std::string context = "sampler case " + std::string(entry.label);
        if (!expectTexture(decoded, kPngAFixture, context + " pixels", outFail)) {
            return false;
        }
        if (!gltf_texture_contract::expectTextureEnums(
                decoded.wrapS, decoded.wrapT, decoded.minF, decoded.magF, entry.wrapS,
                entry.wrapT, entry.minF, entry.magF, context, outFail)) {
            return false;
        }
    }
    return true;
}

// A real lossy JPEG through a loaded glTF: dimensions and opaque alpha are
// exact, colour is compared with a justified small per-channel tolerance.
bool caseJpegFlatPatch(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const CPUTexture decoded =
        decodeRole(Role::BaseColor, gltf->asset, gltf->baseDir, kMatJpegFlat, nullptr);

    const std::string context = "route jpeg-buffer-view role baseColor";
    if (!expect(decoded.width == gltf_texture_contract::kJpegFlatWidth &&
                    decoded.height == gltf_texture_contract::kJpegFlatHeight,
                context + ": size " + std::to_string(decoded.width) + "x" +
                    std::to_string(decoded.height) + " != expected " +
                    std::to_string(gltf_texture_contract::kJpegFlatWidth) + "x" +
                    std::to_string(gltf_texture_contract::kJpegFlatHeight),
                outFail)) {
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(decoded.width) * decoded.height;
    if (!expect(decoded.rgba.size() == pixelCount * 4u,
                context + ": rgba byte count " + std::to_string(decoded.rgba.size()) +
                    " != expected " + std::to_string(pixelCount * 4u),
                outFail)) {
        return false;
    }
    const int tolerance = gltf_texture_contract::kJpegFlatTolerance;
    const std::uint8_t expectedRgb[3] = {gltf_texture_contract::kJpegFlatRed,
                                         gltf_texture_contract::kJpegFlatGreen,
                                         gltf_texture_contract::kJpegFlatBlue};
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            const int actual = static_cast<int>(decoded.rgba[pixel * 4u + channel]);
            const int authored = static_cast<int>(expectedRgb[channel]);
            if (std::abs(actual - authored) <= tolerance) continue;
            outFail = context + ": pixel " + std::to_string(pixel) + " channel " +
                      std::to_string(channel) + " actual " + std::to_string(actual) +
                      " differs from authored " + std::to_string(authored) +
                      " by more than the allowed " + std::to_string(tolerance);
            return false;
        }
        if (decoded.rgba[pixel * 4u + 3u] != 255u) {
            outFail = context + ": pixel " + std::to_string(pixel) + " decoded alpha " +
                      std::to_string(decoded.rgba[pixel * 4u + 3u]) +
                      " != 255 for an RGB JPEG";
            return false;
        }
    }
    return true;
}

// Missing texture references, absent materials and factor-only base color,
// plus the recorded encoded-byte base-color factor policy.
bool caseDefaultsAndFactors(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    // A valid material with no texture for a role.
    const Role allRoles[5] = {Role::BaseColor, Role::Emissive, Role::Normal,
                              Role::MetallicRoughness, Role::Occlusion};
    for (const Role role : allRoles) {
        int texCoord = -1;
        const CPUTexture decoded =
            decodeRole(role, asset, gltf->baseDir, kMatNoTextures, &texCoord);
        const std::string context =
            std::string("material NoTextures role ") + roleName(role);
        // No texture on a valid material: base color resolves to the authored
        // baseColorFactor as RGBA8 (the default factor is opaque white here) and
        // the other roles use their own placeholders.
        if (!expectTexture(decoded, roleFallback(role), context, outFail)) {
            return false;
        }
        if (!expectFallbackEnums(decoded, context + " sampler metadata", outFail)) {
            return false;
        }
        if (!expect(texCoord == 0,
                    context + ": texCoord " + std::to_string(texCoord) + " != 0",
                    outFail)) {
            return false;
        }
    }

    // Absent material selection resolves to the same per-role fallbacks.
    const int absentIndices[3] = {-1, static_cast<int>(asset.materials.size()), 999};
    for (const int index : absentIndices) {
        for (const Role role : allRoles) {
            int texCoord = -1;
            const CPUTexture decoded =
                decodeRole(role, asset, gltf->baseDir, index, &texCoord);
            const std::string context = "absent material " + std::to_string(index) +
                                        " role " + roleName(role);
            if (!expectTexture(decoded, roleFallback(role), context, outFail)) {
                return false;
            }
            if (!expectFallbackEnums(decoded, context + " sampler metadata", outFail)) {
                return false;
            }
            if (!expect(texCoord == 0,
                        context + ": texCoord " + std::to_string(texCoord) + " != 0",
                        outFail)) {
                return false;
            }
        }
    }

    // Factor-only base color: authored factors become literal RGBA8 values.
    // 0.5 -> 128 (half away from zero), 0.25 -> 64, 0.75 -> 191, 0.0 -> 0.
    const ImageFixture factorOnly{1u, 1u, {128u, 64u, 191u, 0u}};
    const CPUTexture factorDecoded = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                                 kMatFactorOnly, nullptr);
    if (!expectTexture(factorDecoded, factorOnly,
                       "material FactorOnly role baseColor", outFail)) {
        return false;
    }
    if (!expectFallbackEnums(factorDecoded, "material FactorOnly sampler metadata",
                             outFail)) {
        return false;
    }

    // Recorded legacy policy: with a base color texture, the authored factor is
    // multiplied into the encoded bytes (and rounded). This is existing engine
    // behavior, not verified linear-light factor application.
    const CPUTexture legacy = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                         kMatFactorLegacyMultiply, nullptr);
    if (!expectTexture(legacy, kFactoredPngA,
                       "material FactorLegacyMultiply role baseColor", outFail)) {
        return false;
    }

    // Emissive/normal/metallic-roughness/occlusion decoded bytes stay raw even
    // when the material carries non-default scalar factors; those factors belong
    // to separate metadata and are not baked a second time.
    const struct {
        Role role;
        const ImageFixture* expected;
    } rawRoles[] = {
        {Role::Emissive, &kPngBFixture},
        {Role::Normal, &kPngCFixture},
        {Role::MetallicRoughness, &kPngDFixture},
        {Role::Occlusion, &kPngEFixture},
    };
    for (const auto& entry : rawRoles) {
        const CPUTexture decoded =
            decodeRole(entry.role, asset, gltf->baseDir, kMatRawDataFactors, nullptr);
        if (!expectTexture(decoded, *entry.expected,
                           std::string("material RawDataFactors role ") +
                               roleName(entry.role) + " stays raw",
                           outFail)) {
            return false;
        }
    }
    // The authored scalar factors are reported through material metadata and
    // must not have been baked into the decoded bytes above.
    const CPUTexture rawBase =
        decodeRole(Role::BaseColor, asset, gltf->baseDir, kMatRawDataFactors, nullptr);
    if (!expectTexture(rawBase, kWhite1x1,
                       "material RawDataFactors role baseColor (no base texture)",
                       outFail)) {
        return false;
    }
    const MaterialRenderInfo rawInfo =
        resolveMaterialRenderInfo(asset, kMatRawDataFactors, rawBase, false);
    if (!expect(gltf_import_contract::nearlyEqual(rawInfo.emissiveFactor,
                                                  glm::vec3(0.5f, 1.0f, 2.0f)),
                "material RawDataFactors emissiveFactor metadata " +
                    gltf_import_contract::vecText(rawInfo.emissiveFactor) +
                    " != (0.5, 1, 2)",
                outFail)) {
        return false;
    }
    if (!expect(gltf_import_contract::nearlyEqual(rawInfo.normalScale, 2.0f) &&
                    gltf_import_contract::nearlyEqual(rawInfo.metallicFactor, 0.25f) &&
                    gltf_import_contract::nearlyEqual(rawInfo.roughnessFactor, 0.75f) &&
                    gltf_import_contract::nearlyEqual(rawInfo.occlusionStrength, 0.5f),
                "material RawDataFactors scalar factor metadata changed",
                outFail)) {
        return false;
    }
    return true;
}

// Real decoded textures feed the existing alpha-mode normalization, and an
// authored base-color alpha factor changes the resolved result.
bool caseAlphaIntegration(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    const struct {
        int material;
        const ImageFixture* expectedPixels;
        const char* label;
        int expectedAlphaMode;
    } cases[] = {
        {kMatBlendOpaque, &kPngCFixture, "opaque base texture", 0},
        {kMatBlendBinary, &kPngDFixture, "binary cutout base texture", 1},
        {kMatBlendIntermediate, &kPngAFixture, "intermediate alpha base texture", 2},
        {kMatBlendAlphaFactor, &kPngCFixture, "opaque texture with alpha factor 0.5", 2},
    };
    for (const auto& entry : cases) {
        const CPUTexture decoded = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                              entry.material, nullptr);
        const std::string context =
            std::string("alpha integration ") + entry.label;
        // The resolver receives the decoder's actual output, not a reconstruction.
        ImageFixture expected = *entry.expectedPixels;
        if (entry.material == kMatBlendAlphaFactor) {
            expected = kFactoredPngCAlpha;
        }
        if (!expectTexture(decoded, expected, context + " decoded pixels", outFail)) {
            return false;
        }
        const MaterialRenderInfo info =
            resolveMaterialRenderInfo(asset, entry.material, decoded, false);
        if (!expect(info.alphaMode == entry.expectedAlphaMode,
                    context + ": alphaMode " + std::to_string(info.alphaMode) +
                        " != expected " + std::to_string(entry.expectedAlphaMode),
                    outFail)) {
            return false;
        }
    }

    // The alpha factor is what changed the result. Both materials are authored
    // BLEND over the same fully opaque image; the factored material's decoded
    // pixels carry alpha 128 and stay BLEND, while the unfactored pixels are
    // opaque and normalize to OPAQUE.
    const CPUTexture opaquePixels = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                               kMatBlendOpaque, nullptr);
    const CPUTexture factoredPixels = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                                 kMatBlendAlphaFactor, nullptr);
    const MaterialRenderInfo opaqueInfo =
        resolveMaterialRenderInfo(asset, kMatBlendOpaque, opaquePixels, false);
    const MaterialRenderInfo factoredInfo =
        resolveMaterialRenderInfo(asset, kMatBlendAlphaFactor, factoredPixels, false);
    return expect(opaqueInfo.alphaMode == 0 && factoredInfo.alphaMode == 2,
                  "the alpha factor must be the difference: alpha mode " +
                      std::to_string(opaqueInfo.alphaMode) +
                      " with unfactored opaque pixels, " +
                      std::to_string(factoredInfo.alphaMode) +
                      " with the factored pixels (alpha 128) of the same image",
                  outFail);
}

// Undecodable but present bytes take the role-specific decode-error fallback,
// which is distinct from both the empty-bytes and the absent-texture paths.
bool caseDecodeErrorFallbacks(const FixtureDirectory& directory, std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    const Role allRoles[5] = {Role::BaseColor, Role::Emissive, Role::Normal,
                              Role::MetallicRoughness, Role::Occlusion};
    const struct {
        int material;
        std::size_t imageIndex;
        const char* label;
    } corruptPayloads[] = {
        {kMatSignatureOnly, 7u, "png-signature-only"},
        {kMatTruncatedPng, 8u, "truncated-png"},
        {kMatTruncatedJpeg, 9u, "truncated-jpeg"},
    };
    for (const auto& payload : corruptPayloads) {
        for (const Role role : allRoles) {
            const std::string context = std::string("decode failure ") + payload.label +
                                        " role " + roleName(role);
            // Fixture assertion first: the role must actually select the corrupt
            // payload, so the fallback cannot come from a missing reference.
            std::size_t textureIndex = 0u;
            std::size_t imageIndex = 0u;
            if (!expect(authoredRoleImage(asset, payload.material, role, textureIndex,
                                          imageIndex),
                        context + ": the role has no authored texture/image reference",
                        outFail)) {
                return false;
            }
            if (!expect(imageIndex == payload.imageIndex,
                        context + ": role selects image " + std::to_string(imageIndex) +
                            " (texture " + std::to_string(textureIndex) +
                            ") != intended corrupt image " +
                            std::to_string(payload.imageIndex),
                        outFail)) {
                return false;
            }
            const CPUTexture decoded = decodeRole(role, asset, gltf->baseDir,
                                                  payload.material, nullptr);
            if (!expectTexture(decoded, roleFallback(role),
                               context + " (role-specific decode-error fallback)",
                               outFail)) {
                return false;
            }
            if (!expectFallbackEnums(decoded, context + " sampler metadata", outFail)) {
                return false;
            }
        }
    }
    return true;
}

// A probe document that shares the authored external buffer and its first
// bufferView (the PNG A payload). `body` supplies the remaining arrays.
std::string probeDocument(const TextureBuffer& layout, const std::string& body) {
    std::string json = R"j({
  "asset": { "version": "2.0" },
  "buffers": [ { "uri": "textures.bin", "byteLength": )j";
    json += std::to_string(layout.bytes.size());
    json += R"j( } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": )j";
    json += std::to_string(layout.pngA.offset);
    json += R"j(, "byteLength": )j";
    json += std::to_string(layout.pngA.length);
    json += " }";
    json += R"j( ],
)j";
    json += body;
    json += "\n}\n";
    return json;
}

// Bounded invalid-selection probes authored in real files. The loader boundary
// for these documents was observed to accept them, so the decoder's own index
// and sampler guards run; that is pinned here and recorded in the phase report.
bool caseInvalidSelectionGuards(const FixtureDirectory& directory,
                                const TextureBuffer& layout,
                                std::string& outFail) {
    // Texture index outside the authored texture list.
    if (!expect(directory.writeText(
                    "probe_bad_texture_index.gltf",
                    probeDocument(
                        layout,
                        R"j(  "images": [ { "bufferView": 0, "mimeType": "image/png" } ],
  "textures": [ { "source": 0 } ],
  "materials": [
    { "name": "BadTexture", "pbrMetallicRoughness": { "baseColorTexture": { "index": 4 } } }
  ])j")),
                "could not write probe_bad_texture_index.gltf", outFail)) {
        return false;
    }
    std::optional<LoadResult> badTexture;
    if (!loadProbeFixture(directory.file("probe_bad_texture_index.gltf"), badTexture, outFail)) {
        return false;
    }
    std::cout << "[gltf-texture] observed authored out-of-range texture index: "
                 "accepted by the loader; decoder guard runs\n";
    if (!expectTexture(decodeRole(Role::BaseColor, badTexture->asset, badTexture->baseDir, 0,
                                  nullptr),
                       kWhite1x1, "authored out-of-range texture index 4 (textures: 1)",
                       outFail)) {
        return false;
    }

    // Image index outside the authored image list.
    if (!expect(directory.writeText(
                    "probe_bad_image_index.gltf",
                    probeDocument(
                        layout,
                        R"j(  "images": [ { "bufferView": 0, "mimeType": "image/png" } ],
  "textures": [ { "source": 7 } ],
  "materials": [
    { "name": "BadImage", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
  ])j")),
                "could not write probe_bad_image_index.gltf", outFail)) {
        return false;
    }
    std::optional<LoadResult> badImage;
    if (!loadProbeFixture(directory.file("probe_bad_image_index.gltf"), badImage, outFail)) {
        return false;
    }
    std::cout << "[gltf-texture] observed authored out-of-range image index: "
                 "accepted by the loader; decoder guard runs\n";
    if (!expectTexture(decodeRole(Role::BaseColor, badImage->asset, badImage->baseDir, 0,
                                  nullptr),
                       kWhite1x1, "authored out-of-range image index 7 (images: 1)",
                       outFail)) {
        return false;
    }

    // A texture with no image index at all.
    if (!expect(directory.writeText(
                    "probe_no_image_index.gltf",
                    probeDocument(
                        layout,
                        R"j(  "images": [ { "bufferView": 0, "mimeType": "image/png" } ],
  "textures": [ { } ],
  "materials": [
    { "name": "NoImageIndex", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
  ])j")),
                "could not write probe_no_image_index.gltf", outFail)) {
        return false;
    }
    std::optional<LoadResult> noImageIndex;
    if (!loadProbeFixture(directory.file("probe_no_image_index.gltf"), noImageIndex, outFail)) {
        return false;
    }
    int texCoord = -1;
    const CPUTexture noImage = decodeRole(Role::BaseColor, noImageIndex->asset,
                                          noImageIndex->baseDir, 0, &texCoord);
    if (!expectTexture(noImage, kWhite1x1, "authored texture with no image index",
                       outFail)) {
        return false;
    }
    if (!expect(texCoord == 0,
                "authored texture with no image index: texCoord " +
                    std::to_string(texCoord) + " != 0",
                outFail)) {
        return false;
    }

    // A sampler index outside the authored sampler list keeps the decoder's own
    // defaults (repeat wrap, mipmapped-linear minification, linear magnification)
    // rather than the authored sampler.
    if (!expect(directory.writeText(
                    "probe_bad_sampler_index.gltf",
                    probeDocument(
                        layout,
                        R"j(  "images": [ { "bufferView": 0, "mimeType": "image/png" } ],
  "samplers": [ { "wrapS": 33071, "minFilter": 9728, "magFilter": 9728 } ],
  "textures": [ { "source": 0, "sampler": 9 } ],
  "materials": [
    { "name": "BadSampler", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
  ])j")),
                "could not write probe_bad_sampler_index.gltf", outFail)) {
        return false;
    }
    std::optional<LoadResult> badSampler;
    if (!loadProbeFixture(directory.file("probe_bad_sampler_index.gltf"), badSampler, outFail)) {
        return false;
    }
    const CPUTexture badSamplerPixels = decodeRole(Role::BaseColor, badSampler->asset,
                                                   badSampler->baseDir, 0, nullptr);
    if (!expectTexture(badSamplerPixels, kPngAFixture,
                       "authored out-of-range sampler index 9 (samplers: 1) pixels",
                       outFail)) {
        return false;
    }
    if (!expectDecodedDefaultEnums(
            badSamplerPixels,
            "authored out-of-range sampler index 9 (samplers: 1) metadata", outFail)) {
        return false;
    }

    // The empty-image-bytes fallback inconsistency is deliberately not asserted
    // here. It is outside this contract's scope and reproduced only by the
    // standalone probe patch in artifacts/m1-i/empty-byte-probe.patch, which
    // records the observed values without turning them into a desired contract.
    return true;
}

// A missing external image is resolved by the loader, not the decoder. The
// boundary is asserted from observed behavior rather than assumed.
bool caseMissingExternalImage(const FixtureDirectory& directory, std::string& outFail) {
    const std::string json = R"j({
  "asset": { "version": "2.0" },
  "images": [ { "name": "absent", "uri": "absent_image.png" } ],
  "textures": [ { "source": 0 } ],
  "materials": [
    { "name": "Absent", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } }
  ]
}
)j";
    if (!expect(directory.writeText("missing_image.gltf", json),
                "could not write missing_image.gltf", outFail)) {
        return false;
    }
    const auto loaded = engine::render::gltf::loader::tryLoad(
        directory.file("missing_image.gltf").string());
    // Observed boundary: with the loader's fixed LoadExternalImages option a
    // referenced-but-absent image file fails the load, so the decoder never
    // runs and no silent placeholder is produced here.
    return expect(!loaded.has_value(),
                  "a missing external image must fail at the loader boundary",
                  outFail);
}

// Previously returned CPU textures must keep their own pixels after further
// decodes of the same source image with different roles and factors.
bool caseDecodedTextureRetention(const FixtureDirectory& directory,
                                 std::string& outFail) {
    std::optional<LoadResult> gltf;
    if (!loadFixture(directory.file("textures.gltf"), gltf, outFail)) return false;
    const fastgltf::Asset& asset = gltf->asset;

    const CPUTexture first = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                        kMatRouteBufferView, nullptr);
    if (!expectTexture(first, kPngAFixture, "retention first decode (baseColor)", outFail)) {
        return false;
    }
    // Same source image (bufferView PNG A), different role.
    const CPUTexture second =
        decodeRole(Role::Normal, asset, gltf->baseDir, kMatAllRolesBufferViewA, nullptr);
    if (!expectTexture(second, kPngAFixture,
                       "retention second decode (normal, same source image)",
                       outFail)) {
        return false;
    }
    // Same source image, different material and factor.
    const CPUTexture third = decodeRole(Role::BaseColor, asset, gltf->baseDir,
                                        kMatFactorLegacyMultiply, nullptr);
    if (!expectTexture(third, kFactoredPngA,
                       "retention third decode (factored baseColor)", outFail)) {
        return false;
    }
    // Both earlier textures must be untouched by the later decodes.
    if (!expectTexture(first, kPngAFixture,
                       "retention first decode re-checked after later decodes",
                       outFail)) {
        return false;
    }
    return expectTexture(second, kPngAFixture,
                         "retention second decode re-checked after later decodes",
                         outFail);
}

}  // namespace

bool test_gltf_texture_import_contract(std::string& outFail) {
    outFail.clear();
    // Hermetic under ambient debug flags: the decoder dumps debug textures into
    // a process-relative `debug/gltf_textures` directory and can log extra
    // output, so this contract forces those flags off for its whole duration and
    // restores the previous values when it returns.
    const ScopedEnvValue dumpTextures("PHLOSION_GLTF_DUMP_TEXTURES", "0");
    const ScopedEnvValue debugAll("PHLOSION_GLTF_DEBUG_ALL", "0");
    const ScopedEnvValue debugMatch("PHLOSION_GLTF_DEBUG_MATCH", "0");
    if (!expect(dumpTextures.applied() && debugAll.applied() && debugMatch.applied(),
                "the contract could not set its scoped debug environment", outFail)) {
        return false;
    }
    FixtureDirectory directory("phlosion-gltf-texture-import-contract");
    if (!expect(directory.ready(),
                "fixture directory was not created: " + directory.error(), outFail)) {
        return false;
    }
    const TextureBuffer layout = buildTextureBuffer();
    if (!writeTextureFixture(directory, layout, outFail)) {
        return false;
    }
    if (!caseRoutesReproducePng(directory, outFail)) {
        return false;
    }
    if (!caseRoleSelection(directory, outFail)) {
        return false;
    }
    if (!caseSamplerMetadata(directory, outFail)) {
        return false;
    }
    if (!caseJpegFlatPatch(directory, outFail)) {
        return false;
    }
    if (!caseDefaultsAndFactors(directory, outFail)) {
        return false;
    }
    if (!caseAlphaIntegration(directory, outFail)) {
        return false;
    }
    if (!caseDecodeErrorFallbacks(directory, outFail)) {
        return false;
    }
    if (!caseMissingExternalImage(directory, outFail)) {
        return false;
    }
    if (!caseInvalidSelectionGuards(directory, layout, outFail)) {
        return false;
    }
    if (!caseDecodedTextureRetention(directory, outFail)) {
        return false;
    }
    return true;
}
