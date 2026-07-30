#include "ModelFastGltfTextures.h"
#include "ModelFastGltfLoaderHelpers.h"
#include "engine/core/Environment.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <glad/glad.h>
#include <fastgltf/glm_element_traits.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace pac::model_fastgltf {
namespace {
GLint wrapToGL(fastgltf::Wrap w) {
    switch (w) {
        case fastgltf::Wrap::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case fastgltf::Wrap::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
        case fastgltf::Wrap::Repeat:
        default:
            return GL_REPEAT;
    }
}
GLint filterToGLMin(int f) {
    switch (f) {
        case 9728:
            return GL_NEAREST;
        case 9729:
            return GL_LINEAR;
        case 9984:
            return GL_NEAREST_MIPMAP_NEAREST;
        case 9985:
            return GL_LINEAR_MIPMAP_NEAREST;
        case 9986:
            return GL_NEAREST_MIPMAP_LINEAR;
        case 9987:
            return GL_LINEAR_MIPMAP_LINEAR;
        default:
            return GL_LINEAR_MIPMAP_LINEAR;
    }
}
GLint filterToGLMag(int f) {
    switch (f) {
        case 9728:
            return GL_NEAREST;
        case 9729:
            return GL_LINEAR;
        default:
            return GL_LINEAR;
    }
}
struct EncodedImageBytes {
    std::vector<std::uint8_t> bytes;
    std::string debugName;
};
int b64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
bool decodeBase64(std::string_view in, std::vector<std::uint8_t>& out) {
    out.clear();
    int val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        const int v = b64Value(c);
        if (v < 0) continue;
        val = (val << 6) + v;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return !out.empty();
}
std::optional<EncodedImageBytes> getEncodedImageBytes(const fastgltf::Asset& asset,
                                                      const std::filesystem::path& baseDir,
                                                      const fastgltf::Image& image) {
    EncodedImageBytes out{};
    if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
        std::string u(uri->uri.string().begin(), uri->uri.string().end());
        out.debugName = u;
        if (u.rfind("data:", 0) == 0) {
            const size_t comma = u.find(',');
            if (comma == std::string::npos) return std::nullopt;
            std::string_view meta(u.data(), comma);
            std::string_view payload(u.data() + comma + 1, u.size() - (comma + 1));
            const bool isBase64 = (meta.find(";base64") != std::string_view::npos);
            if (!isBase64) return std::nullopt;
            if (!decodeBase64(payload, out.bytes)) return std::nullopt;
            return out;
        }
        std::filesystem::path p = baseDir / std::string(uri->uri.path().begin(), uri->uri.path().end());
        out.debugName = p.string();
        std::ifstream f(p, std::ios::binary);
        if (!f) return std::nullopt;
        f.seekg(0, std::ios::end);
        const std::streamsize size = f.tellg();
        f.seekg(0, std::ios::beg);
        if (size <= 0) return std::nullopt;
        out.bytes.resize(static_cast<size_t>(size));
        if (!f.read(reinterpret_cast<char*>(out.bytes.data()), size)) return std::nullopt;
        return out;
    }
    if (const auto* vec = std::get_if<fastgltf::sources::Vector>(&image.data)) {
        out.debugName = image.name.empty() ? "VectorImage" : std::string(image.name.begin(), image.name.end());
        out.bytes.resize(vec->bytes.size());
        if (!vec->bytes.empty()) {
            std::memcpy(out.bytes.data(), vec->bytes.data(), vec->bytes.size());
        }
        return out;
    }
    if (const auto* arr = std::get_if<fastgltf::sources::Array>(&image.data)) {
        out.debugName = image.name.empty() ? "ArrayImage" : std::string(image.name.begin(), image.name.end());
        out.bytes.resize(arr->bytes.size());
        if (!arr->bytes.empty()) {
            std::memcpy(out.bytes.data(), arr->bytes.data(), arr->bytes.size());
        }
        return out;
    }
    if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&image.data)) {
        out.debugName = image.name.empty() ? "ByteViewImage" : std::string(image.name.begin(), image.name.end());
        out.bytes.resize(view->bytes.size());
        if (!view->bytes.empty()) {
            std::memcpy(out.bytes.data(), view->bytes.data(), view->bytes.size());
        }
        return out;
    }
    if (const auto* bv = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        if (bv->bufferViewIndex >= asset.bufferViews.size()) return std::nullopt;
        const auto& bufferView = asset.bufferViews[bv->bufferViewIndex];
        if (bufferView.bufferIndex >= asset.buffers.size()) return std::nullopt;
        const auto& buffer = asset.buffers[bufferView.bufferIndex];
        const std::byte* bufPtr = nullptr;
        size_t bufSize = 0;
        if (const auto* bufVec = std::get_if<fastgltf::sources::Vector>(&buffer.data)) {
            bufPtr = bufVec->bytes.data();
            bufSize = bufVec->bytes.size();
        } else if (const auto* bufArr = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
            bufPtr = bufArr->bytes.data();
            bufSize = bufArr->bytes.size();
        } else if (const auto* bufView = std::get_if<fastgltf::sources::ByteView>(&buffer.data)) {
            bufPtr = bufView->bytes.data();
            bufSize = bufView->bytes.size();
        } else {
            return std::nullopt;
        }
        const size_t start = static_cast<size_t>(bufferView.byteOffset);
        const size_t size = static_cast<size_t>(bufferView.byteLength);
        if (start + size > bufSize) return std::nullopt;
        out.debugName = image.name.empty() ? "BufferViewImage" : std::string(image.name.begin(), image.name.end());
        out.bytes.resize(size);
        if (size > 0) {
            std::memcpy(out.bytes.data(), reinterpret_cast<const void*>(bufPtr + start), size);
        }
        return out;
    }
    return std::nullopt;
}
CPUTexture makeWhiteCPUTexture() {
    CPUTexture t;
    t.width = 1;
    t.height = 1;
    t.wrapS = GL_REPEAT;
    t.wrapT = GL_REPEAT;
    t.minF = GL_LINEAR;
    t.magF = GL_LINEAR;
    t.rgba = {255, 255, 255, 255};
    return t;
}
CPUTexture makeBlackCPUTexture() {
    CPUTexture t;
    t.width = 1;
    t.height = 1;
    t.wrapS = GL_REPEAT;
    t.wrapT = GL_REPEAT;
    t.minF = GL_LINEAR;
    t.magF = GL_LINEAR;
    t.rgba = {0, 0, 0, 255};
    return t;
}
enum class TextureKind {
    BaseColor,
    Emissive,
    Normal,
    MetallicRoughness,
    Occlusion
};
CPUTexture makeFlatNormalCPUTexture() {
    CPUTexture t;
    t.width = 1;
    t.height = 1;
    t.wrapS = GL_REPEAT;
    t.wrapT = GL_REPEAT;
    t.minF = GL_LINEAR;
    t.magF = GL_LINEAR;
    t.rgba = {128, 128, 255, 255};
    return t;
}
CPUTexture makeMetallicRoughnessDefaultCPUTexture() {
    CPUTexture t;
    t.width = 1;
    t.height = 1;
    t.wrapS = GL_REPEAT;
    t.wrapT = GL_REPEAT;
    t.minF = GL_LINEAR;
    t.magF = GL_LINEAR;
    // glTF convention: R=occlusion (when packed), G=roughness, B=metallic.
    t.rgba = {255, 255, 255, 255};
    return t;
}
const char* textureKindName(TextureKind kind) {
    switch (kind) {
        case TextureKind::BaseColor: return "BaseColor";
        case TextureKind::Emissive: return "Emissive";
        case TextureKind::Normal: return "Normal";
        case TextureKind::MetallicRoughness: return "MetallicRoughness";
        case TextureKind::Occlusion: return "Occlusion";
        default: return "Unknown";
    }
}
const char* magicName(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() >= 12) {
        static const unsigned char pngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(bytes.data(), pngSig, 8) == 0) return "PNG";
        if (bytes[0] == 0xFF && bytes[1] == 0xD8) return "JPG";
        static const unsigned char ktx2Sig[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(bytes.data(), ktx2Sig, 12) == 0) return "KTX2";
        if (bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ') return "DDS";
        if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
            bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
            return "WEBP";
        }
    }
    return "UNKNOWN";
}
void logTexDecode(const std::string& prefix, const std::string& msg) {
    std::cerr << prefix << msg << "\n";
}
std::string glEnumName(GLint e) {
    switch (e) {
        case GL_REPEAT:
            return "GL_REPEAT";
        case GL_CLAMP_TO_EDGE:
            return "GL_CLAMP_TO_EDGE";
        case GL_MIRRORED_REPEAT:
            return "GL_MIRRORED_REPEAT";
        case GL_NEAREST:
            return "GL_NEAREST";
        case GL_LINEAR:
            return "GL_LINEAR";
        case GL_NEAREST_MIPMAP_NEAREST:
            return "GL_NEAREST_MIPMAP_NEAREST";
        case GL_LINEAR_MIPMAP_NEAREST:
            return "GL_LINEAR_MIPMAP_NEAREST";
        case GL_NEAREST_MIPMAP_LINEAR:
            return "GL_NEAREST_MIPMAP_LINEAR";
        case GL_LINEAR_MIPMAP_LINEAR:
            return "GL_LINEAR_MIPMAP_LINEAR";
        default:
            return "GL_ENUM(" + std::to_string(static_cast<int>(e)) + ")";
    }
}
void dumpRGBAtoPNG(const std::filesystem::path& outPath, const CPUTexture& t) {
    if (t.rgba.empty() || t.width == 0 || t.height == 0) return;
    try {
        std::filesystem::create_directories(outPath.parent_path());
    } catch (const std::exception& e) {
        if (envTruthy("PAC_GLTF_DEBUG_ALL") || envTruthy("PAC_GLTF_DUMP_TEXTURES")) {
            std::cerr << "[gltf][TEX] create_directories failed: " << e.what() << "\n";
        }
    }
    stbi_write_png(outPath.string().c_str(), static_cast<int>(t.width), static_cast<int>(t.height), 4, t.rgba.data(),
                   static_cast<int>(t.width) * 4);
}
CPUTexture decodeTextureFast(const fastgltf::Asset& asset,
                             const std::filesystem::path& baseDir,
                             int materialIndex,
                             TextureKind kind,
                             bool dbg,
                             const std::string& modelPath,
                             int* outTexCoordIndex) {
    if (outTexCoordIndex) *outTexCoordIndex = 0;
    const bool forceDbg = dbg || envTruthy("PAC_GLTF_DEBUG_ALL");
    const std::string prefix = "[gltf][TEX] ";
    auto wantLog = [&]() {
        if (forceDbg) return true;
        const auto envMatch = [&](const char* envVar) -> bool {
            const auto v = engine::env::get(envVar);
            if (!v.has_value()) return false;
            std::string s(*v);
            for (char& c : s) {
                if (c == ';') c = ',';
            }
            std::stringstream ss(s);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                std::string cur;
                for (char ch : tok) {
                    if (std::isspace(static_cast<unsigned char>(ch))) {
                        if (!cur.empty()) {
                            if (ciContains(modelPath, cur)) return true;
                            cur.clear();
                        }
                    } else {
                        cur.push_back(ch);
                    }
                }
                if (!cur.empty() && ciContains(modelPath, cur)) return true;
            }
            return false;
        };
        return envMatch("PAC_GLTF_DEBUG_MATCH");
    };
    const auto white = makeWhiteCPUTexture();
    const auto black = makeBlackCPUTexture();
    const auto flatNormal = makeFlatNormalCPUTexture();
    const auto mrDefault = makeMetallicRoughnessDefaultCPUTexture();
    const auto fallbackForKind = [&](TextureKind k) -> const CPUTexture& {
        switch (k) {
            case TextureKind::BaseColor: return white;
            case TextureKind::Emissive: return black;
            case TextureKind::Normal: return flatNormal;
            case TextureKind::MetallicRoughness: return mrDefault;
            case TextureKind::Occlusion: return white;
            default: return white;
        }
    };
    if (materialIndex < 0 || materialIndex >= static_cast<int>(asset.materials.size())) {
        return fallbackForKind(kind);
    }
    const auto& mat = asset.materials[static_cast<size_t>(materialIndex)];
    std::size_t texIndex = static_cast<std::size_t>(-1);
    int texCoord = 0;
    bool hasTexture = false;
    auto setTextureRef = [&](std::size_t index, int coord) {
        texIndex = index;
        texCoord = coord;
        hasTexture = true;
    };
    if (kind == TextureKind::BaseColor) {
        if (mat.pbrData.baseColorTexture.has_value()) {
            setTextureRef(mat.pbrData.baseColorTexture->textureIndex,
                          static_cast<int>(mat.pbrData.baseColorTexture->texCoordIndex));
        }
    } else if (kind == TextureKind::Emissive) {
        if (mat.emissiveTexture.has_value()) {
            setTextureRef(mat.emissiveTexture->textureIndex,
                          static_cast<int>(mat.emissiveTexture->texCoordIndex));
        }
    } else if (kind == TextureKind::Normal) {
        if (mat.normalTexture.has_value()) {
            setTextureRef(mat.normalTexture->textureIndex,
                          static_cast<int>(mat.normalTexture->texCoordIndex));
        }
    } else if (kind == TextureKind::MetallicRoughness) {
        if (mat.pbrData.metallicRoughnessTexture.has_value()) {
            setTextureRef(mat.pbrData.metallicRoughnessTexture->textureIndex,
                          static_cast<int>(mat.pbrData.metallicRoughnessTexture->texCoordIndex));
        }
    } else if (kind == TextureKind::Occlusion) {
        if (mat.occlusionTexture.has_value()) {
            setTextureRef(mat.occlusionTexture->textureIndex,
                          static_cast<int>(mat.occlusionTexture->texCoordIndex));
        }
    }
    if (outTexCoordIndex) *outTexCoordIndex = texCoord;
    if (!hasTexture) {
        if (kind == TextureKind::BaseColor) {
            CPUTexture t;
            t.width = 1;
            t.height = 1;
            t.wrapS = GL_REPEAT;
            t.wrapT = GL_REPEAT;
            t.minF = GL_LINEAR;
            t.magF = GL_LINEAR;
            const auto f = mat.pbrData.baseColorFactor;
            auto toU8 = [](float x) -> std::uint8_t {
                x = (std::max)(0.0f, (std::min)(1.0f, x));
                return static_cast<std::uint8_t>(std::lround(x * 255.0f));
            };
            t.rgba = {toU8(f[0]), toU8(f[1]), toU8(f[2]), toU8(f[3])};
            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] '" +
                                         std::string(mat.name.begin(), mat.name.end()) +
                                         "' has NO baseColorTexture; using baseColorFactor RGBA8.");
            }
            return t;
        }
        if (wantLog()) {
            logTexDecode(prefix,
                         "mat[" + std::to_string(materialIndex) + "] '" +
                             std::string(mat.name.begin(), mat.name.end()) +
                             "' has NO " + textureKindName(kind) + " texture; using fallback.");
        }
        return fallbackForKind(kind);
    }
    if (texIndex >= asset.textures.size()) {
        if (wantLog()) {
            logTexDecode(prefix, "mat[" + std::to_string(materialIndex) +
                                     "] textureIndex out of range: " + std::to_string(texIndex));
        }
        return fallbackForKind(kind);
    }
    const auto& tex = asset.textures[texIndex];
    fastgltf::Optional<std::size_t> imgIndexOpt = tex.imageIndex;
    const char* imgSlot = "imageIndex";
    if (!imgIndexOpt.has_value()) {
        imgIndexOpt = tex.webpImageIndex;
        imgSlot = "webpImageIndex";
    }
    if (!imgIndexOpt.has_value()) {
        imgIndexOpt = tex.basisuImageIndex;
        imgSlot = "basisuImageIndex";
    }
    if (!imgIndexOpt.has_value()) {
        imgIndexOpt = tex.ddsImageIndex;
        imgSlot = "ddsImageIndex";
    }
    if (!imgIndexOpt.has_value()) {
        if (wantLog()) {
            logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] tex[" +
                                     std::to_string(texIndex) + "] has NO image index (all empty).");
        }
        return fallbackForKind(kind);
    }
    const size_t imgIndex = imgIndexOpt.value();
    if (imgIndex >= asset.images.size()) {
        if (wantLog()) {
            logTexDecode(prefix, "mat[" + std::to_string(materialIndex) +
                                     "] imageIndex out of range: " + std::to_string(imgIndex));
        }
        return fallbackForKind(kind);
    }
    const auto& img = asset.images[imgIndex];
    auto enc = getEncodedImageBytes(asset, baseDir, img);
    if (!enc.has_value() || enc->bytes.empty()) {
        if (wantLog()) {
            logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] tex[" +
                                     std::to_string(texIndex) + "] img[" + std::to_string(imgIndex) + "] (" + imgSlot +
                                     ") encoded bytes EMPTY (" +
                                     (enc.has_value() ? enc->debugName : std::string("no enc")) + ")");
        }
        return (kind == TextureKind::BaseColor) ? white : black;
    }
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* decoded = stbi_load_from_memory(enc->bytes.data(), static_cast<int>(enc->bytes.size()), &w, &h, &comp, 4);
    if (wantLog()) {
        const std::string matName(mat.name.begin(), mat.name.end());
        const char* fmt = magicName(enc->bytes);
        std::string msg = "mat[" + std::to_string(materialIndex) + "] '" + matName + "' ";
        msg += textureKindName(kind);
        msg += " tex[" + std::to_string(texIndex) + "] img[" + std::to_string(imgIndex) + "](" + imgSlot + ")";
        msg += " texCoord=" + std::to_string(texCoord);
        msg += " src='" + enc->debugName + "'";
        msg += " encodedBytes=" + std::to_string(enc->bytes.size());
        msg += " magic=" + std::string(fmt);
        msg += " stbi=" + std::string(decoded ? "OK" : "FAIL");
        if (decoded) {
            msg += " w=" + std::to_string(w) + " h=" + std::to_string(h) + " comp=" + std::to_string(comp);
        } else {
            const char* reason = stbi_failure_reason();
            msg += " reason='" + std::string(reason ? reason : "unknown") + "'";
            if (std::string(fmt) == "KTX2" || std::string(fmt) == "DDS") {
                msg += " (likely unsupported compressed texture format)";
            }
        }
        logTexDecode(prefix, msg);
    }
    if (decoded == nullptr || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return fallbackForKind(kind);
    }
    CPUTexture out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.wrapS = GL_REPEAT;
    out.wrapT = GL_REPEAT;
    out.minF = GL_LINEAR_MIPMAP_LINEAR;
    out.magF = GL_LINEAR;
    if (tex.samplerIndex.has_value() && tex.samplerIndex.value() < asset.samplers.size()) {
        const auto& s = asset.samplers[tex.samplerIndex.value()];
        out.wrapS = wrapToGL(s.wrapS);
        out.wrapT = wrapToGL(s.wrapT);
        if (s.minFilter.has_value()) out.minF = filterToGLMin(static_cast<int>(s.minFilter.value()));
        if (s.magFilter.has_value()) out.magF = filterToGLMag(static_cast<int>(s.magFilter.value()));
    }
    const size_t pxCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    out.rgba.resize(pxCount * 4);
    std::memcpy(out.rgba.data(), decoded, pxCount * 4);
    if (kind == TextureKind::BaseColor) {
        const auto f = mat.pbrData.baseColorFactor;
        const float fr = f[0];
        const float fg = f[1];
        const float fb = f[2];
        const float fa = f[3];
        if (fr != 1.0f || fg != 1.0f || fb != 1.0f || fa != 1.0f) {
            auto mulClampU8 = [](std::uint8_t v, float factor) -> std::uint8_t {
                float x = static_cast<float>(v) * factor;
                x = (std::max)(0.0f, (std::min)(255.0f, x));
                return static_cast<std::uint8_t>(std::lround(x));
            };
            for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
                out.rgba[i + 0] = mulClampU8(out.rgba[i + 0], fr);
                out.rgba[i + 1] = mulClampU8(out.rgba[i + 1], fg);
                out.rgba[i + 2] = mulClampU8(out.rgba[i + 2], fb);
                out.rgba[i + 3] = mulClampU8(out.rgba[i + 3], fa);
            }
        }
    }
    stbi_image_free(decoded);
    if (wantLog() && envTruthy("PAC_GLTF_DUMP_TEXTURES")) {
        std::string kindStr = textureKindName(kind);
        std::transform(kindStr.begin(), kindStr.end(), kindStr.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const std::filesystem::path dumpDir = std::filesystem::path("debug") / "gltf_textures";
        const std::filesystem::path outP =
            dumpDir / (std::filesystem::path(modelPath).stem().string() + "_mat" + std::to_string(materialIndex) +
                       "_" + kindStr + ".png");
        dumpRGBAtoPNG(outP, out);
        logTexDecode(prefix,
                     "dumped " + kindStr + " -> " + outP.string() + " (" + std::to_string(out.width) + "x" +
                         std::to_string(out.height) + ", wrapS=" + glEnumName(out.wrapS) +
                         ", wrapT=" + glEnumName(out.wrapT) + ")");
    }
    return out;
}
}  // namespace
CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex) {
    return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::BaseColor, dbg, modelPath, outTexCoordIndex);
}
CPUTexture decodeEmissiveTextureFast(const fastgltf::Asset& asset,
                                     const std::filesystem::path& baseDir,
                                     int materialIndex,
                                     bool dbg,
                                     const std::string& modelPath,
                                     int* outTexCoordIndex) {
    return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::Emissive, dbg, modelPath, outTexCoordIndex);
}
CPUTexture decodeNormalTextureFast(const fastgltf::Asset& asset,
                                   const std::filesystem::path& baseDir,
                                   int materialIndex,
                                   bool dbg,
                                   const std::string& modelPath,
                                   int* outTexCoordIndex) {
    return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::Normal, dbg, modelPath, outTexCoordIndex);
}
CPUTexture decodeMetallicRoughnessTextureFast(const fastgltf::Asset& asset,
                                              const std::filesystem::path& baseDir,
                                              int materialIndex,
                                              bool dbg,
                                              const std::string& modelPath,
                                              int* outTexCoordIndex) {
    return decodeTextureFast(
        asset,
        baseDir,
        materialIndex,
        TextureKind::MetallicRoughness,
        dbg,
        modelPath,
        outTexCoordIndex);
}
CPUTexture decodeOcclusionTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex) {
    return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::Occlusion, dbg, modelPath, outTexCoordIndex);
}
}  // namespace pac::model_fastgltf
