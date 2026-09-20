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

constexpr std::uint32_t kTextureMagic = 0x58455450u;  // "PTEX"
constexpr std::uint32_t kFileVersion = 1u;
constexpr std::size_t kTextureHeaderBytes = 16u;

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

std::vector<std::uint8_t> encodeTexture(
    const FixtureMaterialBinding& binding) {
    const TextureHeader header{
        kTextureMagic,
        kFileVersion,
        static_cast<std::uint32_t>(binding.textureWidth),
        static_cast<std::uint32_t>(binding.textureHeight)};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kTextureHeaderBytes + binding.textureRgba.size());
    appendValue(bytes, header);
    appendBytes(
        bytes,
        binding.textureRgba.data(),
        binding.textureRgba.size());
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

engine::assets::phlosion::MeshVertex makeVertex(
    float x,
    float y,
    float z,
    float u,
    float v,
    const std::array<float, 4>& color) {
    using engine::assets::phlosion::MeshVertex;
    MeshVertex vertex;
    vertex.position = {x, y, z};
    vertex.normal = {0.0f, 0.0f, 1.0f};
    vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    vertex.texcoord = {u, v};
    vertex.color = color;
    return vertex;
}

engine::assets::phlosion::MeshBounds computeMeshBounds(
    const engine::assets::phlosion::MeshAsset& mesh) {
    using engine::assets::phlosion::MeshBounds;
    MeshBounds bounds;
    if (mesh.vertices.empty()) {
        return bounds;
    }
    bounds.min = mesh.vertices.front().position;
    bounds.max = mesh.vertices.front().position;
    for (const engine::assets::phlosion::MeshVertex& vertex : mesh.vertices) {
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            bounds.min[channel] =
                std::min(bounds.min[channel], vertex.position[channel]);
            bounds.max[channel] =
                std::max(bounds.max[channel], vertex.position[channel]);
        }
    }
    return bounds;
}

// One flat quad built as a typed static mesh with a single material slot. The
// mesh id is the stable logical identity; the versioned registry id is applied
// per content variant after the object list is built.
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
    object.mesh.meshId = fixtureMeshId(object.role);
    object.mesh.vertices = {
        makeVertex(x0, y0, z, 0.0f, 0.0f, color),
        makeVertex(x1, y0, z, 1.0f, 0.0f, color),
        makeVertex(x1, y1, z, 1.0f, 1.0f, color),
        makeVertex(x0, y1, z, 0.0f, 1.0f, color)};
    object.mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    object.mesh.submeshes = {
        engine::assets::phlosion::MeshSubmesh{
            0u,
            6u,
            0u}};
    object.mesh.materialSlots = {kFixtureMeshMaterialSlot};
    object.mesh.bounds = computeMeshBounds(object.mesh);

    FixtureMaterialBinding binding;
    binding.slotLabel = kFixtureMeshMaterialSlot;
    binding.textureKey = "fixture/tex/" + object.id;
    binding.textureWidth = textureWidth;
    binding.textureHeight = textureHeight;
    binding.textureRgba = std::move(rgba);
    binding.material = material;
    object.materialBindings = {std::move(binding)};
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
    std::vector<FixtureObject> objects = {
        makeBackgroundObject(),
        makeBoardObject(contentId),
        makeOccluderObject(contentId),
        makeHiddenObject(contentId),
        makeBlendObject(contentId),
        makeMarkerObject(contentId)};
    for (FixtureObject& object : objects) {
        object.meshAssetId = fixtureMeshAssetId(object.role, contentId);
        object.meshVirtualPath = fixtureMeshVirtualPath(object.meshAssetId);
    }
    return objects;
}

std::string textureFileOf(const FixtureObject& object) {
    return "objects/" + object.id + ".tex.bin";
}

std::uint64_t meshDocumentHash(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < engine::assets::phrc::kHeaderBytes) {
        return 0u;
    }
    return engine::assets::phrc::contentHash64(
        bytes.data() + engine::assets::phrc::kHeaderBytes,
        bytes.size() - engine::assets::phrc::kHeaderBytes);
}

std::uint64_t contentHashOfObjects(
    const std::vector<FixtureObject>& objects,
    const std::vector<FixtureMeshResource>& meshes) {
    std::uint64_t hash = kFnvOffset;
    for (std::size_t index = 0u; index < objects.size(); ++index) {
        hash = fnv1a64(meshes[index].bytes, hash);
        for (const FixtureMaterialBinding& binding :
             objects[index].materialBindings) {
            const std::vector<std::uint8_t> texture = encodeTexture(binding);
            hash = fnv1a64(texture, hash);
        }
    }
    return hash;
}

std::string buildSceneJson(
    std::string_view contentId,
    const std::vector<FixtureObject>& objects,
    const std::vector<FixtureMeshResource>& meshes,
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
    for (std::size_t index = 0u; index < objects.size(); ++index) {
        const FixtureObject& object = objects[index];
        nlohmann::json entry;
        entry["id"] = object.id;
        entry["role"] = object.role;
        entry["mesh"] = object.meshAssetId;
        entry["mesh_id"] = object.mesh.meshId;
        entry["mesh_hash"] = hashHex(meshes[index].contentHash);
        entry["mesh_submeshes"] = object.mesh.submeshes.size();
        entry["mesh_material_slots"] = object.mesh.materialSlots.size();
        nlohmann::json slots = nlohmann::json::array();
        for (const FixtureMaterialBinding& binding :
             object.materialBindings) {
            const std::vector<std::uint8_t> texture = encodeTexture(binding);
            slots.push_back(
                {{"slot", binding.slotLabel},
                 {"texture", textureFileOf(object)},
                 {"texture_hash", fnv1a64(texture)},
                 {"texture_width", binding.textureWidth},
                 {"texture_height", binding.textureHeight},
                 {"alpha_mode", binding.material.alphaMode},
                 {"blend_mode", binding.material.blendMode},
                 {"depth_test", binding.material.depthTest},
                 {"material_mode", binding.material.materialMode},
                 {"alpha_cutoff", binding.material.alphaCutoff}});
        }
        entry["material_slots"] = std::move(slots);
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

bool readRequiredFloat(
    const nlohmann::json& object,
    const char* key,
    float& out,
    std::string* outError) {
    if (!object.contains(key) || !object.at(key).is_number()) {
        return fail(
            outError,
            std::string("fixture record is missing number field '") + key +
                "'");
    }
    out = object.at(key).get<float>();
    return true;
}

// Reads the 16 hex digits written by `hashHex`. A malformed or mistyped value
// is rejected instead of silently comparing as zero.
bool readHashHex(
    const nlohmann::json& object,
    const char* key,
    std::uint64_t& out,
    std::string* outError) {
    std::string text;
    if (!readRequired(object, key, text, outError)) {
        return false;
    }
    if (text.size() != 16u) {
        return fail(
            outError,
            std::string("fixture hash field '") + key +
                "' must contain 16 hex digits");
    }
    std::uint64_t value = 0u;
    for (const char digit : text) {
        std::uint64_t nibble = 0u;
        if (digit >= '0' && digit <= '9') {
            nibble = static_cast<std::uint64_t>(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = static_cast<std::uint64_t>(digit - 'a') + 10u;
        } else {
            return fail(
                outError,
                std::string("fixture hash field '") + key +
                    "' must contain 16 hex digits");
        }
        value = (value << 4u) | nibble;
    }
    out = value;
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

const FixtureMaterialBinding* FixtureObject::findMaterialBinding(
    std::string_view slotLabel) const noexcept {
    const auto found = std::find_if(
        materialBindings.begin(),
        materialBindings.end(),
        [slotLabel](const FixtureMaterialBinding& binding) {
            return binding.slotLabel == slotLabel;
        });
    return found == materialBindings.end() ? nullptr : &*found;
}

std::string fixtureMeshId(std::string_view role) {
    return std::string(kFixtureMeshIdPrefix) + std::string(role);
}

std::string fixtureMeshAssetId(
    std::string_view role,
    std::string_view contentId) {
    return fixtureMeshId(role) + "-" + std::string(contentId);
}

std::string fixtureMeshVirtualPath(const std::string& assetId) {
    return assetId + ".phmesh";
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
    std::vector<FixtureMeshResource> meshes;
    meshes.reserve(objects.size());
    for (const FixtureObject& object : objects) {
        FixtureMeshResource resource;
        resource.assetId = object.meshAssetId;
        resource.virtualPath = object.meshVirtualPath;
        resource.meshId = object.mesh.meshId;
        std::string meshError;
        if (!engine::assets::phlosion::encodeMeshAsset(
                object.mesh,
                resource.bytes,
                {},
                &meshError)) {
            throw std::runtime_error(
                "fixture mesh encode failed for '" + object.meshAssetId +
                "': " + meshError);
        }
        resource.contentHash = meshDocumentHash(resource.bytes);
        meshes.push_back(std::move(resource));
    }
    const std::uint64_t contentHash = contentHashOfObjects(objects, meshes);
    const std::string sceneJson =
        buildSceneJson(contentId, objects, meshes, contentHash);

    std::vector<engine::assets::phlosion::SceneArchiveFile> files;
    files.push_back(engine::assets::phlosion::SceneArchiveFile{
        "fixture/scene.json",
        std::vector<std::uint8_t>(sceneJson.begin(), sceneJson.end())});
    for (const FixtureObject& object : objects) {
        for (const FixtureMaterialBinding& binding :
             object.materialBindings) {
            files.push_back(engine::assets::phlosion::SceneArchiveFile{
                textureFileOf(object),
                encodeTexture(binding)});
        }
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
    std::vector<engine::assets::phrc::Dependency> dependencies{dependency};
    for (const FixtureMeshResource& mesh : meshes) {
        engine::assets::phrc::Dependency meshDependency;
        meshDependency.assetId = mesh.assetId;
        meshDependency.expectedContentHash = mesh.contentHash;
        meshDependency.flags = engine::assets::phrc::kDependencyRequired;
        dependencies.push_back(std::move(meshDependency));
    }
    document.dependencies = std::move(dependencies);
    if (!engine::assets::phrc::encode(document, bundle.archiveBytes, &error)) {
        throw std::runtime_error("fixture dependency archive encode failed: " + error);
    }
    bundle.meshes = std::move(meshes);
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
    if (assetId.rfind(kFixtureMeshIdPrefix, 0u) == 0u) {
        outLocation = engine::assets::phlosion::AssetLocation{
            fixtureMeshVirtualPath(assetId),
            AssetKind::Phrc};
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
            std::string declaredMeshId;
            std::uint64_t declaredMeshHash = 0u;
            std::uint64_t declaredSubmeshes = 0u;
            std::uint64_t declaredMaterialSlots = 0u;
            if (!readRequired(entry, "id", object.id, outError) ||
                !readRequired(entry, "role", object.role, outError) ||
                !readRequired(entry, "mesh", object.meshAssetId, outError) ||
                !readRequired(entry, "mesh_id", declaredMeshId, outError) ||
                !readHashHex(entry, "mesh_hash", declaredMeshHash, outError) ||
                !readRequired(
                    entry,
                    "mesh_submeshes",
                    declaredSubmeshes,
                    outError) ||
                !readRequired(
                    entry,
                    "mesh_material_slots",
                    declaredMaterialSlots,
                    outError)) {
                return false;
            }
            if (object.id.empty() || object.role.empty()) {
                return fail(
                    outError,
                    "fixture objects require a non-empty id and role");
            }
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
            if (!entry.contains("material_slots") ||
                !entry.at("material_slots").is_array() ||
                entry.at("material_slots").empty()) {
                return fail(
                    outError,
                    "fixture objects require at least one material binding for object '" +
                        object.id + "'");
            }
            if (object.meshAssetId !=
                fixtureMeshAssetId(object.role, content.contentId)) {
                return fail(
                    outError,
                    "fixture mesh reference does not match the object role for object '" +
                        object.id + "'");
            }
            object.meshVirtualPath =
                fixtureMeshVirtualPath(object.meshAssetId);
            object.meshDocumentHash = declaredMeshHash;

            // Geometry comes from the staged snapshot graph only: no resolver,
            // provider or fallback read happens after activation.
            const auto meshAsset =
                snapshot.graph.assets.find(object.meshAssetId);
            if (meshAsset == snapshot.graph.assets.end()) {
                return fail(
                    outError,
                    "fixture snapshot has no mesh asset '" +
                        object.meshAssetId + "'");
            }
            if (meshAsset->second.kind != AssetKind::Phrc ||
                meshAsset->second.virtualPath != object.meshVirtualPath) {
                return fail(
                    outError,
                    "fixture mesh asset '" + object.meshAssetId +
                        "' is not the expected PHRC resource");
            }
            if (meshAsset->second.contentHash != declaredMeshHash) {
                return fail(
                    outError,
                    "fixture mesh hash does not match the document for object '" +
                        object.id + "'");
            }
            MeshAsset decodedMesh;
            std::string meshError;
            if (!decodeMeshAsset(
                    meshAsset->second.bytes,
                    decodedMesh,
                    {},
                    &meshError)) {
                return fail(
                    outError,
                    "fixture mesh asset '" + object.meshAssetId +
                        "' is invalid: " + meshError);
            }
            if (decodedMesh.meshId != declaredMeshId) {
                return fail(
                    outError,
                    "fixture mesh id does not match the document for object '" +
                        object.id + "'");
            }
            if (decodedMesh.submeshes.size() != declaredSubmeshes ||
                decodedMesh.materialSlots.size() != declaredMaterialSlots) {
                return fail(
                    outError,
                    "fixture mesh structure does not match the document for object '" +
                        object.id + "'");
            }
            object.mesh = std::move(decodedMesh);
            computedHash = fnv1a64(meshAsset->second.bytes, computedHash);

            for (const auto& slotRecord : entry.at("material_slots")) {
                FixtureMaterialBinding binding;
                std::string texturePath;
                std::uint64_t textureHash = 0u;
                int declaredTextureWidth = 0;
                int declaredTextureHeight = 0;
                if (!readRequired(
                        slotRecord,
                        "slot",
                        binding.slotLabel,
                        outError) ||
                    !readRequired(
                        slotRecord,
                        "texture",
                        texturePath,
                        outError) ||
                    !readRequired(
                        slotRecord,
                        "texture_hash",
                        textureHash,
                        outError) ||
                    !readRequiredInt(
                        slotRecord,
                        "texture_width",
                        declaredTextureWidth,
                        outError) ||
                    !readRequiredInt(
                        slotRecord,
                        "texture_height",
                        declaredTextureHeight,
                        outError) ||
                    !readRequiredUint8(
                        slotRecord,
                        "alpha_mode",
                        binding.material.alphaMode,
                        outError) ||
                    !readRequiredUint8(
                        slotRecord,
                        "blend_mode",
                        binding.material.blendMode,
                        outError) ||
                    !readRequiredUint8(
                        slotRecord,
                        "depth_test",
                        binding.material.depthTest,
                        outError) ||
                    !readRequiredUint8(
                        slotRecord,
                        "material_mode",
                        binding.material.materialMode,
                        outError) ||
                    !readRequiredFloat(
                        slotRecord,
                        "alpha_cutoff",
                        binding.material.alphaCutoff,
                        outError)) {
                    return false;
                }
                if (binding.slotLabel.empty()) {
                    return fail(
                        outError,
                        "fixture material bindings require a non-empty slot label for object '" +
                            object.id + "'");
                }
                if (texturePath != textureFileOf(object)) {
                    return fail(
                        outError,
                        "fixture texture path does not match the object for object '" +
                            object.id + "'");
                }
                if (declaredTextureWidth <= 0 || declaredTextureHeight <= 0) {
                    return fail(
                        outError,
                        "fixture texture dimensions must be positive for object '" +
                            object.id + "'");
                }
                if (binding.material.alphaMode > 2u ||
                    binding.material.blendMode > 2u ||
                    binding.material.depthTest > 1u ||
                    binding.material.materialMode > 7u) {
                    return fail(
                        outError,
                        "fixture material values are unsupported for object '" +
                            object.id + "'");
                }
                if (!std::isfinite(binding.material.alphaCutoff) ||
                    binding.material.alphaCutoff < 0.0f ||
                    binding.material.alphaCutoff > 1.0f) {
                    return fail(
                        outError,
                        "fixture alpha cutoff must be finite and inside [0, 1] for object '" +
                            object.id + "'");
                }

                std::vector<std::uint8_t> textureBytes;
                if (!readArchiveFile(
                        archive,
                        texturePath,
                        textureBytes,
                        outError)) {
                    return false;
                }
                if (fnv1a64(textureBytes) != textureHash) {
                    return fail(
                        outError,
                        "fixture texture hash mismatch for object '" +
                            object.id + "'");
                }
                int decodedTextureWidth = 0;
                int decodedTextureHeight = 0;
                if (!decodeTexture(
                        textureBytes,
                        decodedTextureWidth,
                        decodedTextureHeight,
                        binding.textureRgba,
                        outError)) {
                    return false;
                }
                if (decodedTextureWidth != declaredTextureWidth ||
                    decodedTextureHeight != declaredTextureHeight) {
                    return fail(
                        outError,
                        "fixture texture dimensions do not match the document for object '" +
                            object.id + "'");
                }
                binding.textureWidth = decodedTextureWidth;
                binding.textureHeight = decodedTextureHeight;
                binding.textureKey = "fixture/tex/" + object.id;
                binding.textureCacheKey =
                    binding.textureKey + "#" + content.contentHash;
                computedHash = fnv1a64(textureBytes, computedHash);
                object.materialBindings.push_back(std::move(binding));
            }

            for (const std::string& slot : object.mesh.materialSlots) {
                const std::size_t boundCount =
                    static_cast<std::size_t>(std::count_if(
                        object.materialBindings.begin(),
                        object.materialBindings.end(),
                        [&slot](const FixtureMaterialBinding& binding) {
                            return binding.slotLabel == slot;
                        }));
                if (boundCount == 0u) {
                    return fail(
                        outError,
                        "fixture object '" + object.id +
                            "' has no material binding for mesh slot '" +
                            slot + "'");
                }
                if (boundCount > 1u) {
                    return fail(
                        outError,
                        "fixture object '" + object.id +
                            "' must bind each mesh material slot exactly once; slot '" +
                            slot + "' is duplicated");
                }
            }
            for (const FixtureMaterialBinding& binding :
                 object.materialBindings) {
                if (std::find(
                        object.mesh.materialSlots.begin(),
                        object.mesh.materialSlots.end(),
                        binding.slotLabel) ==
                    object.mesh.materialSlots.end()) {
                    return fail(
                        outError,
                        "fixture material binding slot '" + binding.slotLabel +
                            "' is not declared by the mesh for object '" +
                            object.id + "'");
                }
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

            object.geometryCacheKey =
                "fixture/geom/" + object.id + "#" + content.contentHash;
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
