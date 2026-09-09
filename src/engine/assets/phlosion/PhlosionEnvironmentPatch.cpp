#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace engine::assets::phlosion {
namespace {

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

template <typename Value, std::size_t Size>
std::array<Value, Size> fixedArray(
    const nlohmann::json& value,
    std::string_view label) {
    if (!value.is_array() || value.size() != Size) {
        throw std::runtime_error(
            std::string(label) + " must contain " +
            std::to_string(Size) + " values.");
    }
    std::array<Value, Size> out{};
    for (std::size_t index = 0u; index < Size; ++index) {
        out[index] = value.at(index).get<Value>();
    }
    return out;
}

template <std::size_t Rows, std::size_t Columns>
std::array<std::array<float, Columns>, Rows> floatMatrix(
    const nlohmann::json& value,
    std::string_view label) {
    if (!value.is_array() || value.size() != Rows) {
        throw std::runtime_error(
            std::string(label) + " must contain " +
            std::to_string(Rows) + " rows.");
    }
    std::array<std::array<float, Columns>, Rows> out{};
    for (std::size_t row = 0u; row < Rows; ++row) {
        out[row] = fixedArray<float, Columns>(value.at(row), label);
    }
    return out;
}

bool validSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= '0' && ch <= '9') ||
                (ch >= 'a' && ch <= 'f') ||
                (ch >= 'A' && ch <= 'F');
        });
}

bool finiteVertex(const EnvironmentPatchVertex& vertex) {
    const auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](auto value) {
            return std::isfinite(static_cast<double>(value));
        });
    };
    if (!finite(vertex.position) || !finite(vertex.normal) ||
        !finite(vertex.tangent) || !finite(vertex.bitangent) ||
        !std::isfinite(vertex.normalW) || !finite(vertex.weights)) {
        return false;
    }
    for (const auto& uv : vertex.texcoords) {
        if (!finite(uv)) return false;
    }
    for (const auto& color : vertex.colors) {
        if (!finite(color)) return false;
    }
    return true;
}

nlohmann::json vertexJson(const EnvironmentPatchVertex& vertex) {
    return {
        {"position", vertex.position},
        {"normal", vertex.normal},
        {"tangent", vertex.tangent},
        {"bitangent", vertex.bitangent},
        {"texcoords", vertex.texcoords},
        {"colors", vertex.colors},
        {"normal_w", vertex.normalW},
        {"joints", vertex.joints},
        {"weights", vertex.weights},
        {"source_vertex_index", vertex.sourceVertexIndex}};
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void appendFloat(std::vector<std::uint8_t>& out, float value) {
    appendU32(out, std::bit_cast<std::uint32_t>(value));
}

void appendString(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Environment patch string exceeds binary limits.");
    }
    appendU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    bool u32(std::uint32_t& out) {
        if (remaining() < 4u) return false;
        out = static_cast<std::uint32_t>(bytes_[offset_]) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 1u]) << 8u) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 2u]) << 16u) |
            (static_cast<std::uint32_t>(bytes_[offset_ + 3u]) << 24u);
        offset_ += 4u;
        return true;
    }

    bool i32(std::int32_t& out) {
        std::uint32_t value = 0u;
        if (!u32(value)) return false;
        out = static_cast<std::int32_t>(value);
        return true;
    }

    bool floating(float& out) {
        std::uint32_t value = 0u;
        if (!u32(value)) return false;
        out = std::bit_cast<float>(value);
        return true;
    }

    bool string(std::string& out) {
        std::uint32_t size = 0u;
        if (!u32(size) || size > remaining()) return false;
        out.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            size);
        offset_ += size;
        return true;
    }

    std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0u;
};

template <typename Values>
bool readFloats(BinaryReader& reader, Values& values) {
    for (auto& value : values) {
        if (!reader.floating(value)) return false;
    }
    return true;
}

bool readVertex(BinaryReader& reader, EnvironmentPatchVertex& vertex) {
    if (!readFloats(reader, vertex.position) ||
        !readFloats(reader, vertex.normal) ||
        !readFloats(reader, vertex.tangent) ||
        !readFloats(reader, vertex.bitangent)) {
        return false;
    }
    for (auto& row : vertex.texcoords) {
        if (!readFloats(reader, row)) return false;
    }
    for (auto& row : vertex.colors) {
        if (!readFloats(reader, row)) return false;
    }
    if (!reader.floating(vertex.normalW)) return false;
    for (auto& joint : vertex.joints) {
        if (!reader.i32(joint)) return false;
    }
    if (!readFloats(reader, vertex.weights) ||
        !reader.i32(vertex.sourceVertexIndex)) {
        return false;
    }
    return true;
}

void appendVertex(
    std::vector<std::uint8_t>& out,
    const EnvironmentPatchVertex& vertex) {
    for (const float value : vertex.position) appendFloat(out, value);
    for (const float value : vertex.normal) appendFloat(out, value);
    for (const float value : vertex.tangent) appendFloat(out, value);
    for (const float value : vertex.bitangent) appendFloat(out, value);
    for (const auto& row : vertex.texcoords) {
        for (const float value : row) appendFloat(out, value);
    }
    for (const auto& row : vertex.colors) {
        for (const float value : row) appendFloat(out, value);
    }
    appendFloat(out, vertex.normalW);
    for (const auto value : vertex.joints) {
        appendU32(out, static_cast<std::uint32_t>(value));
    }
    for (const float value : vertex.weights) appendFloat(out, value);
    appendU32(out, static_cast<std::uint32_t>(vertex.sourceVertexIndex));
}

} // namespace

bool validateEnvironmentPatchDocument(
    const EnvironmentPatchDocument& document,
    std::string* outError) {
    if (document.source.profileId.empty() ||
        document.source.coordinateSystem.empty() ||
        !validSha256(document.source.modelSha256) ||
        !validSha256(document.source.geometrySha256)) {
        return fail(outError, "Environment patch source lock is invalid.");
    }
    if (document.terrainReplacement) {
        if (!std::isfinite(document.terrainReplacement->tileSizeCm) ||
            document.terrainReplacement->tileSizeCm <= 0.0f ||
            document.terrainReplacement->cells.empty()) {
            return fail(outError, "Environment patch terrain replacement is invalid.");
        }
        std::set<std::array<std::int32_t, 2>> cells;
        for (const auto& cell : document.terrainReplacement->cells) {
            if (!cells.insert(cell).second) {
                return fail(outError, "Environment patch terrain replacement contains duplicate cells.");
            }
        }
    }
    std::set<std::string> meshIds;
    for (const auto& mesh : document.meshes) {
        if (mesh.id.empty() || mesh.displayName.empty() ||
            !meshIds.insert(mesh.id).second || mesh.vertices.empty() ||
            mesh.materialGroups.empty()) {
            return fail(outError, "Environment patch mesh identity or payload is invalid: " + mesh.id);
        }
        if (mesh.vertices.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return fail(outError, "Environment patch mesh exceeds 32-bit index limits: " + mesh.id);
        }
        if (!std::all_of(mesh.vertices.begin(), mesh.vertices.end(), finiteVertex)) {
            return fail(outError, "Environment patch mesh contains non-finite vertex data: " + mesh.id);
        }
        for (const auto& group : mesh.materialGroups) {
            if (group.indices.empty() || group.indices.size() % 3u != 0u ||
                std::any_of(group.indices.begin(), group.indices.end(), [&](std::uint32_t index) {
                    return index >= mesh.vertices.size();
                })) {
                return fail(outError, "Environment patch mesh has invalid triangle indices: " + mesh.id);
            }
            for (std::size_t index = 0u; index < group.indices.size(); index += 3u) {
                if (group.indices[index] == group.indices[index + 1u] ||
                    group.indices[index] == group.indices[index + 2u] ||
                    group.indices[index + 1u] == group.indices[index + 2u]) {
                    return fail(outError, "Environment patch mesh contains a degenerate index triangle: " + mesh.id);
                }
            }
        }
    }
    if (outError) outError->clear();
    return true;
}

bool parseEnvironmentPatchDocument(
    const std::string& jsonText,
    EnvironmentPatchDocument& out,
    std::string* outError) {
    try {
        const auto root = nlohmann::json::parse(jsonText);
        if (root.at("schema_version").get<std::uint32_t>() !=
                kEnvironmentPatchSchemaVersion ||
            root.at("kind").get<std::string>() != kEnvironmentPatchKind) {
            return fail(outError, "Unsupported Phlosion environment-patch contract.");
        }
        const auto& source = root.at("source");
        EnvironmentPatchDocument decoded{
            .source = EnvironmentPatchSourceLock{
                .profileId = source.at("profile_id").get<std::string>(),
                .modelSha256 = source.at("model_sha256").get<std::string>(),
                .geometrySha256 = source.at("geometry_sha256").get<std::string>(),
                .coordinateSystem = source.at("coordinate_system").get<std::string>()}};
        if (const auto replacement = root.find("terrain_replacement");
            replacement != root.end()) {
            EnvironmentPatchTerrainReplacement decodedReplacement{
                .tileSizeCm = replacement->at("tile_size_cm").get<float>()};
            for (const auto& cell : replacement->at("cells")) {
                decodedReplacement.cells.push_back(
                    fixedArray<std::int32_t, 2>(cell, "terrain replacement cell"));
            }
            decoded.terrainReplacement = std::move(decodedReplacement);
        }
        for (const auto& meshJson : root.at("meshes")) {
            EnvironmentPatchMesh mesh{
                .id = meshJson.at("id").get<std::string>(),
                .displayName = meshJson.at("display_name").get<std::string>()};
            for (const auto& value : meshJson.at("vertices")) {
                mesh.vertices.push_back(EnvironmentPatchVertex{
                    .position = fixedArray<float, 3>(value.at("position"), "position"),
                    .normal = fixedArray<float, 3>(value.at("normal"), "normal"),
                    .tangent = fixedArray<float, 4>(value.at("tangent"), "tangent"),
                    .bitangent = fixedArray<float, 4>(value.at("bitangent"), "bitangent"),
                    .texcoords = floatMatrix<4, 2>(value.at("texcoords"), "texcoords"),
                    .colors = floatMatrix<4, 4>(value.at("colors"), "colors"),
                    .normalW = value.value("normal_w", 1.0f),
                    .joints = fixedArray<std::int32_t, 4>(value.at("joints"), "joints"),
                    .weights = fixedArray<float, 4>(value.at("weights"), "weights"),
                    .sourceVertexIndex = value.value("source_vertex_index", -1)});
            }
            for (const auto& groupJson : meshJson.at("material_groups")) {
                mesh.materialGroups.push_back(EnvironmentPatchMaterialGroup{
                    .materialIndex = groupJson.at("material_index").get<std::uint32_t>(),
                    .indices = groupJson.at("indices").get<std::vector<std::uint32_t>>()});
            }
            decoded.meshes.push_back(std::move(mesh));
        }
        if (!validateEnvironmentPatchDocument(decoded, outError)) return false;
        out = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        return fail(outError, "Invalid Phlosion environment-patch document: " + std::string(exception.what()));
    }
}

bool loadEnvironmentPatchDocument(
    const IAssetStore& store,
    const std::string& virtualPath,
    EnvironmentPatchDocument& out,
    std::string* outError) {
    std::vector<std::uint8_t> bytes;
    if (!store.readBytes(virtualPath, bytes, outError)) return false;
    if (bytes.size() >= kEnvironmentPatchBinaryMagic.size() &&
        std::equal(
            kEnvironmentPatchBinaryMagic.begin(),
            kEnvironmentPatchBinaryMagic.end(),
            bytes.begin())) {
        return parseEnvironmentPatchBinary(bytes, out, outError);
    }
    return parseEnvironmentPatchDocument(
        std::string(bytes.begin(), bytes.end()), out, outError);
}

std::string serializeEnvironmentPatchDocument(
    const EnvironmentPatchDocument& document) {
    nlohmann::json root{
        {"schema_version", kEnvironmentPatchSchemaVersion},
        {"kind", kEnvironmentPatchKind},
        {"source", {
            {"profile_id", document.source.profileId},
            {"model_sha256", document.source.modelSha256},
            {"geometry_sha256", document.source.geometrySha256},
            {"coordinate_system", document.source.coordinateSystem}}},
        {"meshes", nlohmann::json::array()}};
    if (document.terrainReplacement) {
        root["terrain_replacement"] = {
            {"tile_size_cm", document.terrainReplacement->tileSizeCm},
            {"cells", document.terrainReplacement->cells}};
    }
    for (const auto& mesh : document.meshes) {
        nlohmann::json meshJson{
            {"id", mesh.id},
            {"display_name", mesh.displayName},
            {"vertices", nlohmann::json::array()},
            {"material_groups", nlohmann::json::array()}};
        for (const auto& vertex : mesh.vertices) {
            meshJson["vertices"].push_back(vertexJson(vertex));
        }
        for (const auto& group : mesh.materialGroups) {
            meshJson["material_groups"].push_back({
                {"material_index", group.materialIndex},
                {"indices", group.indices}});
        }
        root["meshes"].push_back(std::move(meshJson));
    }
    return root.dump() + '\n';
}

bool parseEnvironmentPatchBinary(
    std::span<const std::uint8_t> bytes,
    EnvironmentPatchDocument& out,
    std::string* outError) {
    try {
        if (bytes.size() < kEnvironmentPatchBinaryMagic.size() ||
            !std::equal(
                kEnvironmentPatchBinaryMagic.begin(),
                kEnvironmentPatchBinaryMagic.end(),
                bytes.begin())) {
            return fail(outError, "Invalid Phlosion environment-patch binary magic.");
        }
        BinaryReader reader(bytes.subspan(kEnvironmentPatchBinaryMagic.size()));
        std::uint32_t schemaVersion = 0u;
        if (!reader.u32(schemaVersion) ||
            schemaVersion != kEnvironmentPatchSchemaVersion) {
            return fail(outError, "Unsupported Phlosion environment-patch binary schema.");
        }
        EnvironmentPatchDocument decoded;
        if (!reader.string(decoded.source.profileId) ||
            !reader.string(decoded.source.modelSha256) ||
            !reader.string(decoded.source.geometrySha256) ||
            !reader.string(decoded.source.coordinateSystem)) {
            return fail(outError, "Truncated Phlosion environment-patch binary source lock.");
        }
        std::uint32_t hasReplacement = 0u;
        if (!reader.u32(hasReplacement) || hasReplacement > 1u) {
            return fail(outError, "Invalid Phlosion environment-patch binary terrain marker.");
        }
        if (hasReplacement != 0u) {
            EnvironmentPatchTerrainReplacement replacement;
            std::uint32_t cellCount = 0u;
            if (!reader.floating(replacement.tileSizeCm) ||
                !reader.u32(cellCount) ||
                cellCount > reader.remaining() / 8u) {
                return fail(outError, "Truncated Phlosion environment-patch terrain replacement.");
            }
            replacement.cells.resize(cellCount);
            for (auto& cell : replacement.cells) {
                if (!reader.i32(cell[0]) || !reader.i32(cell[1])) {
                    return fail(outError, "Truncated Phlosion environment-patch terrain cell.");
                }
            }
            decoded.terrainReplacement = std::move(replacement);
        }
        std::uint32_t meshCount = 0u;
        if (!reader.u32(meshCount)) {
            return fail(outError, "Truncated Phlosion environment-patch mesh table.");
        }
        decoded.meshes.reserve(meshCount);
        for (std::uint32_t meshIndex = 0u; meshIndex < meshCount; ++meshIndex) {
            EnvironmentPatchMesh mesh;
            std::uint32_t vertexCount = 0u;
            if (!reader.string(mesh.id) ||
                !reader.string(mesh.displayName) ||
                !reader.u32(vertexCount) ||
                vertexCount > reader.remaining() / 192u) {
                return fail(outError, "Truncated Phlosion environment-patch binary mesh.");
            }
            mesh.vertices.resize(vertexCount);
            for (auto& vertex : mesh.vertices) {
                if (!readVertex(reader, vertex)) {
                    return fail(outError, "Truncated Phlosion environment-patch binary vertex stream.");
                }
            }
            std::uint32_t groupCount = 0u;
            if (!reader.u32(groupCount)) {
                return fail(outError, "Truncated Phlosion environment-patch material table.");
            }
            mesh.materialGroups.reserve(groupCount);
            for (std::uint32_t groupIndex = 0u;
                 groupIndex < groupCount;
                 ++groupIndex) {
                EnvironmentPatchMaterialGroup group;
                std::uint32_t indexCount = 0u;
                if (!reader.u32(group.materialIndex) ||
                    !reader.u32(indexCount) ||
                    indexCount > reader.remaining() / 4u) {
                    return fail(outError, "Truncated Phlosion environment-patch index stream.");
                }
                group.indices.resize(indexCount);
                for (auto& index : group.indices) {
                    if (!reader.u32(index)) {
                        return fail(outError, "Truncated Phlosion environment-patch triangle index.");
                    }
                }
                mesh.materialGroups.push_back(std::move(group));
            }
            decoded.meshes.push_back(std::move(mesh));
        }
        if (reader.remaining() != 0u) {
            return fail(outError, "Phlosion environment-patch binary contains trailing bytes.");
        }
        if (!validateEnvironmentPatchDocument(decoded, outError)) return false;
        out = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        return fail(outError, "Invalid Phlosion environment-patch binary: " + std::string(exception.what()));
    }
}

std::vector<std::uint8_t> serializeEnvironmentPatchBinary(
    const EnvironmentPatchDocument& document) {
    std::string error;
    if (!validateEnvironmentPatchDocument(document, &error)) {
        throw std::runtime_error("Cannot serialize invalid environment patch: " + error);
    }
    std::vector<std::uint8_t> out(
        kEnvironmentPatchBinaryMagic.begin(),
        kEnvironmentPatchBinaryMagic.end());
    appendU32(out, kEnvironmentPatchSchemaVersion);
    appendString(out, document.source.profileId);
    appendString(out, document.source.modelSha256);
    appendString(out, document.source.geometrySha256);
    appendString(out, document.source.coordinateSystem);
    appendU32(out, document.terrainReplacement ? 1u : 0u);
    if (document.terrainReplacement) {
        appendFloat(out, document.terrainReplacement->tileSizeCm);
        appendU32(
            out,
            static_cast<std::uint32_t>(
                document.terrainReplacement->cells.size()));
        for (const auto& cell : document.terrainReplacement->cells) {
            appendU32(out, static_cast<std::uint32_t>(cell[0]));
            appendU32(out, static_cast<std::uint32_t>(cell[1]));
        }
    }
    appendU32(out, static_cast<std::uint32_t>(document.meshes.size()));
    for (const auto& mesh : document.meshes) {
        appendString(out, mesh.id);
        appendString(out, mesh.displayName);
        appendU32(out, static_cast<std::uint32_t>(mesh.vertices.size()));
        for (const auto& vertex : mesh.vertices) appendVertex(out, vertex);
        appendU32(out, static_cast<std::uint32_t>(mesh.materialGroups.size()));
        for (const auto& group : mesh.materialGroups) {
            appendU32(out, group.materialIndex);
            appendU32(out, static_cast<std::uint32_t>(group.indices.size()));
            for (const auto index : group.indices) appendU32(out, index);
        }
    }
    return out;
}

} // namespace engine::assets::phlosion
