#include "engine/assets/phlosion/PhlosionAuthoredScene.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
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

template <std::size_t Size>
std::array<float, Size> floatArray(
    const nlohmann::json& value,
    std::string_view label) {
    if (!value.is_array() || value.size() != Size) {
        throw std::runtime_error(
            std::string(label) + " must contain " +
            std::to_string(Size) + " numbers.");
    }
    std::array<float, Size> out{};
    for (std::size_t index = 0u; index < Size; ++index) {
        out[index] = value.at(index).get<float>();
    }
    return out;
}

AuthoredSceneTransform parseTransform(
    const nlohmann::json& value,
    std::string_view label) {
    return {
        .translation = floatArray<3>(
            value.at("translation"),
            std::string(label) + " translation"),
        .rotationDegrees = floatArray<3>(
            value.at("rotation_degrees"),
            std::string(label) + " rotation_degrees"),
        .scale = floatArray<3>(
            value.at("scale"),
            std::string(label) + " scale")};
}

nlohmann::json transformJson(
    const AuthoredSceneTransform& transform) {
    return {
        {"translation", transform.translation},
        {"rotation_degrees", transform.rotationDegrees},
        {"scale", transform.scale}};
}

bool validTransform(
    const AuthoredSceneTransform& transform) {
    const auto finite = [](const auto& values) {
        return std::all_of(
            values.begin(),
            values.end(),
            [](float value) {
                return std::isfinite(value);
            });
    };
    return finite(transform.translation) &&
        finite(transform.rotationDegrees) &&
        finite(transform.scale) &&
        std::all_of(
            transform.scale.begin(),
            transform.scale.end(),
            [](float value) {
                return value > 0.0f;
            });
}

} // namespace

bool validateAuthoredSceneDocument(
    const AuthoredSceneDocument& document,
    std::string* outError) {
    if (document.sceneId.empty() ||
        document.baseEnvironmentAssetId.empty() ||
        document.coordinateSystem.empty()) {
        return fail(
            outError,
            "An authored scene requires scene, base-environment, and coordinate-system identities.");
    }

    std::map<std::string, const AuthoredSceneNode*> nodes;
    for (const auto& node : document.nodes) {
        if (node.id.empty() || node.displayName.empty() ||
            !nodes.emplace(node.id, &node).second) {
            return fail(
                outError,
                "Authored scene nodes require unique stable IDs and non-empty display names.");
        }
        const std::uint32_t bindingCount =
            static_cast<std::uint32_t>(node.importedSource.has_value()) +
            static_cast<std::uint32_t>(node.prefabInstance.has_value()) +
            static_cast<std::uint32_t>(node.terrainTile.has_value());
        const bool tileNode = node.terrainTile.has_value();
        if (bindingCount > 1u ||
            (bindingCount == 0u && node.transform.has_value()) ||
            (!tileNode && bindingCount == 1u &&
             !node.transform.has_value()) ||
            (tileNode && node.transform.has_value())) {
            return fail(
                outError,
                "Each authored scene node must be a folder, a transformed object binding, or one tile-set-derived terrain tile: " +
                    node.id);
        }
        if (node.transform && !validTransform(*node.transform)) {
            return fail(
                outError,
                "Authored scene node has an invalid transform: " +
                    node.id);
        }
        if (node.importedSource) {
            const auto& binding = *node.importedSource;
            if (binding.targetKind.empty() ||
                binding.logicalName.empty() ||
                !validTransform(binding.expectedSourceTransform)) {
                return fail(
                    outError,
                    "Authored scene imported-source binding is invalid: " +
                        node.id);
            }
        }
        if (node.prefabInstance) {
            const auto& binding = *node.prefabInstance;
            if (binding.prototypeNodeId.empty() ||
                binding.prefabAssetId.empty() ||
                !validTransform(binding.creationTransform)) {
                return fail(
                    outError,
                    "Authored scene prefab-instance binding is invalid: " +
                        node.id);
            }
        }
        if (node.terrainTile) {
            const auto& binding = *node.terrainTile;
            const bool validShape =
                binding.shape == "flat" ||
                binding.shape == "ramp_north" ||
                binding.shape == "ramp_east" ||
                binding.shape == "ramp_south" ||
                binding.shape == "ramp_west";
            if (binding.tileSetAssetId.empty() ||
                binding.surface.empty() ||
                binding.visualVariant.empty() || !validShape ||
                binding.elevationLevel < -128 ||
                binding.elevationLevel > 128) {
                return fail(
                    outError,
                    "Authored scene terrain-tile binding is invalid: " +
                        node.id);
            }
        }
    }

    for (const auto& node : document.nodes) {
        if (!node.parentId.empty()) {
            const auto parent = nodes.find(node.parentId);
            if (parent == nodes.end() || !parent->second->folder()) {
                return fail(
                    outError,
                    "Authored scene node parent is missing or is not a folder: " +
                        node.id);
            }
        }
        if (node.prefabInstance &&
            (!nodes.contains(
                 node.prefabInstance->prototypeNodeId) ||
             nodes.at(
                 node.prefabInstance->prototypeNodeId)->folder())) {
            return fail(
                outError,
                "Authored scene prefab prototype node is missing or is a folder: " +
                    node.id);
        }

        std::set<std::string> visited;
        const AuthoredSceneNode* cursor = &node;
        while (!cursor->parentId.empty()) {
            if (!visited.insert(cursor->id).second) {
                return fail(
                    outError,
                    "Authored scene hierarchy contains a cycle at: " +
                        node.id);
            }
            cursor = nodes.at(cursor->parentId);
        }
    }
    std::set<std::pair<std::string, std::uint32_t>>
        siblingSlots;
    for (const auto& node : document.nodes) {
        if (!siblingSlots.emplace(
                node.parentId,
                node.siblingOrder).second) {
            return fail(
                outError,
                "Authored scene siblings require unique sibling_order values under parent: " +
                    node.parentId);
        }
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool parseAuthoredSceneDocument(
    const std::string& jsonText,
    AuthoredSceneDocument& out,
    std::string* outError) {
    try {
        const nlohmann::json root =
            nlohmann::json::parse(jsonText);
        const std::uint32_t schemaVersion =
            root.at("schema_version").get<std::uint32_t>();
        if (schemaVersion < kMinimumAuthoredSceneSchemaVersion ||
            schemaVersion > kAuthoredSceneSchemaVersion ||
            root.at("kind").get<std::string>() !=
                kAuthoredSceneKind) {
            return fail(
                outError,
                "Unsupported Phlosion authored-scene document contract.");
        }
        AuthoredSceneDocument decoded{
            .sceneId = root.at("scene_id").get<std::string>(),
            .baseEnvironmentAssetId =
                root.at("base_environment_asset_id")
                    .get<std::string>(),
            .coordinateSystem =
                root.at("coordinate_system").get<std::string>()};
        for (const auto& record : root.at("nodes")) {
            AuthoredSceneNode node{
                .id = record.at("id").get<std::string>(),
                .displayName =
                    record.at("display_name").get<std::string>(),
                .parentId =
                    record.value("parent_id", std::string{}),
                .siblingOrder =
                    record.value("sibling_order", 0u),
                .enabled = record.value("enabled", true),
                .reason =
                    record.value("reason", std::string{})};
            const auto components = record.find("components");
            if (components != record.end()) {
                if (const auto transform =
                        components->find("transform");
                    transform != components->end()) {
                    node.transform =
                        parseTransform(*transform, "node");
                }
                if (const auto imported =
                        components->find(
                            "imported_source_binding");
                    imported != components->end()) {
                    node.importedSource =
                        ImportedSourceBinding{
                            .targetKind =
                                imported->at("target_kind")
                                    .get<std::string>(),
                            .logicalName =
                                imported->at("logical_name")
                                    .get<std::string>(),
                            .recordIndex =
                                imported->at("record_index")
                                    .get<std::uint32_t>(),
                            .expectedSourceTransform =
                                parseTransform(
                                    imported->at(
                                        "expected_source_transform"),
                                    "expected source")};
                }
                if (const auto prefab =
                        components->find("prefab_instance");
                    prefab != components->end()) {
                    node.prefabInstance =
                        PrefabInstanceBinding{
                            .prototypeNodeId =
                                prefab->at("prototype_node_id")
                                    .get<std::string>(),
                            .prefabAssetId =
                                prefab->at("prefab_asset_id")
                                    .get<std::string>(),
                            .creationTransform =
                                parseTransform(
                                    prefab->at("creation_transform"),
                                    "creation")};
                }
                if (const auto terrainTile =
                        components->find("terrain_tile");
                    terrainTile != components->end()) {
                    TerrainTileBinding binding{
                        .tileSetAssetId =
                            terrainTile->at("tile_set_asset_id")
                                .get<std::string>(),
                        .gridX = terrainTile->at("grid_x")
                            .get<std::int32_t>(),
                        .gridZ = terrainTile->at("grid_z")
                            .get<std::int32_t>(),
                        .elevationLevel =
                            terrainTile->at("elevation_level")
                                .get<std::int32_t>(),
                        .surface = terrainTile->at("surface")
                            .get<std::string>(),
                        .shape = terrainTile->value(
                            "shape", std::string("flat")),
                        .visualVariant = terrainTile->value(
                            "visual_variant", std::string("auto")),
                        .receivesProjectedShadow = terrainTile->value(
                            "receives_projected_shadow", true),
                        .normalizeSourceTint = terrainTile->value(
                            "normalize_source_tint", false)};
                    if (const auto sourceReference =
                            terrainTile->find("source_reference");
                        sourceReference != terrainTile->end()) {
                        binding.sourceReference =
                            TerrainTileSourceReference{
                                .gridX = sourceReference->at("grid_x")
                                    .get<std::int32_t>(),
                                .gridZ = sourceReference->at("grid_z")
                                    .get<std::int32_t>()};
                    }
                    node.terrainTile = std::move(binding);
                }
            }
            decoded.nodes.push_back(std::move(node));
        }
        if (!validateAuthoredSceneDocument(decoded, outError)) {
            return false;
        }
        out = std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Invalid Phlosion authored-scene document: " +
                std::string(exception.what()));
    }
}

bool loadAuthoredSceneDocument(
    const IAssetStore& store,
    const std::string& virtualPath,
    AuthoredSceneDocument& out,
    std::string* outError) {
    std::string text;
    if (!store.readText(virtualPath, text, outError)) {
        return false;
    }
    return parseAuthoredSceneDocument(text, out, outError);
}

std::string serializeAuthoredSceneDocument(
    const AuthoredSceneDocument& document) {
    nlohmann::json root{
        {"schema_version", kAuthoredSceneSchemaVersion},
        {"kind", kAuthoredSceneKind},
        {"scene_id", document.sceneId},
        {"base_environment_asset_id",
         document.baseEnvironmentAssetId},
        {"coordinate_system", document.coordinateSystem},
        {"nodes", nlohmann::json::array()}};
    for (const auto& node : document.nodes) {
        nlohmann::json record{
            {"id", node.id},
            {"display_name", node.displayName},
            {"parent_id", node.parentId},
            {"sibling_order", node.siblingOrder},
            {"enabled", node.enabled},
            {"reason", node.reason},
            {"components", nlohmann::json::object()}};
        if (node.transform) {
            record["components"]["transform"] =
                transformJson(*node.transform);
        }
        if (node.importedSource) {
            const auto& binding = *node.importedSource;
            record["components"]["imported_source_binding"] = {
                {"target_kind", binding.targetKind},
                {"logical_name", binding.logicalName},
                {"record_index", binding.recordIndex},
                {"expected_source_transform",
                 transformJson(
                     binding.expectedSourceTransform)}};
        }
        if (node.prefabInstance) {
            const auto& binding = *node.prefabInstance;
            record["components"]["prefab_instance"] = {
                {"prototype_node_id",
                 binding.prototypeNodeId},
                {"prefab_asset_id", binding.prefabAssetId},
                {"creation_transform",
                 transformJson(binding.creationTransform)}};
        }
        if (node.terrainTile) {
            const auto& binding = *node.terrainTile;
            record["components"]["terrain_tile"] = {
                {"tile_set_asset_id", binding.tileSetAssetId},
                {"grid_x", binding.gridX},
                {"grid_z", binding.gridZ},
                {"elevation_level", binding.elevationLevel},
                {"surface", binding.surface},
                {"shape", binding.shape},
                {"visual_variant", binding.visualVariant},
                {"receives_projected_shadow",
                 binding.receivesProjectedShadow},
                {"normalize_source_tint",
                 binding.normalizeSourceTint}};
            if (binding.sourceReference) {
                record["components"]["terrain_tile"]
                    ["source_reference"] = {
                        {"grid_x", binding.sourceReference->gridX},
                        {"grid_z", binding.sourceReference->gridZ}};
            }
        }
        root["nodes"].push_back(std::move(record));
    }
    return root.dump(2) + '\n';
}

} // namespace engine::assets::phlosion
