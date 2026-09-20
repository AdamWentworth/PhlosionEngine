#include "engine/assets/phlosion/PhlosionMeshAsset.h"

#include "engine/assets/phlosion/PhlosionBinaryCodec.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <utility>

namespace engine::assets::phlosion {
namespace {

using phrc::BinaryReader;
using phrc::BinaryWriter;

constexpr char kVertexChunkType[] = "VTX0";
constexpr char kIndexChunkType[] = "IDX0";
constexpr std::uint32_t kChunkAlignment = 16u;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

void clearError(std::string* outError) {
    if (outError) {
        outError->clear();
    }
}

std::string indexed(const std::string& label, std::size_t index) {
    return label + "[" + std::to_string(index) + "]";
}

bool validName(const std::string& name) {
    return !name.empty() && name.size() <= kMeshMaxNameBytes;
}

// Strict UTF-8: rejects truncation, lone continuation bytes, overlong
// encodings, surrogates and values above U+10FFFF. Names are carried verbatim,
// so an invalid byte sequence is rejected instead of being replaced, and the
// manifest writer can never receive a string it would refuse to encode.
bool validUtf8(const std::string& text) {
    std::size_t index = 0u;
    while (index < text.size()) {
        const std::uint32_t lead =
            static_cast<std::uint8_t>(text[index]);
        if (lead < 0x80u) {
            ++index;
            continue;
        }
        std::uint32_t continuationCount = 0u;
        std::uint32_t codepoint = 0u;
        if ((lead & 0xe0u) == 0xc0u) {
            continuationCount = 1u;
            codepoint = lead & 0x1fu;
        } else if ((lead & 0xf0u) == 0xe0u) {
            continuationCount = 2u;
            codepoint = lead & 0x0fu;
        } else if ((lead & 0xf8u) == 0xf0u) {
            continuationCount = 3u;
            codepoint = lead & 0x07u;
        } else {
            return false;
        }
        if (continuationCount > text.size() - index - 1u) {
            return false;
        }
        for (std::uint32_t offset = 1u; offset <= continuationCount; ++offset) {
            const std::uint32_t next = static_cast<std::uint8_t>(
                text[index + static_cast<std::size_t>(offset)]);
            if ((next & 0xc0u) != 0x80u) {
                return false;
            }
            codepoint = (codepoint << 6u) | (next & 0x3fu);
        }
        const std::uint32_t minimum = continuationCount == 1u
            ? 0x80u
            : (continuationCount == 2u ? 0x800u : 0x10000u);
        if (codepoint < minimum || codepoint > 0x10ffffu) {
            return false;
        }
        if (codepoint >= 0xd800u && codepoint <= 0xdfffu) {
            return false;
        }
        index += static_cast<std::size_t>(continuationCount) + 1u;
    }
    return true;
}

bool checkedMultiply(
    std::size_t left,
    std::size_t right,
    std::size_t& out) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    out = left * right;
    return true;
}

bool nonzeroLength(const std::array<float, 3>& value) {
    const double lengthSquared =
        static_cast<double>(value[0]) * static_cast<double>(value[0]) +
        static_cast<double>(value[1]) * static_cast<double>(value[1]) +
        static_cast<double>(value[2]) * static_cast<double>(value[2]);
    return lengthSquared > 0.0;
}

bool validateVertex(
    const MeshVertex& vertex,
    std::size_t index,
    std::string* outError) {
    const std::string label = "mesh vertex " + std::to_string(index);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        if (!std::isfinite(vertex.position[channel])) {
            return fail(
                outError,
                label + " position must be finite");
        }
    }
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        if (!std::isfinite(vertex.normal[channel])) {
            return fail(outError, label + " normal must be finite");
        }
    }
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        if (!std::isfinite(vertex.tangent[channel])) {
            return fail(outError, label + " tangent must be finite");
        }
    }
    for (std::size_t channel = 0u; channel < 2u; ++channel) {
        if (!std::isfinite(vertex.texcoord[channel])) {
            return fail(outError, label + " texcoord must be finite");
        }
    }
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        const float value = vertex.color[channel];
        if (!std::isfinite(value)) {
            return fail(outError, label + " color must be finite");
        }
        if (value < 0.0f || value > 1.0f) {
            return fail(
                outError,
                label + " color channel must be inside [0, 1]");
        }
    }
    if (vertex.tangent[3] != 1.0f && vertex.tangent[3] != -1.0f) {
        return fail(
            outError,
            label + " tangent w must be exactly -1 or +1");
    }
    if (!nonzeroLength(vertex.normal)) {
        return fail(outError, label + " normal must have nonzero length");
    }
    if (!nonzeroLength({vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]})) {
        return fail(outError, label + " tangent xyz must have nonzero length");
    }
    return true;
}

// Shared authored/decoded semantics. Nothing is clamped, normalized, flipped or
// repaired, so a caller that supplies inconsistent bounds or a partial submesh
// partition is rejected instead of quietly corrected.
bool validateMeshData(
    const MeshAsset& asset,
    const MeshAssetLimits& limits,
    std::string* outError) {
    if (asset.meshId.empty()) {
        return fail(outError, "mesh id must be non-empty");
    }
    if (asset.meshId.size() > kMeshMaxNameBytes) {
        return fail(
            outError,
            "mesh id exceeds " + std::to_string(kMeshMaxNameBytes) + " bytes");
    }
    if (!validUtf8(asset.meshId)) {
        return fail(outError, "mesh id must be valid UTF-8");
    }
    if (asset.vertices.size() < 3u) {
        return fail(outError, "mesh must contain at least three vertices");
    }
    if (asset.vertices.size() > limits.maxVertices) {
        return fail(outError, "mesh vertex count exceeds the limit");
    }
    if (asset.indices.size() < 3u) {
        return fail(outError, "mesh must contain at least one triangle");
    }
    if (asset.indices.size() % 3u != 0u) {
        return fail(outError, "mesh index count must be a multiple of three");
    }
    if (asset.indices.size() > limits.maxIndices) {
        return fail(outError, "mesh index count exceeds the limit");
    }
    if (asset.submeshes.empty()) {
        return fail(outError, "mesh must declare at least one submesh");
    }
    if (asset.submeshes.size() > limits.maxSubmeshes) {
        return fail(outError, "mesh submesh count exceeds the limit");
    }
    if (asset.materialSlots.empty()) {
        return fail(outError, "mesh must declare at least one material slot");
    }
    if (asset.materialSlots.size() > limits.maxMaterialSlots) {
        return fail(outError, "mesh material slot count exceeds the limit");
    }
    std::set<std::string> slotLabels;
    for (std::size_t index = 0u; index < asset.materialSlots.size(); ++index) {
        const std::string& slot = asset.materialSlots[index];
        if (!validName(slot)) {
            return fail(
                outError,
                "mesh material slot labels must be non-empty and at most " +
                    std::to_string(kMeshMaxNameBytes) + " bytes");
        }
        if (!validUtf8(slot)) {
            return fail(
                outError,
                "mesh material slot label " + std::to_string(index) +
                    " must be valid UTF-8");
        }
        if (!slotLabels.insert(slot).second) {
            return fail(
                outError,
                "mesh material slot label is duplicated: " + slot);
        }
    }
    for (std::size_t index = 0u; index < asset.vertices.size(); ++index) {
        if (!validateVertex(asset.vertices[index], index, outError)) {
            return false;
        }
    }
    for (std::size_t index = 0u; index < asset.indices.size(); ++index) {
        if (asset.indices[index] >= asset.vertices.size()) {
            return fail(
                outError,
                "mesh index " + std::to_string(index) + " is out of range");
        }
    }

    std::array<float, 3> minimum = asset.vertices.front().position;
    std::array<float, 3> maximum = asset.vertices.front().position;
    for (const MeshVertex& vertex : asset.vertices) {
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            minimum[channel] = std::min(minimum[channel], vertex.position[channel]);
            maximum[channel] = std::max(maximum[channel], vertex.position[channel]);
        }
    }
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        if (!std::isfinite(asset.bounds.min[channel]) ||
            !std::isfinite(asset.bounds.max[channel])) {
            return fail(outError, "mesh bounds must be finite");
        }
    }
    if (asset.bounds.min != minimum || asset.bounds.max != maximum) {
        return fail(outError, "mesh bounds do not match the vertex extents");
    }

    std::size_t expectedFirstIndex = 0u;
    for (std::size_t index = 0u; index < asset.submeshes.size(); ++index) {
        const MeshSubmesh& submesh = asset.submeshes[index];
        if (submesh.indexCount == 0u) {
            return fail(
                outError,
                "mesh submesh " + std::to_string(index) + " is empty");
        }
        if (submesh.indexCount % 3u != 0u) {
            return fail(
                outError,
                "mesh submesh " + std::to_string(index) +
                    " is not triangle aligned");
        }
        if (submesh.materialSlot >= asset.materialSlots.size()) {
            return fail(
                outError,
                "mesh submesh " + std::to_string(index) +
                    " references an invalid material slot");
        }
        if (submesh.firstIndex != expectedFirstIndex) {
            return fail(
                outError,
                "mesh submeshes must partition the index list in order");
        }
        // Subtraction form: no addition can wrap, even on a narrow host.
        if (static_cast<std::size_t>(submesh.indexCount) >
            asset.indices.size() - expectedFirstIndex) {
            return fail(
                outError,
                "mesh submeshes must not overlap or exceed the index list");
        }
        expectedFirstIndex += static_cast<std::size_t>(submesh.indexCount);
    }
    if (expectedFirstIndex != asset.indices.size()) {
        return fail(
            outError,
            "mesh submeshes must cover the whole index list");
    }
    return true;
}

bool jsonUnsigned(
    const nlohmann::json& value,
    const std::string& path,
    std::uint64_t& out,
    std::string* outError) {
    if (value.is_number_unsigned()) {
        out = value.get<std::uint64_t>();
        return true;
    }
    if (value.is_number_integer()) {
        return fail(
            outError,
            "mesh manifest field '" + path +
                "' must be a nonnegative integer");
    }
    if (value.is_number_float()) {
        return fail(
            outError,
            "mesh manifest field '" + path +
                "' must be an integer, not a fraction");
    }
    return fail(
        outError,
        "mesh manifest field '" + path + "' must be an integer");
}

bool jsonString(
    const nlohmann::json& value,
    const std::string& path,
    std::string& out,
    std::string* outError) {
    if (!value.is_string()) {
        return fail(
            outError,
            "mesh manifest field '" + path + "' must be a string");
    }
    out = value.get<std::string>();
    return true;
}

bool jsonFloat(
    const nlohmann::json& value,
    const std::string& path,
    float& out,
    std::string* outError) {
    if (!value.is_number()) {
        return fail(
            outError,
            "mesh manifest field '" + path + "' must be a number");
    }
    const double raw = value.get<double>();
    if (!std::isfinite(raw) ||
        std::abs(raw) > static_cast<double>(std::numeric_limits<float>::max())) {
        return fail(
            outError,
            "mesh manifest field '" + path +
                "' must be a finite float32 value");
    }
    out = static_cast<float>(raw);
    return true;
}

bool jsonFloatArray(
    const nlohmann::json& value,
    std::size_t count,
    const std::string& path,
    float* out,
    std::string* outError) {
    if (!value.is_array() || value.size() != count) {
        return fail(
            outError,
            "mesh manifest field '" + path + "' must be an array of " +
                std::to_string(count) + " numbers");
    }
    for (std::size_t channel = 0u; channel < count; ++channel) {
        if (!jsonFloat(
                value.at(channel),
                indexed(path, channel),
                out[channel],
                outError)) {
            return false;
        }
    }
    return true;
}

bool rejectUnknownKeys(
    const nlohmann::json& object,
    const char* const* allowed,
    std::size_t allowedCount,
    const std::string& path,
    std::string* outError) {
    if (!object.is_object()) {
        return fail(
            outError,
            "mesh manifest field '" + path + "' must be an object");
    }
    for (auto entry = object.begin(); entry != object.end(); ++entry) {
        bool known = false;
        for (std::size_t index = 0u; index < allowedCount; ++index) {
            if (entry.key() == allowed[index]) {
                known = true;
                break;
            }
        }
        if (!known) {
            return fail(
                outError,
                "mesh manifest field '" + path + "." + entry.key() +
                    "' is unsupported");
        }
    }
    return true;
}

bool requireMember(
    const nlohmann::json& object,
    const char* key,
    const std::string& path,
    const nlohmann::json** out,
    std::string* outError) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return fail(
            outError,
            "mesh manifest field '" + path + "." + key + "' is missing");
    }
    *out = &(*found);
    return true;
}

struct MeshManifestCounts {
    std::uint64_t vertices = 0u;
    std::uint64_t indices = 0u;
    std::uint64_t submeshes = 0u;
    std::uint64_t materialSlots = 0u;
};

bool parseCounts(
    const nlohmann::json& root,
    MeshManifestCounts& counts,
    const MeshAssetLimits& limits,
    std::string* outError) {
    static constexpr const char* kAllowed[] = {
        "vertices",
        "indices",
        "submeshes",
        "material_slots"};
    const nlohmann::json* value = nullptr;
    if (!requireMember(root, "counts", "mesh", &value, outError) ||
        !rejectUnknownKeys(
            *value,
            kAllowed,
            std::size(kAllowed),
            "mesh.counts",
            outError)) {
        return false;
    }
    const nlohmann::json* member = nullptr;
    if (!requireMember(*value, "vertices", "mesh.counts", &member, outError) ||
        !jsonUnsigned(*member, "mesh.counts.vertices", counts.vertices, outError) ||
        !requireMember(*value, "indices", "mesh.counts", &member, outError) ||
        !jsonUnsigned(*member, "mesh.counts.indices", counts.indices, outError) ||
        !requireMember(*value, "submeshes", "mesh.counts", &member, outError) ||
        !jsonUnsigned(*member, "mesh.counts.submeshes", counts.submeshes, outError) ||
        !requireMember(*value, "material_slots", "mesh.counts", &member, outError) ||
        !jsonUnsigned(*member, "mesh.counts.material_slots", counts.materialSlots, outError)) {
        return false;
    }
    const auto withinLimit = [](std::uint64_t count, std::size_t limit) {
        return count <= static_cast<std::uint64_t>(limit);
    };
    if (!withinLimit(counts.vertices, limits.maxVertices)) {
        return fail(outError, "mesh vertex count exceeds the limit");
    }
    if (!withinLimit(counts.indices, limits.maxIndices)) {
        return fail(outError, "mesh index count exceeds the limit");
    }
    if (!withinLimit(counts.submeshes, limits.maxSubmeshes)) {
        return fail(outError, "mesh submesh count exceeds the limit");
    }
    if (!withinLimit(counts.materialSlots, limits.maxMaterialSlots)) {
        return fail(outError, "mesh material slot count exceeds the limit");
    }
    return true;
}

bool parseBounds(
    const nlohmann::json& root,
    MeshBounds& bounds,
    std::string* outError) {
    static constexpr const char* kAllowed[] = {"min", "max"};
    const nlohmann::json* value = nullptr;
    if (!requireMember(root, "bounds", "mesh", &value, outError) ||
        !rejectUnknownKeys(
            *value,
            kAllowed,
            std::size(kAllowed),
            "mesh.bounds",
            outError)) {
        return false;
    }
    const nlohmann::json* member = nullptr;
    return requireMember(*value, "min", "mesh.bounds", &member, outError) &&
        jsonFloatArray(*member, 3u, "mesh.bounds.min", bounds.min.data(), outError) &&
        requireMember(*value, "max", "mesh.bounds", &member, outError) &&
        jsonFloatArray(*member, 3u, "mesh.bounds.max", bounds.max.data(), outError);
}

bool parseMaterialSlots(
    const nlohmann::json& root,
    const MeshManifestCounts& counts,
    MeshAsset& asset,
    std::string* outError) {
    const nlohmann::json* value = nullptr;
    if (!requireMember(root, "material_slots", "mesh", &value, outError)) {
        return false;
    }
    if (!value->is_array()) {
        return fail(
            outError,
            "mesh manifest field 'mesh.material_slots' must be an array");
    }
    if (value->size() != counts.materialSlots) {
        return fail(
            outError,
            "mesh material slot count does not match the manifest counts");
    }
    asset.materialSlots.clear();
    asset.materialSlots.reserve(value->size());
    for (std::size_t index = 0u; index < value->size(); ++index) {
        std::string label;
        if (!jsonString(
                value->at(index),
                indexed("mesh.material_slots", index),
                label,
                outError)) {
            return false;
        }
        asset.materialSlots.push_back(std::move(label));
    }
    return true;
}

bool parseSubmeshes(
    const nlohmann::json& root,
    const MeshManifestCounts& counts,
    MeshAsset& asset,
    std::string* outError) {
    static constexpr const char* kAllowed[] = {
        "first_index",
        "index_count",
        "material_slot"};
    const nlohmann::json* value = nullptr;
    if (!requireMember(root, "submeshes", "mesh", &value, outError)) {
        return false;
    }
    if (!value->is_array()) {
        return fail(
            outError,
            "mesh manifest field 'mesh.submeshes' must be an array");
    }
    if (value->size() != counts.submeshes) {
        return fail(
            outError,
            "mesh submesh count does not match the manifest counts");
    }
    asset.submeshes.clear();
    asset.submeshes.reserve(value->size());
    for (std::size_t index = 0u; index < value->size(); ++index) {
        const nlohmann::json& record = value->at(index);
        const std::string path = indexed("mesh.submeshes", index);
        if (!rejectUnknownKeys(
                record,
                kAllowed,
                std::size(kAllowed),
                path,
                outError)) {
            return false;
        }
        std::uint64_t firstIndex = 0u;
        std::uint64_t indexCount = 0u;
        std::uint64_t materialSlot = 0u;
        const nlohmann::json* member = nullptr;
        if (!requireMember(record, "first_index", path, &member, outError) ||
            !jsonUnsigned(*member, path + ".first_index", firstIndex, outError) ||
            !requireMember(record, "index_count", path, &member, outError) ||
            !jsonUnsigned(*member, path + ".index_count", indexCount, outError) ||
            !requireMember(record, "material_slot", path, &member, outError) ||
            !jsonUnsigned(*member, path + ".material_slot", materialSlot, outError)) {
            return false;
        }
        const std::uint64_t maxIndexField =
            std::numeric_limits<std::uint32_t>::max();
        if (firstIndex > maxIndexField) {
            return fail(
                outError,
                "mesh manifest field '" + path + ".first_index' exceeds uint32");
        }
        if (indexCount > maxIndexField) {
            return fail(
                outError,
                "mesh manifest field '" + path + ".index_count' exceeds uint32");
        }
        if (materialSlot > maxIndexField) {
            return fail(
                outError,
                "mesh manifest field '" + path + ".material_slot' exceeds uint32");
        }
        MeshSubmesh submesh;
        submesh.firstIndex = static_cast<std::uint32_t>(firstIndex);
        submesh.indexCount = static_cast<std::uint32_t>(indexCount);
        submesh.materialSlot = static_cast<std::uint32_t>(materialSlot);
        asset.submeshes.push_back(submesh);
    }
    return true;
}

// Structural manifest validation: known fields only, exact types, checked
// integer ranges before narrowing and explicit rejection of unsupported
// semantics such as skin or LOD declarations.
bool parseManifest(
    const std::string& manifestJson,
    MeshAsset& asset,
    MeshManifestCounts& counts,
    const MeshAssetLimits& limits,
    std::string* outError) {
    static constexpr const char* kAllowed[] = {
        "root_type",
        "schema_version",
        "mesh_id",
        "vertex_layout",
        "coordinate_system",
        "topology",
        "counts",
        "bounds",
        "material_slots",
        "submeshes",
        "cooker",
        "target_profile"};
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(manifestJson);
    } catch (const nlohmann::json::exception& exception) {
        return fail(
            outError,
            std::string("mesh manifest is not valid JSON: ") + exception.what());
    }
    if (!rejectUnknownKeys(
            root,
            kAllowed,
            std::size(kAllowed),
            "mesh",
            outError)) {
        return false;
    }
    const nlohmann::json* member = nullptr;
    std::string text;
    std::uint64_t version = 0u;
    if (!requireMember(root, "root_type", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.root_type", text, outError)) {
        return false;
    }
    if (text != kMeshRootType) {
        return fail(outError, "mesh manifest root type is unsupported: " + text);
    }
    if (!requireMember(root, "schema_version", "mesh", &member, outError) ||
        !jsonUnsigned(*member, "mesh.schema_version", version, outError)) {
        return false;
    }
    if (version != kMeshSchemaVersion) {
        return fail(outError, "mesh manifest schema version is unsupported");
    }
    if (!requireMember(root, "mesh_id", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.mesh_id", asset.meshId, outError)) {
        return false;
    }
    if (!requireMember(root, "vertex_layout", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.vertex_layout", text, outError)) {
        return false;
    }
    if (text != kMeshVertexLayout) {
        return fail(
            outError,
            "mesh manifest vertex layout is unsupported: " + text);
    }
    if (!requireMember(root, "coordinate_system", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.coordinate_system", text, outError)) {
        return false;
    }
    if (text != kMeshCoordinateSystem) {
        return fail(
            outError,
            "mesh manifest coordinate system is unsupported: " + text);
    }
    if (!requireMember(root, "topology", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.topology", text, outError)) {
        return false;
    }
    if (text != kMeshTopology) {
        return fail(outError, "mesh manifest topology is unsupported: " + text);
    }
    if (!requireMember(root, "cooker", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.cooker", text, outError)) {
        return false;
    }
    if (text.empty() || text.size() > kMeshMaxNameBytes) {
        return fail(outError, "mesh manifest cooker is unsupported");
    }
    if (!requireMember(root, "target_profile", "mesh", &member, outError) ||
        !jsonString(*member, "mesh.target_profile", text, outError)) {
        return false;
    }
    if (text != kMeshTargetProfile) {
        return fail(
            outError,
            "mesh manifest target profile is unsupported: " + text);
    }
    if (!parseCounts(root, counts, limits, outError) ||
        !parseBounds(root, asset.bounds, outError) ||
        !parseMaterialSlots(root, counts, asset, outError) ||
        !parseSubmeshes(root, counts, asset, outError)) {
        return false;
    }
    return true;
}

void writeVertex(BinaryWriter& writer, const MeshVertex& vertex) {
    for (const float value : vertex.position) {
        writer.f32(value);
    }
    for (const float value : vertex.normal) {
        writer.f32(value);
    }
    for (const float value : vertex.tangent) {
        writer.f32(value);
    }
    for (const float value : vertex.texcoord) {
        writer.f32(value);
    }
    for (const float value : vertex.color) {
        writer.f32(value);
    }
}

bool readVertex(BinaryReader& reader, MeshVertex& vertex) {
    for (float& value : vertex.position) {
        if (!reader.f32(value)) return false;
    }
    for (float& value : vertex.normal) {
        if (!reader.f32(value)) return false;
    }
    for (float& value : vertex.tangent) {
        if (!reader.f32(value)) return false;
    }
    for (float& value : vertex.texcoord) {
        if (!reader.f32(value)) return false;
    }
    for (float& value : vertex.color) {
        if (!reader.f32(value)) return false;
    }
    return true;
}

bool rejectUnsupportedContainer(
    const phrc::Document& document,
    std::string* outError) {
    if (document.magic != phrc::magic(kMeshContainerMagic)) {
        return fail(
            outError,
            "mesh container magic is not " + std::string(kMeshContainerMagic));
    }
    if (document.schemaVersion != kMeshSchemaVersion) {
        return fail(outError, "mesh container schema version is unsupported");
    }
    if (document.flags != 0u) {
        return fail(outError, "mesh container flags must be zero");
    }
    if (!document.dependencies.empty()) {
        return fail(outError, "mesh v1 must not declare dependencies");
    }
    if (document.chunks.size() != 2u) {
        return fail(outError, "mesh document requires exactly two chunks");
    }
    if (document.chunks[0].type != phrc::magic(kVertexChunkType)) {
        return fail(outError, "mesh document requires the VTX0 chunk first");
    }
    if (document.chunks[1].type != phrc::magic(kIndexChunkType)) {
        return fail(outError, "mesh document requires the IDX0 chunk second");
    }
    for (const phrc::Chunk& chunk : document.chunks) {
        if (chunk.flags != 0u) {
            return fail(outError, "mesh chunks must declare zero flags");
        }
        if (chunk.alignment != kChunkAlignment) {
            return fail(outError, "mesh chunks must declare 16-byte alignment");
        }
    }
    return true;
}

} // namespace

bool validateMeshAsset(
    const MeshAsset& asset,
    const MeshAssetLimits& limits,
    std::string* outError) {
    if (!validateMeshData(asset, limits, outError)) {
        return false;
    }
    clearError(outError);
    return true;
}

bool encodeMeshAsset(
    const MeshAsset& asset,
    std::vector<std::uint8_t>& outBytes,
    const MeshAssetLimits& limits,
    std::string* outError) {
    if (!validateMeshData(asset, limits, outError)) {
        return false;
    }
    std::size_t vertexBytes = 0u;
    std::size_t indexBytes = 0u;
    if (!checkedMultiply(asset.vertices.size(), kMeshVertexBytes, vertexBytes) ||
        !checkedMultiply(asset.indices.size(), sizeof(std::uint32_t), indexBytes)) {
        return fail(outError, "mesh payload size exceeds the host address space");
    }

    nlohmann::json manifest;
    manifest["root_type"] = kMeshRootType;
    manifest["schema_version"] = kMeshSchemaVersion;
    manifest["mesh_id"] = asset.meshId;
    manifest["vertex_layout"] = kMeshVertexLayout;
    manifest["coordinate_system"] = kMeshCoordinateSystem;
    manifest["topology"] = kMeshTopology;
    manifest["cooker"] = kMeshCooker;
    manifest["target_profile"] = kMeshTargetProfile;
    manifest["counts"] = {
        {"vertices", asset.vertices.size()},
        {"indices", asset.indices.size()},
        {"submeshes", asset.submeshes.size()},
        {"material_slots", asset.materialSlots.size()}};
    manifest["bounds"] = {
        {"min", {asset.bounds.min[0], asset.bounds.min[1], asset.bounds.min[2]}},
        {"max", {asset.bounds.max[0], asset.bounds.max[1], asset.bounds.max[2]}}};
    nlohmann::json slots = nlohmann::json::array();
    for (const std::string& slot : asset.materialSlots) {
        slots.push_back(slot);
    }
    manifest["material_slots"] = std::move(slots);
    nlohmann::json submeshes = nlohmann::json::array();
    for (const MeshSubmesh& submesh : asset.submeshes) {
        submeshes.push_back(
            {{"first_index", submesh.firstIndex},
             {"index_count", submesh.indexCount},
             {"material_slot", submesh.materialSlot}});
    }
    manifest["submeshes"] = std::move(submeshes);
    const std::string manifestJson = manifest.dump();
    if (manifestJson.size() > limits.maxEncodedBytes) {
        return fail(outError, "mesh document exceeds the encoded byte limit");
    }

    BinaryWriter vertices;
    for (const MeshVertex& vertex : asset.vertices) {
        writeVertex(vertices, vertex);
    }
    BinaryWriter indices;
    for (const std::uint32_t index : asset.indices) {
        indices.u32(index);
    }

    phrc::Document document;
    document.magic = phrc::magic(kMeshContainerMagic);
    document.schemaVersion = kMeshSchemaVersion;
    document.flags = 0u;
    document.manifestJson = manifestJson;
    document.chunks = {
        phrc::Chunk{
            phrc::magic(kVertexChunkType),
            0u,
            kChunkAlignment,
            vertices.take()},
        phrc::Chunk{
            phrc::magic(kIndexChunkType),
            0u,
            kChunkAlignment,
            indices.take()}};

    std::vector<std::uint8_t> encoded;
    std::string encodeError;
    if (!phrc::encode(document, encoded, &encodeError)) {
        return fail(outError, "mesh encode failed: " + encodeError);
    }
    if (encoded.size() > limits.maxEncodedBytes) {
        return fail(outError, "mesh document exceeds the encoded byte limit");
    }
    outBytes = std::move(encoded);
    clearError(outError);
    return true;
}

bool decodeMeshAsset(
    const std::vector<std::uint8_t>& bytes,
    MeshAsset& outAsset,
    const MeshAssetLimits& limits,
    std::string* outError) {
    if (bytes.size() > limits.maxEncodedBytes) {
        return fail(outError, "mesh document exceeds the encoded byte limit");
    }
    phrc::Document document;
    std::string decodeError;
    if (!phrc::decode(bytes, document, &decodeError)) {
        return fail(outError, "mesh container decode failed: " + decodeError);
    }
    if (!rejectUnsupportedContainer(document, outError)) {
        return false;
    }

    MeshAsset asset;
    MeshManifestCounts counts;
    if (!parseManifest(
            document.manifestJson,
            asset,
            counts,
            limits,
            outError)) {
        return false;
    }

    std::size_t expectedVertexBytes = 0u;
    std::size_t expectedIndexBytes = 0u;
    if (!checkedMultiply(
            static_cast<std::size_t>(counts.vertices),
            kMeshVertexBytes,
            expectedVertexBytes) ||
        !checkedMultiply(
            static_cast<std::size_t>(counts.indices),
            sizeof(std::uint32_t),
            expectedIndexBytes)) {
        return fail(outError, "mesh payload size exceeds the host address space");
    }
    if (document.chunks[0].bytes.size() != expectedVertexBytes) {
        return fail(
            outError,
            "mesh VTX0 byte count does not match the manifest vertex count");
    }
    if (document.chunks[1].bytes.size() != expectedIndexBytes) {
        return fail(
            outError,
            "mesh IDX0 byte count does not match the manifest index count");
    }

    asset.vertices.resize(static_cast<std::size_t>(counts.vertices));
    asset.indices.resize(static_cast<std::size_t>(counts.indices));
    BinaryReader vertexReader(document.chunks[0].bytes);
    for (MeshVertex& vertex : asset.vertices) {
        if (!readVertex(vertexReader, vertex)) {
            return fail(outError, "mesh VTX0 payload is truncated");
        }
    }
    if (!vertexReader.finished()) {
        return fail(outError, "mesh VTX0 payload contains trailing bytes");
    }
    BinaryReader indexReader(document.chunks[1].bytes);
    for (std::uint32_t& index : asset.indices) {
        if (!indexReader.u32(index)) {
            return fail(outError, "mesh IDX0 payload is truncated");
        }
    }
    if (!indexReader.finished()) {
        return fail(outError, "mesh IDX0 payload contains trailing bytes");
    }

    if (!validateMeshData(asset, limits, outError)) {
        return false;
    }
    outAsset = std::move(asset);
    clearError(outError);
    return true;
}

bool loadMeshAsset(
    const IAssetStore& store,
    const std::string& virtualPath,
    MeshAsset& outAsset,
    const MeshAssetLimits& limits,
    std::string* outError) {
    if (virtualPath.empty()) {
        return fail(outError, "mesh virtual path must not be empty");
    }
    std::vector<std::uint8_t> bytes;
    std::string readError;
    if (!store.readBytes(virtualPath, bytes, &readError)) {
        return fail(
            outError,
            "mesh read failed for '" + virtualPath + "': " + readError);
    }
    MeshAsset asset;
    if (!decodeMeshAsset(bytes, asset, limits, outError)) {
        return false;
    }
    outAsset = std::move(asset);
    clearError(outError);
    return true;
}

} // namespace engine::assets::phlosion
