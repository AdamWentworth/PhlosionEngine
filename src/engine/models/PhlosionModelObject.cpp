#include "engine/models/PhlosionModelObject.h"
#include "engine/models/ModelBillboards.h"

#include "engine/models/PhlosionTextureDependencyStore.h"

#include "engine/assets/phlosion/PhlosionBinaryCodec.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/core/Paths.h"

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace engine::models {
namespace {

namespace fs = std::filesystem;
using engine::assets::phrc::BinaryReader;
using engine::assets::phrc::BinaryWriter;
using engine::assets::phrc::Chunk;
using engine::assets::phrc::Dependency;
using engine::assets::phrc::Document;
using engine::models::CachedTextureRgba;
using engine::models::MeshData;

constexpr std::uint32_t kOldestReadableSchemaVersion = 1u;
constexpr std::uint32_t kSchemaVersion = 3u;
constexpr std::uint32_t kMaxArrayEntries = 64u * 1024u * 1024u;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

std::string lowerHex64(std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (std::size_t index = 0u; index < out.size(); ++index) {
        const std::size_t reverseIndex = out.size() - 1u - index;
        out[reverseIndex] = kDigits[value & 0x0full];
        value >>= 4u;
    }
    return out;
}

std::string stableSourceIdentity(const std::string& sourceModelPath) {
    const fs::path source =
        fs::path(sourceModelPath).lexically_normal();
    if (!source.is_absolute()) {
        return source.generic_string();
    }
    const auto relativeWithin = [](
        const fs::path& candidate,
        const fs::path& root,
        fs::path& outRelative) {
        const fs::path relative = candidate.lexically_relative(root);
        if (relative.empty() || relative.is_absolute()) return false;
        const auto first = relative.begin();
        if (first != relative.end() && *first == "..") return false;
        outRelative = relative;
        return true;
    };

    std::error_code errorCode;
    const fs::path dataRoot = fs::absolute(
        engine::paths::dataRoot(), errorCode).lexically_normal();
    fs::path relative;
    if (!errorCode && relativeWithin(source, dataRoot, relative)) {
        return relative.generic_string();
    }
    errorCode.clear();
    const fs::path assetRoot = fs::absolute(
        engine::paths::assetRoot(), errorCode).lexically_normal();
    if (!errorCode && relativeWithin(source, assetRoot, relative)) {
        return (fs::path("assets") / relative).generic_string();
    }
    return source.generic_string();
}

std::string safeStem(const std::string& sourceModelPath) {
    std::string stem = fs::path(sourceModelPath).stem().string();
    if (stem.empty()) {
        stem = "object";
    }
    for (char& character : stem) {
        const bool accepted =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_';
        if (!accepted) {
            character = '_';
        }
    }
    return stem;
}

bool readFile(
    const fs::path& path,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return fail(
            outError,
            "Could not open Phlosion resource: " + path.string());
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        return fail(
            outError,
            "Could not measure Phlosion resource: " + path.string());
    }
    out.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    }
    if (!input) {
        return fail(
            outError,
            "Could not read Phlosion resource: " + path.string());
    }
    return true;
}

bool writeFile(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string* outError) {
    std::error_code errorCode;
    fs::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        return fail(
            outError,
            "Could not create Phlosion output directory: " +
                errorCode.message());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail(
            outError,
            "Could not create Phlosion resource: " + path.string());
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        return fail(
            outError,
            "Could not write Phlosion resource: " + path.string());
    }
    return true;
}

bool isSafeRelativeAssetId(std::string_view assetId) {
    if (assetId.empty()) return false;
    const fs::path path(assetId);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const fs::path& component : path) {
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

struct StagingDirectoryCleanup {
    fs::path path;
    bool active = true;

    ~StagingDirectoryCleanup() {
        if (!active) return;
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

bool publishDirectoryAtomically(
    const fs::path& staging,
    const fs::path& destination,
    std::string* outError) {
    const auto nonce = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const fs::path backup =
        destination.string() + ".backup." + std::to_string(nonce);
    std::error_code errorCode;
    const bool hadExisting = fs::exists(destination, errorCode);
    if (errorCode) {
        return fail(
            outError,
            "Could not inspect existing Phlosion object: " +
                errorCode.message());
    }
    if (hadExisting) {
        if (!fs::is_directory(destination, errorCode) || errorCode) {
            return fail(
                outError,
                "Existing Phlosion object path is not a directory: " +
                    destination.string());
        }
        fs::rename(destination, backup, errorCode);
        if (errorCode) {
            return fail(
                outError,
                "Could not preserve existing Phlosion object: " +
                    errorCode.message());
        }
    }
    fs::rename(staging, destination, errorCode);
    if (errorCode) {
        const std::string publishFailure = errorCode.message();
        if (hadExisting) {
            std::error_code restoreError;
            fs::rename(backup, destination, restoreError);
            if (restoreError) {
                return fail(
                    outError,
                    "Could not publish Phlosion object (" + publishFailure +
                        ") or restore its backup (" +
                        restoreError.message() + "). Backup remains at " +
                        backup.string());
            }
        }
        return fail(
            outError,
            "Could not publish Phlosion object: " + publishFailure);
    }
    if (hadExisting) {
        std::error_code ignored;
        fs::remove_all(backup, ignored);
    }
    return true;
}

bool writeDocument(
    const fs::path& path,
    const Document& document,
    std::uint64_t& outContentHash,
    std::uint64_t& outFileBytes,
    std::string* outError) {
    std::vector<std::uint8_t> bytes;
    if (!engine::assets::phrc::encode(document, bytes, outError) ||
        !writeFile(path, bytes, outError)) {
        return false;
    }
    Document check;
    if (!engine::assets::phrc::decode(bytes, check, outError)) {
        return false;
    }
    outContentHash = check.contentHash;
    outFileBytes = bytes.size();
    return true;
}

bool readDocument(
    const fs::path& path,
    std::string_view expectedMagic,
    Document& out,
    std::string* outError) {
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes, outError) ||
        !engine::assets::phrc::decode(bytes, out, outError)) {
        return false;
    }
    if (out.magic != engine::assets::phrc::magic(expectedMagic) ||
        out.schemaVersion < kOldestReadableSchemaVersion ||
        out.schemaVersion > kSchemaVersion) {
        return fail(
            outError,
            "Phlosion resource has an unexpected type or schema: " +
                path.string());
    }
    return true;
}

template <typename T, typename WriteValue>
bool writeVector(
    BinaryWriter& writer,
    const std::vector<T>& values,
    WriteValue writeValue,
    std::string* outError) {
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        return fail(outError, "Phlosion array is too large to serialize.");
    }
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const T& value : values) {
        writeValue(value);
    }
    return true;
}

template <typename T, typename ReadValue>
bool readVector(
    BinaryReader& reader,
    std::vector<T>& values,
    ReadValue readValue,
    std::string* outError,
    std::uint32_t maxEntries = kMaxArrayEntries) {
    std::uint32_t count = 0u;
    if (!reader.u32(count) || count > maxEntries) {
        return fail(outError, "Phlosion array count is invalid.");
    }
    values.clear();
    values.resize(count);
    for (T& value : values) {
        if (!readValue(value)) {
            return fail(outError, "Phlosion array payload is truncated.");
        }
    }
    return true;
}

void writeVec2(BinaryWriter& writer, const glm::vec2& value) {
    writer.f32(value.x);
    writer.f32(value.y);
}

void writeVec3(BinaryWriter& writer, const glm::vec3& value) {
    writer.f32(value.x);
    writer.f32(value.y);
    writer.f32(value.z);
}

void writeVec4(BinaryWriter& writer, const glm::vec4& value) {
    writer.f32(value.x);
    writer.f32(value.y);
    writer.f32(value.z);
    writer.f32(value.w);
}

bool readVec2(BinaryReader& reader, glm::vec2& value) {
    return reader.f32(value.x) &&
        reader.f32(value.y);
}

bool readVec3(BinaryReader& reader, glm::vec3& value) {
    return reader.f32(value.x) &&
        reader.f32(value.y) &&
        reader.f32(value.z);
}

bool readVec4(BinaryReader& reader, glm::vec4& value) {
    return reader.f32(value.x) &&
        reader.f32(value.y) &&
        reader.f32(value.z) &&
        reader.f32(value.w);
}

void writeMat4(BinaryWriter& writer, const glm::mat4& value) {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            writer.f32(value[column][row]);
        }
    }
}

bool readMat4(BinaryReader& reader, glm::mat4& value) {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!reader.f32(value[column][row])) return false;
        }
    }
    return true;
}

bool encodeKtx2(
    const CachedTextureRgba& texture,
    bool srgb,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    out.clear();
    if (!texture.hasPixels()) {
        return fail(outError, "Cannot encode an empty KTX2 texture.");
    }
    ktxTextureCreateInfo info{};
    info.vkFormat = srgb
        ? VK_FORMAT_R8G8B8A8_SRGB
        : VK_FORMAT_R8G8B8A8_UNORM;
    info.baseWidth = static_cast<ktx_uint32_t>(texture.width);
    info.baseHeight = static_cast<ktx_uint32_t>(texture.height);
    info.baseDepth = 1u;
    info.numDimensions = 2u;
    info.numLevels = 1u;
    info.numLayers = 1u;
    info.numFaces = 1u;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;

    ktxTexture2* rawTexture = nullptr;
    KTX_error_code code = ktxTexture2_Create(
        &info,
        KTX_TEXTURE_CREATE_ALLOC_STORAGE,
        &rawTexture);
    if (code != KTX_SUCCESS || rawTexture == nullptr) {
        return fail(
            outError,
            "KTX2 allocation failed: " +
                std::string(ktxErrorString(code)));
    }
    const auto destroy = [&]() {
        ktxTexture2_Destroy(rawTexture);
    };
    code = ktxTexture_SetImageFromMemory(
        ktxTexture(rawTexture),
        0u,
        0u,
        0u,
        texture.rgba.data(),
        static_cast<ktx_size_t>(
            static_cast<std::uint64_t>(texture.width) *
            static_cast<std::uint64_t>(texture.height) *
            4ull));
    if (code != KTX_SUCCESS) {
        destroy();
        return fail(
            outError,
            "KTX2 image upload failed: " +
                std::string(ktxErrorString(code)));
    }

    ktx_uint8_t* encoded = nullptr;
    ktx_size_t encodedBytes = 0u;
    code = ktxTexture_WriteToMemory(
        ktxTexture(rawTexture),
        &encoded,
        &encodedBytes);
    if (code != KTX_SUCCESS || encoded == nullptr) {
        destroy();
        return fail(
            outError,
            "KTX2 serialization failed: " +
                std::string(ktxErrorString(code)));
    }
    out.assign(encoded, encoded + encodedBytes);
    std::free(encoded);
    destroy();
    return true;
}

bool decodeKtx2(
    const std::vector<std::uint8_t>& bytes,
    CachedTextureRgba& out,
    std::string* outError) {
    ktxTexture2* texture = nullptr;
    const KTX_error_code code = ktxTexture2_CreateFromMemory(
        bytes.data(),
        static_cast<ktx_size_t>(bytes.size()),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &texture);
    if (code != KTX_SUCCESS || texture == nullptr) {
        return fail(
            outError,
            "KTX2 decode failed: " + std::string(ktxErrorString(code)));
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(texture->baseWidth) *
        static_cast<std::uint64_t>(texture->baseHeight) *
        4ull;
    ktxTexture* base = ktxTexture(texture);
    const ktx_size_t available = ktxTexture_GetDataSize(base);
    const ktx_uint8_t* pixels = ktxTexture_GetData(base);
    if (pixels == nullptr ||
        required > static_cast<std::uint64_t>(available) ||
        required > std::numeric_limits<std::size_t>::max()) {
        ktxTexture2_Destroy(texture);
        return fail(outError, "KTX2 RGBA payload is invalid.");
    }
    out.width = static_cast<int>(texture->baseWidth);
    out.height = static_cast<int>(texture->baseHeight);
    out.rgba.assign(
        pixels,
        pixels + static_cast<std::size_t>(required));
    ktxTexture2_Destroy(texture);
    return true;
}

Document dataDocument(
    std::string_view magic,
    const nlohmann::json& manifest,
    std::vector<std::uint8_t> data,
    std::vector<Dependency> dependencies = {}) {
    Document document;
    document.magic = engine::assets::phrc::magic(magic);
    document.schemaVersion = kSchemaVersion;
    document.manifestJson = manifest.dump();
    document.dependencies = std::move(dependencies);
    Chunk chunk;
    chunk.type = engine::assets::phrc::magic("DATA");
    chunk.alignment = 16u;
    chunk.bytes = std::move(data);
    document.chunks.push_back(std::move(chunk));
    return document;
}

bool meshBytes(
    const MeshData& source,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    BinaryWriter writer;
    writer.f32(source.modelScaleFactor);
    writeVec3(writer, source.boundsMin);
    writeVec3(writer, source.boundsMax);
    if (!writeVector(
            writer,
            source.vertices,
            [&](const engine::models::MeshVertex& vertex) {
                writeVec3(writer, vertex.position);
                writeVec3(writer, vertex.normal);
                writeVec4(writer, vertex.tangent);
                writeVec2(writer, vertex.uv);
                writeVec4(writer, vertex.color);
                writer.u16(vertex.j0);
                writer.u16(vertex.j1);
                writer.u16(vertex.j2);
                writer.u16(vertex.j3);
                writer.f32(vertex.w0);
                writer.f32(vertex.w1);
                writer.f32(vertex.w2);
                writer.f32(vertex.w3);
            },
            outError) ||
        !writeVector(
            writer,
            source.indices,
            [&](std::uint32_t value) { writer.u32(value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleSubmesh,
            [&](std::uint16_t value) { writer.u16(value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleBaseColors,
            [&](const glm::vec3& value) { writeVec3(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleOpacity,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleDoubleSided,
            [&](std::uint8_t value) { writer.u8(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMeshIndex,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshIndexOffset,
            [&](std::uint32_t value) { writer.u32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshIndexCount,
            [&](std::uint32_t value) { writer.u32(value); },
            outError) ||
        !writeVector(
            writer,
            source.meshIndexToNode,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleNodeIndex,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.triangleSkinIndex,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.vertexBaseColors,
            [&](const glm::vec3& value) { writeVec3(writer, value); },
            outError)) {
        return false;
    }
    writer.u8(source.hasVertexColor ? 1u : 0u);
    writer.u8(source.hasVertexBaseColor ? 1u : 0u);
    out = writer.take();
    return true;
}

bool readMeshBytes(
    const std::vector<std::uint8_t>& bytes,
    MeshData& out,
    std::string* outError) {
    BinaryReader reader(bytes);
    if (!reader.f32(out.modelScaleFactor) ||
        !readVec3(reader, out.boundsMin) ||
        !readVec3(reader, out.boundsMax) ||
        !readVector(
            reader,
            out.vertices,
            [&](engine::models::MeshVertex& vertex) {
                return readVec3(reader, vertex.position) &&
                    readVec3(reader, vertex.normal) &&
                    readVec4(reader, vertex.tangent) &&
                    readVec2(reader, vertex.uv) &&
                    readVec4(reader, vertex.color) &&
                    reader.u16(vertex.j0) &&
                    reader.u16(vertex.j1) &&
                    reader.u16(vertex.j2) &&
                    reader.u16(vertex.j3) &&
                    reader.f32(vertex.w0) &&
                    reader.f32(vertex.w1) &&
                    reader.f32(vertex.w2) &&
                    reader.f32(vertex.w3);
            },
            outError) ||
        !readVector(
            reader,
            out.indices,
            [&](std::uint32_t& value) { return reader.u32(value); },
            outError) ||
        !readVector(
            reader,
            out.triangleSubmesh,
            [&](std::uint16_t& value) { return reader.u16(value); },
            outError) ||
        !readVector(
            reader,
            out.triangleBaseColors,
            [&](glm::vec3& value) { return readVec3(reader, value); },
            outError) ||
        !readVector(
            reader,
            out.triangleOpacity,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.triangleDoubleSided,
            [&](std::uint8_t& value) { return reader.u8(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMeshIndex,
            [&](int& value) {
                std::int32_t decoded = 0;
                if (!reader.i32(decoded)) return false;
                value = decoded;
                return true;
            },
            outError) ||
        !readVector(
            reader,
            out.submeshIndexOffset,
            [&](std::uint32_t& value) { return reader.u32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshIndexCount,
            [&](std::uint32_t& value) { return reader.u32(value); },
            outError) ||
        !readVector(
            reader,
            out.meshIndexToNode,
            [&](int& value) {
                std::int32_t decoded = 0;
                if (!reader.i32(decoded)) return false;
                value = decoded;
                return true;
            },
            outError) ||
        !readVector(
            reader,
            out.triangleNodeIndex,
            [&](int& value) {
                std::int32_t decoded = 0;
                if (!reader.i32(decoded)) return false;
                value = decoded;
                return true;
            },
            outError) ||
        !readVector(
            reader,
            out.triangleSkinIndex,
            [&](int& value) {
                std::int32_t decoded = 0;
                if (!reader.i32(decoded)) return false;
                value = decoded;
                return true;
            },
            outError) ||
        !readVector(
            reader,
            out.vertexBaseColors,
            [&](glm::vec3& value) { return readVec3(reader, value); },
            outError)) {
        return false;
    }
    std::uint8_t hasVertexColor = 0u;
    std::uint8_t hasVertexBaseColor = 0u;
    if (!reader.u8(hasVertexColor) ||
        !reader.u8(hasVertexBaseColor) ||
        !reader.finished()) {
        return fail(outError, "PHMESH payload has trailing or missing data.");
    }
    out.hasVertexColor = hasVertexColor != 0u;
    out.hasVertexBaseColor = hasVertexBaseColor != 0u;
    return true;
}

bool skeletonBytes(
    const MeshData& source,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    BinaryWriter writer;
    if (!writeVector(
            writer,
            source.nodesDefault,
            [&](const engine::render::model_types::NodeTRS& node) {
                writeVec3(writer, node.t);
                writer.f32(node.r.w);
                writer.f32(node.r.x);
                writer.f32(node.r.y);
                writer.f32(node.r.z);
                writeVec3(writer, node.s);
                writer.u8(node.segmentScaleCompensate ? 1u : 0u);
                writer.u8(node.hasMatrix ? 1u : 0u);
                writeMat4(writer, node.matrix);
            },
            outError) ||
        !writeVector(
            writer,
            source.nodeNames,
            [&](const std::string& value) { writer.string(value); },
            outError) ||
        !writeVector(
            writer,
            source.nodeChildren,
            [&](const std::vector<int>& children) {
                writer.u32(static_cast<std::uint32_t>(children.size()));
                for (int child : children) writer.i32(child);
            },
            outError) ||
        !writeVector(
            writer,
            source.nodeParent,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.nodeMesh,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.nodeSkin,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.sceneRoots,
            [&](int value) { writer.i32(value); },
            outError) ||
        !writeVector(
            writer,
            source.bindNodeGlobals,
            [&](const glm::mat4& value) { writeMat4(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.skins,
            [&](const engine::render::model_types::SkinData& skin) {
                writer.u32(static_cast<std::uint32_t>(skin.joints.size()));
                for (int joint : skin.joints) writer.i32(joint);
                writer.u32(
                    static_cast<std::uint32_t>(skin.inverseBind.size()));
                for (const glm::mat4& matrix : skin.inverseBind) {
                    writeMat4(writer, matrix);
                }
            },
            outError)) {
        return false;
    }
    out = writer.take();
    return true;
}

bool readIntVector(
    BinaryReader& reader,
    std::vector<int>& values,
    std::string* outError) {
    return readVector(
        reader,
        values,
        [&](int& value) {
            std::int32_t decoded = 0;
            if (!reader.i32(decoded)) return false;
            value = decoded;
            return true;
        },
        outError);
}

bool readSkeletonBytes(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t schemaVersion,
    MeshData& out,
    std::string* outError) {
    BinaryReader reader(bytes);
    if (!readVector(
            reader,
            out.nodesDefault,
            [&](engine::render::model_types::NodeTRS& node) {
                std::uint8_t segmentScaleCompensate = 0u;
                std::uint8_t hasMatrix = 0u;
                bool decoded =
                    readVec3(reader, node.t) &&
                    reader.f32(node.r.w) &&
                    reader.f32(node.r.x) &&
                    reader.f32(node.r.y) &&
                    reader.f32(node.r.z) &&
                    readVec3(reader, node.s);
                if (decoded && schemaVersion >= 2u) {
                    decoded = reader.u8(segmentScaleCompensate);
                }
                decoded = decoded && reader.u8(hasMatrix) &&
                    readMat4(reader, node.matrix);
                node.segmentScaleCompensate =
                    segmentScaleCompensate != 0u;
                node.hasMatrix = hasMatrix != 0u;
                return decoded;
            },
            outError) ||
        !readVector(
            reader,
            out.nodeNames,
            [&](std::string& value) { return reader.string(value); },
            outError) ||
        !readVector(
            reader,
            out.nodeChildren,
            [&](std::vector<int>& children) {
                std::uint32_t count = 0u;
                if (!reader.u32(count) || count > kMaxArrayEntries) {
                    return false;
                }
                children.resize(count);
                for (int& child : children) {
                    std::int32_t decoded = 0;
                    if (!reader.i32(decoded)) return false;
                    child = decoded;
                }
                return true;
            },
            outError) ||
        !readIntVector(reader, out.nodeParent, outError) ||
        !readIntVector(reader, out.nodeMesh, outError) ||
        !readIntVector(reader, out.nodeSkin, outError) ||
        !readIntVector(reader, out.sceneRoots, outError) ||
        !readVector(
            reader,
            out.bindNodeGlobals,
            [&](glm::mat4& value) { return readMat4(reader, value); },
            outError) ||
        !readVector(
            reader,
            out.skins,
            [&](engine::render::model_types::SkinData& skin) {
                std::uint32_t jointCount = 0u;
                if (!reader.u32(jointCount) ||
                    jointCount > kMaxArrayEntries) {
                    return false;
                }
                skin.joints.resize(jointCount);
                for (int& joint : skin.joints) {
                    std::int32_t decoded = 0;
                    if (!reader.i32(decoded)) return false;
                    joint = decoded;
                }
                std::uint32_t matrixCount = 0u;
                if (!reader.u32(matrixCount) ||
                    matrixCount > kMaxArrayEntries) {
                    return false;
                }
                skin.inverseBind.resize(matrixCount);
                for (glm::mat4& matrix : skin.inverseBind) {
                    if (!readMat4(reader, matrix)) return false;
                }
                return true;
            },
            outError) ||
        !reader.finished()) {
        return fail(
            outError,
            "PHSKEL payload has trailing, missing, or invalid data.");
    }
    return true;
}

bool animationBytes(
    const MeshData& source,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    BinaryWriter writer;
    if (!writeVector(
            writer,
            source.animations,
            [&](const engine::render::model_types::AnimationClip& clip) {
                writer.string(clip.name);
                writer.f32(clip.durationSec);
                writer.u32(
                    static_cast<std::uint32_t>(clip.samplers.size()));
                for (const auto& sampler : clip.samplers) {
                    writer.u32(
                        static_cast<std::uint32_t>(
                            sampler.inputs.size()));
                    for (float value : sampler.inputs) writer.f32(value);
                    writer.u32(
                        static_cast<std::uint32_t>(
                            sampler.outputs.size()));
                    for (const glm::vec4& value : sampler.outputs) {
                        writeVec4(writer, value);
                    }
                    writer.string(sampler.interpolation);
                    writer.u8(sampler.isVec4 ? 1u : 0u);
                }
                writer.u32(
                    static_cast<std::uint32_t>(clip.channels.size()));
                for (const auto& channel : clip.channels) {
                    writer.i32(channel.samplerIndex);
                    writer.i32(channel.targetNode);
                    writer.u8(static_cast<std::uint8_t>(channel.path));
                }
            },
            outError)) {
        return false;
    }
    out = writer.take();
    return true;
}

bool readAnimationBytes(
    const std::vector<std::uint8_t>& bytes,
    MeshData& out,
    std::string* outError) {
    BinaryReader reader(bytes);
    if (!readVector(
            reader,
            out.animations,
            [&](engine::render::model_types::AnimationClip& clip) {
                if (!reader.string(clip.name) ||
                    !reader.f32(clip.durationSec)) {
                    return false;
                }
                std::uint32_t samplerCount = 0u;
                if (!reader.u32(samplerCount) ||
                    samplerCount > kMaxArrayEntries) {
                    return false;
                }
                clip.samplers.resize(samplerCount);
                for (auto& sampler : clip.samplers) {
                    std::uint32_t inputCount = 0u;
                    if (!reader.u32(inputCount) ||
                        inputCount > kMaxArrayEntries) {
                        return false;
                    }
                    sampler.inputs.resize(inputCount);
                    for (float& value : sampler.inputs) {
                        if (!reader.f32(value)) return false;
                    }
                    std::uint32_t outputCount = 0u;
                    if (!reader.u32(outputCount) ||
                        outputCount > kMaxArrayEntries) {
                        return false;
                    }
                    sampler.outputs.resize(outputCount);
                    for (glm::vec4& value : sampler.outputs) {
                        if (!readVec4(reader, value)) return false;
                    }
                    std::uint8_t isVec4 = 0u;
                    if (!reader.string(sampler.interpolation) ||
                        !reader.u8(isVec4)) {
                        return false;
                    }
                    sampler.isVec4 = isVec4 != 0u;
                }
                std::uint32_t channelCount = 0u;
                if (!reader.u32(channelCount) ||
                    channelCount > kMaxArrayEntries) {
                    return false;
                }
                clip.channels.resize(channelCount);
                for (auto& channel : clip.channels) {
                    std::int32_t samplerIndex = 0;
                    std::int32_t targetNode = 0;
                    std::uint8_t path = 0u;
                    if (!reader.i32(samplerIndex) ||
                        !reader.i32(targetNode) ||
                        !reader.u8(path) ||
                        path > static_cast<std::uint8_t>(
                            engine::render::model_types::ChannelPath::Scale)) {
                        return false;
                    }
                    channel.samplerIndex = samplerIndex;
                    channel.targetNode = targetNode;
                    channel.path =
                        static_cast<engine::render::model_types::ChannelPath>(path);
                }
                return true;
            },
            outError) ||
        !reader.finished()) {
        return fail(
            outError,
            "PHANIM payload has trailing, missing, or invalid data.");
    }
    return true;
}

struct TextureReference {
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    int minF = 9729;
    int magF = 9729;
    std::string path;
};

using TextureReferenceSet = std::vector<TextureReference>;

struct MaterialTextureReferences {
    TextureReferenceSet base;
    TextureReferenceSet emissionBase;
    TextureReferenceSet normal;
    TextureReferenceSet metallicRoughness;
    TextureReferenceSet occlusion;
    TextureReferenceSet emissive;
    TextureReferenceSet environment;
};

void writeTextureReferences(
    BinaryWriter& writer,
    const TextureReferenceSet& references) {
    writer.u32(static_cast<std::uint32_t>(references.size()));
    for (const TextureReference& reference : references) {
        writer.i32(reference.width);
        writer.i32(reference.height);
        writer.i32(reference.wrapS);
        writer.i32(reference.wrapT);
        writer.i32(reference.minF);
        writer.i32(reference.magF);
        writer.string(reference.path);
    }
}

bool readTextureReferences(
    BinaryReader& reader,
    TextureReferenceSet& references) {
    std::uint32_t count = 0u;
    if (!reader.u32(count) || count > kMaxArrayEntries) return false;
    references.resize(count);
    for (TextureReference& reference : references) {
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int32_t wrapS = 0;
        std::int32_t wrapT = 0;
        std::int32_t minF = 0;
        std::int32_t magF = 0;
        if (!reader.i32(width) ||
            !reader.i32(height) ||
            !reader.i32(wrapS) ||
            !reader.i32(wrapT) ||
            !reader.i32(minF) ||
            !reader.i32(magF) ||
            !reader.string(reference.path)) {
            return false;
        }
        reference.width = width;
        reference.height = height;
        reference.wrapS = wrapS;
        reference.wrapT = wrapT;
        reference.minF = minF;
        reference.magF = magF;
    }
    return true;
}

bool materialBytes(
    const MeshData& source,
    const MaterialTextureReferences& textures,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    BinaryWriter writer;
    if (!writeVector(
            writer,
            source.submeshBaseColors,
            [&](const glm::vec4& value) { writeVec4(writer, value); },
            outError)) {
        return false;
    }
    writeTextureReferences(writer, textures.base);
    writeTextureReferences(writer, textures.normal);
    writeTextureReferences(writer, textures.metallicRoughness);
    writeTextureReferences(writer, textures.occlusion);
    writeTextureReferences(writer, textures.emissive);
    if (!writeVector(
            writer,
            source.submeshAlphaMode,
            [&](std::uint8_t value) { writer.u8(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshAlphaCutoff,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshNormalScale,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMetallicFactor,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshRoughnessFactor,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshOcclusionStrength,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshEmissiveFactors,
            [&](const glm::vec3& value) { writeVec3(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialModes,
            [&](std::uint8_t value) { writer.u8(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialFlags,
            [&](float value) { writer.f32(value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialParams0,
            [&](const glm::vec4& value) { writeVec4(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialParams1,
            [&](const glm::vec4& value) { writeVec4(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialParams2,
            [&](const glm::vec4& value) { writeVec4(writer, value); },
            outError) ||
        !writeVector(
            writer,
            source.submeshMaterialParams3,
            [&](const glm::vec4& value) { writeVec4(writer, value); },
            outError)) {
        return false;
    }
    // PHMAT v3 appends the optional authored environment texture set after
    // every v1/v2 field so older cooked objects remain readable.
    writeTextureReferences(writer, textures.environment);
    if (std::any_of(textures.emissionBase.begin(), textures.emissionBase.end(),
                    [](const auto& texture) { return !texture.path.empty(); })) {
        writeTextureReferences(writer, textures.emissionBase);
    }
    out = writer.take();
    return true;
}

bool decodeReferencedTexture(
    const fs::path& materialDirectory,
    const TextureReference& reference,
    const std::map<std::string, std::uint64_t>& expectedHashes,
    CachedTextureRgba& out,
    std::string* outError) {
    out.width = reference.width;
    out.height = reference.height;
    out.wrapS = reference.wrapS;
    out.wrapT = reference.wrapT;
    out.minF = reference.minF;
    out.magF = reference.magF;
    if (reference.path.empty()) {
        out.rgba.clear();
        return true;
    }
    fs::path dependencyPath;
    std::vector<std::uint8_t> bytes;
    if (!texture_dependency_store::readDependency(
            materialDirectory,
            reference.path,
            dependencyPath,
            bytes,
            outError)) {
        return false;
    }
    const auto expected = expectedHashes.find(reference.path);
    if (expected == expectedHashes.end() ||
        engine::assets::phrc::contentHash64(bytes) != expected->second) {
        return fail(
            outError,
            "KTX2 dependency hash mismatch: " + reference.path);
    }
    const int wrapS = out.wrapS;
    const int wrapT = out.wrapT;
    const int minF = out.minF;
    const int magF = out.magF;
    if (!decodeKtx2(bytes, out, outError)) {
        return false;
    }
    out.wrapS = wrapS;
    out.wrapT = wrapT;
    out.minF = minF;
    out.magF = magF;
    if (out.width != reference.width || out.height != reference.height) {
        return fail(outError, "KTX2 dimensions differ from PHMAT metadata.");
    }
    return true;
}

bool decodeTextureSet(
    const fs::path& materialDirectory,
    const TextureReferenceSet& references,
    const std::map<std::string, std::uint64_t>& expectedHashes,
    std::vector<CachedTextureRgba>& out,
    std::string* outError) {
    out.resize(references.size());
    for (std::size_t index = 0u; index < references.size(); ++index) {
        if (!decodeReferencedTexture(
                materialDirectory,
                references[index],
                expectedHashes,
                out[index],
                outError)) {
            return false;
        }
    }
    return true;
}

bool readMaterialBytes(
    const std::vector<std::uint8_t>& bytes,
    const fs::path& materialDirectory,
    const std::map<std::string, std::uint64_t>& expectedHashes,
    MeshData& out,
    std::string* outError) {
    BinaryReader reader(bytes);
    MaterialTextureReferences textures;
    if (!readVector(
            reader,
            out.submeshBaseColors,
            [&](glm::vec4& value) { return readVec4(reader, value); },
            outError) ||
        !readTextureReferences(reader, textures.base) ||
        !readTextureReferences(reader, textures.normal) ||
        !readTextureReferences(reader, textures.metallicRoughness) ||
        !readTextureReferences(reader, textures.occlusion) ||
        !readTextureReferences(reader, textures.emissive) ||
        !readVector(
            reader,
            out.submeshAlphaMode,
            [&](std::uint8_t& value) { return reader.u8(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshAlphaCutoff,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshNormalScale,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMetallicFactor,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshRoughnessFactor,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshOcclusionStrength,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshEmissiveFactors,
            [&](glm::vec3& value) { return readVec3(reader, value); },
            outError)) {
        return fail(
            outError,
            "PHMAT payload has trailing, missing, or invalid data.");
    }

    // PHMAT v1 ended after emissive factors.  The native runtime material
    // contract is an append-only extension so existing cooked objects remain
    // loadable and default to the regular PBR model path.
    if (reader.finished()) {
        const std::size_t materialCount = out.submeshBaseColors.size();
        out.submeshMaterialModes.assign(materialCount, 2u);
        out.submeshMaterialFlags.assign(materialCount, 0.0f);
        out.submeshMaterialParams0.assign(materialCount, glm::vec4(0.0f));
        out.submeshMaterialParams1.assign(materialCount, glm::vec4(0.0f));
        out.submeshMaterialParams2.assign(materialCount, glm::vec4(0.0f));
        out.submeshMaterialParams3.assign(materialCount, glm::vec4(0.0f));
    } else if (
        !readVector(
            reader,
            out.submeshMaterialModes,
            [&](std::uint8_t& value) { return reader.u8(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMaterialFlags,
            [&](float& value) { return reader.f32(value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMaterialParams0,
            [&](glm::vec4& value) { return readVec4(reader, value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMaterialParams1,
            [&](glm::vec4& value) { return readVec4(reader, value); },
            outError)) {
        return fail(
            outError,
            "PHMAT native material extension is invalid.");
    } else if (reader.finished()) {
        const std::size_t materialCount = out.submeshBaseColors.size();
        out.submeshMaterialParams2.assign(materialCount, glm::vec4(0.0f));
        out.submeshMaterialParams3.assign(materialCount, glm::vec4(0.0f));
    } else if (
        !readVector(
            reader,
            out.submeshMaterialParams2,
            [&](glm::vec4& value) { return readVec4(reader, value); },
            outError) ||
        !readVector(
            reader,
            out.submeshMaterialParams3,
            [&](glm::vec4& value) { return readVec4(reader, value); },
            outError)) {
        return fail(
            outError,
            "PHMAT extended native material payload is invalid.");
    }
    if (!reader.finished() &&
        !readTextureReferences(reader, textures.environment)) {
        return fail(
            outError,
            "PHMAT authored environment texture extension is invalid.");
    }
    if (!reader.finished() &&
        (!readTextureReferences(reader, textures.emissionBase) ||
         textures.emissionBase.size() != out.submeshBaseColors.size())) {
        return fail(outError, "PHMAT clip emission base texture extension is invalid.");
    }
    if (!reader.finished()) {
        return fail(
            outError,
            "PHMAT payload has trailing data.");
    }
    return
        decodeTextureSet(
            materialDirectory,
            textures.base,
            expectedHashes,
            out.submeshBaseTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.normal,
            expectedHashes,
            out.submeshNormalTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.metallicRoughness,
            expectedHashes,
            out.submeshMetallicRoughnessTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.occlusion,
            expectedHashes,
            out.submeshOcclusionTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.emissive,
            expectedHashes,
            out.submeshEmissiveTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.environment,
            expectedHashes,
            out.submeshEnvironmentTextures,
            outError) &&
        decodeTextureSet(
            materialDirectory,
            textures.emissionBase,
            expectedHashes,
            out.submeshEmissionBaseTextures,
            outError);
}

bool dependencyHash(
    const Document& owner,
    const std::string& assetId,
    std::uint64_t& outHash) {
    const auto found = std::find_if(
        owner.dependencies.begin(),
        owner.dependencies.end(),
        [&](const Dependency& dependency) {
            return dependency.assetId == assetId;
        });
    if (found == owner.dependencies.end()) return false;
    outHash = found->expectedContentHash;
    return true;
}

bool loadChildDocument(
    const fs::path& directory,
    const Document& phlo,
    const std::string& relativePath,
    std::string_view magic,
    Document& out,
    std::string* outError) {
    if (!isSafeRelativeAssetId(relativePath)) {
        return fail(
            outError,
            "PHLO child dependency has an unsafe asset ID: " +
                relativePath);
    }
    if (!readDocument(directory / relativePath, magic, out, outError)) {
        return false;
    }
    std::uint64_t expectedHash = 0u;
    if (!dependencyHash(phlo, relativePath, expectedHash) ||
        expectedHash != out.contentHash) {
        return fail(
            outError,
            "PHLO dependency hash mismatch: " + relativePath);
    }
    return true;
}

const std::vector<std::uint8_t>* dataChunk(
    const Document& document,
    std::string* outError) {
    const Chunk* chunk = engine::assets::phrc::findChunk(document, "DATA");
    if (chunk == nullptr) {
        fail(outError, "Phlosion typed resource has no DATA chunk.");
        return nullptr;
    }
    return &chunk->bytes;
}

} // namespace

std::string objectPathForModel(
    const std::string& sourceModelPath,
    const std::string& cookedRoot) {
    const std::string stem = safeStem(sourceModelPath);
    const std::string normalizedSource =
        stableSourceIdentity(sourceModelPath);
    const std::uint64_t sourceIdHash =
        engine::assets::phrc::contentHash64(
            normalizedSource.data(),
            normalizedSource.size());
    const std::string uniqueDirectory =
        stem + "-" + lowerHex64(sourceIdHash);
    return (
        fs::path(cookedRoot) /
        "objects" /
        uniqueDirectory /
        (stem + ".phlo")).generic_string();
}

bool cookModelObject(
    const std::string& sourceModelPath,
    const MeshData& source,
    const std::string& cookedRoot,
    std::string_view prefabKind,
    ModelCookStats& outStats,
    std::string* outError) {
    outStats = ModelCookStats{};
    if (prefabKind.empty()) {
        return fail(outError, "PHLO prefab kind must not be empty.");
    }
    const std::string stem = safeStem(sourceModelPath);
    const fs::path targetObjectPath =
        objectPathForModel(sourceModelPath, cookedRoot);
    const fs::path targetDirectory = targetObjectPath.parent_path();
    const auto stagingNonce = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const fs::path directory =
        targetDirectory.string() + ".partial." +
        std::to_string(stagingNonce);
    const fs::path objectPath = directory / targetObjectPath.filename();
    StagingDirectoryCleanup stagingCleanup{directory};

    std::error_code errorCode;
    fs::create_directories(directory, errorCode);
    if (errorCode) {
        return fail(
            outError,
            "Could not create Phlosion object staging directory: " +
                errorCode.message());
    }
    if (fs::exists(sourceModelPath, errorCode) && !errorCode) {
        outStats.sourceBytes =
            static_cast<std::uint64_t>(
                fs::file_size(sourceModelPath, errorCode));
        if (errorCode) outStats.sourceBytes = 0u;
    }

    std::vector<std::uint8_t> meshData;
    std::vector<std::uint8_t> skeletonData;
    std::vector<std::uint8_t> animationData;
    if (!meshBytes(source, meshData, outError) ||
        !skeletonBytes(source, skeletonData, outError) ||
        !animationBytes(source, animationData, outError)) {
        return false;
    }

    std::vector<Dependency> materialDependencies;
    MaterialTextureReferences textureReferences;
    std::map<std::string, std::string> textureFilesByKey;

    const auto cookTextureSet = [&](
        const std::vector<CachedTextureRgba>& sourceTextures,
        std::string_view role,
        bool srgb,
        TextureReferenceSet& outReferences) -> bool {
        outReferences.reserve(sourceTextures.size());
        for (std::size_t materialIndex = 0u;
             materialIndex < sourceTextures.size();
             ++materialIndex) {
            const CachedTextureRgba& texture = sourceTextures[materialIndex];
            TextureReference reference;
            reference.width = texture.width;
            reference.height = texture.height;
            reference.wrapS = texture.wrapS;
            reference.wrapT = texture.wrapT;
            reference.minF = texture.minF;
            reference.magF = texture.magF;
            if (texture.hasPixels()) {
                BinaryWriter semanticWriter;
                semanticWriter.string("desktop-rgba8");
                semanticWriter.string(std::string(role));
                semanticWriter.u8(srgb ? 1u : 0u);
                semanticWriter.i32(texture.width);
                semanticWriter.i32(texture.height);
                semanticWriter.i32(texture.wrapS);
                semanticWriter.i32(texture.wrapT);
                semanticWriter.i32(texture.minF);
                semanticWriter.i32(texture.magF);
                semanticWriter.u8(
                    materialIndex < source.submeshMaterialModes.size()
                    ? source.submeshMaterialModes[materialIndex]
                    : 2u);
                semanticWriter.f32(
                    materialIndex < source.submeshMaterialFlags.size()
                    ? source.submeshMaterialFlags[materialIndex]
                    : 0.0f);
                const std::vector<std::uint8_t> semanticBytes =
                    semanticWriter.take();
                const std::uint64_t semanticHash =
                    engine::assets::phrc::contentHash64(semanticBytes);
                std::vector<std::uint8_t> ktxBytes;
                if (!encodeKtx2(texture, srgb, ktxBytes, outError)) {
                    return false;
                }
                const std::string assetId =
                    texture_dependency_store::sharedAssetId(
                        ktxBytes, semanticHash);
                const auto existing = textureFilesByKey.find(assetId);
                if (existing != textureFilesByKey.end()) {
                    reference.path = existing->second;
                } else {
                    reference.path = assetId;
                    if (!texture_dependency_store::publishShared(
                            fs::path(cookedRoot),
                            reference.path,
                            ktxBytes,
                            semanticHash,
                            outError)) {
                        return false;
                    }
                    materialDependencies.push_back(
                        Dependency{
                            reference.path,
                            engine::assets::phrc::contentHash64(ktxBytes),
                            engine::assets::phrc::kDependencyRequired});
                    textureFilesByKey.emplace(assetId, reference.path);
                    outStats.cookedBytes += ktxBytes.size();
                    ++outStats.textureCount;
                }
            }
            outReferences.push_back(std::move(reference));
        }
        return true;
    };

    if (!cookTextureSet(
            source.submeshBaseTextures,
            "base",
            true,
            textureReferences.base) ||
        !cookTextureSet(
            source.submeshNormalTextures,
            "normal",
            false,
            textureReferences.normal) ||
        !cookTextureSet(
            source.submeshMetallicRoughnessTextures,
            "metalrough",
            false,
            textureReferences.metallicRoughness) ||
        !cookTextureSet(
            source.submeshOcclusionTextures,
            "occlusion",
            false,
            textureReferences.occlusion) ||
        !cookTextureSet(
            source.submeshEmissiveTextures,
            "emissive",
            true,
            textureReferences.emissive) ||
        !cookTextureSet(
            source.submeshEnvironmentTextures,
            "environment",
            false,
            textureReferences.environment) ||
        !cookTextureSet(
            source.submeshEmissionBaseTextures,
            "emission-base",
            true,
            textureReferences.emissionBase)) {
        return false;
    }

    std::vector<std::uint8_t> materialData;
    if (!materialBytes(
            source,
            textureReferences,
            materialData,
            outError)) {
        return false;
    }

    const auto counts = nlohmann::json{
        {"vertices", source.vertices.size()},
        {"indices", source.indices.size()},
        {"submeshes", source.submeshIndexCount.size()},
        {"nodes", source.nodesDefault.size()},
        {"skins", source.skins.size()},
        {"animations", source.animations.size()}};
    const nlohmann::json commonManifest{
        {"schema_version", kSchemaVersion},
        {"container", "PHRC-1"},
        {"cooker", "PhlosionForge"},
        {"target_profile", "desktop-rgba8"},
        {"source_model", fs::path(sourceModelPath).generic_string()},
        {"object_id", stem}};

    struct ResourceResult {
        std::string path;
        std::uint64_t hash = 0u;
    };
    std::array<ResourceResult, 4> resources{{
        {"model.phmesh", 0u},
        {"model.phskel", 0u},
        {"model.phanim", 0u},
        {"model.phmat", 0u}}};
    std::uint64_t fileBytes = 0u;
    nlohmann::json meshManifest = commonManifest;
    meshManifest["root_type"] = "Mesh";
    meshManifest["counts"] = counts;
    if (!writeDocument(
            directory / resources[0].path,
            dataDocument("PHME", meshManifest, std::move(meshData)),
            resources[0].hash,
            fileBytes,
            outError)) {
        return false;
    }
    outStats.cookedBytes += fileBytes;

    nlohmann::json skeletonManifest = commonManifest;
    skeletonManifest["root_type"] = "Skeleton";
    skeletonManifest["node_count"] = source.nodesDefault.size();
    skeletonManifest["skin_count"] = source.skins.size();
    if (!writeDocument(
            directory / resources[1].path,
            dataDocument(
                "PHSK",
                skeletonManifest,
                std::move(skeletonData)),
            resources[1].hash,
            fileBytes,
            outError)) {
        return false;
    }
    outStats.cookedBytes += fileBytes;

    nlohmann::json animationManifest = commonManifest;
    animationManifest["root_type"] = "AnimationSet";
    animationManifest["clip_count"] = source.animations.size();
    if (!source.animatedBillboards.empty()) {
        animationManifest["animated_billboards"] = encodeModelBillboards(source);
    }
    nlohmann::json visibilityClips = nlohmann::json::array();
    for (std::size_t clipIndex = 0u;
         clipIndex < source.animationMeshVisibility.size();
         ++clipIndex) {
        const auto& tracks =
            source.animationMeshVisibility[clipIndex];
        if (tracks.empty()) continue;
        nlohmann::json trackRecords = nlohmann::json::array();
        for (const auto& track : tracks) {
            trackRecords.push_back({
                {"node", track.nodeIndex},
                {"source_frame_rate", track.sourceFrameRate},
                {"inputs", track.inputs},
                {"values", track.values}});
        }
        visibilityClips.push_back({
            {"clip", clipIndex},
            {"tracks", std::move(trackRecords)}});
    }
    if (!visibilityClips.empty()) {
        animationManifest["mesh_visibility"] =
            std::move(visibilityClips);
    }
    const auto serializeMaterialTrack =
        [](const engine::models::ContinuousMaterialAnimationTrack& track) {
            nlohmann::json components = nlohmann::json::array();
            for (const auto& component : track.components) {
                nlohmann::json keys = nlohmann::json::array();
                for (const auto& key : component.keys) {
                    keys.push_back({key.timeSec, key.value});
                }
                components.push_back(std::move(keys));
            }
            return nlohmann::json{
                {"submesh", track.submeshIndex},
                {"parameter",
                 track.parameter == engine::models::MaterialAnimationParameter::EmissiveFactor
                     ? "emissive_factor"
                     : track.parameter == engine::models::
                                            MaterialAnimationParameter::
                                                UvScaleOffset3
                     ? "uv_scale_offset3"
                     : "uv_scale_offset"},
                {"duration_seconds", track.durationSec},
                {"source_frame_rate", track.sourceFrameRate},
                {"loop", track.loop},
                {"sampling",
                 track.sampling == engine::models::
                                       MaterialAnimationSampling::
                                           HoldSourceFrame
                     ? "hold_source_frame"
                     : "linear"},
                {"default", {
                     track.defaultValue.x,
                     track.defaultValue.y,
                     track.defaultValue.z,
                     track.defaultValue.w}},
                {"components", std::move(components)}};
        };
    nlohmann::json materialParameterClips = nlohmann::json::array();
    for (std::size_t clipIndex = 0u;
         clipIndex < source.animationMaterialParameters.size();
         ++clipIndex) {
        const auto& tracks =
            source.animationMaterialParameters[clipIndex];
        if (tracks.empty()) continue;
        nlohmann::json trackRecords = nlohmann::json::array();
        for (const auto& track : tracks) {
            trackRecords.push_back(serializeMaterialTrack(track));
        }
        materialParameterClips.push_back({
            {"clip", clipIndex},
            {"tracks", std::move(trackRecords)}});
    }
    if (!materialParameterClips.empty()) {
        animationManifest["material_parameters"] =
            std::move(materialParameterClips);
    }
    nlohmann::json continuousMaterialTracks = nlohmann::json::array();
    for (const auto& track : source.continuousMaterialAnimations) {
        continuousMaterialTracks.push_back(
            serializeMaterialTrack(track));
    }
    if (!continuousMaterialTracks.empty()) {
        animationManifest["continuous_material_parameters"] =
            std::move(continuousMaterialTracks);
    }
    if (!writeDocument(
            directory / resources[2].path,
            dataDocument(
                "PHAN",
                animationManifest,
                std::move(animationData)),
            resources[2].hash,
            fileBytes,
            outError)) {
        return false;
    }
    outStats.cookedBytes += fileBytes;

    nlohmann::json materialManifest = commonManifest;
    materialManifest["root_type"] = "MaterialSet";
    materialManifest["material_count"] =
        source.submeshBaseColors.size();
    materialManifest["texture_payload"] = "KTX2";
    materialManifest["texture_dependency_layout"] =
        "shared-content-addressed-v1";
    const std::size_t expectedTextureDependencyCount =
        materialDependencies.size();
    if (!writeDocument(
            directory / resources[3].path,
            dataDocument(
                "PHMT",
                materialManifest,
                std::move(materialData),
                std::move(materialDependencies)),
            resources[3].hash,
            fileBytes,
            outError)) {
        return false;
    }
    outStats.cookedBytes += fileBytes;

    nlohmann::json phloManifest = commonManifest;
    phloManifest["root_type"] = "Prefab";
    phloManifest["prefab_kind"] = prefabKind;
    phloManifest["resources"] = {
        {"mesh", resources[0].path},
        {"skeleton", resources[1].path},
        {"animations", resources[2].path},
        {"materials", resources[3].path}};
    phloManifest["counts"] = counts;

    Document phlo;
    phlo.magic = engine::assets::phrc::magic("PHLO");
    phlo.schemaVersion = kSchemaVersion;
    phlo.manifestJson = phloManifest.dump();
    for (const ResourceResult& resource : resources) {
        phlo.dependencies.push_back(
            Dependency{
                resource.path,
                resource.hash,
                engine::assets::phrc::kDependencyRequired});
    }
    std::uint64_t phloHash = 0u;
    if (!writeDocument(
            objectPath,
            phlo,
            phloHash,
            fileBytes,
            outError)) {
        return false;
    }
    outStats.cookedBytes += fileBytes;
    std::vector<ModelTextureDependency> verificationDependencies;
    if (!listModelObjectTextureDependencies(
            objectPath.generic_string(),
            verificationDependencies,
            outError) ||
        verificationDependencies.size() != expectedTextureDependencyCount) {
        if (outError && outError->empty()) {
            *outError =
                "Phlosion object dependency count changed during staging.";
        }
        return false;
    }
    if (!publishDirectoryAtomically(
            directory,
            targetDirectory,
            outError)) {
        return false;
    }
    stagingCleanup.active = false;
    return true;
}

bool loadModelObject(
    const std::string& phloPath,
    MeshData& out,
    std::string* outError) {
    Document phlo;
    if (!readDocument(phloPath, "PHLO", phlo, outError)) {
        return false;
    }
    try {
        const nlohmann::json manifest =
            nlohmann::json::parse(phlo.manifestJson);
        const std::uint32_t manifestSchemaVersion =
            manifest.at("schema_version").get<std::uint32_t>();
        if (manifest.at("root_type") != "Prefab" ||
            manifestSchemaVersion < kOldestReadableSchemaVersion ||
            manifestSchemaVersion > kSchemaVersion) {
            return fail(outError, "PHLO prefab manifest is unsupported.");
        }
        const auto& resources = manifest.at("resources");
        const std::string meshPath =
            resources.at("mesh").get<std::string>();
        const std::string skeletonPath =
            resources.at("skeleton").get<std::string>();
        const std::string animationPath =
            resources.at("animations").get<std::string>();
        const std::string materialPath =
            resources.at("materials").get<std::string>();
        const fs::path directory = fs::path(phloPath).parent_path();

        Document mesh;
        Document skeleton;
        Document animation;
        Document material;
        if (!loadChildDocument(
                directory,
                phlo,
                meshPath,
                "PHME",
                mesh,
                outError) ||
            !loadChildDocument(
                directory,
                phlo,
                skeletonPath,
                "PHSK",
                skeleton,
                outError) ||
            !loadChildDocument(
                directory,
                phlo,
                animationPath,
                "PHAN",
                animation,
                outError) ||
            !loadChildDocument(
                directory,
                phlo,
                materialPath,
                "PHMT",
                material,
                outError)) {
            return false;
        }
        const auto* meshData = dataChunk(mesh, outError);
        const auto* skeletonData = dataChunk(skeleton, outError);
        const auto* animationData = dataChunk(animation, outError);
        const auto* materialData = dataChunk(material, outError);
        if (meshData == nullptr ||
            skeletonData == nullptr ||
            animationData == nullptr ||
            materialData == nullptr) {
            return false;
        }
        std::map<std::string, std::uint64_t> textureHashes;
        for (const Dependency& dependency : material.dependencies) {
            textureHashes[dependency.assetId] =
                dependency.expectedContentHash;
        }
        MeshData decoded;
        if (!readMeshBytes(*meshData, decoded, outError) ||
            !readSkeletonBytes(
                *skeletonData,
                skeleton.schemaVersion,
                decoded,
                outError) ||
            !readAnimationBytes(*animationData, decoded, outError) ||
            !readMaterialBytes(
                *materialData,
                directory,
                textureHashes,
                decoded,
                outError)) {
            return false;
        }
        const nlohmann::json animationManifest =
            nlohmann::json::parse(animation.manifestJson);
        decoded.animationMeshVisibility.assign(
            decoded.animations.size(),
            {});
        if (animationManifest.contains("mesh_visibility")) {
            for (const auto& clipRecord :
                 animationManifest.at("mesh_visibility")) {
                const std::size_t clipIndex =
                    clipRecord.at("clip").get<std::size_t>();
                if (clipIndex >=
                    decoded.animationMeshVisibility.size()) {
                    return fail(
                        outError,
                        "PHAN mesh visibility clip index is invalid.");
                }
                auto& tracks =
                    decoded.animationMeshVisibility[clipIndex];
                for (const auto& trackRecord :
                     clipRecord.at("tracks")) {
                    engine::models::MeshVisibilityTrack track;
                    track.nodeIndex =
                        trackRecord.at("node").get<int>();
                    track.sourceFrameRate =
                        trackRecord.value(
                            "source_frame_rate",
                            0.0f);
                    track.inputs =
                        trackRecord.at("inputs")
                            .get<std::vector<float>>();
                    track.values =
                        trackRecord.at("values")
                            .get<std::vector<std::uint8_t>>();
                    if (track.nodeIndex < 0 ||
                        static_cast<std::size_t>(track.nodeIndex) >=
                            decoded.nodesDefault.size() ||
                        track.inputs.empty() ||
                        track.inputs.size() != track.values.size() ||
                        !std::isfinite(track.sourceFrameRate) ||
                        track.sourceFrameRate < 0.0f ||
                        !std::is_sorted(
                            track.inputs.begin(),
                            track.inputs.end()) ||
                        std::any_of(
                            track.values.begin(),
                            track.values.end(),
                            [](std::uint8_t value) {
                                return value > 1u;
                            })) {
                        return fail(
                            outError,
                            "PHAN mesh visibility track is invalid.");
                    }
                    tracks.push_back(std::move(track));
                }
            }
        }
        const auto decodeMaterialTrack =
            [&](const nlohmann::json& trackRecord,
                engine::models::ContinuousMaterialAnimationTrack& track,
                std::string_view kind) {
                track.submeshIndex =
                    trackRecord.at("submesh").get<std::size_t>();
                const std::string parameter =
                    trackRecord.at("parameter").get<std::string>();
                if (parameter == "uv_scale_offset") {
                    track.parameter =
                        engine::models::MaterialAnimationParameter::UvScaleOffset;
                } else if (parameter == "uv_scale_offset3") {
                    track.parameter =
                        engine::models::MaterialAnimationParameter::UvScaleOffset3;
                } else if (parameter == "emissive_factor") {
                    track.parameter = engine::models::MaterialAnimationParameter::EmissiveFactor;
                } else {
                    return fail(
                        outError,
                        "PHAN " + std::string(kind) +
                            " material parameter is invalid.");
                }
                track.durationSec =
                    trackRecord.at("duration_seconds").get<float>();
                track.sourceFrameRate =
                    trackRecord.value("source_frame_rate", 0.0f);
                track.loop = trackRecord.value("loop", false);
                const std::string sampling =
                    trackRecord.value("sampling", std::string{"linear"});
                if (sampling == "linear") {
                    track.sampling = engine::models::
                        MaterialAnimationSampling::Linear;
                } else if (sampling == "hold_source_frame") {
                    track.sampling = engine::models::
                        MaterialAnimationSampling::HoldSourceFrame;
                } else {
                    return fail(
                        outError,
                        "PHAN " + std::string(kind) +
                            " material sampling mode is invalid.");
                }
                const auto& defaultValue = trackRecord.at("default");
                const auto& components = trackRecord.at("components");
                if (track.submeshIndex >= decoded.submeshIndexCount.size() ||
                    !std::isfinite(track.durationSec) ||
                    track.durationSec <= 0.0f ||
                    !std::isfinite(track.sourceFrameRate) ||
                    track.sourceFrameRate < 0.0f ||
                    !defaultValue.is_array() ||
                    defaultValue.size() != 4u ||
                    !components.is_array() ||
                    components.size() != 4u) {
                    return fail(
                        outError,
                        "PHAN " + std::string(kind) +
                            " material track is invalid.");
                }
                for (std::size_t component = 0u; component < 4u; ++component) {
                    track.defaultValue[static_cast<glm::length_t>(component)] =
                        defaultValue.at(component).get<float>();
                    if (!std::isfinite(
                            track.defaultValue[
                                static_cast<glm::length_t>(component)])) {
                        return fail(
                            outError,
                            "PHAN " + std::string(kind) +
                                " material default is invalid.");
                    }
                    float previousTime = -1.0f;
                    for (const auto& keyRecord : components.at(component)) {
                        if (!keyRecord.is_array() || keyRecord.size() != 2u) {
                            return fail(
                                outError,
                                "PHAN " + std::string(kind) +
                                    " material key is invalid.");
                        }
                        engine::models::MaterialAnimationKey key{
                            keyRecord.at(0).get<float>(),
                            keyRecord.at(1).get<float>()};
                        if (!std::isfinite(key.timeSec) ||
                            !std::isfinite(key.value) ||
                            key.timeSec < 0.0f ||
                            key.timeSec < previousTime) {
                            return fail(
                                outError,
                                "PHAN " + std::string(kind) +
                                    " material key order is invalid.");
                        }
                        previousTime = key.timeSec;
                        track.components[component].keys.push_back(key);
                    }
                }
                return true;
            };
        decoded.animationMaterialParameters.assign(
            decoded.animations.size(),
            {});
        if (animationManifest.contains("material_parameters")) {
            for (const auto& clipRecord :
                 animationManifest.at("material_parameters")) {
                const std::size_t clipIndex =
                    clipRecord.at("clip").get<std::size_t>();
                if (clipIndex >=
                    decoded.animationMaterialParameters.size()) {
                    return fail(
                        outError,
                        "PHAN material parameter clip index is invalid.");
                }
                auto& tracks =
                    decoded.animationMaterialParameters[clipIndex];
                for (const auto& trackRecord :
                     clipRecord.at("tracks")) {
                    engine::models::ContinuousMaterialAnimationTrack track;
                    if (!decodeMaterialTrack(
                            trackRecord,
                            track,
                            "clip-bound")) {
                        return false;
                    }
                    tracks.push_back(std::move(track));
                }
            }
        }
        if (animationManifest.contains("continuous_material_parameters")) {
            for (const auto& trackRecord :
                 animationManifest.at("continuous_material_parameters")) {
                engine::models::ContinuousMaterialAnimationTrack track;
                if (!decodeMaterialTrack(
                        trackRecord,
                        track,
                        "continuous")) {
                    return false;
                }
                decoded.continuousMaterialAnimations.push_back(
                    std::move(track));
            }
        }
        if (animationManifest.contains("animated_billboards")) {
            decodeModelBillboards(animationManifest.at("animated_billboards"), decoded);
        }
        // The child resources describe where the data came from, while the
        // prefab path identifies this cooked runtime object. Keep that stable
        // identity on the decoded mesh so shared geometry/material caches do
        // not alias sibling variants that intentionally reuse source geometry
        // but carry different material payloads (for example, alternate surface variants).
        decoded.assetCacheIdentity =
            "phlo:" + fs::path(phloPath).lexically_normal().generic_string();
        out = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Could not load PHLO prefab: " +
                std::string(exception.what()));
    }
}

bool listModelObjectTextureDependencies(
    const std::string& phloPath,
    std::vector<ModelTextureDependency>& out,
    std::string* outError) {
    out.clear();
    Document phlo;
    if (!readDocument(phloPath, "PHLO", phlo, outError)) {
        return false;
    }
    try {
        const nlohmann::json manifest =
            nlohmann::json::parse(phlo.manifestJson);
        const std::string materialPath =
            manifest.at("resources").at("materials").get<std::string>();
        const fs::path directory = fs::path(phloPath).parent_path();
        Document material;
        if (!loadChildDocument(
                directory,
                phlo,
                materialPath,
                "PHMT",
                material,
                outError)) {
            return false;
        }
        std::set<std::string> observed;
        out.reserve(material.dependencies.size());
        for (const Dependency& dependency : material.dependencies) {
            if (!observed.insert(dependency.assetId).second) {
                return fail(
                    outError,
                    "PHMAT repeats texture dependency: " +
                        dependency.assetId);
            }
            fs::path physicalPath;
            std::vector<std::uint8_t> bytes;
            if (!texture_dependency_store::readDependency(
                    directory,
                    dependency.assetId,
                    physicalPath,
                    bytes,
                    outError)) {
                return false;
            }
            if (engine::assets::phrc::contentHash64(bytes) !=
                dependency.expectedContentHash) {
                return fail(
                    outError,
                    "KTX2 dependency hash mismatch: " +
                        dependency.assetId);
            }
            out.push_back(ModelTextureDependency{
                dependency.assetId,
                physicalPath.lexically_normal().generic_string(),
                dependency.expectedContentHash,
                static_cast<std::uint64_t>(bytes.size())});
        }
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Could not inspect PHLO texture dependencies: " +
                std::string(exception.what()));
    }
}

} // namespace engine::models
