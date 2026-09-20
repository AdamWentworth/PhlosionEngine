#include "FixtureData.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace engine::fixture {
namespace {

constexpr std::uint32_t kGeometryMagic = 0x4f454750u; // "PGEO"
constexpr std::uint32_t kTextureMagic = 0x58455450u;  // "PTEX"
constexpr std::uint32_t kFileVersion = 1u;
constexpr std::size_t kGeometryHeaderBytes = 16u;
constexpr std::size_t kTextureHeaderBytes = 16u;

struct GeometryHeader {
    std::uint32_t magic = kGeometryMagic;
    std::uint32_t version = kFileVersion;
    std::uint32_t vertexCount = 0u;
    std::uint32_t indexCount = 0u;
};

struct TextureHeader {
    std::uint32_t magic = kTextureMagic;
    std::uint32_t version = kFileVersion;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

void appendBytes(
    std::vector<std::uint8_t>& out,
    const void* data,
    std::size_t byteCount) {
    const auto* first = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), first, first + byteCount);
}

template <typename T>
void appendValue(std::vector<std::uint8_t>& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    appendBytes(out, &value, sizeof(T));
}

bool readValue(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    void* out,
    std::size_t byteCount) {
    if (offset > bytes.size() || byteCount > bytes.size() - offset) {
        return false;
    }
    std::memcpy(out, bytes.data() + offset, byteCount);
    offset += byteCount;
    return true;
}

template <typename T>
bool readValue(const std::vector<std::uint8_t>& bytes, std::size_t& offset, T& out) {
    return readValue(bytes, offset, &out, sizeof(T));
}

std::vector<std::uint8_t> encodeGeometry(const FixtureObject& object) {
    const GeometryHeader header{
        kGeometryMagic,
        kFileVersion,
        static_cast<std::uint32_t>(object.vertices.size()),
        static_cast<std::uint32_t>(object.indices.size())};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(
        kGeometryHeaderBytes +
        object.vertices.size() * sizeof(FixtureVertex) +
        object.indices.size() * sizeof(std::uint32_t));
    appendValue(bytes, header);
    for (const FixtureVertex& vertex : object.vertices) {
        appendValue(bytes, vertex);
    }
    for (const std::uint32_t index : object.indices) {
        appendValue(bytes, index);
    }
    return bytes;
}

bool decodeGeometry(
    const std::vector<std::uint8_t>& bytes,
    std::vector<FixtureVertex>& outVertices,
    std::vector<std::uint32_t>& outIndices,
    std::string* outError) {
    outVertices.clear();
    outIndices.clear();
    std::size_t offset = 0u;
    GeometryHeader header;
    if (!readValue(bytes, offset, header) ||
        header.magic != kGeometryMagic ||
        header.version != kFileVersion) {
        return fail(outError, "fixture geometry header is invalid");
    }
    const std::size_t expected =
        kGeometryHeaderBytes +
        static_cast<std::size_t>(header.vertexCount) * sizeof(FixtureVertex) +
        static_cast<std::size_t>(header.indexCount) * sizeof(std::uint32_t);
    if (bytes.size() != expected) {
        return fail(outError, "fixture geometry byte count does not match its header");
    }
    outVertices.resize(header.vertexCount);
    for (FixtureVertex& vertex : outVertices) {
        if (!readValue(bytes, offset, vertex)) {
            return fail(outError, "fixture geometry vertex data is truncated");
        }
    }
    outIndices.resize(header.indexCount);
    for (std::uint32_t& index : outIndices) {
        if (!readValue(bytes, offset, index)) {
            return fail(outError, "fixture geometry index data is truncated");
        }
        if (index >= header.vertexCount) {
            return fail(outError, "fixture geometry index is out of range");
        }
    }
    const auto finite = [](float value) {
        return std::isfinite(value);
    };
    for (const FixtureVertex& vertex : outVertices) {
        if (!finite(vertex.x) || !finite(vertex.y) || !finite(vertex.z) ||
            !finite(vertex.u) || !finite(vertex.v) ||
            !finite(vertex.r) || !finite(vertex.g) || !finite(vertex.b) ||
            !finite(vertex.a) || !finite(vertex.nx) || !finite(vertex.ny) ||
            !finite(vertex.nz)) {
            return fail(
                outError,
                "fixture geometry contains a non-finite vertex value");
        }
    }
    return true;
}

std::vector<std::uint8_t> encodeTexture(const FixtureObject& object) {
    const TextureHeader header{
        kTextureMagic,
        kFileVersion,
        static_cast<std::uint32_t>(object.textureWidth),
        static_cast<std::uint32_t>(object.textureHeight)};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kTextureHeaderBytes + object.textureRgba.size());
    appendValue(bytes, header);
    appendBytes(bytes, object.textureRgba.data(), object.textureRgba.size());
    return bytes;
}

bool decodeTexture(
    const std::vector<std::uint8_t>& bytes,
    int& outWidth,
    int& outHeight,
    std::vector<std::uint8_t>& outRgba,
    std::string* outError) {
    outWidth = 0;
    outHeight = 0;
    outRgba.clear();
    std::size_t offset = 0u;
    TextureHeader header;
    if (!readValue(bytes, offset, header) ||
        header.magic != kTextureMagic ||
        header.version != kFileVersion ||
        header.width == 0u ||
        header.height == 0u) {
        return fail(outError, "fixture texture header is invalid");
    }
    const std::size_t pixelBytes =
        static_cast<std::size_t>(header.width) *
        static_cast<std::size_t>(header.height) * 4u;
    if (bytes.size() != kTextureHeaderBytes + pixelBytes) {
        return fail(outError, "fixture texture byte count does not match its header");
    }
    outWidth = static_cast<int>(header.width);
    outHeight = static_cast<int>(header.height);
    outRgba.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(kTextureHeaderBytes),
        bytes.end());
    return true;
}

FixtureVertex makeVertex(
    float x,
    float y,
    float z,
    float u,
    float v,
    const std::array<float, 4>& color) {
    FixtureVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = u;
    vertex.v = v;
    vertex.r = color[0];
    vertex.g = color[1];
    vertex.b = color[2];
    vertex.a = color[3];
    vertex.nx = 0.0f;
    vertex.ny = 0.0f;
    vertex.nz = 1.0f;
    return vertex;
}

FixtureObject makeQuad(
    std::string id,
    std::string role,
    float x0,
    float x1,
    float y0,
    float y1,
    float z,
    int textureWidth,
    int textureHeight,
    std::vector<std::uint8_t> rgba,
    const std::array<float, 4>& color,
    FixtureMaterial material) {
    FixtureObject object;
    object.id = std::move(id);
    object.role = std::move(role);
    object.vertices = {
        makeVertex(x0, y0, z, 0.0f, 0.0f, color),
        makeVertex(x1, y0, z, 1.0f, 0.0f, color),
        makeVertex(x1, y1, z, 1.0f, 1.0f, color),
        makeVertex(x0, y1, z, 0.0f, 1.0f, color)};
    object.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    object.textureWidth = textureWidth;
    object.textureHeight = textureHeight;
    object.textureRgba = std::move(rgba);
    object.material = material;
    return object;
}

std::vector<std::uint8_t> whiteTexture() {
    return std::vector<std::uint8_t>{
        255u, 255u, 255u, 255u,
        255u, 255u, 255u, 255u,
        255u, 255u, 255u, 255u,
        255u, 255u, 255u, 255u};
}

struct Rgba8 {
    std::uint8_t r = 0u;
    std::uint8_t g = 0u;
    std::uint8_t b = 0u;
    std::uint8_t a = 255u;
};

Rgba8 toRgba8(const std::array<float, 3>& linear) {
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(
            std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return Rgba8{channel(linear[0]), channel(linear[1]), channel(linear[2]), 255u};
}

std::vector<std::uint8_t> makeCheckerTexture(
    const std::array<float, 3>& first,
    const std::array<float, 3>& second,
    int cells,
    int cellPixels,
    bool swapPhase) {
    const int size = cells * cellPixels;
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u,
        255u);
    const Rgba8 a = toRgba8(first);
    const Rgba8 b = toRgba8(second);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int cellX = x / cellPixels;
            const int cellY = y / cellPixels;
            const bool even = ((cellX + cellY) % 2) == 0;
            const bool useFirst = even != swapPhase;
            const Rgba8 color = useFirst ? a : b;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                 static_cast<std::size_t>(x)) * 4u;
            rgba[offset + 0u] = color.r;
            rgba[offset + 1u] = color.g;
            rgba[offset + 2u] = color.b;
            rgba[offset + 3u] = color.a;
        }
    }
    return rgba;
}

FixtureObject makeBackgroundObject() {
    FixtureMaterial material;
    return makeQuad(
        "background",
        "background",
        -6.0f,
        6.0f,
        -4.5f,
        4.5f,
        -2.0f,
        2,
        2,
        whiteTexture(),
        {0.05f, 0.60f, 0.08f, 1.0f},
        material);
}

FixtureObject makeBoardObject(std::string_view contentId) {
    FixtureMaterial material;
    if (contentId == "b") {
        return makeQuad(
            "board",
            "board",
            -1.50f,
            1.50f,
            -1.05f,
            1.05f,
            0.0f,
            16,
            16,
            makeCheckerTexture(
                {0.95f, 0.40f, 0.05f},
                {0.95f, 0.90f, 0.05f},
                2,
                8,
                true),
            {1.0f, 1.0f, 1.0f, 1.0f},
            material);
    }
    return makeQuad(
        "board",
        "board",
        -1.30f,
        1.30f,
        -0.90f,
        0.90f,
        0.0f,
        16,
        16,
        makeCheckerTexture(
            {0.90f, 0.05f, 0.85f},
            {0.05f, 0.85f, 0.95f},
            2,
            8,
            false),
        {1.0f, 1.0f, 1.0f, 1.0f},
        material);
}

FixtureObject makeOccluderObject(std::string_view contentId) {
    FixtureMaterial material;
    if (contentId == "b") {
        return makeQuad(
            "occluder",
            "occluder",
            -0.125f,
            0.625f,
            -0.375f,
            0.375f,
            0.5f,
            2,
            2,
            whiteTexture(),
            {0.05f, 0.12f, 0.95f, 1.0f},
            material);
    }
    return makeQuad(
        "occluder",
        "occluder",
        -0.30f,
        0.30f,
        -0.30f,
        0.30f,
        0.5f,
        2,
        2,
        whiteTexture(),
        {0.05f, 0.12f, 0.95f, 1.0f},
        material);
}

FixtureObject makeHiddenObject(std::string_view contentId) {
    FixtureMaterial material;
    if (contentId == "b") {
        return makeQuad(
            "hidden",
            "hidden",
            -0.30f,
            0.80f,
            -0.55f,
            0.55f,
            0.25f,
            2,
            2,
            whiteTexture(),
            {0.02f, 0.12f, 0.03f, 1.0f},
            material);
    }
    return makeQuad(
        "hidden",
        "hidden",
        -0.50f,
        0.50f,
        -0.50f,
        0.50f,
        0.25f,
        2,
        2,
        whiteTexture(),
        {0.02f, 0.12f, 0.03f, 1.0f},
        material);
}

FixtureObject makeBlendObject(std::string_view contentId) {
    FixtureMaterial material;
    material.alphaMode = 2u;
    material.blendMode = 0u;
    material.depthTest = 1u;
    if (contentId == "b") {
        return makeQuad(
            "blend",
            "blend",
            -1.90f,
            -0.90f,
            0.90f,
            1.70f,
            0.5f,
            2,
            2,
            whiteTexture(),
            {0.95f, 0.06f, 0.06f, 0.5f},
            material);
    }
    return makeQuad(
        "blend",
        "blend",
        0.90f,
        1.90f,
        0.90f,
        1.70f,
        0.5f,
        2,
        2,
        whiteTexture(),
        {0.95f, 0.06f, 0.06f, 0.5f},
        material);
}

FixtureObject makeMarkerObject(std::string_view contentId) {
    FixtureMaterial material;
    FixtureObject marker;
    if (contentId == "b") {
        marker = makeQuad(
            "marker",
            "marker",
            -0.13f,
            0.13f,
            -1.48f,
            -1.22f,
            0.6f,
            2,
            2,
            whiteTexture(),
            {1.0f, 1.0f, 1.0f, 1.0f},
            material);
        marker.motion.enabled = true;
        marker.motion.fromFrame = 0;
        marker.motion.toFrame = 30;
        marker.motion.from = {1.20f, 0.0f, 0.0f};
        marker.motion.to = {-1.20f, 0.0f, 0.0f};
        return marker;
    }
    marker = makeQuad(
        "marker",
        "marker",
        -0.18f,
        0.18f,
        -1.53f,
        -1.17f,
        0.6f,
        2,
        2,
        whiteTexture(),
        {1.0f, 1.0f, 1.0f, 1.0f},
        material);
    marker.motion.enabled = true;
    marker.motion.fromFrame = 0;
    marker.motion.toFrame = 30;
    marker.motion.from = {-1.00f, 0.0f, 0.0f};
    marker.motion.to = {1.00f, 0.0f, 0.0f};
    return marker;
}

std::vector<FixtureObject> makeFixtureObjects(std::string_view contentId) {
    return {
        makeBackgroundObject(),
        makeBoardObject(contentId),
        makeOccluderObject(contentId),
        makeHiddenObject(contentId),
        makeBlendObject(contentId),
        makeMarkerObject(contentId)};
}

std::string objectFileBase(const FixtureObject& object) {
    return "objects/" + object.id;
}

std::uint64_t contentHashOfObjects(const std::vector<FixtureObject>& objects) {
    std::uint64_t hash = kFnvOffset;
    for (const FixtureObject& object : objects) {
        const std::vector<std::uint8_t> geometry = encodeGeometry(object);
        hash = fnv1a64(geometry, hash);
        const std::vector<std::uint8_t> texture = encodeTexture(object);
        hash = fnv1a64(texture, hash);
    }
    return hash;
}

std::string buildSceneJson(
    std::string_view contentId,
    const std::vector<FixtureObject>& objects,
    std::uint64_t contentHash) {
    nlohmann::json root;
    root["schema"] = kFixtureSceneSchema;
    root["schema_version"] = kFixtureSceneSchemaVersion;
    root["scene_id"] = kFixtureSceneId;
    root["content_id"] = std::string(contentId);
    root["content_hash"] = hashHex(contentHash);
    root["camera"] = {
        {"position", {0.0f, 0.0f, 6.0f}},
        {"target", {0.0f, 0.0f, 0.0f}},
        {"fov_degrees", 35.0f},
        {"near", 0.1f},
        {"far", 100.0f}};
    nlohmann::json objectArray = nlohmann::json::array();
    for (const FixtureObject& object : objects) {
        const std::vector<std::uint8_t> geometry = encodeGeometry(object);
        const std::vector<std::uint8_t> texture = encodeTexture(object);
        nlohmann::json entry;
        entry["id"] = object.id;
        entry["role"] = object.role;
        entry["geometry"] = objectFileBase(object) + ".geom.bin";
        entry["geometry_hash"] = fnv1a64(geometry);
        entry["geometry_vertices"] = object.vertices.size();
        entry["geometry_indices"] = object.indices.size();
        entry["texture"] = objectFileBase(object) + ".tex.bin";
        entry["texture_hash"] = fnv1a64(texture);
        entry["texture_width"] = object.textureWidth;
        entry["texture_height"] = object.textureHeight;
        entry["alpha_mode"] = object.material.alphaMode;
        entry["blend_mode"] = object.material.blendMode;
        entry["depth_test"] = object.material.depthTest;
        entry["material_mode"] = object.material.materialMode;
        entry["alpha_cutoff"] = object.material.alphaCutoff;
        if (object.motion.enabled) {
            entry["motion"] = {
                {"enabled", true},
                {"from_frame", object.motion.fromFrame},
                {"to_frame", object.motion.toFrame},
                {"from", object.motion.from},
                {"to", object.motion.to}};
        } else {
            entry["motion"] = {{"enabled", false}};
        }
        objectArray.push_back(std::move(entry));
    }
    root["objects"] = std::move(objectArray);
    return root.dump(2);
}

bool readRequired(
    const nlohmann::json& object,
    const char* key,
    std::string& out,
    std::string* outError) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        return fail(outError, std::string("fixture object is missing string field '") + key + "'");
    }
    out = object.at(key).get<std::string>();
    return true;
}

bool readRequired(
    const nlohmann::json& object,
    const char* key,
    std::uint64_t& out,
    std::string* outError) {
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        return fail(outError, std::string("fixture object is missing unsigned field '") + key + "'");
    }
    out = object.at(key).get<std::uint64_t>();
    return true;
}

bool readRequiredInt(
    const nlohmann::json& object,
    const char* key,
    int& out,
    std::string* outError) {
    if (!object.contains(key) || !object.at(key).is_number_integer()) {
        return fail(outError, std::string("fixture object is missing integer field '") + key + "'");
    }
    out = object.at(key).get<int>();
    return true;
}

bool readRequiredUint8(
    const nlohmann::json& object,
    const char* key,
    std::uint8_t& out,
    std::string* outError) {
    int value = 0;
    if (!readRequiredInt(object, key, value, outError) || value < 0 || value > 255) {
        return fail(outError, std::string("fixture object field '") + key + "' is out of range");
    }
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool readVec3(
    const nlohmann::json& value,
    std::array<float, 3>& out,
    std::string* outError) {
    if (!value.is_array() || value.size() != 3u) {
        return fail(outError, "fixture vector must contain three numbers");
    }
    for (std::size_t index = 0u; index < 3u; ++index) {
        if (!value.at(index).is_number()) {
            return fail(outError, "fixture vector contains a non-number");
        }
        out[index] = value.at(index).get<float>();
    }
    return true;
}

bool readArchiveFile(
    const engine::assets::phlosion::SceneArchiveStore& archive,
    const std::string& virtualPath,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) {
    if (!archive.readBytes(virtualPath, outBytes, outError)) {
        return fail(outError, "fixture archive is missing required file '" + virtualPath + "'");
    }
    return true;
}

} // namespace

const FixtureObject* FixtureContent::findRole(std::string_view role) const noexcept {
    const auto found = std::find_if(
        objects.begin(),
        objects.end(),
        [role](const FixtureObject& object) {
            return object.role == role;
        });
    return found == objects.end() ? nullptr : &*found;
}

std::size_t FixtureContent::triangleCount() const noexcept {
    std::size_t triangles = 0u;
    for (const FixtureObject& object : objects) {
        triangles += object.triangleCount();
    }
    return triangles;
}

void FixtureAssetStore::add(std::string virtualPath, std::vector<std::uint8_t> bytes) {
    const auto existing = std::find_if(
        assets_.begin(),
        assets_.end(),
        [&virtualPath](const Entry& entry) {
            return entry.virtualPath == virtualPath;
        });
    if (existing != assets_.end()) {
        existing->bytes = std::move(bytes);
        return;
    }
    assets_.push_back(Entry{std::move(virtualPath), std::move(bytes)});
}

bool FixtureAssetStore::readText(
    const std::string& virtualPath,
    std::string& outText,
    std::string* outError) const {
    std::vector<std::uint8_t> bytes;
    if (!readBytes(virtualPath, bytes, outError)) {
        return false;
    }
    outText.assign(bytes.begin(), bytes.end());
    return true;
}

bool FixtureAssetStore::readBytes(
    const std::string& virtualPath,
    std::vector<std::uint8_t>& outBytes,
    std::string* outError) const {
    const auto found = std::find_if(
        assets_.begin(),
        assets_.end(),
        [&virtualPath](const Entry& entry) {
            return entry.virtualPath == virtualPath;
        });
    if (found == assets_.end()) {
        if (outError) {
            *outError = "fixture store has no asset: " + virtualPath;
        }
        return false;
    }
    outBytes = found->bytes;
    return true;
}

bool FixtureAssetStore::exists(const std::string& virtualPath) const {
    return std::any_of(
        assets_.begin(),
        assets_.end(),
        [&virtualPath](const Entry& entry) {
            return entry.virtualPath == virtualPath;
        });
}

std::uint64_t fnv1a64(
    const void* data,
    std::size_t byteCount,
    std::uint64_t seed) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t index = 0u; index < byteCount; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) noexcept {
    return fnv1a64(bytes.data(), bytes.size());
}

std::uint64_t fnv1a64(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t seed) noexcept {
    return fnv1a64(bytes.data(), bytes.size(), seed);
}

std::string hashHex(std::uint64_t hash) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (std::size_t index = 0u; index < 16u; ++index) {
        const std::size_t shift = (15u - index) * 4u;
        out[index] = digits[(hash >> shift) & 0x0fu];
    }
    return out;
}

std::vector<std::uint8_t> fixtureDependencyBytes() {
    constexpr char text[] = "phlosion-public-asset-fixture-required-leaf-v1";
    return std::vector<std::uint8_t>(text, text + sizeof(text) - 1u);
}

std::uint64_t fixtureDependencyHash() {
    return fnv1a64(fixtureDependencyBytes());
}

FixtureBundle buildFixtureBundle(FixtureContentVariant variant) {
    FixtureBundle bundle;
    bundle.variant = variant;
    const bool missing = variant == FixtureContentVariant::MissingDependency;
    const bool contentB = variant == FixtureContentVariant::B;
    const std::string contentId = contentB ? "b" : "a";
    bundle.contentId = contentId;
    bundle.rootAssetId = missing
        ? kFixtureRootMissing
        : (contentB ? kFixtureRootB : kFixtureRootA);
    bundle.virtualPath = bundle.rootAssetId + ".phscene";
    bundle.dependencyAssetId =
        missing ? kFixtureMissingDependencyId : kFixtureDependencyId;
    bundle.dependencyVirtualPath = bundle.dependencyAssetId + ".bin";
    bundle.dependencyHash = fixtureDependencyHash();
    bundle.dependencyPresent = !missing;

    const std::vector<FixtureObject> objects = makeFixtureObjects(contentId);
    const std::uint64_t contentHash = contentHashOfObjects(objects);
    const std::string sceneJson =
        buildSceneJson(contentId, objects, contentHash);

    std::vector<engine::assets::phlosion::SceneArchiveFile> files;
    files.push_back(engine::assets::phlosion::SceneArchiveFile{
        "fixture/scene.json",
        std::vector<std::uint8_t>(sceneJson.begin(), sceneJson.end())});
    for (const FixtureObject& object : objects) {
        files.push_back(engine::assets::phlosion::SceneArchiveFile{
            objectFileBase(object) + ".geom.bin",
            encodeGeometry(object)});
        files.push_back(engine::assets::phlosion::SceneArchiveFile{
            objectFileBase(object) + ".tex.bin",
            encodeTexture(object)});
    }

    std::vector<std::uint8_t> encoded;
    std::string error;
    if (!engine::assets::phlosion::encodeSceneArchive(
            kFixtureSceneId,
            std::move(files),
            encoded,
            &error)) {
        // The fixture is deterministic; a failure here is a programming error.
        throw std::runtime_error("fixture scene archive encode failed: " + error);
    }

    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(encoded, document, &error)) {
        throw std::runtime_error("fixture scene archive decode failed: " + error);
    }
    engine::assets::phrc::Dependency dependency;
    dependency.assetId = bundle.dependencyAssetId;
    dependency.expectedContentHash = bundle.dependencyHash;
    dependency.flags = engine::assets::phrc::kDependencyRequired;
    document.dependencies = {dependency};
    if (!engine::assets::phrc::encode(document, bundle.archiveBytes, &error)) {
        throw std::runtime_error("fixture dependency archive encode failed: " + error);
    }
    return bundle;
}

void addFixtureDependency(FixtureAssetStore& store) {
    store.add(
        std::string(kFixtureDependencyId) + ".bin",
        fixtureDependencyBytes());
}

bool fixtureAssetResolver(
    const std::string& assetId,
    engine::assets::phlosion::AssetLocation& outLocation,
    std::string* outError) {
    using engine::assets::phlosion::AssetKind;
    if (assetId == kFixtureRootA || assetId == kFixtureRootB ||
        assetId == kFixtureRootMissing) {
        outLocation = engine::assets::phlosion::AssetLocation{
            assetId + ".phscene",
            AssetKind::Phrc};
        return true;
    }
    if (assetId == kFixtureDependencyId ||
        assetId == kFixtureMissingDependencyId) {
        outLocation = engine::assets::phlosion::AssetLocation{
            assetId + ".bin",
            AssetKind::Bytes};
        return true;
    }
    if (outError) {
        *outError = "fixture resolver does not know asset id: " + assetId;
    }
    return false;
}

bool validateFixtureSnapshot(
    const engine::assets::phlosion::ArchiveSnapshot& snapshot,
    FixtureContent& outContent,
    std::string* outError) {
    using namespace engine::assets::phlosion;
    if (!snapshot.scene) {
        return fail(outError, "fixture snapshot has no decoded scene archive");
    }
    const SceneArchiveStore& archive = *snapshot.scene;
    std::string sceneJson;
    std::vector<std::uint8_t> sceneJsonBytes;
    if (!archive.readText("fixture/scene.json", sceneJson, outError) ||
        !archive.readBytes("fixture/scene.json", sceneJsonBytes, outError)) {
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(sceneJson);
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            outError,
            std::string("fixture scene JSON is invalid: ") + exception.what());
    }
    try {
        if (root.at("schema").get<std::string>() != kFixtureSceneSchema) {
            return fail(outError, "fixture scene schema is unknown");
        }
        if (root.at("schema_version").get<std::uint32_t>() !=
            kFixtureSceneSchemaVersion) {
            return fail(outError, "fixture scene schema version is unsupported");
        }
        if (root.at("scene_id").get<std::string>() != kFixtureSceneId) {
            return fail(outError, "fixture scene id does not match the logical identity");
        }

        FixtureContent content;
        content.sceneId = kFixtureSceneId;
        content.contentId = root.at("content_id").get<std::string>();
        if (content.contentId != "a" && content.contentId != "b") {
            return fail(
                outError,
                "fixture content id is unsupported: " + content.contentId);
        }
        content.contentHash = root.at("content_hash").get<std::string>();
        content.rootAssetId = snapshot.graph.rootAssetId;
        content.archiveFileCount = archive.fileCount();

        const auto& camera = root.at("camera");
        if (!readVec3(camera.at("position"), content.camera.position, outError) ||
            !readVec3(camera.at("target"), content.camera.target, outError)) {
            return false;
        }
        content.camera.fovDegrees = camera.at("fov_degrees").get<float>();
        content.camera.nearPlane = camera.at("near").get<float>();
        content.camera.farPlane = camera.at("far").get<float>();
        const auto finiteVec3 = [](const std::array<float, 3>& value) {
            return std::isfinite(value[0]) && std::isfinite(value[1]) &&
                std::isfinite(value[2]);
        };
        if (!finiteVec3(content.camera.position) ||
            !finiteVec3(content.camera.target) ||
            !std::isfinite(content.camera.fovDegrees) ||
            !std::isfinite(content.camera.nearPlane) ||
            !std::isfinite(content.camera.farPlane)) {
            return fail(outError, "fixture camera values must be finite");
        }
        if (content.camera.fovDegrees <= 0.0f ||
            content.camera.fovDegrees >= 179.0f) {
            return fail(
                outError,
                "fixture camera field of view must be inside (0, 179) degrees");
        }
        if (content.camera.nearPlane <= 0.0f ||
            content.camera.farPlane <= content.camera.nearPlane) {
            return fail(
                outError,
                "fixture camera clip planes must satisfy 0 < near < far");
        }
        if (content.camera.position == content.camera.target) {
            return fail(
                outError,
                "fixture camera position and target must differ");
        }

        const auto& objectArray = root.at("objects");
        if (!objectArray.is_array() || objectArray.size() != 6u) {
            return fail(outError, "fixture scene must declare exactly six objects");
        }
        std::uint64_t computedHash = kFnvOffset;
        for (const auto& entry : objectArray) {
            FixtureObject object;
            int declaredTextureWidth = 0;
            int declaredTextureHeight = 0;
            std::uint64_t geometryHash = 0u;
            std::uint64_t textureHash = 0u;
            std::uint64_t geometryVertices = 0u;
            std::uint64_t geometryIndices = 0u;
            if (!readRequired(entry, "id", object.id, outError) ||
                !readRequired(entry, "role", object.role, outError) ||
                !readRequired(entry, "geometry", object.geometryFile, outError) ||
                !readRequired(entry, "texture", object.textureFile, outError) ||
                !readRequired(entry, "geometry_hash", geometryHash, outError) ||
                !readRequired(entry, "texture_hash", textureHash, outError) ||
                !readRequired(entry, "geometry_vertices", geometryVertices, outError) ||
                !readRequired(entry, "geometry_indices", geometryIndices, outError) ||
                !readRequiredInt(entry, "texture_width", declaredTextureWidth, outError) ||
                !readRequiredInt(entry, "texture_height", declaredTextureHeight, outError) ||
                !readRequiredUint8(entry, "alpha_mode", object.material.alphaMode, outError) ||
                !readRequiredUint8(entry, "blend_mode", object.material.blendMode, outError) ||
                !readRequiredUint8(entry, "depth_test", object.material.depthTest, outError) ||
                !readRequiredUint8(entry, "material_mode", object.material.materialMode, outError)) {
                return false;
            }
            object.material.alphaCutoff = entry.at("alpha_cutoff").get<float>();
            if (object.id.empty() || object.role.empty()) {
                return fail(
                    outError,
                    "fixture objects require a non-empty id and role");
            }
            if (declaredTextureWidth <= 0 || declaredTextureHeight <= 0) {
                return fail(
                    outError,
                    "fixture texture dimensions must be positive for object '" +
                        object.id + "'");
            }
            if (object.material.alphaMode > 2u ||
                object.material.blendMode > 2u ||
                object.material.depthTest > 1u ||
                object.material.materialMode > 7u) {
                return fail(
                    outError,
                    "fixture material values are unsupported for object '" +
                        object.id + "'");
            }
            if (!std::isfinite(object.material.alphaCutoff) ||
                object.material.alphaCutoff < 0.0f ||
                object.material.alphaCutoff > 1.0f) {
                return fail(
                    outError,
                    "fixture alpha cutoff must be finite and inside [0, 1] for object '" +
                        object.id + "'");
            }

            const auto& motion = entry.at("motion");
            object.motion.enabled = motion.at("enabled").get<bool>();
            if (object.motion.enabled) {
                object.motion.fromFrame = motion.at("from_frame").get<int>();
                object.motion.toFrame = motion.at("to_frame").get<int>();
                if (!readVec3(motion.at("from"), object.motion.from, outError) ||
                    !readVec3(motion.at("to"), object.motion.to, outError)) {
                    return false;
                }
                if (!finiteVec3(object.motion.from) ||
                    !finiteVec3(object.motion.to)) {
                    return fail(
                        outError,
                        "fixture motion values must be finite for object '" +
                            object.id + "'");
                }
                if (object.motion.fromFrame < 0 ||
                    object.motion.toFrame <= object.motion.fromFrame) {
                    return fail(
                        outError,
                        "fixture motion frames are invalid for object '" +
                            object.id + "'");
                }
            }

            std::vector<std::uint8_t> geometryBytes;
            std::vector<std::uint8_t> textureBytes;
            if (!readArchiveFile(archive, object.geometryFile, geometryBytes, outError) ||
                !readArchiveFile(archive, object.textureFile, textureBytes, outError)) {
                return false;
            }
            if (fnv1a64(geometryBytes) != geometryHash) {
                return fail(
                    outError,
                    "fixture geometry hash mismatch for object '" + object.id + "'");
            }
            if (fnv1a64(textureBytes) != textureHash) {
                return fail(
                    outError,
                    "fixture texture hash mismatch for object '" + object.id + "'");
            }
            int decodedTextureWidth = 0;
            int decodedTextureHeight = 0;
            if (!decodeGeometry(
                    geometryBytes,
                    object.vertices,
                    object.indices,
                    outError) ||
                !decodeTexture(
                    textureBytes,
                    decodedTextureWidth,
                    decodedTextureHeight,
                    object.textureRgba,
                    outError)) {
                return false;
            }
            object.textureWidth = decodedTextureWidth;
            object.textureHeight = decodedTextureHeight;
            if (decodedTextureWidth != declaredTextureWidth ||
                decodedTextureHeight != declaredTextureHeight) {
                return fail(
                    outError,
                    "fixture texture dimensions do not match the document for object '" +
                        object.id + "'");
            }
            if (object.vertices.size() < 3u) {
                return fail(
                    outError,
                    "fixture geometry for object '" + object.id +
                        "' requires at least three vertices");
            }
            if (object.vertices.size() != geometryVertices ||
                object.indices.size() != geometryIndices) {
                return fail(
                    outError,
                    "fixture geometry counts do not match the document for object '" +
                        object.id + "'");
            }
            if (object.indices.size() < 3u || object.indices.size() % 3u != 0u) {
                return fail(
                    outError,
                    "fixture geometry for object '" + object.id +
                        "' does not form triangles");
            }
            computedHash = fnv1a64(geometryBytes, computedHash);
            computedHash = fnv1a64(textureBytes, computedHash);

            object.geometryCacheKey =
                "fixture/geom/" + object.id + "#" + content.contentHash;
            object.textureKey = "fixture/tex/" + object.id;
            object.textureCacheKey =
                object.textureKey + "#" + content.contentHash;
            const auto duplicateId = std::find_if(
                content.objects.begin(),
                content.objects.end(),
                [&object](const FixtureObject& candidate) {
                    return candidate.id == object.id;
                });
            if (duplicateId != content.objects.end()) {
                return fail(
                    outError,
                    "fixture object id is duplicated: " + object.id);
            }
            const auto duplicateRole = std::find_if(
                content.objects.begin(),
                content.objects.end(),
                [&object](const FixtureObject& candidate) {
                    return candidate.role == object.role;
                });
            if (duplicateRole != content.objects.end()) {
                return fail(
                    outError,
                    "fixture object role is duplicated: " + object.role);
            }
            content.objects.push_back(std::move(object));
        }

        if (hashHex(computedHash) != content.contentHash) {
            return fail(outError, "fixture content hash does not match the document");
        }
        const std::array<std::string_view, 6> requiredRoles{
            "background",
            "board",
            "occluder",
            "hidden",
            "blend",
            "marker"};
        for (const std::string_view role : requiredRoles) {
            if (content.findRole(role) == nullptr) {
                return fail(
                    outError,
                    "fixture scene is missing required role '" +
                        std::string(role) + "'");
            }
        }
        const FixtureObject* marker = content.findRole("marker");
        if (marker == nullptr || !marker->motion.enabled ||
            marker->motion.toFrame <= marker->motion.fromFrame) {
            return fail(outError, "fixture marker motion metadata is invalid");
        }
        outContent = std::move(content);
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            outError,
            std::string("fixture scene document fields are invalid: ") +
                exception.what());
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

std::optional<FixtureCase> parseFixtureCase(std::string_view name) {
    if (name == "a-frame0") {
        return FixtureCase::AFrame0;
    }
    if (name == "a-frame30") {
        return FixtureCase::AFrame30;
    }
    if (name == "failed-replacement") {
        return FixtureCase::FailedReplacement;
    }
    if (name == "valid-replacement-b") {
        return FixtureCase::ValidReplacementB;
    }
    return std::nullopt;
}

std::string_view fixtureCaseName(FixtureCase value) {
    switch (value) {
    case FixtureCase::AFrame0:
        return "a-frame0";
    case FixtureCase::AFrame30:
        return "a-frame30";
    case FixtureCase::FailedReplacement:
        return "failed-replacement";
    case FixtureCase::ValidReplacementB:
        return "valid-replacement-b";
    }
    return "unknown";
}

int fixtureCaseLogicalFrame(FixtureCase value) {
    return value == FixtureCase::AFrame0 ? 0 : 30;
}

bool fixtureCaseReplaces(FixtureCase value) {
    return value == FixtureCase::FailedReplacement ||
        value == FixtureCase::ValidReplacementB;
}

std::string fixtureCaseReplacementRoot(FixtureCase value) {
    switch (value) {
    case FixtureCase::FailedReplacement:
        return kFixtureRootMissing;
    case FixtureCase::ValidReplacementB:
        return kFixtureRootB;
    case FixtureCase::AFrame0:
    case FixtureCase::AFrame30:
        break;
    }
    return {};
}

std::string fixtureCaseExpectedActiveRoot(FixtureCase value) {
    return value == FixtureCase::ValidReplacementB
        ? kFixtureRootB
        : kFixtureRootA;
}

std::string fixtureCaseExpectedContentId(FixtureCase value) {
    return value == FixtureCase::ValidReplacementB ? "b" : "a";
}

bool fixtureCaseExpectsReplacementFailure(FixtureCase value) {
    return value == FixtureCase::FailedReplacement;
}

} // namespace engine::fixture
