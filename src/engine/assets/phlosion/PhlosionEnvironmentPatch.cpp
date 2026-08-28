#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
    std::string text;
    if (!store.readText(virtualPath, text, outError)) return false;
    return parseEnvironmentPatchDocument(text, out, outError);
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
    return root.dump(2) + '\n';
}

} // namespace engine::assets::phlosion
